// SPDX-License-Identifier: MS-PL
/**
 * @file StudioShellLayout.cpp
 * @brief Resolving the shell's regions from a window size and a theme.
 */

#include "CNA/Studio/UiCore/StudioShellLayout.hpp"

#include <algorithm>
#include <array>

namespace CNA::Studio
{
    namespace
    {
        /** @brief Returns true when two rectangles share any area. */
        bool overlaps(const UiRect& a, const UiRect& b)
        {
            if (a.isEmpty() || b.isEmpty()) { return false; }
            return !a.intersect(b).isEmpty();
        }

        /**
         * @brief Resolves a dock extent from a fraction, honouring minimums.
         *
         * Returns zero -- collapse the dock entirely -- rather than a sliver, when there is not
         * enough room for both the dock's minimum and the centre's. A two-pixel-wide outliner is
         * not a smaller outliner; it is a rendering artefact with a splitter attached.
         *
         * @param available Total extent to divide.
         * @param fraction Requested dock fraction.
         * @param reservedForOthers Extent already promised to other docks and splitters.
         * @return The dock's extent, or zero.
         */
        float resolveDockExtent(float available, float fraction, float reservedForOthers)
        {
            const float usable = available - reservedForOthers - kStudioMinimumCenterExtent;
            if (usable < kStudioMinimumDockExtent) { return 0.0f; }

            const float requested = available * std::clamp(fraction, 0.0f, 1.0f);
            if (requested < kStudioMinimumDockExtent) { return 0.0f; }
            return std::min(requested, usable);
        }
    } // namespace

    bool StudioShellLayout::isWellFormed() const
    {
        const std::array<const UiRect*, 9> regions{
            &menuBar, &toolbar, &statusBar, &leftDock, &rightDock, &bottomDock,
            &centerDock, &leftSplitter, &rightSplitter};

        for (const UiRect* region : regions)
        {
            if (region->isEmpty()) { continue; }

            // Inside the window. A region that escapes draws over the desktop on a borderless
            // window and over the wrong panel on every other kind.
            if (region->left() < window.left() - 0.01f || region->top() < window.top() - 0.01f
                || region->right() > window.right() + 0.01f
                || region->bottom() > window.bottom() + 0.01f)
            {
                return false;
            }
        }

        // No two regions share a pixel. Overlap is how a panel ends up drawn over its neighbour
        // and how a click lands in the wrong one.
        for (std::size_t i = 0; i < regions.size(); ++i)
        {
            for (std::size_t j = i + 1; j < regions.size(); ++j)
            {
                if (overlaps(*regions[i], *regions[j])) { return false; }
            }
        }
        return true;
    }

    StudioShellLayout computeStudioShellLayout(float windowWidth, float windowHeight,
                                               const StudioTheme& theme,
                                               const StudioShellProportions& proportions)
    {
        StudioShellLayout layout;
        layout.window = UiRect{0.0f, 0.0f, std::max(0.0f, windowWidth), std::max(0.0f, windowHeight)};

        UiRect remaining = layout.window;

        // Chrome first, in the order the user sees it. splitTop/splitBottom clamp, so a window
        // shorter than its own chrome yields progressively empty bars rather than negative ones.
        layout.menuBar = remaining.splitTop(static_cast<float>(theme.metric(StudioMetric::MenuBarHeight)));
        layout.toolbar = remaining.splitTop(static_cast<float>(theme.metric(StudioMetric::ToolbarHeight)));
        layout.statusBar = remaining.splitBottom(static_cast<float>(theme.metric(StudioMetric::StatusBarHeight)));
        layout.dockArea = remaining;

        const auto splitter = static_cast<float>(theme.metric(StudioMetric::SplitterThickness));

        // The bottom dock is taken before the side docks, so the Content Browser and Output Log
        // span the full width beneath the outliner and the inspector. That is the arrangement this
        // shell chooses: a log is read across, and a wide one costs the side panels nothing.
        if (proportions.bottomDockVisible)
        {
            const float extent = resolveDockExtent(remaining.height,
                                                   proportions.bottomDockFraction, splitter);
            if (extent > 0.0f)
            {
                layout.bottomDock = remaining.splitBottom(extent);
                layout.bottomSplitter = remaining.splitBottom(splitter);
            }
        }

        if (proportions.leftDockVisible)
        {
            const float reserved = splitter
                + (proportions.rightDockVisible ? remaining.width * proportions.rightDockFraction + splitter
                                                : 0.0f);
            const float extent = resolveDockExtent(remaining.width,
                                                   proportions.leftDockFraction, reserved);
            if (extent > 0.0f)
            {
                layout.leftDock = remaining.splitLeft(extent);
                layout.leftSplitter = remaining.splitLeft(splitter);
            }
        }

        if (proportions.rightDockVisible)
        {
            const float extent = resolveDockExtent(remaining.width,
                                                   proportions.rightDockFraction, splitter);
            if (extent > 0.0f)
            {
                layout.rightDock = remaining.splitRight(extent);
                layout.rightSplitter = remaining.splitRight(splitter);
            }
        }

        layout.centerDock = remaining;

        UiRect center = layout.centerDock;
        layout.centerTabStrip = center.splitTop(static_cast<float>(theme.metric(StudioMetric::TabHeight)));
        layout.viewport = center;

        return layout;
    }
} // namespace CNA::Studio
