// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/ShellPanels/StudioShellPanels.hpp
 * @brief Binds every ported panel to a shell, in one place.
 *
 * `plan.md` STUDIO-07001, STUDIO-07017.
 *
 * ### Why this exists rather than being written where the shell is created
 *
 * It was written there, and there is exactly one place a running Studio is created — behind a CNA
 * checkout, in `cna-studio-viewport`. That meant the ported panels could only be *seen* in a build
 * with CNA, so the headless shell preview — the one screenshot harness that runs on a machine with
 * no GPU and no display, and the only visual test the project has — showed five empty rectangles
 * where the ported panels are.
 *
 * Nothing about binding a panel needs CNA. Moved here, the same binding serves the real editor and
 * the preview, so what CI photographs is what a user sees rather than a frame of the shell with
 * its content missing.
 *
 * ### It holds the panels' retained state
 *
 * Which tree rows are open, which asset is selected, the Build panel itself: state that belongs to
 * a panel rather than to a frame, and that has to outlive both description passes. Keeping it in
 * one object is also what lets a test drive the whole set the way the editor does.
 */

#pragma once

#include "CNA/Studio/Core/Uuid.hpp"
#include "CNA/Studio/Project/BuildRunner.hpp"
#include "CNA/Studio/ShellPanels/StudioBuildPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioDiagnosticsPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioHistoryPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioProblemsPanel.hpp"
#include "CNA/Studio/Ui/StudioLog.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"
#include "CNA/Studio/UiCore/StudioTreeView.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

namespace CNA::Studio
{
    class StudioContext;

    /** @brief What a host can offer the panels that the panels cannot do themselves. */
    struct StudioShellPanelServices
    {
        /**
         * @brief Puts text on the system clipboard, returning whether it got there.
         *
         * A seam rather than a direct call, because the clipboard is behind a default-off CNA
         * option (gap G-02) and the CNA-free build has none at all. Unset means "no clipboard",
         * which the Output Log reports rather than silently doing nothing.
         */
        std::function<bool(const std::string&)> setClipboardText;
    };

    /** @brief What the panels reported over the last frame, for diagnostics and smoke tests. */
    struct StudioShellPanelCounts
    {
        std::size_t outlinerRowsDrawn = 0;
        std::size_t outlinerRowsTotal = 0;
        std::size_t detailsRowsDrawn = 0;
        std::size_t contentRowsDrawn = 0;
        std::size_t contentRowsTotal = 0;
        std::size_t logRowsDrawn = 0;
        std::size_t logRowsMatching = 0;
        std::size_t problemRowsDrawn = 0;
        std::size_t historyRowsDrawn = 0;
        std::size_t historyPositions = 0;
        std::size_t diagnosticRowsDrawn = 0;
        std::size_t brokenReferences = 0;
        std::size_t sceneErrors = 0;
        std::size_t sceneWarnings = 0;
    };

    /**
     * @brief The ported panels, bound to a shell and holding their retained state.
     *
     * Construct one beside the shell and it stays bound for the shell's life. It borrows the
     * shell, the context and the log, so all three must outlive it.
     */
    class StudioShellPanels
    {
    public:
        /**
         * @brief Binds every ported panel's content to @p shell.
         * @param shell The shell to bind into.
         * @param context The editor context the panels read and write.
         * @param log The log the Output Log shows and the panels append to.
         * @param services Host facilities the panels cannot reach themselves.
         */
        StudioShellPanels(StudioShell& shell, StudioContext& context, StudioLog& log,
                          StudioShellPanelServices services = {});

        StudioShellPanels(const StudioShellPanels&) = delete;
        StudioShellPanels& operator=(const StudioShellPanels&) = delete;

        /**
         * @brief Advances anything the panels own that runs between frames.
         *
         * Today that is the build process, which must be polled whether or not its panel is the
         * visible tab: a build that advanced only while somebody was looking at it would stall the
         * moment they looked away.
         */
        void poll();

        /** @brief What the panels reported over the last frame. */
        [[nodiscard]] const StudioShellPanelCounts& counts() const { return counts_; }

        /** @brief The build this Studio would run. */
        [[nodiscard]] BuildProcess& build() { return build_; }

        /**
         * @brief What the Diagnostics panel reports, for the host to fill in each frame.
         *
         * A snapshot rather than a live device: the panel is CNA-free and testable because it
         * reads this, and a build with no device leaves it at its honest empty defaults.
         */
        [[nodiscard]] StudioDiagnosticsInfo& diagnostics() { return diagnostics_; }

    private:
        void bind(StudioShell& shell);

        StudioContext& context_;
        StudioLog& log_;
        StudioShellPanelServices services_;

        StudioTreeState outlinerState_;
        StudioTreeState contentState_;
        StudioProblemsState problemsState_;
        StudioTreeState historyState_;
        StudioTreeState diagnosticsState_;
        Uuid selectedAsset_;

        BuildProcess build_;
        std::unique_ptr<StudioBuildPanel> buildPanel_;

        StudioDiagnosticsInfo diagnostics_;
        StudioShellPanelCounts counts_;
    };
}
