// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/UiCore/StudioShellLayout.hpp
 * @brief The geometry of the CNA Studio application frame's fixed chrome.
 *
 * `plan.md` STUDIO-06003, STUDIO-06006, STUDIO-06007.
 *
 * The shell has two kinds of region and they belong to different owners. The **chrome** — menu bar,
 * toolbar, status bar — is fixed furniture: the user does not move it, its heights come from theme
 * metrics so the whole frame scales with DPI, and it is the same on every workspace. Everything
 * between them is the **dock area**, which belongs to @ref StudioDockTree and is arranged by the
 * user.
 *
 * Splitting it that way is what stops the two from interfering. An earlier version of this file
 * resolved four fixed docks from three fractions, which made a left dock a property of the *shell*
 * rather than of the arrangement — so a user could never have two panels side by side on the right,
 * and a saved workspace could never describe one.
 *
 * Computing this separately from drawing it is what makes the layout testable: a headless test can
 * assert that the chrome does not overlap, that nothing escapes the window, and that a window too
 * small for its own chrome degrades to empty bars rather than to inverted rectangles.
 */

#include "CNA/Studio/UiCore/StudioTheme.hpp"
#include "CNA/Studio/UiCore/UiRect.hpp"

#include <cstddef>

namespace CNA::Studio
{
    /**
     * @brief The shell's fixed regions, resolved to pixels.
     *
     * A region with no room resolves to an empty rectangle rather than being absent, so consumers
     * do not need a presence check at every use. An empty rectangle draws nothing and hit-tests to
     * nothing, which is the behaviour a region with no room should have.
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
        /** @brief Everything between the toolbar and the status bar: the docking area. */
        UiRect dockArea;

        /**
         * @brief Reports whether every region lies inside the window and none overlap.
         *
         * The invariant a frame must never violate, checked directly rather than inferred from a
         * screenshot.
         *
         * @return True when the layout is well-formed.
         */
        [[nodiscard]] bool isWellFormed() const;
    };

    /**
     * @brief Computes the shell's chrome for a window.
     *
     * Chrome heights come from @p theme, so the frame scales with DPI. When the window is too small
     * to hold its own chrome the regions collapse to empty in a defined order — the dock area
     * first, then the bars from the bottom up — rather than producing negative sizes: a user
     * dragging a window very small must see a cramped tool, not a broken one.
     *
     * @param windowWidth Window width in logical units.
     * @param windowHeight Window height in logical units.
     * @param theme Theme supplying chrome metrics.
     * @return The resolved layout.
     */
    [[nodiscard]] StudioShellLayout computeStudioShellLayout(float windowWidth, float windowHeight,
                                                             const StudioTheme& theme);

    /** @brief Smallest extent a docked panel is resolved to, in logical units. */
    inline constexpr float kStudioMinimumDockExtent = 120.0f;

    /**
     * @brief The size a panel's window takes when it is first undocked, in logical units.
     *
     * Large enough to be worth having undocked and small enough not to cover the workspace it was
     * taken out of: an inspector that opened at half the window would hide the thing it inspects.
     */
    inline constexpr float kStudioFloatDropWidth = 360.0f;
    /** @brief The height a panel's window takes when it is first undocked. @see kStudioFloatDropWidth */
    inline constexpr float kStudioFloatDropHeight = 280.0f;
} // namespace CNA::Studio
