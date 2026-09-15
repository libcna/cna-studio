// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/ShellPanels/StudioDiagnosticsPanel.hpp
 * @brief The Diagnostics panel — what this Studio is actually running on.
 *
 * `plan.md` STUDIO-07011.
 *
 * ### It takes a snapshot, not a device
 *
 * The ImGui panel reaches through the application object into the viewport and asks the live
 * graphics device what it can do. That is why it could never be tested without one, and why it
 * could not exist in the CNA-free build at all. Here the *host* fills in a plain struct once a
 * frame, and the panel draws it. A build with no device fills in the honest empty answer and the
 * panel says so, which is the same thing a headless run should have said all along.
 *
 * "Why does a model look different on that machine" is the first question of every graphics bug
 * report, and it should not need a debugger to answer — so the panel exists to be *copied into a
 * report*, not merely looked at.
 */

#pragma once

#include "CNA/Studio/Project/StudioHostRequirements.hpp"
#include "CNA/Studio/RuntimeBridge/PlayerProcess.hpp"
#include "CNA/Studio/UiCore/StudioFrame.hpp"
#include "CNA/Studio/UiCore/StudioTreeView.hpp"
#include "CNA/Studio/UiCore/UiRect.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace CNA::Studio
{
    /** @brief What the host knows about itself, gathered once a frame. */
    struct StudioDiagnosticsInfo
    {
        /** @brief Which UI is drawing, e.g. `"Studio native"` or `"Dear ImGui"`. */
        std::string uiBackend = "Studio native";

        /** @brief The CNA renderer this process is built against, or empty with no device. */
        std::string renderer;

        /**
         * @brief What is drawing the scene, e.g. `"cna-software"`, or empty when nothing is.
         *
         * Separate from @ref renderer because they can differ in principle and because "the UI
         * draws but the viewport does not" is a real state worth being able to see.
         */
        std::string viewportBackend;

        /** @brief The CNA platform implementation, or empty with no device. */
        std::string platform;

        /** @brief Whether the modern CNAEXT graphics API is available. */
        bool modernApi = false;

        /** @brief What Studio's host contract made of this renderer. Empty outcomes means "not asked". */
        StudioHostEvaluation host;

        /** @brief Player executables found beside this one, by renderer. */
        std::vector<PlayerBuild> players;

        /** @brief Draw calls in the last frame. */
        std::size_t drawCalls = 0;

        /** @brief Triangles in the last frame. */
        std::size_t triangles = 0;

        /** @brief Frames rendered since start-up. */
        std::uint64_t frames = 0;
    };

    /** @brief What the Diagnostics panel showed. */
    struct StudioDiagnosticsResult
    {
        /** @brief How many rows were drawn. */
        std::size_t rowsDrawn = 0;

        /** @brief How many rows the report produced. */
        std::size_t rowsTotal = 0;

        /** @brief The whole report as text, on the frame the user asked to copy it. Input pass only. */
        std::string copyText;

        /** @brief Whether the user asked for it. Input pass only. */
        bool copyRequested = false;
    };

    /**
     * @brief Builds the report rows from @p info.
     *
     * Separate from drawing so a test can assert on what the report *says* without a frame — which
     * is the whole value of a diagnostics panel, and the part a screenshot cannot check.
     *
     * @param info What the host knows.
     * @param state Expansion of the groups.
     * @return The rows, in display order.
     */
    [[nodiscard]] std::vector<StudioTreeRow> studioDiagnosticsRows(const StudioDiagnosticsInfo& info,
                                                                   const StudioTreeState& state);

    /**
     * @brief The report as plain text, for a bug report.
     * @param info What the host knows.
     * @return One line per fact, in the order the panel shows them.
     */
    [[nodiscard]] std::string studioDiagnosticsText(const StudioDiagnosticsInfo& info);

    /**
     * @brief Draws the Diagnostics panel.
     * @param frame The frame.
     * @param bounds Where the panel's content goes.
     * @param info What the host knows.
     * @param state The panel's retained expansion state.
     * @return What was shown and what the user asked for.
     */
    StudioDiagnosticsResult studioDiagnosticsPanel(StudioFrame& frame, const UiRect& bounds,
                                                   const StudioDiagnosticsInfo& info,
                                                   StudioTreeState& state);
}
