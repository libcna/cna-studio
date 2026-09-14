// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/RuntimeBridge/BackendComparison.hpp
 * @brief Running one scene on every installed backend and comparing the pictures.
 *
 * plan.md ED-510, and the item that turns ANALYSIS.md finding F-01 from a constraint into a
 * feature. CNA fixes its graphics backend at *compile* time, so "does my game look the same on
 * Direct3D as on OpenGL" is a question no single process can answer -- and the editor already
 * launches one process per backend for play mode. Doing it several times over and comparing the
 * results is the same mechanism, not new architecture.
 *
 * What this class owns is the *sequence*: launch each player, wait for its handshake, ask each for
 * the same frame, wait for the files, compare them against the first backend to answer. It owns no
 * pixels and decodes no images -- decoding a PNG needs a graphics API, and only one module may link
 * CNA (decision D-03), so the reader is injected. That also makes the whole sequence testable with
 * images built in a test.
 */

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "CNA/Editor/Core/ImageDiff.hpp"
#include "CNA/Editor/RuntimeBridge/PlayerProcess.hpp"

namespace CNA::Editor
{
    /** @brief Reads an image file into memory. Returns an empty buffer when it cannot. */
    using ImageReader = std::function<ImageBuffer(const std::string& path)>;

    /** @brief Writes an image to a file. Returns false when it cannot. */
    using ImageWriter = std::function<bool(const std::string& path, const ImageBuffer& image)>;

    /** @brief What a comparison run has got to. */
    enum class ComparisonState
    {
        Idle,
        /** @brief Players are starting and handshaking. */
        Launching,
        /** @brief Every player has been asked for a frame; waiting for the files. */
        Capturing,
        /** @brief Finished; every entry carries its result. */
        Finished,
        /** @brief Could not run at all -- fewer than one player, or no project. */
        Failed
    };

    /** @brief Returns the display name of @p state. */
    const char* toString(ComparisonState state);

    /** @brief One backend's part in a comparison. */
    struct ComparisonEntry
    {
        /** @brief The backend name as discovery reported it, e.g. "easygl". */
        std::string backend;

        /** @brief Where this backend's capture was written. */
        std::string capturePath;

        /** @brief The filename stem the capture and its difference image share. Unique per entry. */
        std::string fileStem;

        /**
         * @brief Where the difference image against the reference was written.
         *
         * Empty for the reference itself, for an entry that produced no frame, and when the caller
         * supplied no writer. Written beside the captures rather than shown alone, because "which
         * pixels" is only half the answer -- *where on the picture* is the other half, and it is
         * usually the whole diagnosis.
         */
        std::string differencePath;

        /** @brief True once the player answered that the file is on disk. */
        bool captured = false;

        /** @brief How this backend's frame differs from the reference's. */
        ImageDifference difference;

        /** @brief Why this entry has no result. Empty while it is still working or when it worked. */
        std::string errorMessage;

        /** @brief True for the backend every other one is compared against. */
        bool isReference = false;
    };

    /**
     * @brief Settings for one comparison run.
     */
    struct ComparisonRequest
    {
        /** @brief Absolute path to the `.cnaproject` every player opens. */
        std::string projectPath;

        /** @brief Project-relative scene, or empty for the project's startup scene. */
        std::string scenePath;

        /** @brief Directory the captures and difference images are written to. */
        std::string outputDirectory;

        /** @brief The player builds to run. Normally everything discovery found. */
        std::vector<PlayerBuild> builds;

        /**
         * @brief Frames to let each player draw before asking it for one.
         *
         * Not zero: a first frame can catch a backend before its first texture upload has
         * completed, and comparing warm-up artefacts would report differences that vanish a
         * sixtieth of a second later.
         */
        int warmupFrames = 10;

        /** @brief Largest per-channel difference still counted as the same pixel. */
        int tolerance = kDefaultImageTolerance;

        /**
         * @brief How long to wait for a player to hand back its capture, in seconds.
         *
         * A player that never answers must not hang the editor. Passed in rather than fixed
         * because a first run on a cold shader cache is far slower than the ones after it.
         */
        double timeoutSeconds = 30.0;
    };

    /** @brief Returns the default output directory for @p projectPath: `<project>/build/comparison`. */
    [[nodiscard]] std::string getDefaultComparisonDirectory(const std::string& projectPath);

    /**
     * @brief Returns why @p request cannot run, or an empty string when it can.
     *
     * Checked before anything is launched. The common failure is having exactly one player build
     * installed, which is not a comparison -- and saying so beats spawning one process and
     * reporting that it matches itself.
     */
    [[nodiscard]] std::string describeComparisonProblem(const ComparisonRequest& request);

    /**
     * @brief Runs one scene on several backends and compares the frames they draw.
     *
     * Non-blocking: `start()` launches the players and `poll()` is called once per editor frame.
     * A comparison that blocked would freeze the editor for as long as the slowest backend takes
     * to open a window, which on a first run is seconds.
     */
    class BackendComparison
    {
    public:
        BackendComparison();
        ~BackendComparison();

        BackendComparison(const BackendComparison&) = delete;
        BackendComparison& operator=(const BackendComparison&) = delete;

        /**
         * @brief Launches every build in @p request.
         *
         * @param reader Decodes a written capture. Supplied by the caller because decoding needs a
         *        graphics API and this module may not link CNA.
         * @return False when the request is unusable or no player could be started; the state is
         *         then Failed and getError() says why.
         */
        bool start(const ComparisonRequest& request, ImageReader reader, ImageWriter writer = {});

        /**
         * @brief Advances the run: handshakes, capture requests, replies and comparisons.
         * @param nowSeconds A monotonic clock, passed in so a test can drive the timeout without
         *        waiting for one -- the same reason every other clock in the editor is a parameter.
         */
        void poll(double nowSeconds);

        /** @brief Stops every player and abandons the run. */
        void cancel();

        [[nodiscard]] ComparisonState getState() const { return state_; }
        [[nodiscard]] const std::string& getError() const { return error_; }
        [[nodiscard]] const std::vector<ComparisonEntry>& getEntries() const { return entries_; }

        /** @brief Returns the backend every other one was compared against, or an empty string. */
        [[nodiscard]] std::string getReferenceBackend() const;

        /** @brief Returns true when every comparable entry matched its reference. */
        [[nodiscard]] bool allBackendsAgree() const;

    private:
        void requestCaptures();
        void finish();
        void compareAgainstReference();

        struct Session;

        ComparisonRequest request_;
        ImageReader reader_;
        ImageWriter writer_;
        std::vector<std::unique_ptr<Session>> sessions_;
        std::vector<ComparisonEntry> entries_;
        std::string error_;
        ComparisonState state_ = ComparisonState::Idle;
        double startedAtSeconds_ = 0.0;
        double readyAtSeconds_ = 0.0;
        bool capturesRequested_ = false;
    };
}
