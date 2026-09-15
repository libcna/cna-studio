// SPDX-License-Identifier: MS-PL
/**
 * @file StudioActionRegistry.cpp
 * @brief Command storage, dispatch, shortcut binding, and Studio's core command set.
 */

#include "CNA/Studio/UiCore/StudioActionRegistry.hpp"

#include <algorithm>

namespace CNA::Studio
{
    namespace
    {
        /** @brief Builds a modifier set from the flags a shortcut needs. */
        UiKeyModifiers mods(bool control = false, bool shift = false, bool alt = false)
        {
            UiKeyModifiers m;
            m.control = control;
            m.shift = shift;
            m.alt = alt;
            return m;
        }

        /** @brief Builds a shortcut. */
        StudioShortcut chord(UiKey key, UiKeyModifiers modifiers = {})
        {
            StudioShortcut shortcut;
            shortcut.key = key;
            shortcut.modifiers = modifiers;
            return shortcut;
        }

        /** @brief The display text for a key, for menu shortcut hints. */
        std::string_view keyName(UiKey key)
        {
            switch (key)
            {
                case UiKey::Tab:        return "Tab";
                case UiKey::LeftArrow:  return "Left";
                case UiKey::RightArrow: return "Right";
                case UiKey::UpArrow:    return "Up";
                case UiKey::DownArrow:  return "Down";
                case UiKey::PageUp:     return "PageUp";
                case UiKey::PageDown:   return "PageDown";
                case UiKey::Home:       return "Home";
                case UiKey::End:        return "End";
                case UiKey::Insert:     return "Insert";
                case UiKey::Delete:     return "Delete";
                case UiKey::Backspace:  return "Backspace";
                case UiKey::Space:      return "Space";
                case UiKey::Enter:      return "Enter";
                case UiKey::Escape:     return "Escape";
                case UiKey::A: return "A";  case UiKey::C: return "C";
                case UiKey::V: return "V";  case UiKey::X: return "X";
                case UiKey::Y: return "Y";  case UiKey::Z: return "Z";
                case UiKey::D: return "D";  case UiKey::F: return "F";
                case UiKey::N: return "N";  case UiKey::Q: return "Q";
                case UiKey::S: return "S";  case UiKey::W: return "W";
                case UiKey::E: return "E";  case UiKey::R: return "R";
                case UiKey::F1: return "F1"; case UiKey::F2: return "F2";
                case UiKey::F5: return "F5";
                case UiKey::Digit2: return "2";
                case UiKey::Digit3: return "3";
                case UiKey::None:
                case UiKey::Count: break;
            }
            return "";
        }
    } // namespace

    std::string_view studioActionCategoryName(StudioActionCategory category)
    {
        switch (category)
        {
            case StudioActionCategory::File:    return "File";
            case StudioActionCategory::Edit:    return "Edit";
            case StudioActionCategory::View:    return "View";
            case StudioActionCategory::Project: return "Project";
            case StudioActionCategory::Build:   return "Build";
            case StudioActionCategory::Play:    return "Play";
            case StudioActionCategory::Tools:   return "Tools";
            case StudioActionCategory::Window:  return "Window";
            case StudioActionCategory::Help:    return "Help";
        }
        return "";
    }

    std::string describeStudioShortcut(const StudioShortcut& shortcut)
    {
        if (!shortcut.isBound()) { return {}; }

        std::string text;
        // Conventional order, which is what every platform's menus use. Reordering the modifiers
        // makes a familiar chord read as an unfamiliar one.
        if (shortcut.modifiers.control) { text += "Ctrl+"; }
        if (shortcut.modifiers.alt) { text += "Alt+"; }
        if (shortcut.modifiers.shift) { text += "Shift+"; }
        if (shortcut.modifiers.super) { text += "Super+"; }
        text += keyName(shortcut.key);
        return text;
    }

    bool StudioActionRegistry::add(StudioAction command)
    {
        const auto existing = std::find_if(commands_.begin(), commands_.end(),
            [&](const StudioAction& c) { return c.id == command.id; });

        if (existing != commands_.end())
        {
            *existing = std::move(command);
            return true;
        }
        commands_.push_back(std::move(command));
        return false;
    }

    const StudioAction* StudioActionRegistry::find(std::string_view id) const
    {
        const auto found = std::find_if(commands_.begin(), commands_.end(),
            [&](const StudioAction& c) { return c.id == id; });
        return found == commands_.end() ? nullptr : &*found;
    }

    bool StudioActionRegistry::isEnabled(std::string_view id) const
    {
        const StudioAction* command = find(id);
        if (command == nullptr) { return false; }

        // A command with no handler is not available, whatever its predicate says. Otherwise every
        // half-migrated menu row draws as though it works, and a control that looks available and
        // then does nothing is indistinguishable from one that is broken -- which is worse than a
        // greyed-out row, because the user cannot tell whether to report it.
        if (!command->run) { return false; }

        // No predicate means always available. That is the common case, and requiring every
        // command to supply a trivial one would be noise that hides the ones that matter.
        return !command->isEnabled || command->isEnabled();
    }

    bool StudioActionRegistry::isChecked(std::string_view id) const
    {
        const StudioAction* command = find(id);
        if (command == nullptr || !command->checkable || !command->isChecked) { return false; }
        return command->isChecked();
    }

    StudioActionResult StudioActionRegistry::invoke(std::string_view id)
    {
        const StudioAction* command = find(id);
        if (command == nullptr) { return StudioActionResult::NotFound; }
        if (command->isEnabled && !command->isEnabled()) { return StudioActionResult::Disabled; }
        if (!command->run) { return StudioActionResult::NotImplemented; }

        // Copied before invoking. A handler may register commands -- a plugin loading, a tool
        // installing its own actions -- and that reallocates the vector out from under this
        // pointer. The crash would be intermittent and would look like anything but this.
        const std::function<void()> handler = command->run;
        handler();
        return StudioActionResult::Invoked;
    }

    const StudioAction* StudioActionRegistry::findByShortcut(const StudioShortcut& shortcut) const
    {
        if (!shortcut.isBound()) { return nullptr; }

        const auto found = std::find_if(commands_.begin(), commands_.end(),
            [&](const StudioAction& c) { return c.shortcut.isBound() && c.shortcut == shortcut; });
        return found == commands_.end() ? nullptr : &*found;
    }

    StudioActionResult StudioActionRegistry::invokeShortcut(const StudioShortcut& shortcut)
    {
        const StudioAction* command = findByShortcut(shortcut);
        if (command == nullptr) { return StudioActionResult::NotFound; }
        return invoke(command->id);
    }

    bool StudioActionRegistry::rebind(std::string_view id, const StudioShortcut& shortcut,
                                       std::string* outConflictingCommandId)
    {
        const auto target = std::find_if(commands_.begin(), commands_.end(),
            [&](const StudioAction& c) { return c.id == id; });
        if (target == commands_.end()) { return false; }

        if (shortcut.isBound())
        {
            const StudioAction* holder = findByShortcut(shortcut);
            if (holder != nullptr && holder->id != target->id)
            {
                // Refused rather than shadowed. Two commands on one chord means one has stopped
                // working, and the user who bound the second has no way to find out which.
                if (outConflictingCommandId != nullptr) { *outConflictingCommandId = holder->id; }
                return false;
            }
        }
        target->shortcut = shortcut;
        return true;
    }

    std::vector<const StudioAction*> StudioActionRegistry::inCategory(
        StudioActionCategory category) const
    {
        std::vector<const StudioAction*> result;
        for (const StudioAction& command : commands_)
        {
            if (command.category == category) { result.push_back(&command); }
        }
        return result;
    }

    void registerCoreStudioActions(StudioActionRegistry& registry)
    {
        const auto command = [&](const char* id, const char* label, const char* description,
                                 StudioActionCategory category, StudioShortcut shortcut,
                                 bool checkable = false) {
            StudioAction entry;
            entry.id = id;
            entry.label = label;
            entry.description = description;
            entry.category = category;
            entry.shortcut = shortcut;
            entry.checkable = checkable;
            registry.add(std::move(entry));
        };

        using C = StudioActionCategory;

        command("studio.file.newProject", "New Project...",
                "Create a new CNA game project.", C::File, chord(UiKey::N, mods(true)));
        command("studio.file.openProject", "Open Project...",
                "Open an existing CNA game project.", C::File, chord(UiKey::D, mods(true)));
        command("studio.file.save", "Save",
                "Save the active document.", C::File, chord(UiKey::S, mods(true)));
        command("studio.file.saveAll", "Save All",
                "Save every document with unsaved changes.", C::File,
                chord(UiKey::S, mods(true, true)));
        command("studio.file.quit", "Quit",
                "Close CNA Studio.", C::File, chord(UiKey::Q, mods(true)));

        command("studio.edit.undo", "Undo",
                "Undo the last change.", C::Edit, chord(UiKey::Z, mods(true)));
        command("studio.edit.redo", "Redo",
                "Redo the last undone change.", C::Edit, chord(UiKey::Y, mods(true)));
        command("studio.edit.duplicate", "Duplicate",
                "Duplicate the selection.", C::Edit, chord(UiKey::D, mods(true, true)));
        command("studio.edit.delete", "Delete",
                "Delete the selection.", C::Edit, chord(UiKey::Delete));

        command("studio.view.focusSelected", "Focus Selected",
                "Move the viewport camera to frame the selection.", C::View, chord(UiKey::F));
        command("studio.view.toggleGrid", "Show Grid",
                "Show or hide the viewport grid.", C::View, {}, /*checkable=*/true);
        command("studio.view.translate", "Translate",
                "Switch the gizmo to translation.", C::View, chord(UiKey::W));
        command("studio.view.rotate", "Rotate",
                "Switch the gizmo to rotation.", C::View, chord(UiKey::E));
        command("studio.view.scale", "Scale",
                "Switch the gizmo to scaling.", C::View, chord(UiKey::R));

        command("studio.play.play", "Play",
                "Launch the game in a player process.", C::Play, chord(UiKey::F5));
        command("studio.play.stop", "Stop",
                "Stop the running player.", C::Play, chord(UiKey::F5, mods(false, true)));

        command("studio.build.build", "Build",
                "Build the project with its own CMake.", C::Build, chord(UiKey::F2));
        command("studio.build.package", "Package...",
                "Package a standalone build of the game.", C::Build, {});

        command("studio.window.resetLayout", "Reset Layout",
                "Restore the default panel arrangement.", C::Window, {});

        command("studio.window.dockAll", "Dock All Windows",
                "Return every floating panel to the workspace.", C::Window, {});

        command("studio.help.about", "About CNA Studio",
                "Version, renderer and platform information.", C::Help, chord(UiKey::F1));

        // Toggles carry their checked state the same way they carry enablement -- through a
        // predicate pulled when needed, so it cannot go stale. The predicate itself is attached
        // when the viewport service that owns the grid setting lands (STUDIO-11005).
    }
} // namespace CNA::Studio
