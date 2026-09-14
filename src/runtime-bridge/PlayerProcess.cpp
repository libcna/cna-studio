// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/RuntimeBridge/PlayerProcess.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <thread>

#if defined(_WIN32)
#    include <windows.h>
#else
#    include <csignal>
#    include <sys/types.h>
#    include <sys/wait.h>
#    include <unistd.h>
#endif

namespace CNA::Editor
{
    namespace
    {
        constexpr const char* kPlayerPrefix = "cna-player";

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
            case PlayerExitReason::StoppedByEditor: return "stopped by the editor";
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

        [[nodiscard]] bool isAlive() const
        {
#if defined(_WIN32)
            if (!spawned) { return false; }
            DWORD exitCode = 0;
            return GetExitCodeProcess(process.hProcess, &exitCode) && exitCode == STILL_ACTIVE;
#else
            if (pid <= 0) { return false; }
            // WNOHANG so the editor never blocks on a player that is still running.
            int status = 0;
            const pid_t result = ::waitpid(pid, &status, WNOHANG);
            return result == 0;
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
            int status = 0;
            // Reaping matters: an unreaped child stays a zombie for the editor's whole session,
            // and a user who starts play mode fifty times would leak fifty process table entries.
            ::waitpid(pid, &status, WNOHANG);
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

            const pid_t child = ::fork();
            if (child < 0)
            {
                error = "fork failed";
                return false;
            }
            if (child == 0)
            {
                ::execv(executable.c_str(), argv.data());
                // Reached only when execv failed. _exit rather than exit: the child is a copy of
                // the editor, and running the editor's atexit handlers here would flush its
                // buffers twice and could corrupt files it had open.
                ::_exit(127);
            }
            pid = child;
            return true;
#endif
        }
    };

    PlayerProcess::PlayerProcess() : impl_(std::make_unique<Impl>()) {}

    PlayerProcess::~PlayerProcess()
    {
        if (impl_ && impl_->isAlive()) { stop(); }
    }

    bool PlayerProcess::start(const PlayerBuild& build,
                              const std::string& projectPath,
                              const std::string& scenePath)
    {
        error_.clear();
        reportedBackend_.clear();
        projectPath_ = projectPath;
        helloSent_ = false;
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
        arguments.push_back("--editor-port=" + std::to_string(channel_.getPort()));
        if (!scenePath.empty()) { arguments.push_back("--scene=" + scenePath); }
        if (build.backend != "default") { arguments.push_back("--graphics=" + build.backend); }

        if (!impl_->spawn(build.executablePath, arguments, error_))
        {
            channel_.close();
            exitReason_ = PlayerExitReason::FailedToStart;
            return false;
        }

        return true;
    }

    std::vector<EditorMessage> PlayerProcess::poll()
    {
        std::vector<EditorMessage> messages = channel_.poll();

        // The player announces Ready the moment it connects and then waits for our Hello to
        // consider the handshake complete, so this has to go out as soon as the channel is up.
        if (!helloSent_ && channel_.isConnected())
        {
            helloSent_ = channel_.send(EditorMessage::makeHello(projectPath_));
        }

        for (const EditorMessage& message : messages)
        {
            if (message.type == EditorMessageType::Ready)
            {
                reportedBackend_ = message.payload["backend"].asString();
            }
        }

        if (exitReason_ == PlayerExitReason::StillRunning && !impl_->isAlive())
        {
            // A player that stopped without the editor asking has either finished or died. The
            // channel state distinguishes the two: a clean exit closes the connection first.
            exitReason_ = channel_.getState() == ChannelState::Failed || !channel_.isConnected()
                              ? PlayerExitReason::Exited
                              : PlayerExitReason::Crashed;
            impl_->reap();
        }

        return messages;
    }

    bool PlayerProcess::send(const EditorMessage& message) { return channel_.send(message); }

    bool PlayerProcess::isRunning() const
    {
        return exitReason_ == PlayerExitReason::StillRunning && impl_->isAlive();
    }

    void PlayerProcess::stop()
    {
        if (channel_.isConnected())
        {
            EditorMessage quit;
            quit.type = EditorMessageType::Quit;
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

        if (exitReason_ == PlayerExitReason::StillRunning) { exitReason_ = PlayerExitReason::StoppedByEditor; }
    }
}
