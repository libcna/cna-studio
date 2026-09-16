// SPDX-License-Identifier: MS-PL
/**
 * @file StudioComparisonService.cpp
 * @brief Running one scene on every renderer this Studio can run.
 */

#include "CNA/Studio/ShellPanels/StudioComparisonService.hpp"

#include "CNA/Studio/Project/Project.hpp"
#include "CNA/Studio/Scene/SceneDocument.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"

#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

namespace CNA::Studio
{
    ComparisonRequest StudioComparisonService::makeRequest() const
    {
        ComparisonRequest request;
        if (context_.hasProject())
        {
            request.projectPath = context_.getProject().getFilePath();
            request.outputDirectory = getDefaultComparisonDirectory(request.projectPath);
        }
        // The same list Play chooses from, so the panel cannot offer a renderer Play will not use.
        if (builds_) { request.builds = builds_(); }
        request.tolerance = tolerance_;
        return request;
    }

    bool StudioComparisonService::start()
    {
        // The scene on screen, not the project's startup scene: a comparison of something the user
        // is not looking at answers a question nobody asked. And, like play mode, each player is a
        // separate process reading from disk, so what is on screen has to be written there first.
        if (context_.getScenePath().empty())
        {
            log_.append(LogSeverity::Warning,
                        "Save the scene before comparing renderers: each player is a separate "
                        "process and reads the scene from disk.");
            return false;
        }
        if (context_.getHistory().isDirty())
        {
            if (!context_.saveScene())
            {
                log_.append(LogSeverity::Error,
                            "Could not save the scene; not comparing renderers.");
                return false;
            }
            log_.append(LogSeverity::Info, "Saved the scene before comparing renderers.");
        }

        ComparisonRequest request = makeRequest();

        // Relative to the project, like play mode's: several processes need not agree on a working
        // directory, and the project root is the one anchor all of them already have.
        std::error_code relativeError;
        const std::filesystem::path relativeScene = std::filesystem::relative(
            std::filesystem::path{context_.getScenePath()},
            std::filesystem::path{request.projectPath}.parent_path(), relativeError);
        if (!relativeError) { request.scenePath = relativeScene.generic_string(); }

        if (!comparison_.start(request, readImage_, writeImage_))
        {
            log_.append(LogSeverity::Error, "Cannot compare renderers: " + comparison_.getError());
            return false;
        }

        log_.append(LogSeverity::Info,
                    "Comparing " + std::to_string(request.builds.size())
                        + " renderers; captures go to " + request.outputDirectory + ".");
        return true;
    }

    void StudioComparisonService::cancel()
    {
        comparison_.cancel();
        log_.append(LogSeverity::Warning, "Renderer comparison cancelled.");
    }

    bool StudioComparisonService::poll(double nowSeconds)
    {
        const bool wasRunning = isRunning();
        comparison_.poll(nowSeconds);
        if (!wasRunning || isRunning()) { return false; }

        StudioNotification notification;
        notification.id = "studio.comparison";
        notification.actionId = StudioShell::showPanelActionId("comparison");

        if (!comparison_.getError().empty())
        {
            notification.severity = StudioNotificationSeverity::Error;
            notification.title = "Renderer comparison failed";
            notification.detail = comparison_.getError();
        }
        else
        {
            // What the comparison is *for*: renderers that disagree. Saying "finished" and leaving
            // the answer in a panel would be announcing the part the user already knew.
            const bool agree = comparison_.allBackendsAgree();
            notification.severity = agree ? StudioNotificationSeverity::Success
                                          : StudioNotificationSeverity::Warning;
            notification.title = agree ? "Every renderer drew the same frame"
                                       : "Renderers disagree";
            notification.detail = std::to_string(comparison_.getEntries().size())
                                + " renderers compared";
        }

        if (notify_) { notify_(std::move(notification)); }
        return true;
    }
} // namespace CNA::Studio
