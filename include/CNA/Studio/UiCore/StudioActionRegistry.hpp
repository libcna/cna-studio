// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/UiCore/StudioActionRegistry.hpp
 * @brief The one place a Studio action is defined, and the only place its logic lives.
 *
 * ### Action, not Command
 *
 * `CNA::Studio::StudioCommand` already exists and means something else: it is the undoable
 * document mutation in `CNA/Studio/Core/StudioCommand.hpp`, pushed through `CommandHistory`. A
 * menu entry is a different concept -- it may push one of those, or several, or none at all --
 * so it is an **action**.
 *
 * This distinction was not chosen on aesthetics. Declaring a second `CNA::Studio::StudioCommand`
 * here compiled cleanly, linked cleanly, and corrupted memory at run time: an ODR violation
 * across two translation units that each believed a different layout, which presented as a
 * `std::string` destructor freeing a pointer into the data segment, hundreds of tests away from
 * the cause. `STUDIO-02039` is the guard test that now refuses a duplicate type name in this
 * namespace.
 *
 * `plan.md` STUDIO-06001, STUDIO-06002, STUDIO-06008, STUDIO-06012.
 *
 * A menu item, a toolbar button and a keyboard shortcut are three ways of asking for the *same*
 * thing. When each of them carries its own copy of the logic, they drift: the toolbar button stays
 * enabled after the menu item has correctly greyed out, Ctrl+S saves a document the File menu
 * thinks is already saved, and a fourth entry point added later gets the behaviour of whichever
 * one its author happened to copy.
 *
 * So a command is a value with an id, a label, an enablement predicate and a handler, and every
 * surface *invokes* it rather than reimplementing it. Menus and toolbars are built by iterating
 * this registry, so a command added here appears in them without either being edited.
 *
 * ### Why enablement is a predicate rather than a flag
 *
 * "Can this run right now" depends on state that changes continuously — is a document open, is
 * anything selected, is a build already running. A flag has to be pushed by whoever changes that
 * state, from every place that changes it, and the bug is always the place nobody remembered. A
 * predicate is pulled when the answer is needed and cannot go stale.
 */

#include "CNA/Studio/Ui/UiInputState.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace CNA::Studio
{
    /** @brief A keyboard chord: one key plus the modifiers held with it. */
    struct StudioShortcut
    {
        UiKey key = UiKey::None;
        UiKeyModifiers modifiers;

        /** @brief Reports whether this shortcut is bound to anything. */
        [[nodiscard]] bool isBound() const { return key != UiKey::None; }

        friend bool operator==(const StudioShortcut& lhs, const StudioShortcut& rhs)
        {
            return lhs.key == rhs.key && lhs.modifiers == rhs.modifiers;
        }
    };

    /** @brief Where a command belongs in the menu structure. */
    enum class StudioActionCategory
    {
        File,
        Edit,
        View,
        Project,
        Build,
        Play,
        Tools,
        Window,
        Help
    };

    /** @brief One invocable Studio action. */
    struct StudioAction
    {
        /** @brief Stable identifier, e.g. `"studio.file.save"`. Used in preferences and tests. */
        std::string id;
        /** @brief Text shown in menus and tooltips. */
        std::string label;
        /** @brief One sentence of help, shown as a tooltip and in the shortcut editor. */
        std::string description;
        /** @brief Which menu this belongs to. */
        StudioActionCategory category = StudioActionCategory::File;
        /** @brief The chord that invokes it, if any. */
        StudioShortcut shortcut;

        /**
         * @brief Whether the command can run right now.
         *
         * Pulled when the answer is needed. An unset predicate means always enabled.
         */
        std::function<bool()> isEnabled;

        /** @brief What the command does. */
        std::function<void()> run;

        /**
         * @brief Whether the command is a checkable toggle.
         *
         * A toggle draws a check mark and reports its state through @ref isChecked.
         */
        bool checkable = false;

        /** @brief For a checkable command, whether it is currently on. */
        std::function<bool()> isChecked;
    };

    /** @brief The outcome of asking the registry to run something. */
    enum class StudioActionResult
    {
        /** @brief The handler ran. */
        Invoked,
        /** @brief No command has that id. */
        NotFound,
        /** @brief The command exists but its enablement predicate said no. */
        Disabled,
        /** @brief The command exists and is enabled but has no handler. */
        NotImplemented
    };

    /**
     * @brief Holds every Studio command and dispatches to them.
     */
    class StudioActionRegistry
    {
    public:
        /**
         * @brief Registers a command.
         *
         * Registering an id twice replaces the previous definition rather than adding a second,
         * so a plugin can deliberately override a built-in — and so a copy-paste mistake produces
         * one command rather than two that shadow each other unpredictably.
         *
         * @param command The command to register.
         * @return True when it replaced an existing command with the same id.
         */
        bool add(StudioAction command);

        /**
         * @brief Removes a command.
         *
         * For a command that names something the user deleted — a saved layout, a plugin's entry
         * when the plugin unloads. Leaving it registered would put a row in a menu that names
         * nothing and a chord in the shortcut table that does nothing, and neither reports itself.
         *
         * @param id Command id.
         * @return True when one was removed.
         */
        bool remove(std::string_view id);

        /**
         * @brief Finds a command by id.
         * @param id Command id.
         * @return The command, or nullptr.
         */
        [[nodiscard]] const StudioAction* find(std::string_view id) const;

        /**
         * @brief Reports whether a command can run right now.
         * @param id Command id.
         * @return True when the command exists and its predicate allows it.
         */
        [[nodiscard]] bool isEnabled(std::string_view id) const;

        /**
         * @brief Reports whether a checkable command is currently on.
         * @param id Command id.
         * @return True when the command is checkable and checked.
         */
        [[nodiscard]] bool isChecked(std::string_view id) const;

        /**
         * @brief Runs a command, if it exists and is enabled.
         *
         * Enablement is checked here rather than trusted to the caller. Every surface would
         * otherwise have to remember, and the one that forgets is the one that runs Save on a
         * project that is not open.
         *
         * @param id Command id.
         * @return What happened.
         */
        StudioActionResult invoke(std::string_view id);

        /**
         * @brief Finds the command bound to a chord.
         * @param shortcut The chord pressed.
         * @return The command, or nullptr when nothing is bound to it.
         */
        [[nodiscard]] const StudioAction* findByShortcut(const StudioShortcut& shortcut) const;

        /**
         * @brief Runs whatever is bound to a chord.
         * @param shortcut The chord pressed.
         * @return What happened; `NotFound` when nothing is bound.
         */
        StudioActionResult invokeShortcut(const StudioShortcut& shortcut);

        /**
         * @brief Rebinds a command's shortcut.
         *
         * Refuses a chord already bound to a different command rather than silently shadowing it:
         * two commands on one chord means one of them has stopped working, and the user who bound
         * the second has no way to discover which.
         *
         * @param id Command to rebind.
         * @param shortcut New chord; an unbound one clears the binding.
         * @param outConflictingCommandId Receives the id already holding the chord, on failure.
         * @return True when the rebinding was applied.
         */
        bool rebind(std::string_view id, const StudioShortcut& shortcut,
                    std::string* outConflictingCommandId = nullptr);

        /** @brief Every registered command, in registration order. */
        [[nodiscard]] const std::vector<StudioAction>& commands() const { return commands_; }

        /**
         * @brief The commands in one menu category, in registration order.
         * @param category Category to list.
         * @return Pointers into the registry, valid until the next add().
         */
        [[nodiscard]] std::vector<const StudioAction*> inCategory(
            StudioActionCategory category) const;

        /** @brief Number of registered commands. */
        [[nodiscard]] std::size_t size() const { return commands_.size(); }

    private:
        std::vector<StudioAction> commands_;
    };

    /**
     * @brief Returns a stable English name for a category, for menus and diagnostics.
     * @param category Category to name.
     * @return The menu title, e.g. `"File"`.
     */
    [[nodiscard]] std::string_view studioActionCategoryName(StudioActionCategory category);

    /**
     * @brief Renders a shortcut as the text a menu shows, e.g. `"Ctrl+Shift+S"`.
     * @param shortcut Chord to describe.
     * @return Display text, empty for an unbound shortcut.
     */
    [[nodiscard]] std::string describeStudioShortcut(const StudioShortcut& shortcut);

    /**
     * @brief Parses what @ref describeStudioShortcut writes, e.g. `"Ctrl+Shift+S"`.
     *
     * The inverse rather than a second spelling, because a rebinding stored in the user's
     * preferences is written by one and read by the other: a chord that round-trips differently is
     * a shortcut that changes when Studio restarts.
     *
     * @param text The chord.
     * @param out Receives the shortcut.
     * @return False when @p text names no key this build knows; @p out is then unchanged.
     */
    [[nodiscard]] bool parseStudioShortcut(std::string_view text, StudioShortcut& out);

    /** @brief The display text for a key, e.g. `"PageDown"`. Empty for @ref UiKey::None. */
    [[nodiscard]] std::string_view studioKeyName(UiKey key);

    /**
     * @brief Parses a name from @ref studioKeyName.
     * @param name The key's name.
     * @param out Receives the key.
     * @return False for a name this build does not know.
     */
    [[nodiscard]] bool parseStudioKey(std::string_view name, UiKey& out);

    /**
     * @brief Registers Studio's core command set with no handlers attached.
     *
     * The shell needs menus and a toolbar before the services those commands will call exist. This
     * registers the *set* -- ids, labels, categories, shortcuts and enablement -- so the shell can
     * be built and tested against it, and each handler is attached as its service lands.
     *
     * A command with no handler reports `NotImplemented` when invoked rather than doing nothing:
     * an unimplemented action that silently succeeds is indistinguishable from a broken one.
     *
     * @param registry Registry to populate.
     */
    void registerCoreStudioActions(StudioActionRegistry& registry);
} // namespace CNA::Studio
