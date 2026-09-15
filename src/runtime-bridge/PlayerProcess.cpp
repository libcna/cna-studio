// SPDX-License-Identifier: MS-PL
#include "CNA/Studio/RuntimeBridge/PlayerProcess.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <thread>

#if defined(_WIN32)
#    include <windows.h>
#else
#    include <cerrno>
#    include <csignal>
#    include <cstring>
#    include <fcntl.h>
#    include <sys/types.h>
#    include <sys/wait.h>
#    include <unistd.h>
#endif

namespace CNA::Studio
{
    namespace
    {
        constexpr const char* kPlayerPrefix = "cna-player";

#if !defined(_WIN32)
        /** @brief What the child returns when execv failed, matching the shell's convention. */
        constexpr int kExecFailedExitCode = 127;
#endif

#if defined(_WIN32)
        constexpr const char* kExecutableSuffix = ".exe";
#else
        constexpr const char* kExecutableSuffix = "";
#endif

        /** @brief Returns the backend a player file name encodes, or "default" for plain cna-player. */
        std::string backendFromFileName(const std::string& stem)
        {
            if (stem == kPlayerPrefix) { return "default"; }

            const std::string prefix = std::string{kPlayerPrefix} + "-";
            if (stem.rfind(prefix, 0) != 0) { return {}; }
            return stem.substr(prefix.size());
        }
    }

    const char* toString(PlayerExitReason reason)
    {
        switch (reason)
        {
            case PlayerExitReason::StillRunning: return "still running";
            case PlayerExitReason::Exited: return "exited";
            case PlayerExitReason::Crashed: return "crashed";
            case PlayerExitReason::StoppedByStudio: return "stopped by Studio";
            case PlayerExitReason::FailedToStart: return "failed to start";
        }
        return "still running";
    }

    std::vector<PlayerBuild> discoverPlayerBuilds(const std::string& searchDirectory)
    {
        std::vector<PlayerBuild> builds;

        std::error_code errorCode;
        if (!std::filesystem::is_directory(searchDirectory, errorCode)) { return builds; }

        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator{searchDirectory, errorCode})
        {
            if (!entry.is_regular_file(errorCode)) { continue; }

            const std::string fileName = entry.path().filename().string();
            const std::string suffix{kExecutableSuffix};
            if (!suffix.empty())
            {
                if (fileName.size() <= suffix.size()
                    || fileName.compare(fileName.size() - suffix.size(), suffix.size(), suffix) != 0)
                {
                    continue;
                }
            }

            const std::string stem = entry.path().stem().string();
            const std::string backend = backendFromFileName(stem);
            if (backend.empty()) { continue; }

            builds.push_back(PlayerBuild{backend, entry.path().generic_string()});
        }

        std::sort(builds.begin(), builds.end(),
                  [](const PlayerBuild& lhs, const PlayerBuild& rhs) { return lhs.backend < rhs.backend; });
        return builds;
    }

    struct PlayerProcess::Impl
    {
#if defined(_WIN32)
        PROCESS_INFORMATION process{};
        bool spawned = false;
#else
        pid_t pid = -1;
#endif

        /**
         * @brief How the player ended, read once and kept.
         *
         * The status can only be collected by whichever wait sees the child first, and that is
         * whatever the editor happens to call -- so it is recorded here rather than left to be
         * asked for later, when it would already have been thrown away.
         */
        mutable bool finished = false;
        mutable bool killedBySignal = false;
        mutable int exitCode = 0;

        /** @brief True when the player ran to completion and returned success. */
        [[nodiscard]] bool exitedCleanly() const
        {
            return finished && !killedBySignal && exitCode == 0;
        }

        [[nodiscard]] bool isAlive() const
        {
#if defined(_WIN32)
            if (!spawned || finished) { return false; }
            DWORD code = 0;
            if (!GetExitCodeProcess(process.hProcess, &code))
            {
                finished = true;
                return false;
            }
            if (code == STILL_ACTIVE) { return true; }
            finished = true;
            exitCode = static_cast<int>(code);
            return false;
#else
            if (pid <= 0 || finished) { return false; }
            // WNOHANG so the editor never blocks on a player that is still running.
            int status = 0;
            const pid_t result = ::waitpid(pid, &status, WNOHANG);
            if (result == 0) { return true; }

            // Anything else means the child is gone, and when the wait collected it this is the
            // one chance to read how it went: a second wait would report only ECHILD.
            finished = true;
            if (result > 0)
            {
                killedBySignal = WIFSIGNALED(status);
                exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 0;
            }
            return false;
#endif
        }

        void reap()
        {
#if defined(_WIN32)
            if (!spawned) { return; }
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            process = PROCESS_INFORMATION{};
            spawned = false;
#else
            if (pid <= 0) { return; }
            if (!finished)
            {
                // Reaping matters: an unreaped child stays a zombie for the editor's whole
                // session, and a user who starts play mode fifty times would leak fifty process
                // table entries.
                int status = 0;
                if (::waitpid(pid, &status, WNOHANG) > 0)
                {
                    finished = true;
                    killedBySignal = WIFSIGNALED(status);
                    exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 0;
                }
            }
            pid = -1;
#endif
        }

        void terminate()
        {
#if defined(_WIN32)
            if (spawned) { TerminateProcess(process.hProcess, 1); }
#else
            if (pid > 0) { ::kill(pid, SIGKILL); }
#endif
        }

        bool spawn(const std::string& executable, const std::vector<std::string>& arguments, std::string& error)
        {
            // Cleared first: the same PlayerProcess is reused for every play session, and a status
            // left over from the last one would report the new player as already dead.
            finished = false;
            killedBySignal = false;
            exitCode = 0;

#if defined(_WIN32)
            std::string commandLine = "\"" + executable + "\"";
            for (const std::string& argument : arguments) { commandLine += " \"" + argument + "\""; }

            STARTUPINFOA startup{};
            startup.cb = sizeof(startup);

            if (!CreateProcessA(nullptr, commandLine.data(), nullptr, nullptr, FALSE,
                                0, nullptr, nullptr, &startup, &process))
            {
                error = "CreateProcess failed with " + std::to_string(GetLastError());
                return false;
            }
            spawned = true;
            return true;
#else
            std::vector<std::string> storage;
            storage.push_back(executable);
            storage.insert(storage.end(), arguments.begin(), arguments.end());

            std::vector<char*> argv;
            argv.reserve(storage.size() + 1);
            for (std::string& value : storage) { argv.push_back(value.data()); }
            argv.push_back(nullptr);

            // A close-on-exec pipe, so the child can report a failure that happens after the
            // fork. Without it "the player binary is missing" is indistinguishable from "the
            // player started and exited at once": exec failure lives in the child, where there is
            // nothing left to return it to. The successful case writes nothing and the descriptor
            // closes itself on exec, so the read ends at once with end-of-file.
            int report[2] = {-1, -1};
            if (::pipe(report) != 0)
            {
                error = "could not create the launch pipe: " + std::string{std::strerror(errno)};
                return false;
            }
            ::fcntl(report[1], F_SETFD, ::fcntl(report[1], F_GETFD) | FD_CLOEXEC);

            const pid_t child = ::fork();
            if (child < 0)
            {
                error = "fork failed: " + std::string{std::strerror(errno)};
                ::close(report[0]);
                ::close(report[1]);
                return false;
            }
            if (child == 0)
            {
                ::close(report[0]);
                ::execv(executable.c_str(), argv.data());

                // Reached only when execv failed. The parent is waiting on the other end of the
                // pipe for exactly this.
                const int failure = errno;
                const ssize_t written =
                    ::write(report[1], &failure, sizeof(failure));
                (void)written;

                // _exit rather than exit: the child is a copy of the editor, and running the
                // editor's atexit handlers here would flush its buffers twice and could corrupt
                // files it had open.
                ::_exit(kExecFailedExitCode);
            }

            ::close(report[1]);
            int childErrno = 0;
            ssize_t received = 0;
            for (;;)
            {
                const ssize_t chunk = ::read(report[0], reinterpret_cast<char*>(&childErrno) + received,
                                             sizeof(childErrno) - static_cast<std::size_t>(received));
                if (chunk > 0)
                {
                    received += chunk;
                    if (received == static_cast<ssize_t>(sizeof(childErrno))) { break; }
                    continue;
                }
                // Zero is end-of-file, which is the child having exec'd successfully. EINTR is a
                // signal arriving mid-read and says nothing about the child.
                if (chunk == 0 || errno != EINTR) { break; }
            }
            ::close(report[0]);

            if (received == static_cast<ssize_t>(sizeof(childErrno)))
            {
                // The child is already on its way out; collect it so it does not linger as a
                // zombie for a launch that never happened.
                int status = 0;
                ::waitpid(child, &status, 0);
                error = "could not launch " + executable + ": " + std::strerror(childErrno);
                return false;
            }

            pid = child;
            return true;
#endif
        }
    };

    PlayerProcess::PlayerProcess() : impl_(std::make_unique<Impl>()) {}

    PlayerProcess::~PlayerProcess()
    {
        if (impl_ && started_ && impl_->isAlive()) { stop(); }
    }

    bool PlayerProcess::start(const PlayerBuild& build,
                              const std::string& projectPath,
                              const std::string& scenePath)
    {
        error_.clear();
        reportedBackend_.clear();
        projectPath_ = projectPath;
        helloSent_ = false;
        started_ = false;
        exitReason_ = PlayerExitReason::StillRunning;

        // Listen first, spawn second. The port is then known before the player exists, and the
        // listener is guaranteed up before it connects -- no retry loop, no race.
        if (!channel_.listen(0))
        {
            error_ = channel_.getError();
            exitReason_ = PlayerExitReason::FailedToStart;
            return false;
        }

        std::vector<std::string> arguments;
        arguments.push_back("--project=" + projectPath);
        arguments.push_back("--studio-port=" + std::to_string(channel_.getPort()));
        if (!scenePath.empty()) { arguments.push_back("--scene=" + scenePath); }
        if (build.backend != "default") { arguments.push_back("--graphics=" + build.backend); }

        if (!impl_->spawn(build.executablePath, arguments, error_))
        {
            channel_.close();
            exitReason_ = PlayerExitReason::FailedToStart;
            return false;
        }

        started_ = true;
        return true;
    }

    void PlayerProcess::refreshExitReason() const
    {
        if (!started_ || exitReason_ != PlayerExitReason::StillRunning) { return; }
        if (impl_->isAlive()) { return; }

        // A player that stopped without the editor asking has either finished or died, and the
        // process's own status is what says which. Guessing from the channel would report a
        // segfault as a clean exit whenever the socket happened to drop first.
        exitReason_ = impl_->exitedCleanly() ? PlayerExitReason::Exited : PlayerExitReason::Crashed;
        impl_->reap();
    }

    std::vector<StudioMessage> PlayerProcess::poll()
    {
        std::vector<StudioMessage> messages = channel_.poll();

        // The player announces Ready the moment it connects and then waits for our Hello to
        // consider the handshake complete, so this has to go out as soon as the channel is up.
        if (!helloSent_ && channel_.isConnected())
        {
            helloSent_ = channel_.send(StudioMessage::makeHello(projectPath_));
        }

        for (const StudioMessage& message : messages)
        {
            if (message.type == StudioMessageType::Ready)
            {
                reportedBackend_ = message.payload["backend"].asString();
            }
        }

        refreshExitReason();
        return messages;
    }

    bool PlayerProcess::send(const StudioMessage& message) { return channel_.send(message); }

    bool PlayerProcess::isRunning() const
    {
        refreshExitReason();
        return started_ && exitReason_ == PlayerExitReason::StillRunning;
    }

    void PlayerProcess::stop()
    {
        if (channel_.isConnected())
        {
            StudioMessage quit;
            quit.type = StudioMessageType::Quit;
            channel_.send(quit);
        }

        // Give it a moment to shut down cleanly. A player that ignores the request is terminated:
        // leaving an orphan game window behind is worse than a hard kill on something already
        // unresponsive.
        for (int attempt = 0; attempt < 50 && impl_->isAlive(); ++attempt)
        {
            channel_.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        if (impl_->isAlive()) { impl_->terminate(); }
        impl_->reap();
        channel_.close();

        if (exitReason_ == PlayerExitReason::StillRunning) { exitReason_ = PlayerExitReason::StoppedByStudio; }
    }
}
