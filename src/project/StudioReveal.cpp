// SPDX-License-Identifier: MS-PL
#include "CNA/Studio/Project/StudioReveal.hpp"

#include <filesystem>
#include <system_error>

#if defined(_WIN32)
#    include <windows.h>
#else
#    include <cerrno>
#    include <csignal>
#    include <cstring>
#    include <sys/wait.h>
#    include <unistd.h>
#endif

namespace CNA::Studio
{
    StudioRevealCommand studioRevealCommand(const std::string& absolutePath)
    {
        StudioRevealCommand command;
        if (absolutePath.empty()) { return command; }

#if defined(_WIN32)
        // The comma is part of the switch rather than a separator, and there is no space after it:
        // `explorer /select, C:\x` opens the user's documents folder instead. One of those details
        // that is obvious once and never again, which is why it is asserted by a test.
        command.argv = {"explorer.exe", "/select," + absolutePath};
        command.selectsTheFile = true;
        return command;
#elif defined(__APPLE__)
        command.argv = {"/usr/bin/open", "-R", absolutePath};
        command.selectsTheFile = true;
        return command;
#else
        // The *containing folder*, because `xdg-open` on a file opens it in whatever application
        // claims the type -- which for a texture is an image viewer, not a file manager. That is a
        // different action and not the one the user asked for.
        std::error_code error;
        const std::filesystem::path path{absolutePath};
        const bool isDirectory = std::filesystem::is_directory(path, error) && !error;

        const std::filesystem::path target = isDirectory ? path : path.parent_path();
        if (target.empty()) { return command; }

        command.argv = {"xdg-open", target.string()};
        command.selectsTheFile = false;
        return command;
#endif
    }

    bool studioRevealInFileManager(const std::string& absolutePath, std::string* errorMessage)
    {
        const auto fail = [errorMessage](std::string reason) {
            if (errorMessage != nullptr) { *errorMessage = std::move(reason); }
            return false;
        };

        std::error_code error;
        if (absolutePath.empty() || !std::filesystem::exists(absolutePath, error) || error)
        {
            return fail("'" + absolutePath + "' is not there");
        }

        const StudioRevealCommand command = studioRevealCommand(absolutePath);
        if (!command.isValid()) { return fail("this platform has no file manager to ask"); }

#if defined(_WIN32)
        std::string commandLine;
        for (const std::string& argument : command.argv)
        {
            if (!commandLine.empty()) { commandLine += ' '; }
            // explorer's /select argument carries a path that may hold spaces, and the switch and
            // the path are one argument -- so the quotes go round the path only.
            commandLine += argument.find(' ') == std::string::npos ? argument
                                                                   : "\"" + argument + "\"";
        }

        STARTUPINFOA startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};

        if (!CreateProcessA(nullptr, commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
                            nullptr, &startup, &process))
        {
            return fail("could not start explorer (" + std::to_string(GetLastError()) + ")");
        }

        // Closed at once: the file manager is the user's to keep, not this process's to own.
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return true;
#else
        std::vector<std::string> storage = command.argv;
        std::vector<char*> argv;
        argv.reserve(storage.size() + 1);
        for (std::string& value : storage) { argv.push_back(value.data()); }
        argv.push_back(nullptr);

        // Double-forked, so the file manager is reparented to init and outlives Studio. A direct
        // child would have to be waited for -- and a file manager the user is about to work in is
        // not something an editor should be able to close by exiting.
        const pid_t first = ::fork();
        if (first < 0) { return fail("fork failed: " + std::string{std::strerror(errno)}); }

        if (first == 0)
        {
            const pid_t second = ::fork();
            if (second == 0)
            {
                ::execvp(argv[0], argv.data());

                // _exit rather than exit: this is a copy of the editor, and running its atexit
                // handlers here would flush its buffers twice and could corrupt files it had open.
                ::_exit(127);
            }
            ::_exit(second < 0 ? 127 : 0);
        }

        int status = 0;
        ::waitpid(first, &status, 0);

        // The intermediate child exiting non-zero means the second fork failed. Whether the file
        // manager itself started cannot be known from here, and pretending otherwise would be a
        // truthful-looking error message about something this process never saw.
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
        {
            return fail("could not start " + command.argv.front());
        }
        return true;
#endif
    }
}
