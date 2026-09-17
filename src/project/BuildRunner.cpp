// SPDX-License-Identifier: MS-PL
/**
 * @file BuildRunner.cpp
 * @brief Running a planned build, in no particular build system.
 *
 * `plan.md` STUDIO-02081. Whatever decides *which* commands these are lives behind
 * `StudioLanguageAdapter`; for a C++ project that is `src/project/cpp/CppToolchain.cpp`, which
 * this file was split from and which it deliberately no longer knows about.
 */

#include "CNA/Studio/Project/BuildRunner.hpp"

#include <deque>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#if defined(_WIN32)
#    include <windows.h>
#else
#    include <csignal>
#    include <cstdio>
#    include <sys/wait.h>
#    include <unistd.h>
#endif

namespace CNA::Studio
{
    namespace
    {
        /** @brief Returns @p value with quotes around it when it contains a space. */
        std::string quoteIfNeeded(const std::string& value)
        {
            return value.find(' ') == std::string::npos ? value : "\"" + value + "\"";
        }
    }

    std::string BuildStep::toCommandLine() const
    {
        std::string line = quoteIfNeeded(executable);
        for (const std::string& argument : arguments) { line += " " + quoteIfNeeded(argument); }
        return line;
    }

    const char* toString(BuildState state)
    {
        switch (state)
        {
            case BuildState::Idle: return "idle";
            case BuildState::Running: return "running";
            case BuildState::Succeeded: return "succeeded";
            case BuildState::Failed: return "failed";
        }
        return "idle";
    }


    struct BuildProcess::Impl
    {
#if defined(_WIN32)
        PROCESS_INFORMATION process{};
        HANDLE logHandle = INVALID_HANDLE_VALUE;
        bool spawned = false;
#else
        pid_t pid = -1;
#endif

        /** @brief Returns whether the child is still running. */
        [[nodiscard]] bool isAlive() const
        {
#if defined(_WIN32)
            if (!spawned) { return false; }
            DWORD exitCode = 0;
            return GetExitCodeProcess(process.hProcess, &exitCode) && exitCode == STILL_ACTIVE;
#else
            if (pid <= 0) { return false; }
            int status = 0;
            return ::waitpid(pid, &status, WNOHANG) == 0;
#endif
        }

        /**
         * @brief Reaps the child and reports whether it succeeded.
         *
         * @param stillRunning Set when there is nothing to reap yet.
         */
        bool reap(bool& stillRunning)
        {
            stillRunning = false;
#if defined(_WIN32)
            if (!spawned) { return false; }

            DWORD exitCode = 0;
            if (GetExitCodeProcess(process.hProcess, &exitCode) && exitCode == STILL_ACTIVE)
            {
                stillRunning = true;
                return false;
            }

            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            process = PROCESS_INFORMATION{};
            spawned = false;
            if (logHandle != INVALID_HANDLE_VALUE) { CloseHandle(logHandle); logHandle = INVALID_HANDLE_VALUE; }
            return exitCode == 0;
#else
            if (pid <= 0) { return false; }

            int status = 0;
            const pid_t result = ::waitpid(pid, &status, WNOHANG);
            if (result == 0)
            {
                stillRunning = true;
                return false;
            }

            pid = -1;
            return result > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
        }

        void terminate()
        {
#if defined(_WIN32)
            if (spawned) { TerminateProcess(process.hProcess, 1); }
#else
            if (pid > 0) { ::kill(pid, SIGTERM); }
#endif
        }

        /** @brief Spawns @p step with its output appended to @p logPath. */
        bool spawn(const BuildStep& step, const std::string& logPath, std::string& error)
        {
#if defined(_WIN32)
            SECURITY_ATTRIBUTES security{};
            security.nLength = sizeof(security);
            security.bInheritHandle = TRUE;

            logHandle = CreateFileA(logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    &security, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (logHandle == INVALID_HANDLE_VALUE)
            {
                error = "cannot open the build log for writing";
                return false;
            }

            std::string commandLine = step.toCommandLine();

            STARTUPINFOA startup{};
            startup.cb = sizeof(startup);
            startup.dwFlags = STARTF_USESTDHANDLES;
            startup.hStdOutput = logHandle;
            startup.hStdError = logHandle;

            if (!CreateProcessA(nullptr, commandLine.data(), nullptr, nullptr, TRUE, 0, nullptr,
                                nullptr, &startup, &process))
            {
                error = "CreateProcess failed with " + std::to_string(GetLastError());
                CloseHandle(logHandle);
                logHandle = INVALID_HANDLE_VALUE;
                return false;
            }
            spawned = true;
            return true;
#else
            std::vector<std::string> storage;
            storage.push_back(step.executable);
            storage.insert(storage.end(), step.arguments.begin(), step.arguments.end());

            std::vector<char*> argv;
            argv.reserve(storage.size() + 1);
            for (std::string& value : storage) { argv.push_back(value.data()); }
            argv.push_back(nullptr);

            const pid_t child = ::fork();
            if (child < 0)
            {
                error = "fork failed";
                return false;
            }
            if (child == 0)
            {
                // Both streams to the log, appended: a build's errors and its progress interleave
                // in the order they happened, which is the order anyone reading them wants.
                //
                // The results are inspected rather than discarded. glibc marks freopen
                // warn_unused_result, and a (void) cast does not silence that under GCC -- which is
                // just as well, because a redirect that failed means the build's output goes
                // nowhere and the log looks empty for a reason that has nothing to do with the
                // build. There is nowhere useful to report it from inside a forked child whose
                // streams have just been taken away, so the child exits instead: an exec that runs
                // with no captured output would produce a build whose log cannot be read.
                const std::FILE* const outRedirected = std::freopen(logPath.c_str(), "a", stdout);
                const std::FILE* const errRedirected =
                    outRedirected != nullptr ? std::freopen(logPath.c_str(), "a", stderr) : nullptr;
                if (outRedirected == nullptr || errRedirected == nullptr) { ::_exit(126); }

                ::execv(step.executable.c_str(), argv.data());

                // Reached only when execv failed. _exit rather than exit: the child is a copy of
                // the editor, and running its atexit handlers here would flush its buffers twice.
                ::_exit(127);
            }
            pid = child;
            return true;
#endif
        }
    };

    BuildProcess::BuildProcess() : impl_(std::make_unique<Impl>()) {}

    BuildProcess::~BuildProcess()
    {
        if (impl_ && impl_->isAlive()) { impl_->terminate(); }
    }

    bool BuildProcess::start(const StudioBuildJob& job, std::string* errorMessage)
    {
        const auto fail = [errorMessage](std::string reason) {
            if (errorMessage != nullptr) { *errorMessage = std::move(reason); }
            return false;
        };

        if (state_ == BuildState::Running) { return fail("a build is already running"); }

        // Planned by the project's language adapter, which is also what refused it with a sentence
        // a user can act on. "Nothing to build" is the fallback for a caller that started an empty
        // job without asking first, not the message anybody should normally see.
        if (job.empty()) { return fail("nothing to build"); }
        if (job.buildDirectory.empty()) { return fail("the build has nowhere to write"); }

        steps_ = job.steps;

        std::error_code errorCode;
        std::filesystem::create_directories(job.buildDirectory, errorCode);
        if (errorCode)
        {
            return fail("cannot create '" + job.buildDirectory + "': " + errorCode.message());
        }

        logPath_ =
            (std::filesystem::path{job.buildDirectory} / "cna-studio-build.log").generic_string();

        // Truncated at the start of a build rather than appended to for ever. A log holding four
        // builds is one nobody can tell apart; each step then appends to this one.
        {
            std::ofstream truncate{logPath_, std::ios::binary | std::ios::trunc};
            if (!truncate) { return fail("cannot write '" + logPath_ + "'"); }
            truncate << "cna-studio build: " << job.description << "\n";
            for (const BuildStep& step : steps_) { truncate << "  " << step.toCommandLine() << "\n"; }
            truncate << "\n";
        }

        stepIndex_ = 0;
        state_ = BuildState::Running;

        if (!startStep(0, errorMessage))
        {
            state_ = BuildState::Failed;
            return false;
        }
        return true;
    }

    bool BuildProcess::startStep(std::size_t index, std::string* errorMessage)
    {
        if (index >= steps_.size()) { return false; }

        stepIndex_ = index + 1;

        std::string error;
        if (impl_->spawn(steps_[index], logPath_, error))
        {
            return true;
        }
        if (errorMessage != nullptr) { *errorMessage = error; }
        return false;
    }

    void BuildProcess::poll()
    {
        if (state_ != BuildState::Running) { return; }

        bool stillRunning = false;
        const bool succeeded = impl_->reap(stillRunning);
        if (stillRunning) { return; }

        if (!succeeded)
        {
            // The first failing step ends the build. Running the compile after a failed configure
            // would bury the message that mattered under a second one that follows from it.
            state_ = BuildState::Failed;
            return;
        }

        if (stepIndex_ >= steps_.size())
        {
            state_ = BuildState::Succeeded;
            return;
        }

        if (!startStep(stepIndex_, nullptr)) { state_ = BuildState::Failed; }
    }

    void BuildProcess::cancel()
    {
        if (state_ != BuildState::Running) { return; }

        impl_->terminate();

        bool stillRunning = false;
        impl_->reap(stillRunning);
        state_ = BuildState::Failed;
    }

    std::vector<std::string> BuildProcess::readLogTail(std::size_t lines) const
    {
        std::vector<std::string> tail;
        if (logPath_.empty() || lines == 0) { return tail; }

        std::ifstream stream{logPath_, std::ios::binary};
        if (!stream) { return tail; }

        // A ring of the last N lines rather than the whole file in memory. A failing build can
        // produce megabytes, and the panel shows a tail.
        std::deque<std::string> recent;
        std::string line;
        while (std::getline(stream, line))
        {
            if (!line.empty() && line.back() == '\r') { line.pop_back(); }
            recent.push_back(std::move(line));
            if (recent.size() > lines) { recent.pop_front(); }
        }

        tail.assign(recent.begin(), recent.end());
        return tail;
    }
}
