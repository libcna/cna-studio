// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Plugins/PluginExtensions.hpp
 * @brief What a plugin may add to the editor's own UI (plan.md ED-412).
 *
 * Two of ED-412's six extension points need a registry of their own; the other four already have
 * one, and saying which is which is most of what this file is for.
 *
 * - **Component types** and **importer settings** need nothing new. A plugin holds an
 *   `EditorContext` and `ComponentRegistry::registerComponent` is already the supported way in --
 *   ED-305 re-registers `CNA.Layer` through it every time the project's layer list changes, so
 *   this is a path the editor itself relies on rather than a door left open for plugins.
 * - **Panels** and **menu commands** need this file, because the editor draws its own from a fixed
 *   list and there was nowhere for a stranger to be added to.
 * - **Exporters** are a menu command that writes a file. Giving them a registry of their own would
 *   be inventing a concept the editor does not otherwise have, and ED-520 -- the plugin this was
 *   designed for -- wants "Export as MC3", which is a menu item.
 * - **Gizmos** are deliberately *not* here, and the reason is not effort. A gizmo is not a thing
 *   that draws; it is a thing that draws, hit-tests, owns a drag across frames and turns that drag
 *   into undoable commands. `TransformGizmos3D.hpp` is three of those and they share no solver. A
 *   registry that let a plugin add a fourth would have to expose the viewport's interaction loop,
 *   which is the editor's least settled surface. When a plugin actually needs one, *that* plugin
 *   is what the interface should be designed against -- the same argument that kept
 *   `NestedStructure` unbuilt until ED-410 asked for it, and it was right that time.
 *
 * **Everything registered is owned by a plugin id**, so unloading removes exactly what that plugin
 * added and nothing else. That is not bookkeeping for its own sake: a panel left registered after
 * its library is closed is a `std::function` whose code has been unmapped, and it fails on the next
 * frame that draws it rather than at the moment of the mistake.
 */

#include <functional>
#include <string>
#include <vector>

#include "CNA/Editor/Ui/EditorUi.hpp"

namespace CNA::Editor
{
    class EditorContext;

    /** @brief A panel a plugin draws, docked like any of the editor's own. */
    struct PluginPanel
    {
        /** @brief The plugin that registered it, so unloading can take it away again. */
        std::string ownerId;

        /** @brief The window title, which is also its identity in the dock layout. */
        std::string title;

        DockSide preferredSide = DockSide::Right;

        /**
         * @brief Draws the panel's contents. The window is begun and ended around it.
         *
         * Handed the `EditorUi` and the context, so a plugin panel has exactly what a built-in one
         * has. It is called every frame the panel is visible, and it must not outlive the plugin
         * that registered it -- which is what `ownerId` is for.
         */
        std::function<void(EditorUi&, EditorContext&)> draw;
    };

    /** @brief A command a plugin adds to the menu bar. */
    struct PluginMenuCommand
    {
        std::string ownerId;

        /**
         * @brief Which top-level menu it appears under, e.g. "File" or "Tools".
         *
         * A name rather than an enumerator, so a plugin can put a command under a menu this build
         * has never heard of and get that menu created for it. An exporter belongs under File
         * beside the editor's own; a level generator belongs under a menu only the plugin knows
         * the name of.
         */
        std::string menu = "Tools";

        std::string label;

        /** @brief What pressing it does. */
        std::function<void(EditorContext&)> invoke;
    };

    /**
     * @brief Panels and menu commands contributed by plugins (ED-412).
     *
     * Held by `EditorContext`, which is what a plugin is handed, so registering is one call with
     * nothing to look up first.
     */
    class PluginExtensionRegistry
    {
    public:
        /** @brief Adds @p panel. A second panel with the same title replaces the first. */
        void addPanel(PluginPanel panel);

        /** @brief Adds @p command. */
        void addMenuCommand(PluginMenuCommand command);

        /**
         * @brief Removes everything @p ownerId registered.
         *
         * Called by the plugin host as part of unloading, *before* the library is closed -- a
         * `std::function` outliving the code it points at is the specific way this goes wrong.
         *
         * @return How many registrations were removed, which a plugin's own `shutdown` can use to
         *         check it removed what it thought it did.
         */
        std::size_t removeAllFrom(std::string_view ownerId);

        [[nodiscard]] const std::vector<PluginPanel>& getPanels() const { return panels_; }
        [[nodiscard]] const std::vector<PluginMenuCommand>& getMenuCommands() const
        {
            return commands_;
        }

        /** @brief The distinct menu names commands were registered under, in first-seen order. */
        [[nodiscard]] std::vector<std::string> getMenuNames() const;

    private:
        std::vector<PluginPanel> panels_;
        std::vector<PluginMenuCommand> commands_;
    };
}
