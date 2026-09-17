// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Project/BuildRunner.hpp
 * @brief Running a planned build: the commands, their state, and the process that executes them.
 *
 * `plan.md` ED-308, generalised by `STUDIO-02081`. Three decisions are worth stating, because each
 * had a plausible alternative.
 *
 * **Studio shells out rather than writing a script for the user to run.** Shelling out means
 * Studio has the exit code to report and the output to show, which is what makes this a build
 * *command* rather than a note; and the machinery already exists, since play mode spawns and
 * supervises a child process. The cost is that a real build has options Studio does not model,
 * which is why the exact command line is shown before it runs and the build directory is left
 * where the user can drive it by hand.
 *
 * **Studio drives the *project's* build system, not one it generates.** A game's build is the
 * game's business: it has its own targets, its own dependencies and possibly its own options. All
 * Studio contributes is what it actually knows — which renderer to select and where to put the
 * output.
 *
 * **Nothing here knows which build system that is.** A `BuildStep` is an executable and its
 * arguments; a @ref StudioBuildJob is a list of them and a directory to run them into. Which
 * commands those are is decided by the project's language adapter
 * (`CNA/Studio/Project/LanguageAdapter.hpp`), and for a C++ project that adapter runs CMake exactly
 * as Studio always has. Keeping the *runner* free of that is what lets the Build panel, the
 * notifications and the log be written once rather than once per language.
 */

#include <memory>
#include <string>
#include <vector>

namespace CNA::Studio
{
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
     * @brief A build that has been planned and not yet run.
     *
     * The currency between a language adapter, which decides what to run, and everything else,
     * which reports on it. Empty @ref steps means the project cannot be built; the adapter's
     * `describeBuildProblem` says why, in a sentence meant for a user.
     */
    struct StudioBuildJob
    {
        /** @brief The commands, in the order they must run. */
        std::vector<BuildStep> steps;

        /** @brief Absolute path of the directory the build writes into, and the log with it. */
        std::string buildDirectory;

        /** @brief What this build is, for the log's first line, e.g. `"linux-x64, Release"`. */
        std::string description;

        /** @brief Whether there is anything to run. */
        [[nodiscard]] bool empty() const { return steps.empty(); }
    };

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
     * @brief Runs a job's steps one after another, logging to a file.
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
         * @brief Starts @p job, replacing any finished build.
         *
         * The job arrives already planned. A runner that planned its own build would have to know
         * which build system to plan for, which is the one thing this type is kept ignorant of.
         *
         * @return False when a build is already running or @p job has no steps; @p errorMessage
         *         says which.
         */
        bool start(const StudioBuildJob& job, std::string* errorMessage = nullptr);

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
