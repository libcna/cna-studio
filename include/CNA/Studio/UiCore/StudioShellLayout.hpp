// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/UiCore/StudioShellLayout.hpp
 * @brief The geometry of the CNA Studio application frame.
 *
 * `plan.md` STUDIO-06003, STUDIO-06006, STUDIO-06007, STUDIO-05002, STUDIO-05003.
 *
 * The shell is the arrangement a user sees before they have opened anything: a menu bar, a
 * toolbar, a status bar, and between them a dock area holding the outliner, the viewport, the
 * inspector and a bottom panel group. Its proportions are stored as *fractions* so that the
 * arrangement survives a resize, and its chrome heights come from theme metrics so that the whole
 * frame scales with DPI without a single hard-coded number.
 *
 * Computing this separately from drawing it is what makes the layout testable: a headless test can
 * assert that panels do not overlap, that nothing escapes the window, that minimum sizes are
 * honoured and that a window too small for its own chrome degrades to empty panels rather than to
 * inverted rectangles. None of those need a GPU, and all of them are how a layout actually breaks.
 */

#include "CNA/Studio/UiCore/StudioTheme.hpp"
#include "CNA/Studio/UiCore/UiRect.hpp"

#include <cstddef>

namespace CNA::Studio
{
    /**
     * @brief The adjustable proportions of the shell.
     *
     * Fractions of the available area rather than pixel widths, so that dragging a window to
     * another monitor rescales the arrangement instead of leaving the inspector 300px wide on a
     * 4K display. The user's pixel-level adjustments are applied to these fractions.
     */
    struct StudioShellProportions
    {
        /** @brief Width of the left dock, as a fraction of the dock area. */
        float leftDockFraction = 0.18f;
        /** @brief Width of the right dock, as a fraction of the dock area. */
        float rightDockFraction = 0.22f;
        /** @brief Height of the bottom dock, as a fraction of the dock area. */
        float bottomDockFraction = 0.28f;

        /** @brief Whether the left dock is shown. */
        bool leftDockVisible = true;
        /** @brief Whether the right dock is shown. */
        bool rightDockVisible = true;
        /** @brief Whether the bottom dock is shown. */
        bool bottomDockVisible = true;
    };

    /**
     * @brief Every region of the shell, resolved to pixels.
     *
     * A hidden dock resolves to an empty rectangle rather than being absent, so that consumers do
     * not need a presence check at every use. An empty rectangle draws nothing and hit-tests to
     * nothing, which is the behaviour a hidden panel should have.
     */
    struct StudioShellLayout
    {
        /** @brief The whole window. */
        UiRect window;
        /** @brief The application menu bar, across the top. */
        UiRect menuBar;
        /** @brief The main toolbar, beneath the menu bar. */
        UiRect toolbar;
        /** @brief The status bar, across the bottom. */
        UiRect statusBar;
        /** @brief Everything between the toolbar and the status bar. */
        UiRect dockArea;

        /** @brief The left dock: the World Outliner. */
        UiRect leftDock;
        /** @brief The right dock: the Details Inspector. */
        UiRect rightDock;
        /** @brief The bottom dock: Content Browser, Output Log, Build. */
        UiRect bottomDock;
        /** @brief The centre: the viewport and document tabs. */
        UiRect centerDock;

        /** @brief The draggable splitter between the left dock and the centre. */
        UiRect leftSplitter;
        /** @brief The draggable splitter between the centre and the right dock. */
        UiRect rightSplitter;
        /** @brief The draggable splitter between the centre and the bottom dock. */
        UiRect bottomSplitter;

        /** @brief The tab strip at the top of the centre dock. */
        UiRect centerTabStrip;
        /** @brief The viewport itself, beneath the centre tab strip. */
        UiRect viewport;

        /**
         * @brief Reports whether every visible region lies inside the window and none overlap.
         *
         * The invariant a docking layout must never violate, checked directly rather than
         * inferred from a screenshot.
         *
         * @return True when the layout is well-formed.
         */
        [[nodiscard]] bool isWellFormed() const;
    };

    /**
     * @brief Computes the shell layout for a window.
     *
     * Chrome heights come from @p theme, so the frame scales with DPI. When the window is too
     * small to hold its own chrome, the regions collapse to empty in a defined order -- docks
     * first, chrome last -- rather than producing negative sizes: a user dragging a window very
     * small must see a cramped tool, not a broken one.
     *
     * @param windowWidth Window width in logical units.
     * @param windowHeight Window height in logical units.
     * @param theme Theme supplying chrome metrics.
     * @param proportions Adjustable dock proportions.
     * @return The resolved layout.
     */
    [[nodiscard]] StudioShellLayout computeStudioShellLayout(
        float windowWidth, float windowHeight, const StudioTheme& theme,
        const StudioShellProportions& proportions = {});

    /** @brief Smallest dock extent, in logical units, before a dock is collapsed entirely. */
    inline constexpr float kStudioMinimumDockExtent = 80.0f;

    /** @brief Smallest centre extent the viewport is guaranteed, in logical units. */
    inline constexpr float kStudioMinimumCenterExtent = 120.0f;
} // namespace CNA::Studio
