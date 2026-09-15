// SPDX-License-Identifier: MS-PL
/**
 * @file StudioShellPanels.cpp
 * @brief Binding every ported panel to a shell.
 */

#include "CNA/Studio/ShellPanels/StudioShellPanels.hpp"

#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/Project/Project.hpp"
#include "CNA/Studio/Project/ProjectExport.hpp"
#include "CNA/Studio/Scene/SceneCommands.hpp"
#include "CNA/Studio/Scene/SceneDocument.hpp"
#include "CNA/Studio/ShellPanels/StudioComparisonPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioContentBrowser.hpp"
#include "CNA/Studio/ShellPanels/StudioDetailsPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioDiagnosticsPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioHistoryPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioLayersPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioOutlinerPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioPreferencesPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioProblemsPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioViewportPanel.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioLogPanel.hpp"
#include "CNA/Studio/UiCore/StudioWidgets.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace CNA::Studio
{
    StudioShellPanels::StudioShellPanels(StudioShell& shell, StudioContext& context, StudioLog& log,
                                         StudioShellPanelServices services)
        : shell_(&shell), context_(context), log_(log), services_(std::move(services))
    {
        buildPanel_ = std::make_unique<StudioBuildPanel>(context_, build_);

        // A toast is ephemeral by design, which makes it the wrong place to keep anything. Wired
        // here because this is where the shell and the log meet: the shell is CNA-free and knows
        // nothing about a log, and every caller remembering to do both is every caller eventually
        // not doing both.
        shell.notifications().setLog(&log_);

        bind(shell);
    }

    void StudioShellPanels::poll(double nowSeconds)
    {
        build_.poll();
        pollBuild();
        pollPlayer();
        const bool wasComparing = comparison_.getState() == ComparisonState::Launching
                               || comparison_.getState() == ComparisonState::Capturing;
        comparison_.poll(nowSeconds);
        reportComparison(wasComparing);
        publishStatus();
    }

    void StudioShellPanels::notify(StudioNotification notification)
    {
        if (shell_ != nullptr)
        {
            shell_->notifications().post(std::move(notification));
            return;
        }

        // No shell to show it on -- a preview, or a test holding the panels alone. The message is
        // still the point, so it goes to the log the centre would have written it to.
        std::string line = notification.title;
        if (!notification.detail.empty()) { line += " -- " + notification.detail; }
        log_.append(studioNotificationLogSeverity(notification.severity), line);
    }

    void StudioShellPanels::pollBuild()
    {
        // The transition, not the state. A build that has failed stays failed until the next one
        // starts, and a toast raised from the state would be re-raised every frame for ever.
        const BuildState state = build_.getState();
        if (state == buildWasState_) { return; }
        buildWasState_ = state;

        if (shell_ == nullptr) { return; }
        if (state != BuildState::Succeeded && state != BuildState::Failed) { return; }

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
        notify(std::move(notification));
    }

    void StudioShellPanels::reportComparison(bool wasRunning)
    {
        if (shell_ == nullptr || !wasRunning) { return; }

        const ComparisonState now = comparison_.getState();
        if (now == ComparisonState::Launching || now == ComparisonState::Capturing) { return; }

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
        notify(std::move(notification));
    }

    void StudioShellPanels::applyPreferences()
    {
        if (shell_ == nullptr) { return; }

        // Applied before it is persisted, so a write that fails still leaves the user looking at
        // what they chose: they can see it worked and decide what to do about the file.
        StudioTheme theme = preferences_.theme == "light" ? StudioTheme::light()
                                                          : StudioTheme::dark();
        theme.setScale(preferences_.uiScale);
        shell_->setTheme(std::move(theme));

        if (!savePreferences_) { return; }

        std::string problem;
        if (!savePreferences_(preferences_, &problem))
        {
            log_.append(LogSeverity::Warning, "Could not save preferences: " + problem);
        }
    }

    void StudioShellPanels::publishStatus()
    {
        if (shell_ == nullptr) { return; }

        // The Reset confirmation's answer, read here because poll() is the one place this object
        // runs every frame -- and the shell closes the dialog itself, so waiting for the answer is
        // waiting for a frame rather than for a callback.
        if (resettingPreferences_ && !shell_->isDialogOpen())
        {
            const bool confirmed = shell_->dialogResult().chosen == 1;
            resettingPreferences_ = false;
            if (confirmed)
            {
                preferences_ = StudioPreferences{};
                ++counts_.preferenceChanges;
                applyPreferences();
                log_.append(LogSeverity::Info, "Preferences reset to the defaults.");
            }
        }

        StudioStatusModel& status = shell_->status();

        if (context_.hasProject())
        {
            status.message = context_.getProject().getName() + "  --  "
                           + context_.getScene().getName();
            status.target = studioTargetProfileSummary(
                context_.getProject().getActiveTargetProfile());
            // A project that opened answers whatever went wrong before it.
            status.problem.clear();
        }
        else
        {
            status.message = "No project open";
            status.target.clear();
        }

        // The document's own answer rather than a flag somebody has to remember to set: an
        // unsaved-changes mark that can be wrong is worse than none, because it is the one thing
        // a user checks before closing the window.
        status.modified = context_.hasProject() && context_.getHistory().isDirty();

        // Rebuilt every poll rather than edited, so a job that ended cannot leave a bar behind:
        // the status bar reports what is running now, and "now" is what a poll is for.
        status.jobs.clear();

        if (build_.getState() == BuildState::Running)
        {
            StudioStatusJob job;
            const std::size_t steps = build_.getSteps().size();
            const std::size_t done = build_.getStepNumber();
            job.label = steps > 0
                ? "Building, step " + std::to_string(std::min(done + 1, steps)) + " of "
                      + std::to_string(steps)
                : std::string{"Building"};
            job.progress = steps > 0 ? static_cast<float>(done) / static_cast<float>(steps) : -1.0f;
            job.stopActionId = "studio.build.cancel";
            status.jobs.push_back(std::move(job));
        }

        const ComparisonState comparison = comparison_.getState();
        if (comparison == ComparisonState::Launching || comparison == ComparisonState::Capturing)
        {
            StudioStatusJob job;
            job.label = std::string{"Comparing renderers: "} + toString(comparison);
            // No progress: the run is waiting on several games to open windows, and a bar that
            // guessed at how long that takes would be inventing a number in the one place the
            // editor reports facts.
            status.jobs.push_back(std::move(job));
        }

        if (player_.isRunning())
        {
            StudioStatusJob job;
            job.label = "Playing";
            job.stopActionId = "studio.play.stop";
            status.jobs.push_back(std::move(job));
        }
    }

    void StudioShellPanels::setPlayerBuilds(std::vector<PlayerBuild> builds)
    {
        playerBuilds_ = std::move(builds);
        // The Diagnostics panel reports the same list. One setter, so the two cannot disagree
        // about what this Studio can run.
        diagnostics_.players = playerBuilds_;
    }

    bool StudioShellPanels::isPlaying() const { return player_.isRunning(); }

    const PlayerBuild* StudioShellPanels::choosePlayerBuild() const
    {
        if (playerBuilds_.empty()) { return nullptr; }

        // The renderer the project says it ships on, when a player for it was built. Otherwise
        // whatever is there: a user pressing Play wants to see their game, and refusing because
        // the preferred renderer is missing helps nobody.
        const std::string preferred = context_.getProject().getActiveTargetProfile().renderer;
        for (const PlayerBuild& build : playerBuilds_)
        {
            if (build.backend == preferred) { return &build; }
        }
        return &playerBuilds_.front();
    }

    void StudioShellPanels::startPlaying()
    {
        const PlayerBuild* build = choosePlayerBuild();
        if (build == nullptr)
        {
            log_.append(LogSeverity::Error,
                        "No player build was found beside this executable. CNA fixes its renderer "
                        "at compile time, so Play needs a cna-player-<renderer> binary to launch.");
            return;
        }

        // The player is a separate process and reads the scene from disk, so what is on screen has
        // to be *there* first. Saving silently would be worse than refusing: a user who has not
        // saved deliberately would find their file overwritten by pressing Play.
        if (context_.getScenePath().empty())
        {
            log_.append(LogSeverity::Warning,
                        "Save the scene before playing: the player is a separate process and "
                        "reads it from disk.");
            return;
        }
        if (context_.getHistory().isDirty() && !context_.saveScene())
        {
            log_.append(LogSeverity::Error, "Could not save the scene; not starting the player.");
            return;
        }

        // Relative to the project, like the build's paths: two processes need not agree on a
        // working directory, and the project root is the one anchor both already have.
        std::error_code relativeError;
        const std::filesystem::path relative = std::filesystem::relative(
            std::filesystem::path{context_.getScenePath()},
            std::filesystem::path{context_.getProject().getFilePath()}.parent_path(),
            relativeError);

        if (!player_.start(*build, context_.getProject().getFilePath(),
                           relativeError ? std::string{} : relative.generic_string()))
        {
            log_.append(LogSeverity::Error, "Could not start the player: " + player_.getError());
            return;
        }

        playerWasRunning_ = true;
        log_.append(LogSeverity::Info,
                    "Playing on " + build->backend + " (" + build->executablePath + ").");
    }

    void StudioShellPanels::stopPlaying()
    {
        if (!player_.isRunning()) { return; }
        player_.stop();
        // Said here, so the poll that follows does not report the same ending a second time as an
        // exit the editor did not expect.
        playerWasRunning_ = false;
        log_.append(LogSeverity::Info, "Stopped the player.");
    }

    void StudioShellPanels::startComparison()
    {
        // The scene on screen, not the project's startup scene: a comparison of something the user
        // is not looking at answers a question nobody asked. And, like play mode, each player is a
        // separate process reading from disk, so what is on screen has to be written there first.
        if (context_.getScenePath().empty())
        {
            log_.append(LogSeverity::Warning,
                        "Save the scene before comparing renderers: each player is a separate "
                        "process and reads the scene from disk.");
            return;
        }
        if (context_.getHistory().isDirty())
        {
            if (!context_.saveScene())
            {
                log_.append(LogSeverity::Error,
                            "Could not save the scene; not comparing renderers.");
                return;
            }
            log_.append(LogSeverity::Info, "Saved the scene before comparing renderers.");
        }

        ComparisonRequest request = makeComparisonRequest();

        // Relative to the project, like play mode's: several processes need not agree on a working
        // directory, and the project root is the one anchor all of them already have.
        std::error_code relativeError;
        const std::filesystem::path relativeScene = std::filesystem::relative(
            std::filesystem::path{context_.getScenePath()},
            std::filesystem::path{request.projectPath}.parent_path(), relativeError);
        if (!relativeError) { request.scenePath = relativeScene.generic_string(); }

        if (!comparison_.start(request, services_.readImage, services_.writeImage))
        {
            log_.append(LogSeverity::Error, "Cannot compare renderers: " + comparison_.getError());
            return;
        }

        log_.append(LogSeverity::Info,
                    "Comparing " + std::to_string(request.builds.size())
                        + " renderers; captures go to " + request.outputDirectory + ".");
    }

    ComparisonRequest StudioShellPanels::makeComparisonRequest() const
    {
        ComparisonRequest request;
        if (context_.hasProject())
        {
            request.projectPath = context_.getProject().getFilePath();
            request.outputDirectory = getDefaultComparisonDirectory(request.projectPath);
        }
        // The same list Play chooses from, so the panel cannot offer a renderer Play will not use.
        request.builds = playerBuilds_;
        request.tolerance = comparisonTolerance_;
        return request;
    }

    void StudioShellPanels::pollPlayer()
    {
        for (const StudioMessage& message : player_.poll())
        {
            ++counts_.playerMessages;
            switch (message.type)
            {
                case StudioMessageType::Ready:
                    // What the player *actually* got, not what Studio asked for. CNA fixes its
                    // renderer at compile time and a player can be built for one and report
                    // another; hearing it from the player is the only way to know.
                    log_.append(LogSeverity::Info,
                                "Player ready on " + player_.getReportedBackend() + ".");
                    break;
                case StudioMessageType::ReportException:
                    log_.append(LogSeverity::Error,
                                "Player: " + message.payload["message"].asString("an exception"));
                    break;
                default:
                    break;
            }
        }

        // Compared against what was remembered rather than against a fresh query taken a moment
        // ago: anything at all may have asked whether the player is running in between -- the
        // toolbar does, every frame, to decide whether Stop is available -- and the first such
        // question is what notices the exit. Reading the transition from a local `wasRunning`
        // would therefore miss it exactly when the editor was doing its job.
        const bool running = player_.isRunning();
        if (running == playerWasRunning_) { return; }
        playerWasRunning_ = running;
        if (running) { return; }

        // Said either way. A game that exited because it finished and one that crashed look
        // identical from the editor unless the reason is reported.
        const PlayerExitReason reason = player_.getExitReason();
        if (reason != PlayerExitReason::Crashed)
        {
            // A game the user closed is a game the user was looking at. Announcing that would be
            // telling them what they just did.
            log_.append(LogSeverity::Info, std::string{"Player exited: "} + toString(reason) + ".");
            return;
        }

        StudioNotification notification;
        notification.id = "studio.play";
        notification.severity = StudioNotificationSeverity::Error;
        notification.title = "The game crashed";
        notification.detail = std::string{"Player exited: "} + toString(reason) + ".";
        notification.actionId = StudioShell::showPanelActionId("output");
        notify(std::move(notification));
    }



    void StudioShellPanels::setViewportServices(StudioCamera2D& camera,
                                                SpriteSizeProvider spriteSize)
    {
        services_.camera = &camera;
        services_.spriteSize = std::move(spriteSize);
        // Re-bound rather than checked per frame: the content callback captures `this` and reads
        // the services through it, so the only thing that has to happen here is that the viewport
        // gains content it did not have when there was no camera to drive.
        bindViewport(*shell_);
    }

    void StudioShellPanels::packageProject()
    {
        const Project& project = context_.getProject();

        // Beside the project, in a directory named for it. A file dialog would be the better
        // answer and there is no modal yet (STUDIO-03022 covers the layering, not the window), so
        // the export goes somewhere predictable and the log says exactly where -- which is more
        // useful than a command that refuses until a dialog exists.
        StudioExportRequest request;
        request.outputDirectory =
            (std::filesystem::path{project.getRootPath()} / "Exported").generic_string();
        request.overwrite = true;

        const StudioExportResult result = exportStandaloneProject(project, request);

        StudioNotification notification;
        notification.id = "studio.package";
        if (!result.succeeded())
        {
            notification.severity = StudioNotificationSeverity::Error;
            notification.title = "Could not package the project";
            notification.detail = result.errorMessage;
            notify(std::move(notification));
            return;
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
            ? request.outputDirectory + " -- builds with CMake and a CNA checkout, without Studio"
            : request.outputDirectory + " -- with " + std::to_string(result.warnings.size())
                  + " warning(s) in the Output Log";
        notify(std::move(notification));
    }

    void StudioShellPanels::sayViewportIsEmpty(StudioFrame& frame, const UiRect& bounds,
                                               std::string_view headline, std::string_view hint)
    {
        if (!frame.isDrawPass() || bounds.isEmpty()) { return; }

        const StudioTheme& theme = frame.theme();
        const float line = std::ceil(frame.measureText(StudioFontRole::Body, "Ag").height()
                                     + static_cast<float>(theme.metric(StudioMetric::SpacingSmall)));

        // Centred, because a message in the corner of an otherwise empty rectangle reads as a
        // stray label rather than as the thing the rectangle is saying.
        UiRect box{bounds.left(), bounds.centerY() - line, bounds.width, line};
        studioDrawText(frame, box, headline, StudioFontRole::Body,
                       theme.color(StudioColorRole::TextSecondary), StudioTextAlign::Center);
        box.y += line;
        studioDrawText(frame, box, hint, StudioFontRole::BodySmall,
                       theme.color(StudioColorRole::TextSecondary), StudioTextAlign::Center);
    }

    void StudioShellPanels::bindViewport(StudioShell& shell)
    {
        // The viewport (STUDIO-07009) draws nothing: the scene is a texture the shell composites,
        // and this is the camera the pointer moves and what a click in it selects.
        //
        // The *content* is bound whether or not there is a camera. It used to be bound only with
        // one, on the reasoning that a viewport swallowing clicks and moving nothing is worse than
        // one that plainly does not respond -- which is right about the clicks and wrong about the
        // words: a build with no graphics device then showed a bare grid and no hint that the grid
        // was a viewport rather than a panel that had failed (STUDIO-06013). It says so now, and
        // still takes no input.
        if (services_.camera == nullptr)
        {
            shell.setPanelContent("viewport", [this](StudioFrame& frame, const UiRect& bounds) {
                sayViewportIsEmpty(frame, bounds, "This build has no graphics device.",
                                   "The viewport cannot draw the scene without one.");
            });
            return;
        }

        // X, which the prototype has and the native shell did not (docs/MIGRATION-INVENTORY.md).
        // A toggle rather than two commands, as it is there: there are two spaces, and a toggle
        // needs no second binding to get back.
        if (const StudioAction* existing = shell.actions().find("studio.view.toggleGizmoSpace"))
        {
            StudioAction action = *existing;
            action.checkable = true;
            action.isChecked = [this] { return viewportState_.space == GizmoSpace::Local; };
            action.run = [this] {
                viewportState_.space = viewportState_.space == GizmoSpace::World
                    ? GizmoSpace::Local : GizmoSpace::World;
                // Any drag in flight ends with the space that owned it, for the same reason a mode
                // change does: a translate half-finished in world space would keep writing world
                // deltas into a local transform.
                viewportState_.endDrag();
            };
            shell.actions().add(std::move(action));
        }

        // The toolbar's three transform buttons, which have been drawing and doing nothing since
        // the toolbar existed. Bound here rather than with the document commands because the mode
        // they set is the viewport's, and checkable so the toolbar shows which one is on -- three
        // buttons that all look the same whichever is active is three buttons nobody trusts.
        for (const auto& [id, mode] : {std::pair{"studio.view.translate", GizmoMode::Translate},
                                       std::pair{"studio.view.rotate", GizmoMode::Rotate},
                                       std::pair{"studio.view.scale", GizmoMode::Scale}})
        {
            const StudioAction* existing = shell.actions().find(id);
            if (existing == nullptr) { continue; }

            StudioAction action = *existing;
            action.checkable = true;
            action.isChecked = [this, mode] { return viewportState_.mode == mode; };
            action.run = [this, mode] {
                viewportState_.mode = mode;
                // Any drag in flight ends with the mode that owned it: a translate half-finished
                // when the user reaches for Rotate would otherwise keep writing positions.
                viewportState_.endDrag();
            };
            shell.actions().add(std::move(action));
        }

        // Focus Selected, which the View menu and the F key have both been offering since the
        // shell existed. Only bindable now that there is a camera to move.
        if (const StudioAction* found = shell.actions().find("studio.view.focusSelected"))
        {
            StudioAction focus = *found;
            focus.isEnabled = [this] { return !context_.getSelection().empty(); };
            focus.run = [this] {
                if (!studioFrameSelection(context_, *services_.camera, services_.spriteSize))
                {
                    log_.append(LogSeverity::Trace,
                                "Nothing selected has a position to frame.");
                }
            };
            shell.actions().add(std::move(focus));
        }

        shell.setPanelContent("viewport", [this](StudioFrame& frame, const UiRect& bounds) {
            // The first thing a user sees, and with no project it was a grid and nothing else --
            // no hint that a project is what is missing, and none that the grid is a viewport
            // rather than a panel that failed to draw (STUDIO-06013).
            if (!context_.hasProject())
            {
                sayViewportIsEmpty(frame, bounds, "No project open.",
                                   "Open one with File > Open Project, or --project.");
                return;
            }

            const StudioViewportResult viewport = studioViewportPanel(
                frame, bounds, context_, *services_.camera, viewportState_, services_.spriteSize);

            // No "camera changed" callback: the host renders the scene every frame anyway, and a
            // hook nothing sets is scaffolding rather than a seam.
            if (!viewport.selectionChanged) { return; }

            ++counts_.viewportSelections;
            if (const StudioEntity* entity = context_.getScene().findEntity(viewport.picked))
            {
                log_.append(LogSeverity::Trace, "Selected '" + entity->getName() + "'.");
            }
            else
            {
                log_.append(LogSeverity::Trace, "Selection cleared.");
            }
        });
    }

    void StudioShellPanels::bind(StudioShell& shell)
    {
        // The World Outliner (STUDIO-07006), the second ported panel and the first that reads the
        // document model rather than a log.
        shell.setPanelContent("outliner", [this](StudioFrame& frame, const UiRect& bounds) {
            const StudioOutlinerResult outliner =
                studioOutlinerPanel(frame, bounds, context_, outlinerState_);
            if (frame.isDrawPass())
            {
                counts_.outlinerRowsDrawn = outliner.rowsDrawn;
                counts_.outlinerRowsTotal = outliner.rowsTotal;
            }
            if (!outliner.selectionChanged) { return; }

            const std::vector<Uuid>& selection = context_.getSelection();
            if (selection.empty())
            {
                log_.append(LogSeverity::Trace, "Selection cleared.");
            }
            else if (const StudioEntity* entity = context_.getScene().findEntity(selection.back()))
            {
                log_.append(LogSeverity::Trace,
                            "Selected '" + entity->getName() + "' ("
                                + std::to_string(selection.size()) + " selected).");
            }
        });

        // The Content Browser (STUDIO-07008), the fourth ported panel.
        shell.setPanelContent("content", [this](StudioFrame& frame, const UiRect& bounds) {
            const StudioContentBrowserResult content =
                studioContentBrowser(frame, bounds, context_, contentState_, selectedAsset_);
            if (frame.isDrawPass())
            {
                counts_.contentRowsDrawn = content.rowsDrawn;
                counts_.contentRowsTotal = content.rowsTotal;
            }
            if (!content.selectedAsset.isValid()) { return; }

            const AssetRecord* record = context_.getAssets().find(content.selectedAsset);
            if (record != nullptr)
            {
                log_.append(LogSeverity::Trace, "Selected asset '" + record->sourcePath + "'.");
            }
        });

        // The Details panel (STUDIO-07007), the third ported and the first that writes to the
        // document. Every edit goes through the command history, so Ctrl+Z reaches it.
        shell.setPanelContent("details", [this](StudioFrame& frame, const UiRect& bounds) {
            const StudioDetailsResult details = studioDetailsPanel(frame, bounds, context_);
            if (frame.isDrawPass()) { counts_.detailsRowsDrawn = details.rowsDrawn; }
            if (details.edited)
            {
                log_.append(LogSeverity::Info,
                            "Changed " + details.editedProperty + ".  Undo with Ctrl+Z.");
            }
        });

        // The Build panel (STUDIO-07010), the fifth ported -- and the first interface the six-axis
        // target profile has ever had. Until now a project could only change what it ships on by
        // editing its `.cnaproject` in a text editor.
        shell.setPanelContent("build", [this](StudioFrame& frame, const UiRect& bounds) {
            const StudioBuildPanelResult panel = buildPanel_->draw(frame, bounds);
            if (panel.buildRequested)
            {
                std::string problem;
                if (build_.start(buildPanel_->makeRequest(), &problem))
                {
                    log_.append(LogSeverity::Info, "Build started; log at " + build_.getLogPath());
                }
                else
                {
                    log_.append(LogSeverity::Error, "Cannot start the build: " + problem);
                }
            }
            if (panel.cancelRequested)
            {
                build_.cancel();
                log_.append(LogSeverity::Warning, "Build cancelled.");
            }
        });

        // The History panel (STUDIO-07013): the undo stack as a list a user can jump around in,
        // which is the only way to reach a state twenty commands back without counting Ctrl+Z.
        shell.setPanelContent("history", [this](StudioFrame& frame, const UiRect& bounds) {
            const StudioHistoryResult history =
                studioHistoryPanel(frame, bounds, context_, historyState_);
            if (frame.isDrawPass())
            {
                counts_.historyRowsDrawn = history.rowsDrawn;
                counts_.historyPositions = history.positions;
            }
            if (!history.navigateTo.has_value()) { return; }

            const std::size_t ran = studioNavigateHistory(context_, *history.navigateTo);
            if (ran == 0) { return; }

            log_.append(LogSeverity::Trace,
                        "History: moved to position " + std::to_string(context_.getHistory().getCursor())
                            + " of " + std::to_string(context_.getHistory().getCount()) + ".");
        });

        bindViewport(shell);

        // Build and Package, which the Build panel and the exporter can both already do. Bound
        // here rather than with the document commands because the process they drive is this
        // object's: two BuildProcesses would be two builds racing for one output directory.
        if (const StudioAction* found = shell.actions().find("studio.build.build"))
        {
            StudioAction build = *found;
            build.isEnabled = [this] {
                return context_.hasProject() && build_.getState() != BuildState::Running;
            };
            build.run = [this] {
                std::string problem;
                if (build_.start(buildPanel_->makeRequest(), &problem))
                {
                    log_.append(LogSeverity::Info, "Build started; log at " + build_.getLogPath());
                }
                else
                {
                    log_.append(LogSeverity::Error, "Cannot start the build: " + problem);
                }
            };
            shell.actions().add(std::move(build));
        }

        // The one the status bar's Stop button invokes. A job the user can see running and cannot
        // stop is the worst kind of progress report.
        if (const StudioAction* found = shell.actions().find("studio.build.cancel"))
        {
            StudioAction cancel = *found;
            cancel.isEnabled = [this] { return build_.getState() == BuildState::Running; };
            cancel.run = [this] {
                build_.cancel();
                log_.append(LogSeverity::Warning, "Build cancelled.");
            };
            shell.actions().add(std::move(cancel));
        }

        if (const StudioAction* found = shell.actions().find("studio.play.play"))
        {
            StudioAction play = *found;
            play.isEnabled = [this] {
                return context_.hasProject() && !playerBuilds_.empty() && !player_.isRunning();
            };
            play.run = [this] { startPlaying(); };
            shell.actions().add(std::move(play));
        }

        if (const StudioAction* found = shell.actions().find("studio.play.stop"))
        {
            StudioAction stop = *found;
            stop.isEnabled = [this] { return player_.isRunning(); };
            stop.run = [this] { stopPlaying(); };
            shell.actions().add(std::move(stop));
        }

        if (const StudioAction* found = shell.actions().find("studio.build.package"))
        {
            StudioAction package = *found;
            package.isEnabled = [this] { return context_.hasProject(); };
            package.run = [this] { packageProject(); };
            shell.actions().add(std::move(package));
        }

        // The Layers panel (STUDIO-07024), which answers the one question the outliner cannot:
        // what is on this layer. The outliner is ordered by the hierarchy and a layer cuts across
        // it.
        shell.setPanelContent("layers", [this](StudioFrame& frame, const UiRect& bounds) {
            const StudioLayersResult layers =
                studioLayersPanel(frame, bounds, context_, layersState_);
            if (frame.isDrawPass()) { counts_.layerRowsDrawn = layers.rowsDrawn; }
            if (layers.selectEntities.empty()) { return; }

            context_.setSelection(layers.selectEntities);
            if (!layers.clickedLayer.empty())
            {
                log_.append(LogSeverity::Trace,
                            "Selected " + std::to_string(layers.selectEntities.size())
                                + " on layer '" + layers.clickedLayer + "'.");
            }
        });

        // The Problems panel (STUDIO-07012): scene validation and broken asset references, as one
        // report, because a user whose model has the wrong material on it does not know in advance
        // which of the two it is.
        shell.setPanelContent("problems", [this](StudioFrame& frame, const UiRect& bounds) {
            const StudioProblemsResult problems =
                studioProblemsPanel(frame, bounds, context_, problemsState_);
            if (frame.isDrawPass())
            {
                counts_.problemRowsDrawn = problems.rowsDrawn;
                counts_.brokenReferences = problems.brokenReferences;
                counts_.sceneErrors = problems.errors;
                counts_.sceneWarnings = problems.warnings;
            }
            if (problems.selectEntity.isValid()) { context_.select(problems.selectEntity); }
            if (!problems.clearAsset.isValid()) { return; }

            auto command = std::make_unique<RelinkAssetCommand>(
                context_.getScene(), problems.clearAsset, problems.relinkTo);
            if (!command->isValid()) { return; }

            const std::string summary = command->getDescription();
            context_.execute(std::move(command));
            log_.append(LogSeverity::Info, summary + ".  Undo with Ctrl+Z.");
        });

        // The Diagnostics panel (STUDIO-07011): what this Studio is running on, as a report
        // somebody can paste into a bug report rather than a screen somebody has to transcribe.
        shell.setPanelContent("diagnostics", [this](StudioFrame& frame, const UiRect& bounds) {
            const StudioDiagnosticsResult panel =
                studioDiagnosticsPanel(frame, bounds, diagnostics_, diagnosticsState_);
            if (frame.isDrawPass()) { counts_.diagnosticRowsDrawn = panel.rowsDrawn; }
            if (!panel.copyRequested) { return; }

            if (services_.setClipboardText && services_.setClipboardText(panel.copyText))
            {
                log_.append(LogSeverity::Info, "Copied the diagnostics report to the clipboard.");
            }
            else
            {
                log_.append(LogSeverity::Warning,
                            "This build has no clipboard: CNA's Devices module is off "
                            "(CNA gap G-02). Rebuild CNA with CNA_DEVICES=ON.");
            }
        });

        // The Backends panel (STUDIO-07014): the same scene on every installed player, and where
        // the pictures disagree. The run is owned here rather than by the panel, so closing the
        // tab does not abandon several games that are already starting.
        shell.setPanelContent("comparison", [this](StudioFrame& frame, const UiRect& bounds) {
            StudioComparisonView view;
            view.hasProject = context_.hasProject();
            view.tolerance = comparisonTolerance_;
            view.state = comparison_.getState();
            view.entries = &comparison_.getEntries();
            view.error = comparison_.getError();
            view.allAgree = comparison_.allBackendsAgree();
            if (view.hasProject)
            {
                const ComparisonRequest probe = makeComparisonRequest();
                view.outputDirectory = probe.outputDirectory;
                view.problem = describeComparisonProblem(probe);
            }

            const StudioComparisonResult panel =
                studioComparisonPanel(frame, bounds, view, comparisonState_);
            if (frame.isDrawPass()) { counts_.comparisonRowsDrawn = panel.rowsDrawn; }

            if (panel.toleranceChanged) { comparisonTolerance_ = panel.tolerance; }
            if (panel.compareRequested) { startComparison(); }
            if (panel.cancelRequested)
            {
                comparison_.cancel();
                log_.append(LogSeverity::Warning, "Renderer comparison cancelled.");
            }
        });

        // The Preferences panel (STUDIO-06011): a panel rather than a modal, because preferences
        // are read and changed *while* working -- "the camera is too fast" is noticed with a hand
        // on the mouse, and a dialog makes answering it a trip out of and back into the viewport.
        shell.setPanelContent("preferences", [this, &shell](StudioFrame& frame,
                                                            const UiRect& bounds) {
            StudioPreferencesPanelContext context;
            context.layoutNames.reserve(shell.savedLayouts().size());
            for (const StudioNamedLayout& saved : shell.savedLayouts())
            {
                context.layoutNames.push_back(saved.name);
            }

            // The registry rather than a copy of it: rebinding writes through, and a copy would
            // rebind something nothing dispatches from.
            context.actions = &shell.actions();

            const StudioPreferencesPanelResult panel =
                studioPreferencesPanel(frame, bounds, preferences_, context, shortcutEditor_);

            if (panel.resetRequested)
            {
                // Asked first, because it is the one control here that discards decisions the user
                // made deliberately -- and every other change is a single value they can put back.
                StudioDialogRequest request;
                request.title = "Reset preferences";
                request.lines = {"Put every preference back the way Studio ships?",
                                 "This cannot be undone."};
                request.buttons = {"Cancel", "Reset"};
                request.cancelButton = 0;
                shell.openDialog(std::move(request));
                resettingPreferences_ = true;
                return;
            }

            if (!panel.changed) { return; }
            ++counts_.preferenceChanges;
            applyPreferences();
        });

        // The first ported panel (STUDIO-07005). Drawn by the Studio UI, from a log no UI owns --
        // which is the whole shape of the strangler migration: the ImGui Console reads the same
        // model and keeps working until it is deleted.
        shell.setPanelContent("output", [this](StudioFrame& frame, const UiRect& bounds) {
            const StudioLogPanelResult panel = studioLogPanel(frame, bounds, log_);
            if (frame.isDrawPass())
            {
                counts_.logRowsDrawn = panel.rowsDrawn;
                counts_.logRowsMatching = panel.rowsMatching;
            }
            if (panel.cleared) { log_.clear(); }
            if (!panel.copyRequested) { return; }

            // CNA gap G-02: the clipboard is behind a default-off CNA option, so this degrades
            // visibly rather than silently doing nothing.
            if (services_.setClipboardText && services_.setClipboardText(panel.copyText))
            {
                log_.append(LogSeverity::Info, "Copied the log to the clipboard.");
            }
            else
            {
                log_.append(LogSeverity::Warning,
                            "This build has no clipboard: CNA's Devices module is off "
                            "(CNA gap G-02). Rebuild CNA with CNA_DEVICES=ON.");
            }
        });
    }
}
