// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Panels/MainMenuBar.hpp
 * @brief The File / Edit / View menus.
 */

#include "CNA/Editor/Panels/EditorPanel.hpp"

namespace CNA::Editor
{
    /**
     * @brief Draws the menu bar.
     *
     * Every item calls the same EditorActions method its keyboard shortcut does. A menu that did
     * something subtly different from its shortcut is a bug users report as "undo is broken".
     */
    class MainMenuBar final : public EditorPanel
    {
    public:
        using EditorPanel::EditorPanel;

        void draw() override;

    private:
        /** @brief Draws the menus and commands plugins registered (ED-412). */
        void drawPluginMenus();
    };
}
