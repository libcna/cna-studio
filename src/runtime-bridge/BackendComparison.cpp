// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/RuntimeBridge/BackendComparison.hpp"

#include <algorithm>
#include <filesystem>

namespace CNA::Editor
{
    /** @brief One player process and what the run has learned from it. */
    struct BackendComparison::Session
    {
        PlayerProcess process;

        /** @brief Index into `entries_`. Kept rather than searched, so the two cannot drift. */
        std::size_t entry = 0;

        /** @brief True once this player has answered the capture request. */
        bool answered = false;
    };

    namespace
    {
        /** @brief Returns a filename-safe form of @p backend. */
        std::string toFileStem(const std::string& backend)
        {
            std::string stem;
            stem.reserve(backend.size());
            for (const char character : backend)
            {
                const bool safe = (character >= 'a' && character <= 'z')
                                  || (character >= 'A' && character <= 'Z')
                                  || (character >= '0' && character <= '9') || character == '-'
                                  || character == '_';
                stem.push_back(safe ? character : '-');
            }
            return stem.empty() ? std::string{"backend"} : stem;
        }
    }

    const char* toString(ComparisonState state)
    {
        switch (state)
        {
            case ComparisonState::Idle: return "Idle";
            case ComparisonState::Launching: return "Launching";
            case ComparisonState::Capturing: return "Capturing";
            case ComparisonState::Finished: return "Finished";
            case ComparisonState::Failed: return "Failed";
        }
        return "Idle";
    }

    std::string getDefaultComparisonDirectory(const std::string& projectPath)
    {
        if (projectPath.empty()) { return {}; }

        // Beside the project rather than in a temporary directory: the captures are the evidence,
        // and a user who finds a difference will want to look at both frames and keep them.
        const std::filesystem::path root = std::filesystem::path{projectPath}.parent_path();
        return (root / "build" / "comparison").generic_string();
    }

    std::string describeComparisonProblem(const ComparisonRequest& request)
    {
        if (request.projectPath.empty()) { return "no project is open"; }

        if (request.builds.size() < 2)
        {
            // One player is not a comparison. Saying so beats launching it and reporting the
            // useless truth that a backend matches itself.
            return "a comparison needs at least two cna-player builds; "
                   + std::to_string(request.builds.size())
                   + " was found. CNA fixes its backend at compile time, so each backend is a "
                     "separate player binary -- build another and install it beside the editor.";
        }

        if (request.outputDirectory.empty()) { return "no output directory was given"; }
        return {};
    }

    BackendComparison::BackendComparison() = default;
    BackendComparison::~BackendComparison() { cancel(); }

    bool BackendComparison::start(const ComparisonRequest& request, ImageReader reader, ImageWriter writer)
    {
        cancel();

        request_ = request;
        reader_ = std::move(reader);
        writer_ = std::move(writer);
        entries_.clear();
        sessions_.clear();
        error_.clear();
        capturesRequested_ = false;
        startedAtSeconds_ = 0.0;
        readyAtSeconds_ = 0.0;

        error_ = describeComparisonProblem(request_);
        if (!error_.empty())
        {
            state_ = ComparisonState::Failed;
            return false;
        }

        std::error_code errorCode;
        std::filesystem::create_directories(request_.outputDirectory, errorCode);
        if (errorCode)
        {
            error_ = "cannot create '" + request_.outputDirectory + "': " + errorCode.message();
            state_ = ComparisonState::Failed;
            return false;
        }

        std::vector<std::string> usedStems;

        for (const PlayerBuild& build : request_.builds)
        {
            ComparisonEntry entry;
            entry.backend = build.backend;

            // Made unique rather than assumed unique. Two entries can name the same backend --
            // discovery finds a build twice, or someone is comparing a rebuild against the binary
            // it replaced -- and two players writing to one path would compare a file with itself.
            std::string stem = toFileStem(build.backend);
            while (std::find(usedStems.begin(), usedStems.end(), stem) != usedStems.end())
            {
                stem += "-2";
            }
            usedStems.push_back(stem);

            entry.fileStem = stem;
            entry.capturePath =
                (std::filesystem::path{request_.outputDirectory} / (stem + ".png")).generic_string();

            // The first build that starts is the reference. Which backend that is matters less
            // than that it is one of them: a comparison against an absent "correct" image would
            // need someone to decide what correct looks like, and nobody has.
            entry.isReference = entries_.empty();

            auto session = std::make_unique<Session>();
            session->entry = entries_.size();

            if (!session->process.start(build, request_.projectPath, request_.scenePath))
            {
                // Recorded rather than fatal: one backend that will not launch is exactly the kind
                // of thing a comparison exists to find, and the others can still be compared.
                entry.errorMessage = session->process.getError();
                entry.isReference = false;
                entries_.push_back(std::move(entry));
                continue;
            }

            entries_.push_back(std::move(entry));
            sessions_.push_back(std::move(session));
        }

        if (sessions_.empty())
        {
            error_ = "no cna-player build could be started";
            state_ = ComparisonState::Failed;
            return false;
        }

        // The reference has to be one that actually started. Where the first build failed to
        // launch, the flag moved off it above and the first surviving entry takes it.
        if (std::none_of(entries_.begin(), entries_.end(),
                         [](const ComparisonEntry& entry) { return entry.isReference; }))
        {
            entries_[sessions_.front()->entry].isReference = true;
        }

        state_ = ComparisonState::Launching;
        return true;
    }

    void BackendComparison::poll(double nowSeconds)
    {
        if (state_ != ComparisonState::Launching && state_ != ComparisonState::Capturing) { return; }

        if (startedAtSeconds_ == 0.0) { startedAtSeconds_ = nowSeconds; }

        bool everyoneReady = true;
        bool everyoneAnswered = true;

        for (const std::unique_ptr<Session>& session : sessions_)
        {
            ComparisonEntry& entry = entries_[session->entry];

            for (const EditorMessage& message : session->process.poll())
            {
                if (message.type != EditorMessageType::ScreenshotReady) { continue; }

                session->answered = true;
                entry.captured = message.payload["written"].asBoolean(false);
                if (!entry.captured)
                {
                    entry.errorMessage = message.payload["error"].asString();
                    if (entry.errorMessage.empty()) { entry.errorMessage = "the capture failed"; }
                }
            }

            if (!session->process.isRunning() && !session->answered)
            {
                // The player died before answering. Its entry says so, and the run continues:
                // whether the *others* agree is still worth knowing.
                session->answered = true;
                entry.errorMessage = "the player exited before it produced a frame ("
                                     + std::string{toString(session->process.getExitReason())} + ")";
            }

            // A session that has already answered -- normally because its player died -- counts as
            // ready. Without this a player that exits immediately holds the launch phase open for
            // the whole timeout waiting for a handshake that can never arrive.
            if (session->process.getReportedBackend().empty() && !session->answered)
            {
                everyoneReady = false;
            }
            if (!session->answered) { everyoneAnswered = false; }
        }

        if (state_ == ComparisonState::Launching)
        {
            if (!everyoneReady)
            {
                if (nowSeconds - startedAtSeconds_ > request_.timeoutSeconds)
                {
                    error_ = "timed out waiting for the players to start";
                    finish();
                }
                return;
            }

            if (readyAtSeconds_ == 0.0) { readyAtSeconds_ = nowSeconds; }

            // The warm-up, in seconds at a nominal sixty frames. Asking for a frame the instant a
            // player says hello can catch a backend before its first texture upload has finished,
            // and a comparison of two warm-ups reports differences that are gone a moment later.
            constexpr double kNominalFrameSeconds = 1.0 / 60.0;
            if (nowSeconds - readyAtSeconds_ < request_.warmupFrames * kNominalFrameSeconds) { return; }

            requestCaptures();
            state_ = ComparisonState::Capturing;
            return;
        }

        if (everyoneAnswered)
        {
            finish();
            return;
        }

        if (nowSeconds - startedAtSeconds_ > request_.timeoutSeconds)
        {
            for (const std::unique_ptr<Session>& session : sessions_)
            {
                if (session->answered) { continue; }
                entries_[session->entry].errorMessage = "timed out waiting for a frame";
            }
            error_ = "timed out waiting for one or more captures";
            finish();
        }
    }

    void BackendComparison::requestCaptures()
    {
        if (capturesRequested_) { return; }
        capturesRequested_ = true;

        for (const std::unique_ptr<Session>& session : sessions_)
        {
            const ComparisonEntry& entry = entries_[session->entry];

            // The scene is loaded by the player at start-up from the project, so all that is asked
            // for here is the frame. Sending loadScene as well would race the start-up load and
            // capture whichever finished first.
            session->process.send(EditorMessage::makeScreenshot(entry.capturePath));
        }
    }

    void BackendComparison::finish()
    {
        for (const std::unique_ptr<Session>& session : sessions_) { session->process.stop(); }
        sessions_.clear();

        compareAgainstReference();
        state_ = ComparisonState::Finished;
    }

    void BackendComparison::compareAgainstReference()
    {
        if (!reader_) { return; }

        const auto reference = std::find_if(entries_.begin(), entries_.end(),
                                            [](const ComparisonEntry& entry) { return entry.isReference; });
        if (reference == entries_.end() || !reference->captured) { return; }

        const ImageBuffer referenceImage = reader_(reference->capturePath);
        if (referenceImage.isEmpty())
        {
            reference->errorMessage = "the reference capture could not be read back";
            return;
        }

        for (ComparisonEntry& entry : entries_)
        {
            if (entry.isReference || !entry.captured) { continue; }

            const ImageBuffer image = reader_(entry.capturePath);
            if (image.isEmpty())
            {
                entry.errorMessage = "the capture could not be read back";
                continue;
            }

            entry.difference = compareImages(referenceImage, image, request_.tolerance);

            // Only when something actually differs. A difference image of a matching pair is a
            // dimmed copy of the reference with nothing marked on it, and a directory full of
            // those is a directory nobody reads.
            if (!writer_ || entry.difference.matches()) { continue; }

            const std::string path =
                (std::filesystem::path{request_.outputDirectory}
                 / (entry.fileStem + "-diff.png")).generic_string();

            if (writer_(path, makeDifferenceImage(referenceImage, image, request_.tolerance)))
            {
                entry.differencePath = path;
            }
        }
    }

    std::string BackendComparison::getReferenceBackend() const
    {
        for (const ComparisonEntry& entry : entries_)
        {
            if (entry.isReference) { return entry.backend; }
        }
        return {};
    }

    bool BackendComparison::allBackendsAgree() const
    {
        for (const ComparisonEntry& entry : entries_)
        {
            if (entry.isReference) { continue; }
            if (!entry.errorMessage.empty()) { return false; }
            if (!entry.difference.matches()) { return false; }
        }
        return true;
    }

    void BackendComparison::cancel()
    {
        for (const std::unique_ptr<Session>& session : sessions_) { session->process.stop(); }
        sessions_.clear();

        if (state_ == ComparisonState::Launching || state_ == ComparisonState::Capturing)
        {
            state_ = ComparisonState::Idle;
        }
    }
}
