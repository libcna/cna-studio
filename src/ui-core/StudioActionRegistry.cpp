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
        std::string_view keyNameImpl(UiKey key)
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
                case UiKey::N: return "N";  case UiKey::O: return "O";
                case UiKey::B: return "B";
                case UiKey::Q: return "Q";
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

    std::string_view studioKeyName(UiKey key) { return keyNameImpl(key); }

    bool parseStudioKey(std::string_view name, UiKey& out)
    {
        if (name.empty()) { return false; }

        // Over the enum rather than a second table: a name list written twice is a list that
        // disagrees with itself the first time a key is added, and the disagreement is a shortcut
        // that stops loading.
        for (int value = 1; value < static_cast<int>(UiKey::Count); ++value)
        {
            const auto key = static_cast<UiKey>(value);
            if (!keyNameImpl(key).empty() && keyNameImpl(key) == name) { out = key; return true; }
        }
        return false;
    }

    bool parseStudioShortcut(std::string_view text, StudioShortcut& out)
    {
        if (text.empty()) { return false; }

        StudioShortcut parsed;
        std::size_t start = 0;
        while (true)
        {
            const std::size_t plus = text.find('+', start);
            const std::string_view part = text.substr(start, plus == std::string_view::npos
                                                                 ? std::string_view::npos
                                                                 : plus - start);
            if (plus == std::string_view::npos)
            {
                if (!parseStudioKey(part, parsed.key)) { return false; }
                break;
            }

            if (part == "Ctrl") { parsed.modifiers.control = true; }
            else if (part == "Alt") { parsed.modifiers.alt = true; }
            else if (part == "Shift") { parsed.modifiers.shift = true; }
            else if (part == "Super") { parsed.modifiers.super = true; }
            else { return false; }

            start = plus + 1;
        }

        out = parsed;
        return true;
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
        text += studioKeyName(shortcut.key);
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

    bool StudioActionRegistry::remove(std::string_view id)
    {
        const auto found = std::find_if(commands_.begin(), commands_.end(),
            [&](const StudioAction& command) { return command.id == id; });
        if (found == commands_.end()) { return false; }
        commands_.erase(found);
        return true;
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

        // The chords follow the prototype's, which is what existing users' hands already know
        // (docs/MIGRATION-INVENTORY.md). Ctrl+N is New *Scene* there and is the frequent one, so it
        // keeps the plain chord; New Project takes the Shift variant, as it does in most IDEs.
        command("studio.file.newScene", "New Scene",
                "Start an empty scene.", C::File, chord(UiKey::N, mods(true)));
        command("studio.file.newProject", "New Project...",
                "Create a new CNA game project.", C::File, chord(UiKey::N, mods(true, true)));
        // Ctrl+O, not Ctrl+D: Ctrl+D is Duplicate in the prototype and in every editor that has a
        // duplicate, and taking it for Open would silently repurpose a key people press all day.
        command("studio.file.openProject", "Open Project...",
                "Open an existing CNA game project.", C::File, chord(UiKey::O, mods(true)));
        command("studio.file.save", "Save",
                "Save the active document.", C::File, chord(UiKey::S, mods(true)));
        command("studio.file.saveAll", "Save All",
                "Save every document with unsaved changes.", C::File,
                chord(UiKey::S, mods(true, true)));
        // No chord, deliberately. These appear only after a crash, they are answered once, and one
        // of the two throws work away -- a key that does that is a key somebody hits by accident.
        command("studio.file.recoverScene", "Recover Unsaved Scene",
                "Restore the unsaved work a previous session left behind.", C::File,
                StudioShortcut{});
        command("studio.file.discardRecovered", "Discard Recovered Scene",
                "Throw away the unsaved work a previous session left behind.", C::File,
                StudioShortcut{});
        command("studio.file.quit", "Quit",
                "Close CNA Studio.", C::File, chord(UiKey::Q, mods(true)));

        command("studio.edit.undo", "Undo",
                "Undo the last change.", C::Edit, chord(UiKey::Z, mods(true)));
        command("studio.edit.redo", "Redo",
                "Redo the last undone change.", C::Edit, chord(UiKey::Y, mods(true)));
        // F2, as in the prototype and in every file manager and editor that renames in place.
        command("studio.edit.rename", "Rename",
                "Rename the selection in the World Outliner.", C::Edit, chord(UiKey::F2));
        command("studio.edit.duplicate", "Duplicate",
                "Duplicate the selection.", C::Edit, chord(UiKey::D, mods(true)));
        command("studio.edit.delete", "Delete",
                "Delete the selection.", C::Edit, chord(UiKey::Delete));

        command("studio.view.focusSelected", "Focus Selected",
                "Move the viewport camera to frame the selection.", C::View, chord(UiKey::F));
        command("studio.view.toggleGrid", "Show Grid",
                "Show or hide the viewport grid.", C::View, {}, /*checkable=*/true);
        // `STUDIO-07056`. Checkable rather than two commands, because it is one choice with two
        // answers and a pair would put both on the menu with one of them always wrong. Disabled in
        // the 2D view rather than hidden: a user who went looking for it should find it and see
        // why it is greyed out, which a missing row cannot tell them.
        command("studio.view.gridOnGroundPlane", "Grid on Ground Plane",
                "Draw the 3D grid on the ground plane (XZ) rather than the scene's own (XY).",
                C::View, {}, /*checkable=*/true);
        command("studio.view.translate", "Translate",
                "Switch the gizmo to translation.", C::View, chord(UiKey::W));
        command("studio.view.rotate", "Rotate",
                "Switch the gizmo to rotation.", C::View, chord(UiKey::E));
        // The two views, checkable and exclusive. The prototype binds 2 and 3 for these and so
        // does this: they are the keys anybody who has used a 3D editor reaches for, and a view
        // that can only be changed through a menu is one people stop changing.
        command("studio.view.2d", "2D View",
                "Show the scene in the orthographic 2D view.", C::View, chord(UiKey::Digit2),
                /*checkable=*/true);
        command("studio.view.3d", "3D View",
                "Show the scene in the 3D view.", C::View, chord(UiKey::Digit3),
                /*checkable=*/true);

        // The six axis-aligned views (`plan.md` STUDIO-11004). Unbound by default, which is a
        // decision rather than an omission: the keys a user's hands already know for these are the
        // numpad's 1, 3 and 7, which this build's key vocabulary does not carry, and the plain
        // digits next to them are already the 2D and 3D toggles above. Inventing a third scheme
        // nobody knows would be worse than a menu entry somebody can bind for themselves, which
        // the shortcut editor lets them do.
        command("studio.view.front", "Front View",
                "Look at the scene from the front, along -Z.", C::View, StudioShortcut{});
        command("studio.view.back", "Back View",
                "Look at the scene from behind, along +Z.", C::View, StudioShortcut{});
        command("studio.view.left", "Left View",
                "Look at the scene from the left, along +X.", C::View, StudioShortcut{});
        command("studio.view.right", "Right View",
                "Look at the scene from the right, along -X.", C::View, StudioShortcut{});
        command("studio.view.top", "Top View",
                "Look straight down at the scene.", C::View, StudioShortcut{});
        command("studio.view.bottom", "Bottom View",
                "Look straight up at the scene.", C::View, StudioShortcut{});

        // How the 3D view draws geometry (`plan.md` STUDIO-11010). Checkable and exclusive, like
        // the 2D/3D pair: a toolbar has to *show* which is on, because the difference between a
        // clean shaded picture and a hatched one is exactly what a user is looking at when they
        // reach for this.
        command("studio.view.shading.shaded", "Shaded",
                "Draw solid geometry without its edges.", C::View, StudioShortcut{},
                /*checkable=*/true);
        command("studio.view.shading.wireframe", "Wireframe",
                "Draw only the edges: no solid meshes and no textured sprites.", C::View,
                StudioShortcut{}, /*checkable=*/true);
        command("studio.view.shading.shadedWireframe", "Shaded Wireframe",
                "Draw solid geometry with its edges over it.", C::View, StudioShortcut{},
                /*checkable=*/true);

        // The tilemap tools. Checkable, because a toolbar has to *show* which one is armed: a
        // press means something different under each of them, and a user who cannot see which is
        // active finds out by editing their level.
        command("studio.view.tool.select", "Select Tool",
                "Pick entities and drag the gizmo.", C::View, StudioShortcut{}, /*checkable=*/true);
        command("studio.view.tool.paint", "Paint Tiles",
                "Set the tile under the cursor on the selected tilemap.", C::View, StudioShortcut{},
                /*checkable=*/true);
        command("studio.view.tool.erase", "Erase Tiles",
                "Clear the tile under the cursor.", C::View, StudioShortcut{}, /*checkable=*/true);
        command("studio.view.tool.pick", "Pick Tile",
                "Take the tile under the cursor as the brush, then go back to painting.", C::View,
                StudioShortcut{}, /*checkable=*/true);
        command("studio.view.tool.fill", "Fill Tiles",
                "Fill the rectangle a drag encloses.", C::View, StudioShortcut{},
                /*checkable=*/true);

        command("studio.view.toggleGizmoSpace", "Toggle Gizmo Space",
                "Switch the gizmo between world and local space.", C::View, chord(UiKey::X));
        command("studio.view.scale", "Scale",
                "Switch the gizmo to scaling.", C::View, chord(UiKey::R));

        command("studio.play.play", "Play",
                "Launch the game in a player process.", C::Play, chord(UiKey::F5));
        command("studio.play.stop", "Stop",
                "Stop the running player.", C::Play, chord(UiKey::F5, mods(false, true)));
        // Checkable rather than a button whose label flips between Pause and Resume. A menu row
        // that renames itself is one a user cannot find twice, and a toolbar has to *show* whether
        // the game is paused: the window is there either way, so nothing else says which.
        command("studio.play.pause", "Pause",
                "Pause the running game, or resume it.", C::Play,
                chord(UiKey::F5, mods(true)), /*checkable=*/true);
        command("studio.play.step", "Step One Frame",
                "Advance a paused game by a single frame.", C::Play, chord(UiKey::F5, mods(false, false, true)));
        command("studio.play.restart", "Restart",
                "Stop the game and start it again from the scene as it now stands.", C::Play,
                StudioShortcut{});

        command("studio.build.build", "Build",
                // Ctrl+B, not F2: F2 is Rename in the prototype (docs/MIGRATION-INVENTORY.md) and
                // in every file manager, and a shortcut that moved is one every existing user has
                // to relearn -- silently, because it still does something.
                "Build the project with its own CMake.", C::Build, chord(UiKey::B, mods(true)));
        command("studio.build.cancel", "Cancel Build",
                "Stop the build that is running.", C::Build, {});

        command("studio.build.package", "Package...",
                "Package a standalone build of the game.", C::Build, {});

        command("studio.window.resetLayout", "Reset Layout",
                "Restore the default panel arrangement.", C::Window, {});

        command("studio.window.saveLayoutAs", "Save Layout As...",
                "Save this arrangement under a name.", C::Window, {});

        command("studio.window.dockAll", "Dock All Windows",
                "Return every floating panel to the workspace.", C::Window, {});

        command("studio.help.about", "About CNA Studio",
                "Version, renderer and platform information.", C::Help, chord(UiKey::F1));

        // Toggles carry their checked state the same way they carry enablement -- through a
        // predicate pulled when needed, so it cannot go stale. The predicate itself is attached
        // when the viewport service that owns the grid setting lands (STUDIO-11005).
    }
} // namespace CNA::Studio
