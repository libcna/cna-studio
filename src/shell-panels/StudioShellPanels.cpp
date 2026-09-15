// SPDX-License-Identifier: MS-PL
/**
 * @file StudioShellPanels.cpp
 * @brief Binding every ported panel to a shell.
 */

#include "CNA/Studio/ShellPanels/StudioShellPanels.hpp"

#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/Scene/SceneCommands.hpp"
#include "CNA/Studio/Scene/SceneDocument.hpp"
#include "CNA/Studio/ShellPanels/StudioContentBrowser.hpp"
#include "CNA/Studio/ShellPanels/StudioDetailsPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioDiagnosticsPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioHistoryPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioOutlinerPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioProblemsPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioViewportPanel.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioLogPanel.hpp"

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

    void StudioShellPanels::poll() { build_.poll(); }

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
