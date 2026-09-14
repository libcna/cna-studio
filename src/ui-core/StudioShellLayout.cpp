// SPDX-License-Identifier: MS-PL
/**
 * @file StudioShellLayout.cpp
 * @brief Resolving the shell's chrome from a window size and a theme.
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
    } // namespace

    bool StudioShellLayout::isWellFormed() const
    {
        const std::array<const UiRect*, 4> regions{&menuBar, &toolbar, &statusBar, &dockArea};

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

        // No two regions share a pixel. Overlap is how a bar ends up drawn over its neighbour and
        // how a click lands in the wrong one.
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
                                               const StudioTheme& theme)
    {
        StudioShellLayout layout;
        layout.window = UiRect{0.0f, 0.0f, std::max(0.0f, windowWidth), std::max(0.0f, windowHeight)};

        UiRect remaining = layout.window;

        // Chrome first, in the order the user sees it. splitTop/splitBottom clamp, so a window
        // shorter than its own chrome yields progressively empty bars rather than negative ones,
        // and the dock area -- taken last -- is the first thing to run out.
        layout.menuBar = remaining.splitTop(static_cast<float>(theme.metric(StudioMetric::MenuBarHeight)));
        layout.toolbar = remaining.splitTop(static_cast<float>(theme.metric(StudioMetric::ToolbarHeight)));
        layout.statusBar = remaining.splitBottom(static_cast<float>(theme.metric(StudioMetric::StatusBarHeight)));
        layout.dockArea = remaining;

        return layout;
    }
} // namespace CNA::Studio
