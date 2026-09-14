// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Project/BuildRunner.hpp
 * @brief Building a game from the editor, by driving the project's own CMake.
 *
 * plan.md ED-308. Three decisions are worth stating, because each had a plausible alternative.
 *
 * **The editor shells out to `cmake` rather than writing a script for the user to run.** Shelling
 * out means the editor has the exit code to report and the output to show, which is what makes this
 * a build *command* rather than a note; and the machinery already exists, since play mode spawns and
 * supervises a child process. The cost is that a real build has options the editor does not model,
 * which is why the exact command line is shown before it runs and the build directory is left where
 * the user can drive it by hand.
 *
 * **The editor drives the *project's* CMakeLists, not one it generates.** A game's build is the
 * game's business: it has its own targets, its own dependencies and possibly its own options. All
 * the editor contributes is what it actually knows -- which backend to select and where to put the
 * output.
 *
 * **A missing toolchain is detected before anything is offered.** Anyone who installed an editor and
 * not a compiler is the common case, and "nothing happened" is the worst possible answer.
 */

#include <memory>
#include <string>
#include <vector>

namespace CNA::Editor
{
    class Project;

    /** @brief What to build, and where to put it. */
    struct BuildRequest
    {
        /** @brief Absolute path to the directory holding the project's `CMakeLists.txt`. */
        std::string projectRoot;

        /** @brief Absolute path for the build tree. Empty means the default beside the project. */
        std::string buildDirectory;

        /** @brief The platform from the project's `targetPlatforms`, e.g. "linux-x64". */
        std::string targetPlatform;

        /** @brief CNA's own `CNA_GRAPHICS_BACKEND` value, e.g. "EASYGL". */
        std::string graphicsBackend;

        /** @brief CMake build type, e.g. "Release". */
        std::string configuration = "Release";

        /** @brief The `cmake` executable to run. Empty means look on the PATH. */
        std::string cmakePath;
    };

    /** @brief One command the build runs, as an executable and its arguments. */
    struct BuildStep
    {
        std::string description;
        std::string executable;
        std::vector<std::string> arguments;

        /** @brief Returns the command as one line, for showing the user what will run. */
        [[nodiscard]] std::string toCommandLine() const;
    };

    /**
     * @brief Returns the default build directory for @p request: `<root>/build/<platform>`.
     *
     * Beside the project rather than in a temporary directory, because a build people iterate on
     * has to be incremental, and because they will want to open it in their own tools. It is not
     * added to `.gitignore` -- the editor does not edit a user's repository configuration -- so
     * the panel says where it went.
     */
    [[nodiscard]] std::string getDefaultBuildDirectory(const BuildRequest& request);

    /**
     * @brief Returns the configure and build commands, in the order they must run.
     *
     * Pure: no filesystem, no process, no clock. That is what makes the *interesting* part of this
     * feature -- which options are passed and in which order -- testable without a compiler on the
     * machine running the tests.
     *
     * Returns an empty list when the request is unusable, which the caller reports; see
     * `describeBuildProblem`.
     */
    [[nodiscard]] std::vector<BuildStep> planBuild(const BuildRequest& request);

    /**
     * @brief Returns why @p request cannot be built, or an empty string when it can.
     *
     * Checked before anything is offered rather than after something fails, because the failure of
     * a missing compiler arrives as a wall of CMake output that says nothing a user can act on.
     */
    [[nodiscard]] std::string describeBuildProblem(const BuildRequest& request);

    /**
     * @brief Returns the full path to a usable `cmake`, or an empty string.
     *
     * Searched on the PATH rather than at a fixed location, since every platform installs it
     * somewhere different and a user may well have several.
     */
    [[nodiscard]] std::string findCMake();

    /** @brief Fills in a request from @p project, leaving the caller's explicit choices alone. */
    [[nodiscard]] BuildRequest makeBuildRequest(const Project& project,
                                                std::string targetPlatform,
                                                std::string graphicsBackend);

    /** @brief Where a build has got to. */
    enum class BuildState
    {
        Idle,
        Running,
        Succeeded,
        Failed
    };

    /** @brief Returns the display name of @p state. */
    const char* toString(BuildState state);

    /**
     * @brief Runs a build's steps one after another, logging to a file.
     *
     * A file rather than a pipe, deliberately: it survives the editor, the user can open it in
     * whatever they read logs with, and a build that failed an hour ago is still explainable. The
     * panel shows its tail.
     */
    class BuildProcess
    {
    public:
        BuildProcess();
        ~BuildProcess();

        BuildProcess(const BuildProcess&) = delete;
        BuildProcess& operator=(const BuildProcess&) = delete;

        /**
         * @brief Starts @p request, replacing any finished build.
         * @return False when a build is already running or the request is unusable; @p errorMessage
         *         says which.
         */
        bool start(const BuildRequest& request, std::string* errorMessage = nullptr);

        /**
         * @brief Advances the build: reaps a finished step and starts the next.
         *
         * Called once per editor frame. Never blocks -- a build that froze the editor while it ran
         * would be worse than one the user has to start from a terminal.
         */
        void poll();

        /** @brief Asks the running step to stop, and gives up on the rest. */
        void cancel();

        [[nodiscard]] BuildState getState() const { return state_; }
        [[nodiscard]] const std::string& getLogPath() const { return logPath_; }
        [[nodiscard]] const std::vector<BuildStep>& getSteps() const { return steps_; }

        /** @brief Returns which step is running, or has finished, as a 1-based index. */
        [[nodiscard]] std::size_t getStepNumber() const { return stepIndex_; }

        /** @brief Returns the last @p lines of the log, oldest first. */
        [[nodiscard]] std::vector<std::string> readLogTail(std::size_t lines) const;

    private:
        bool startStep(std::size_t index, std::string* errorMessage);

        struct Impl;
        std::unique_ptr<Impl> impl_;

        std::vector<BuildStep> steps_;
        std::string logPath_;
        std::size_t stepIndex_ = 0;
        BuildState state_ = BuildState::Idle;
    };
}
