// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/UiCore/StudioShellRenderer.hpp
 * @brief Draws the CNA Studio shell into a `StudioDrawList`.
 *
 * `plan.md` STUDIO-06003, STUDIO-06006, STUDIO-06007, STUDIO-06015.
 *
 * Layout and drawing are separate on purpose (see `StudioShellLayout`), and this is the drawing
 * half: it takes a resolved layout and a theme and produces geometry. It touches no CNA, so the
 * result can be rasterised in a test and compared against a golden image with no GPU anywhere in
 * the loop -- and the *same* geometry, unchanged, is what `CnaUiRenderer` puts on screen.
 */

#include "CNA/Studio/UiCore/StudioDrawList.hpp"
#include "CNA/Studio/UiCore/StudioShellLayout.hpp"
#include "CNA/Studio/UiCore/StudioTheme.hpp"

#include <string>
#include <vector>

namespace CNA::Studio
{
    /** @brief One entry in the application menu bar. */
    struct StudioMenuBarItem
    {
        std::string label;
        /** @brief Whether this menu is currently open. */
        bool open = false;
    };

    /** @brief One docked panel's presentation in a dock region. */
    struct StudioDockedPanel
    {
        std::string title;
        /** @brief Whether this panel is the active tab in its group. */
        bool active = true;
    };

    /**
     * @brief What the shell should currently show.
     *
     * Content rather than state: this describes a frame to draw, and holds no widget identity or
     * interaction state of its own. Those live in `WidgetStateStore`, keyed by `WidgetId`.
     */
    struct StudioShellContent
    {
        std::vector<StudioMenuBarItem> menus;
        std::vector<StudioDockedPanel> leftPanels;
        std::vector<StudioDockedPanel> rightPanels;
        std::vector<StudioDockedPanel> bottomPanels;
        std::vector<StudioDockedPanel> documentTabs;

        /** @brief Left-aligned status bar text: project and document state. */
        std::string statusLeft;
        /** @brief Right-aligned status bar text: renderer and target profile. */
        std::string statusRight;

        /** @brief Number of toolbar buttons to draw. Real commands arrive with STUDIO-06002. */
        int toolbarButtonCount = 0;

        /** @brief Returns the default content: the shell a user sees with nothing open. */
        [[nodiscard]] static StudioShellContent defaults();
    };

    /**
     * @brief Draws the shell.
     *
     * @param list Draw list to emit into; must already be in a frame.
     * @param layout Resolved region geometry.
     * @param theme Theme supplying every colour and metric.
     * @param content What to show.
     */
    void drawStudioShell(StudioDrawList& list, const StudioShellLayout& layout,
                         const StudioTheme& theme, const StudioShellContent& content);
} // namespace CNA::Studio
