// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Project/BuildRunner.hpp"

#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "CNA/Editor/Project/Project.hpp"

#if defined(_WIN32)
#    include <windows.h>
#else
#    include <csignal>
#    include <cstdio>
#    include <sys/wait.h>
#    include <unistd.h>
#endif

namespace CNA::Editor
{
    namespace
    {
#if defined(_WIN32)
        constexpr const char* kCMakeName = "cmake.exe";
        constexpr char kPathSeparator = ';';
#else
        constexpr const char* kCMakeName = "cmake";
        constexpr char kPathSeparator = ':';
#endif

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

    std::string findCMake()
    {
        const char* path = std::getenv("PATH");
        if (path == nullptr) { return {}; }

        const std::string entries{path};
        std::size_t start = 0;

        while (start <= entries.size())
        {
            const std::size_t end = entries.find(kPathSeparator, start);
            const std::string directory =
                entries.substr(start, end == std::string::npos ? std::string::npos : end - start);

            if (!directory.empty())
            {
                std::error_code errorCode;
                const std::filesystem::path candidate = std::filesystem::path{directory} / kCMakeName;
                if (std::filesystem::is_regular_file(candidate, errorCode))
                {
                    return candidate.generic_string();
                }
            }

            if (end == std::string::npos) { break; }
            start = end + 1;
        }
        return {};
    }

    std::string getDefaultBuildDirectory(const BuildRequest& request)
    {
        if (request.projectRoot.empty()) { return {}; }

        const std::string platform = request.targetPlatform.empty() ? "default" : request.targetPlatform;
        return (std::filesystem::path{request.projectRoot} / "build" / platform).generic_string();
    }

    BuildRequest makeBuildRequest(const Project& project,
                                  std::string targetPlatform,
                                  std::string graphicsBackend)
    {
        BuildRequest request;
        request.projectRoot = project.getRootPath();
        request.targetPlatform = std::move(targetPlatform);
        request.graphicsBackend = std::move(graphicsBackend);
        request.buildDirectory = getDefaultBuildDirectory(request);

        // Deliberately not resolved here. This runs on every frame that draws the build panel, and
        // finding cmake means walking every directory on the PATH; the caller resolves it once and
        // fills it in.
        return request;
    }

    std::string describeBuildProblem(const BuildRequest& request)
    {
        if (request.projectRoot.empty()) { return "no project is open"; }

        std::error_code errorCode;
        if (!std::filesystem::is_directory(request.projectRoot, errorCode))
        {
            return "the project directory '" + request.projectRoot + "' does not exist";
        }

        // Checked here rather than left to CMake, because CMake's own message for this is a wall of
        // text about a missing CMakeLists that says nothing about what the user should do.
        if (!std::filesystem::is_regular_file(std::filesystem::path{request.projectRoot} / "CMakeLists.txt",
                                              errorCode))
        {
            return "the project has no CMakeLists.txt, so there is nothing for the editor to build. "
                   "A CNA game's build is the game's own -- the editor only runs it";
        }

        const std::string cmake = request.cmakePath.empty() ? findCMake() : request.cmakePath;
        if (cmake.empty())
        {
            return "cmake was not found on the PATH. Install it, or set the path in the build panel";
        }
        if (!std::filesystem::is_regular_file(cmake, errorCode))
        {
            return "'" + cmake + "' is not an executable file";
        }

        return {};
    }

    std::vector<BuildStep> planBuild(const BuildRequest& request)
    {
        if (!describeBuildProblem(request).empty()) { return {}; }

        const std::string cmake = request.cmakePath.empty() ? findCMake() : request.cmakePath;
        const std::string buildDirectory = request.buildDirectory.empty()
                                               ? getDefaultBuildDirectory(request)
                                               : request.buildDirectory;
        const std::string configuration =
            request.configuration.empty() ? std::string{"Release"} : request.configuration;

        BuildStep configure;
        configure.description = "Configure";
        configure.executable = cmake;
        configure.arguments = {"-S", request.projectRoot,
                               "-B", buildDirectory,
                               "-DCMAKE_BUILD_TYPE=" + configuration};

        // The one option the editor genuinely knows about. Everything else a game's build needs is
        // the game's own business, and passing more would be the editor guessing at somebody's
        // CMakeLists.
        if (!request.graphicsBackend.empty())
        {
            configure.arguments.push_back("-DCNA_GRAPHICS_BACKEND=" + request.graphicsBackend);
        }

        BuildStep build;
        build.description = "Build";
        build.executable = cmake;

        // --config as well as CMAKE_BUILD_TYPE: single-config generators read the first and
        // multi-config ones (Visual Studio, Xcode) read the second, and a build that worked on one
        // developer's machine and produced a Debug binary on another's is the bug this avoids.
        build.arguments = {"--build", buildDirectory, "--config", configuration, "--parallel"};

        return {std::move(configure), std::move(build)};
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
                if (std::freopen(logPath.c_str(), "a", stdout) != nullptr)
                {
                    (void)std::freopen(logPath.c_str(), "a", stderr);
                }

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

    bool BuildProcess::start(const BuildRequest& request, std::string* errorMessage)
    {
        const auto fail = [errorMessage](std::string reason) {
            if (errorMessage != nullptr) { *errorMessage = std::move(reason); }
            return false;
        };

        if (state_ == BuildState::Running) { return fail("a build is already running"); }

        const std::string problem = describeBuildProblem(request);
        if (!problem.empty()) { return fail(problem); }

        steps_ = planBuild(request);
        if (steps_.empty()) { return fail("nothing to build"); }

        const std::string buildDirectory = request.buildDirectory.empty()
                                               ? getDefaultBuildDirectory(request)
                                               : request.buildDirectory;

        std::error_code errorCode;
        std::filesystem::create_directories(buildDirectory, errorCode);
        if (errorCode) { return fail("cannot create '" + buildDirectory + "': " + errorCode.message()); }

        logPath_ = (std::filesystem::path{buildDirectory} / "cna-editor-build.log").generic_string();

        // Truncated at the start of a build rather than appended to for ever. A log holding four
        // builds is one nobody can tell apart; each step then appends to this one.
        {
            std::ofstream truncate{logPath_, std::ios::binary | std::ios::trunc};
            if (!truncate) { return fail("cannot write '" + logPath_ + "'"); }
            truncate << "cna-editor build: " << request.targetPlatform << ", "
                     << request.configuration << "\n";
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
