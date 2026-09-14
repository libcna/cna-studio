// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Panels/MainMenuBar.hpp"

#include <string>

#include "CNA/Editor/EditorContext.hpp"

namespace CNA::Editor
{
    void MainMenuBar::draw()
    {
        if (ui_.beginMenu("File"))
        {
            if (ui_.menuItem("New Scene", "Ctrl+N")) { actions_.newScene(); }
            if (ui_.menuItem("Save Scene", "Ctrl+S", !context_.getScenePath().empty()))
            {
                actions_.saveScene();
            }
            // Only shown when there is something to recover. A permanently greyed-out pair of
            // items would teach users to ignore the one message that matters after a crash.
            if (const RecoverySnapshot* snapshot = actions_.getRecoverableScene())
            {
                ui_.separator();
                const std::string when = formatRecoveryTime(snapshot->savedAtSeconds);
                if (ui_.menuItem("Recover Unsaved Scene (" + when + ")")) { actions_.recoverScene(); }
                if (ui_.menuItem("Discard Recovered Scene")) { actions_.discardRecoveredScene(); }
            }

            ui_.separator();
            if (ui_.menuItem("Exit", "Alt+F4")) { /* handled by the UI implementation */ }
            ui_.endMenu();
        }

        if (ui_.beginMenu("Edit"))
        {
            CommandHistory& history = context_.getHistory();
            if (ui_.menuItem("Undo " + history.getUndoDescription(), "Ctrl+Z", history.canUndo()))
            {
                actions_.undo();
            }
            if (ui_.menuItem("Redo " + history.getRedoDescription(), "Ctrl+Y", history.canRedo()))
            {
                actions_.redo();
            }

            ui_.separator();

            const bool hasSelection = !context_.getSelection().empty();
            if (ui_.menuItem("Duplicate", "Ctrl+D", hasSelection)) { actions_.duplicateSelection(); }
            if (ui_.menuItem("Delete", "Del", hasSelection)) { actions_.deleteSelection(); }
            ui_.endMenu();
        }

        if (ui_.beginMenu("View"))
        {
            if (ui_.menuItem("Frame Selected", "F", !context_.getSelection().empty()))
            {
                actions_.frameSelection();
            }

            ui_.separator();

            // Same rule as the space item below: named for what pressing it does, because a menu
            // has no checkmark here to carry the state. The toolbar is where the state is shown.
            // The key that selects the view the item offers, not the one the user is in: the
            // item and its shortcut have to do the same thing or the shortcut column is a lie.
            if (ui_.menuItem(actions_.isThreeDimensionalView() ? "2D View" : "3D View",
                             actions_.isThreeDimensionalView() ? "2" : "3"))
            {
                actions_.setThreeDimensionalView(!actions_.isThreeDimensionalView());
            }

            // Offered only where it does something. The 2D viewport has one plane and no choice to
            // make about it, and a menu item that changes nothing visible is a bug report waiting
            // to be filed.
            if (actions_.isThreeDimensionalView())
            {
                const bool ground = actions_.getGridPlane() == GridPlane::Ground;
                if (ui_.menuItem(ground ? "Grid on Scene Plane" : "Grid on Ground Plane"))
                {
                    actions_.setGridPlane(ground ? GridPlane::SceneXY : GridPlane::Ground);
                }
            }

            ui_.separator();

            if (ui_.menuItem("Translate Gizmo", "W")) { actions_.setGizmoMode(GizmoMode::Translate); }
            if (ui_.menuItem("Rotate Gizmo", "E")) { actions_.setGizmoMode(GizmoMode::Rotate); }
            if (ui_.menuItem("Scale Gizmo", "R")) { actions_.setGizmoMode(GizmoMode::Scale); }

            ui_.separator();

            // Named for what pressing it does rather than for the state it is in, since the menu
            // has no checkmark to carry the state.
            if (ui_.menuItem(actions_.getGizmoSpace() == GizmoSpace::World ? "Use Local Space"
                                                                          : "Use World Space",
                             "X"))
            {
                actions_.setGizmoSpace(actions_.getGizmoSpace() == GizmoSpace::World
                                           ? GizmoSpace::Local
                                           : GizmoSpace::World);
            }
            ui_.endMenu();
        }

        drawPluginMenus();
    }

    void MainMenuBar::drawPluginMenus()
    {
        // After the editor's own three, so a plugin cannot push File out from under the cursor of
        // somebody who has been using this editor for a year (ED-412).
        const PluginExtensionRegistry& extensions = context_.getPluginExtensions();

        // Copied, because invoking a command may unload the plugin that registered it -- a
        // "Reload Plugin" command does exactly that -- and the vector it lives in would be gone
        // underneath the loop.
        const std::vector<PluginMenuCommand> commands = extensions.getMenuCommands();

        for (const std::string& menu : extensions.getMenuNames())
        {
            if (!ui_.beginMenu(menu)) { continue; }

            for (const PluginMenuCommand& command : commands)
            {
                if (command.menu != menu || !command.invoke) { continue; }
                if (!ui_.menuItem(command.label)) { continue; }

                try
                {
                    command.invoke(context_);
                }
                catch (const std::exception& thrown)
                {
                    // A plugin command that throws is a plugin problem and is reported as one.
                    // Letting it escape would end the frame inside a menu, with the menu stack
                    // half unwound.
                    context_.log(LogSeverity::Warning,
                                 "Plugin command '" + command.label + "' failed: " + thrown.what());
                }
            }

            ui_.endMenu();
        }
    }
}
