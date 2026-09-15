// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/ShellPanels/StudioComparisonPanel.hpp
 * @brief The Backends panel — the same scene drawn by every installed player, compared.
 *
 * `plan.md` STUDIO-07014.
 *
 * This is the panel that turns `ANALYSIS.md` finding F-01 — CNA fixes its graphics renderer at
 * *compile* time — from a constraint into something useful. A game ships on renderers its author
 * cannot all run at once, so "does it look the same on each" is otherwise a question answered by
 * shipping and waiting. The editor already launches one process per renderer for play mode; doing
 * it several times over and comparing the pictures is the same mechanism rather than new
 * architecture.
 *
 * ### The panel owns nothing
 *
 * The sequencing lives in `BackendComparison`, the pixel arithmetic in `ImageDiff`, and the
 * decoding behind whoever has a graphics device. What is here is only what a user sees and
 * presses — which is why it can be described into a headless frame and photographed by CI.
 *
 * ### A report, not a verdict
 *
 * The legacy panel printed one line per renderer and stopped. Two renderers disagreeing is the
 * *start* of the investigation, not the end of it, so each row opens: how many pixels, how far
 * apart, and — the part that is usually the whole diagnosis — where on the picture. A band along
 * one edge is a viewport or scissor problem; a scattering over one sprite is a filtering one.
 */

#pragma once

#include "CNA/Studio/Core/ImageDiff.hpp"
#include "CNA/Studio/RuntimeBridge/BackendComparison.hpp"
#include "CNA/Studio/UiCore/StudioFrame.hpp"
#include "CNA/Studio/UiCore/StudioTreeView.hpp"
#include "CNA/Studio/UiCore/UiRect.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace CNA::Studio
{
    /** @brief What the panel needs to know about a run it does not own. */
    struct StudioComparisonView
    {
        /** @brief Whether a project is open at all. */
        bool hasProject = false;

        /**
         * @brief Why a comparison cannot be run, or empty when it can.
         *
         * Read from `describeComparisonProblem`, and said *before* the button rather than after
         * it: the common case is a user with one player build installed, and "nothing happened"
         * would be the worst possible answer to pressing Compare.
         */
        std::string problem;

        /** @brief Where the captures are written, so a user can go and look at them. */
        std::string outputDirectory;

        /** @brief The per-channel tolerance the next run will use. */
        int tolerance = kDefaultImageTolerance;

        /** @brief What the run has got to. */
        ComparisonState state = ComparisonState::Idle;

        /**
         * @brief The run's entries, borrowed.
         *
         * A snapshot the host fills in rather than the `BackendComparison` itself, for the same
         * reason the Diagnostics panel takes a struct rather than a graphics device: a panel that
         * can only be shown by launching several games is a panel no test can read. Borrowed
         * rather than copied because it is read twice a frame and nothing here outlives the call.
         */
        const std::vector<ComparisonEntry>* entries = nullptr;

        /** @brief Why the run could not proceed. Empty when it could. */
        std::string error;

        /** @brief Whether every comparable renderer matched the reference. */
        bool allAgree = false;
    };

    /** @brief What the panel showed and what the user asked for. */
    struct StudioComparisonResult
    {
        /** @brief The user pressed Compare. Input pass only. */
        bool compareRequested = false;

        /** @brief The user pressed Cancel on a running comparison. Input pass only. */
        bool cancelRequested = false;

        /** @brief The user edited the tolerance. Input pass only. */
        bool toleranceChanged = false;

        /** @brief The edited tolerance, already clamped. Meaningful when @ref toleranceChanged. */
        int tolerance = kDefaultImageTolerance;

        /** @brief How many rows were drawn. */
        std::size_t rowsDrawn = 0;

        /** @brief How many rows the report produced. */
        std::size_t rowsTotal = 0;
    };

    /**
     * @brief The largest tolerance the panel will accept.
     *
     * Clamped rather than trusted: a tolerance of 255 calls every pair of images identical, which
     * is a comparison that can never report anything — a control that can be set to "always
     * agree" is worse than no control.
     */
    inline constexpr int kStudioMaxComparisonTolerance = 64;

    /**
     * @brief Builds the report rows for @p comparison.
     *
     * Separate from drawing, so a test can assert on what the report *says* without a frame. That
     * is the whole value of this panel and the part no screenshot can check.
     *
     * @param entries The run's entries.
     * @param state What the run has got to, which decides whether a missing capture reads as
     *        "waiting" or as "never arrived".
     * @param expansion Expansion of the per-renderer groups.
     * @return The rows, in display order.
     */
    [[nodiscard]] std::vector<StudioTreeRow> studioComparisonRows(
        const std::vector<ComparisonEntry>& entries, ComparisonState state,
        const StudioTreeState& expansion);

    /**
     * @brief The one-line verdict on a finished run, or empty while one is not.
     * @param state What the run has got to.
     * @param allAgree Whether every comparable renderer matched.
     */
    [[nodiscard]] std::string studioComparisonSummary(ComparisonState state, bool allAgree);

    /**
     * @brief Draws the Backends panel.
     * @param frame The frame.
     * @param bounds Where the panel's content goes.
     * @param view What the host knows about the run.
     * @param state The panel's retained expansion state.
     * @return What was shown and what the user asked for.
     */
    StudioComparisonResult studioComparisonPanel(StudioFrame& frame, const UiRect& bounds,
                                                 const StudioComparisonView& view,
                                                 StudioTreeState& state);
}
