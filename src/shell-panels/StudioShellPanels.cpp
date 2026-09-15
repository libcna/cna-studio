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
#include "CNA/Studio/ShellPanels/StudioContentBrowser.hpp"
#include "CNA/Studio/ShellPanels/StudioDetailsPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioDiagnosticsPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioHistoryPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioLayersPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioOutlinerPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioProblemsPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioViewportPanel.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioLogPanel.hpp"

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
        bind(shell);
    }

    void StudioShellPanels::poll()
    {
        build_.poll();
        pollPlayer();
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
        log_.append(reason == PlayerExitReason::Crashed ? LogSeverity::Error : LogSeverity::Info,
                    std::string{"Player exited: "} + toString(reason) + ".");
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
        if (!result.succeeded())
        {
            log_.append(LogSeverity::Error, "Could not package the project: " + result.errorMessage);
            return;
        }

        for (const std::string& warning : result.warnings)
        {
            log_.append(LogSeverity::Warning, "Packaging: " + warning);
        }
        log_.append(LogSeverity::Info,
                    "Packaged " + std::to_string(result.writtenFiles.size()) + " files into "
                        + request.outputDirectory
                        + ".  It builds with CMake and a CNA checkout, and needs no Studio.");
    }

    void StudioShellPanels::bindViewport(StudioShell& shell)
    {
        // The viewport (STUDIO-07009) draws nothing: the scene is a texture the shell composites,
        // and this is the camera the pointer moves and what a click in it selects. Bound only once
        // there is a camera, because a viewport that swallowed clicks and moved nothing would be
        // worse than one that plainly does not respond.
        if (services_.camera == nullptr) { return; }

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
