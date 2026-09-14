// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Panels/MainMenuBar.hpp
 * @brief The File / Edit / View menus.
 */

#include "CNA/Studio/Panels/StudioPanel.hpp"

namespace CNA::Studio
{
    /**
     * @brief Draws the menu bar.
     *
     * Every item calls the same StudioActions method its keyboard shortcut does. A menu that did
     * something subtly different from its shortcut is a bug users report as "undo is broken".
     */
    class MainMenuBar final : public StudioPanel
    {
    public:
        using StudioPanel::StudioPanel;

        void draw() override;

    private:
        /** @brief Draws the menus and commands plugins registered (ED-412). */
        void drawPluginMenus();
    };
}
