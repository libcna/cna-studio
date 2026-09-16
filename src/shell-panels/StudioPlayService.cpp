// SPDX-License-Identifier: MS-PL
/**
 * @file StudioPlayService.cpp
 * @brief Launching, driving and reporting on the game Studio plays.
 */

#include "CNA/Studio/ShellPanels/StudioPlayService.hpp"

#include "CNA/Studio/Project/Project.hpp"
#include "CNA/Studio/Scene/SceneDocument.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace CNA::Studio
{
    StudioPlayService::StudioPlayService(StudioContext& context, StudioLog& log,
                                         std::function<void(StudioNotification)> notify)
        : context_(context), log_(log), notify_(std::move(notify))
    {
    }

    void StudioPlayService::setBuilds(std::vector<PlayerBuild> builds)
    {
        builds_ = std::move(builds);

        // An override naming a build that is no longer installed is an override that would make
        // Play fall back silently to something else. Dropped rather than kept, so what the panel
        // shows is what Play will do.
        if (!override_.empty()
            && std::none_of(builds_.begin(), builds_.end(),
                            [this](const PlayerBuild& build) {
                                return build.backend == override_;
                            }))
        {
            override_.clear();
        }
    }

        bool StudioPlayService::selectBuild(const std::string& backend)
    {
        if (backend.empty())
        {
            const bool had = !override_.empty();
            override_.clear();
            if (had)
            {
                log_.append(LogSeverity::Info,
                            "Play will use whatever the project's target profile names.");
            }
            return true;
        }

        for (const PlayerBuild& build : builds_)
        {
            if (build.backend != backend) { continue; }
            if (override_ != backend)
            {
                override_ = backend;
                // Said, because it is a promise that now differs from the project's. A user who
                // has forgotten they set it would otherwise see the editor disagree with the
                // project for no visible reason.
                log_.append(LogSeverity::Info,
                            "Play will use " + backend + " for this session, whatever the project "
                            "ships on.");
            }
            return true;
        }
        return false;
    }

    const PlayerBuild* StudioPlayService::chooseBuild() const
    {
        if (builds_.empty()) { return nullptr; }

        // What the user said, if they said anything. An override outranks the project because it
        // is the more recent and more specific decision, and because the only reason to set one is
        // to see this scene on that renderer now.
        if (!override_.empty())
        {
            for (const PlayerBuild& build : builds_)
            {
                if (build.backend == override_) { return &build; }
            }
        }

        // The renderer the project says it ships on, when a player for it was built. Otherwise
        // whatever is there: a user pressing Play wants to see their game, and refusing because
        // the preferred renderer is missing helps nobody.
        const std::string preferred = context_.getProject().getActiveTargetProfile().renderer;
        for (const PlayerBuild& build : builds_)
        {
            if (build.backend == preferred) { return &build; }
        }
        return &builds_.front();
    }

    void StudioPlayService::start()
    {
        const PlayerBuild* build = chooseBuild();
        if (build == nullptr)
        {
            log_.append(LogSeverity::Error,
                        "No player build was found beside this executable. CNA fixes its renderer "
                        "at compile time, so Play needs a cna-player-<renderer> binary to launch.");
            return;
        }

        // The player is a separate process and reads the scene from disk, so what is on screen has
        // to be *there* first. Saving silently would be worse than refusing: a user who has not
        // saved deliberately would find their file overwritten by pressing Play.
        if (context_.getScenePath().empty())
        {
            log_.append(LogSeverity::Warning,
                        "Save the scene before playing: the player is a separate process and "
                        "reads it from disk.");
            return;
        }
        if (context_.getHistory().isDirty() && !context_.saveScene())
        {
            log_.append(LogSeverity::Error, "Could not save the scene; not starting the player.");
            return;
        }

        // Relative to the project, like the build's paths: two processes need not agree on a
        // working directory, and the project root is the one anchor both already have.
        std::error_code relativeError;
        const std::filesystem::path relative = std::filesystem::relative(
            std::filesystem::path{context_.getScenePath()},
            std::filesystem::path{context_.getProject().getFilePath()}.parent_path(),
            relativeError);

        if (!player_.start(*build, context_.getProject().getFilePath(),
                           relativeError ? std::string{} : relative.generic_string()))
        {
            log_.append(LogSeverity::Error, "Could not start the player: " + player_.getError());
            return;
        }

        wasRunning_ = true;
        log_.append(LogSeverity::Info,
                    "Playing on " + build->backend + " (" + build->executablePath + ").");
        state_ = StudioPlayState::Playing;
    }

    bool StudioPlayService::setPaused(bool paused)
    {
        if (state_ == StudioPlayState::Stopped) { return false; }
        if ((state_ == StudioPlayState::Paused) == paused) { return false; }

        StudioMessage message;
        message.type = paused ? StudioMessageType::Pause : StudioMessageType::Resume;
        message.payload = JsonValue::makeObject();

        // Only follow the player's state once the request is actually on the wire.
        if (!player_.send(message)) { return false; }

        state_ = paused ? StudioPlayState::Paused : StudioPlayState::Playing;
        log_.append(LogSeverity::Info, paused ? "Paused the player." : "Resumed the player.");
        return true;
    }

    bool StudioPlayService::forwardInput(const PlayerInputSnapshot& snapshot)
    {
        if (!player_.isRunning() || state_ == StudioPlayState::Stopped) { return false; }

        // Only on a change, and a wheel notch always counts as one. Sixty identical snapshots a
        // second would be sixty round trips that told the player nothing -- and the player answers
        // every one of them, so the waste would be doubled.
        if (snapshot == lastForwarded_ && snapshot.wheel == 0.0f) { return false; }

        lastForwarded_ = snapshot;
        return player_.send(StudioMessage::makeInput(snapshot));
    }

    bool StudioPlayService::reloadAsset(const Uuid& assetId)
    {
        // Only while a game is actually running. Sending to a stopped player is not merely useless
        // -- there is no process to send to, and the failure would be reported as a broken bridge.
        if (!player_.isRunning() || state_ == StudioPlayState::Stopped) { return false; }

        return player_.send(StudioMessage::makeReloadAsset(assetId));
    }

    bool StudioPlayService::mirrorEdit(const Uuid& entityId, const std::string& componentTypeId,
                                       const std::string& propertyName, const PropertyValue& value)
    {
        if (!player_.isRunning() || state_ == StudioPlayState::Stopped) { return false; }

        return player_.send(StudioMessage::makeSetProperty(entityId, componentTypeId, propertyName, value));
    }

    bool StudioPlayService::stepFrame()
    {
        if (state_ != StudioPlayState::Paused) { return false; }

        StudioMessage message;
        message.type = StudioMessageType::StepFrame;
        message.payload = JsonValue::makeObject();
        return player_.send(message);
    }

    void StudioPlayService::restart()
    {
        // Stop and start, rather than a message asking the game to reload itself. The player reads
        // the scene from disk when it starts, so a restart is how the user sees the edits they have
        // made since -- which is what they mean by it.
        if (player_.isRunning()) { stop(); }
        start();
    }

    void StudioPlayService::stop()
    {
        if (!player_.isRunning()) { return; }
        player_.stop();
        state_ = StudioPlayState::Stopped;
        // Said here, so the poll that follows does not report the same ending a second time as an
        // exit the editor did not expect.
        wasRunning_ = false;
        log_.append(LogSeverity::Info, "Stopped the player.");
    }


    std::size_t StudioPlayService::poll()
    {
        std::size_t read = 0;
        for (const StudioMessage& message : player_.poll())
        {
            ++read;
            switch (message.type)
            {
                case StudioMessageType::Ready:
                    // What the player *actually* got, not what Studio asked for. CNA fixes its
                    // renderer at compile time and a player can be built for one and report
                    // another; hearing it from the player is the only way to know.
                    log_.append(LogSeverity::Info,
                                "Player ready on " + player_.getReportedBackend() + ".");
                    break;
                case StudioMessageType::ReportException:
                    log_.append(LogSeverity::Error,
                                "Player: " + message.payload["message"].asString("an exception"));
                    break;
                default:
                    break;
            }
        }

        // Compared against what was remembered rather than against a fresh query taken a moment
        // ago: anything at all may have asked whether the player is running in between -- the
        // toolbar does, every frame, to decide whether Stop is available -- and the first such
        // question is what notices the exit. Reading the transition from a local `wasRunning`
        // would therefore miss it exactly when the editor was doing its job.
        const bool running = player_.isRunning();
        if (running == wasRunning_) { return read; }
        wasRunning_ = running;
        if (running) { return read; }

        // A game that exited while paused leaves nothing paused. Without this the Pause command
        // stays checked over a game that is not there, and Step offers to advance it.
        state_ = StudioPlayState::Stopped;

        // Said either way. A game that exited because it finished and one that crashed look
        // identical from the editor unless the reason is reported.
        const PlayerExitReason reason = player_.getExitReason();
        if (reason != PlayerExitReason::Crashed)
        {
            // A game the user closed is a game the user was looking at. Announcing that would be
            // telling them what they just did.
            log_.append(LogSeverity::Info, std::string{"Player exited: "} + toString(reason) + ".");
            return read;
        }

        StudioNotification notification;
        notification.id = "studio.play";
        notification.severity = StudioNotificationSeverity::Error;
        notification.title = "The game crashed";
        notification.detail = std::string{"Player exited: "} + toString(reason) + ".";
        notification.actionId = StudioShell::showPanelActionId("output");
        if (notify_) { notify_(std::move(notification)); }
        return read;
    }
}
