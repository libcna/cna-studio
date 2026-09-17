// SPDX-License-Identifier: MS-PL
/**
 * @file StudioBuildService.cpp
 * @brief Building the user's game, and packaging it as a project that does not know Studio exists.
 */

#include "CNA/Studio/ShellPanels/StudioBuildService.hpp"

#include "CNA/Studio/Project/Project.hpp"
#include "CNA/Studio/Project/LanguageAdapter.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"

#include <filesystem>
#include <string>
#include <utility>

namespace CNA::Studio
{
    bool StudioBuildService::poll()
    {
        // The transition, not the state. A build that has failed stays failed until the next one
        // starts, and a toast raised from the state would be re-raised every frame for ever.
        const BuildState state = build_.getState();
        if (state == was_) { return false; }
        was_ = state;
        if (state != BuildState::Succeeded && state != BuildState::Failed) { return false; }

        // The case notifications exist for. A build takes minutes and the user goes to read
        // something else, so the result has to find them rather than waiting in a panel.
        StudioNotification notification;
        notification.id = "studio.build";
        notification.severity = state == BuildState::Succeeded
            ? StudioNotificationSeverity::Success
            : StudioNotificationSeverity::Error;
        notification.title = state == BuildState::Succeeded ? "Build succeeded" : "Build failed";
        notification.detail = "Log: " + build_.getLogPath();
        notification.actionId = StudioShell::showPanelActionId("build");
        if (notify_) { notify_(std::move(notification)); }
        return true;
    }


    bool StudioBuildService::packageProject()
    {
        const Project& project = context_.getProject();

        // Which files a package consists of, and how they are built, is the language's business.
        // This service's business is where it goes and how the outcome is announced -- which is
        // the same in every language, and is why it is written once.
        const StudioLanguageAdapter* language = context_.getLanguage();
        if (language == nullptr)
        {
            StudioNotification refused;
            refused.id = "studio.package";
            refused.severity = StudioNotificationSeverity::Error;
            refused.title = "Could not package the project";
            refused.detail = "This build of Studio has no support for the '"
                           + project.getLanguage() + "' language.";
            if (notify_) { notify_(std::move(refused)); }
            return false;
        }

        // Beside the project, in a directory named for it. A file dialog would be the better
        // answer and there is no modal yet (STUDIO-03022 covers the layering, not the window), so
        // the export goes somewhere predictable and the log says exactly where -- which is more
        // useful than a command that refuses until a dialog exists.
        StudioExportRequest request;
        request.outputDirectory =
            (std::filesystem::path{project.getRootPath()} / "Exported").generic_string();
        request.overwrite = true;

        const StudioExportResult result = language->exportStandalone(project, request);

        StudioNotification notification;
        notification.id = "studio.package";
        if (!result.succeeded())
        {
            notification.severity = StudioNotificationSeverity::Error;
            notification.title = "Could not package the project";
            notification.detail = result.errorMessage;
            if (notify_) { notify_(std::move(notification)); }
            return false;
        }

        // The warnings stay in the log rather than becoming toasts of their own. There may be many
        // and they are about the package that was written; the one thing the user has to be told
        // is that it was written, and where.
        for (const std::string& warning : result.warnings)
        {
            log_.append(LogSeverity::Warning, "Packaging: " + warning);
        }

        notification.severity = result.warnings.empty() ? StudioNotificationSeverity::Success
                                                        : StudioNotificationSeverity::Warning;
        notification.title = "Packaged " + std::to_string(result.writtenFiles.size()) + " files";
        notification.detail = result.warnings.empty()
            ? request.outputDirectory + " -- builds with "
                  + language->descriptor().toolchainName + " and a CNA checkout, without Studio"
            : request.outputDirectory + " -- with " + std::to_string(result.warnings.size())
                  + " warning(s) in the Output Log";
        if (notify_) { notify_(std::move(notification)); }
        return true;
    }
}
