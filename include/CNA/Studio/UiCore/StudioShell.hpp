// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/UiCore/StudioShell.hpp
 * @brief The CNA Studio application frame, as something a user can actually operate.
 *
 * `plan.md` STUDIO-06003, STUDIO-06004, STUDIO-06006, STUDIO-06007, STUDIO-06008.
 *
 * `StudioShellLayout` says where the shell's regions are and `drawStudioShell` draws a picture of
 * them. This is the third piece: the object that owns the shell's *state* — which menu is open,
 * which item the keyboard is on, which tab each dock is showing — drives the five frame phases
 * over it, and routes every activation to the one @ref StudioActionRegistry.
 *
 * ### Everything goes through the action registry, including the menus themselves
 *
 * A menu is defined as a category and a list of **action ids**. The labels, the shortcut hints,
 * the check marks and the greying-out are all read from the registry at the moment the menu is
 * drawn. So a command whose enablement predicate turns false greys out in the menu, in the
 * toolbar and in the shortcut table on the same frame, without any of the three being told.
 *
 * That is the whole reason the registry exists, and it is worth being blunt about the alternative:
 * a menu that carries its own copy of "Save is enabled when a document is dirty" will disagree
 * with the toolbar within a month, and the bug will be reported as "the Save button is broken"
 * when in fact it is the only one that is right.
 *
 * ### Menu behaviour that a professional tool is expected to have
 *
 * - A click on a title opens its menu; a click on the open title closes it.
 * - While **any** menu is open, moving the pointer across the bar switches menus with no further
 *   click. This is the behaviour every desktop menu bar has and its absence is felt immediately.
 * - Press on a title, drag down the list, release on an item: that activates the item. The press
 *   was never on the item, which is why @ref StudioInteraction::releasedOver exists.
 * - While a menu is open it **blocks** the panels beneath it. Not by being drawn on top — by
 *   raising the router's blocking layer before anything is described, so the panels never see the
 *   input at all rather than seeing it one frame late.
 * - Up, Down, Home and End move the keyboard highlight, skipping separators and disabled items.
 *   Enter activates. Escape closes and returns focus. Left and Right move between menus.
 * - Clicking anywhere outside the bar and the open list closes the menu without activating
 *   anything.
 *
 * ### Shortcut scope (STUDIO-06008)
 *
 * A chord is dispatched through the registry, never by a widget. Two rules keep it from firing
 * when it should not: while a menu is open the menu owns the keyboard, and while a text field is
 * taking input an unmodified chord is a character rather than a command. `F` focuses the selection
 * in a viewport and types an `f` in a name field, and the difference is not something either the
 * viewport or the field should have to know.
 */

#include "CNA/Studio/Ui/UiInputState.hpp"
#include "CNA/Studio/UiCore/StudioActionRegistry.hpp"
#include "CNA/Studio/UiCore/StudioDockTree.hpp"
#include "CNA/Studio/UiCore/StudioFontAtlas.hpp"
#include "CNA/Studio/UiCore/StudioFrame.hpp"
#include "CNA/Studio/UiCore/StudioShellLayout.hpp"
#include "CNA/Studio/UiCore/StudioTheme.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace CNA::Studio
{
    /** @brief The id an entry uses to mean "a separator here", rather than an action. */
    inline constexpr std::string_view kStudioMenuSeparatorId = "-";

    /**
     * @brief A panel the shell knows about.
     *
     * Identity and presentation are separate fields on purpose: the **id** is what a saved layout
     * stores and what the dock tree arranges, and it must not change when the title is reworded or
     * translated. A workspace that stopped restoring because a panel was renamed from "Details" to
     * "Inspector" would be a self-inflicted data loss.
     */
    struct StudioPanelDescriptor
    {
        /** @brief Stable identifier, e.g. `"outliner"`. Written into saved layouts. */
        std::string id;
        /** @brief Text shown on the tab. */
        std::string title;
        /** @brief True when the panel's content has unsaved changes. */
        bool modified = false;
        /** @brief False for a panel the user must not be able to close, such as the viewport. */
        bool closable = true;
        /** @brief True to draw the body as the 3D viewport rather than as a panel surface. */
        bool isViewport = false;
    };

    /** @brief One menu in the application menu bar. */
    struct StudioMenuDefinition
    {
        /** @brief Title shown in the bar. */
        std::string title;
        /**
         * @brief Action ids in order, with @ref kStudioMenuSeparatorId for a rule.
         *
         * Ids rather than labels: everything a row shows is read from the registry when it is
         * drawn, so a menu cannot fall out of step with the command it invokes.
         */
        std::vector<std::string> entries;
    };

    /**
     * @brief The interactive CNA Studio application frame.
     *
     * Owns a @ref StudioFrame and drives all five of its phases. One call per frame:
     * @ref renderFrame takes input and produces draw data, a cursor shape and whatever actions the
     * user asked for.
     */
    class StudioShell
    {
    public:
        /** @brief Constructs a shell on the dark theme with Studio's core action set registered. */
        StudioShell();

        /**
         * @brief Constructs a shell on a given theme.
         * @param theme Theme to resolve every colour and metric through.
         */
        explicit StudioShell(StudioTheme theme);

        // --- Configuration -------------------------------------------------------------------------

        /** @brief The action registry every surface invokes through. */
        [[nodiscard]] StudioActionRegistry& actions() { return actions_; }

        /** @brief The action registry. */
        [[nodiscard]] const StudioActionRegistry& actions() const { return actions_; }

        /** @brief The frame the shell drives. */
        [[nodiscard]] StudioFrame& frame() { return frame_; }

        /** @brief The frame the shell drives. */
        [[nodiscard]] const StudioFrame& frame() const { return frame_; }

        /** @brief The glyph atlas the shell draws its text through. */
        [[nodiscard]] StudioFontAtlas& fontAtlas() { return fonts_; }

        /** @brief The glyph atlas. */
        [[nodiscard]] const StudioFontAtlas& fontAtlas() const { return fonts_; }

        /** @brief The workspace arrangement. */
        [[nodiscard]] StudioDockTree& dockTree() { return dock_; }

        /** @brief The workspace arrangement. */
        [[nodiscard]] const StudioDockTree& dockTree() const { return dock_; }

        /**
         * @brief Replaces the menu bar definition.
         * @param menus Menus in bar order.
         */
        void setMenus(std::vector<StudioMenuDefinition> menus);

        /** @brief The menu bar definition. */
        [[nodiscard]] const std::vector<StudioMenuDefinition>& menus() const { return menus_; }

        /**
         * @brief Replaces the toolbar contents.
         * @param entries Action ids in order, with @ref kStudioMenuSeparatorId for a group rule.
         */
        void setToolbar(std::vector<std::string> entries);

        /**
         * @brief Registers a panel, or replaces the descriptor of one already registered.
         *
         * Registering does not dock it; @ref dockTree decides where it goes. The two are separate
         * because a panel the user closed still exists and must be re-openable from the Window
         * menu without being reconstructed.
         *
         * @param panel Descriptor to register.
         */
        void registerPanel(StudioPanelDescriptor panel);

        /**
         * @brief Finds a registered panel.
         * @param id Panel id.
         * @return Its descriptor, or nullptr.
         */
        [[nodiscard]] const StudioPanelDescriptor* panel(std::string_view id) const;

        /** @brief Every registered panel, in registration order. */
        [[nodiscard]] const std::vector<StudioPanelDescriptor>& registeredPanels() const
        {
            return panels_;
        }

        /**
         * @brief Marks a panel as having unsaved changes.
         * @param id Panel id.
         * @param modified True when its content is dirty.
         * @return True when the panel is registered.
         */
        bool setPanelModified(std::string_view id, bool modified);

        /**
         * @brief What draws inside a panel.
         *
         * Called once per pass with the panel's content rectangle, inside the panel's own clip and
         * id scope — so a widget in one panel can share an id with a widget in another without the
         * two colliding, and content that overruns its panel is cut off rather than drawn over the
         * neighbour.
         *
         * Only the active tab of each leaf is called: a panel behind another is not drawn, and not
         * described either, so it costs nothing.
         */
        using StudioPanelContent = std::function<void(StudioFrame&, const UiRect&)>;

        /**
         * @brief Sets what draws inside a registered panel.
         *
         * This is the strangler seam for Phase 7: a panel is ported by giving the shell its content
         * function, and the ImGui implementation keeps working untouched until it is deleted. A
         * panel with no content function is drawn as an empty surface, which is what every panel
         * looks like before it is ported.
         *
         * @param id Panel id.
         * @param content What to draw, or an empty function to go back to an empty surface.
         * @return True when the panel is registered.
         */
        bool setPanelContent(std::string_view id, StudioPanelContent content);

        /** @brief Arranges the registered panels into Studio's default workspace. */
        void resetLayout();

        /**
         * @brief Whether a panel is currently docked somewhere.
         * @param id Panel id.
         * @return True when it is in the workspace.
         */
        [[nodiscard]] bool isPanelOpen(std::string_view id) const;

        /**
         * @brief Docks a registered panel that is not currently open.
         *
         * Into the largest leaf, as a new active tab. Remembering where a panel was last docked is
         * `STUDIO-05010`'s problem; putting it somewhere the user can see is this one's, and a
         * panel reopened into a two-tab-wide corner reads as a panel that did not reopen.
         *
         * @param id Panel id.
         * @return True when it was registered and is now open.
         */
        bool openPanel(std::string_view id);

        /**
         * @brief Brings an open panel to the front of its tab group.
         *
         * Distinct from @ref openPanel, which docks a panel that is not open at all. A panel
         * sharing a tab strip with five others is invisible until something raises it, and which
         * one happens to be in front is whichever docked last — so a script that wants to
         * photograph a particular panel, or a command that wants to show the user a result, has to
         * be able to say which.
         *
         * @param id Panel id.
         * @return True when it was open and is now the active tab.
         */
        bool activatePanel(std::string_view id);

        /**
         * @brief Removes a panel from the workspace.
         *
         * Refuses a panel whose descriptor says it is not closable: a workspace with no viewport
         * is not a smaller workspace, it is a broken one.
         *
         * @param id Panel id.
         * @return True when it was open and is now closed.
         */
        bool closePanel(std::string_view id);

        /**
         * @brief Runs a registered action by id, recording what happened.
         *
         * Public because the alternative is worse. `actions()` is public, so anything that needs
         * to run a command can already reach `StudioActionRegistry::invoke` -- and doing so skips
         * the shell's record of what ran and what was refused, which is the only thing that makes
         * a menu row quietly doing nothing discoverable without a debugger. One public route that
         * keeps the books beats a private one everybody goes around.
         *
         * A refusal -- an unknown id, a disabled action, one with no handler -- is recorded rather
         * than thrown or ignored. See @ref refusedActions.
         *
         * @param id The action's id.
         */
        void invoke(std::string_view id);

        /**
         * @brief Serializes the workspace arrangement.
         * @return The JSON document, versioned by @ref StudioDockTree::kLayoutVersion.
         */
        [[nodiscard]] JsonValue saveLayout() const;

        /**
         * @brief Restores a workspace arrangement, never failing into an unusable state.
         *
         * A document this Studio cannot read, a tree that is not well-formed, or one naming panels
         * that no longer exist all resolve to something usable: the default workspace in the first
         * two cases, and the same arrangement minus the unknown panels in the third. A corrupt
         * layout file must never be the reason Studio will not start (`STUDIO-05012`), and an
         * upgrade that removed a panel must not cost the user the rest of their arrangement
         * (`STUDIO-05011`).
         *
         * @param value Document to read.
         * @param outProblem Receives what could not be read, when anything could not be.
         * @return True when the document was read exactly as written.
         */
        bool loadLayout(const JsonValue& value, std::string* outProblem = nullptr);

        /**
         * @brief Where a panel's body is drawn, or an empty rectangle when it is not showing.
         *
         * Empty for a panel that is docked but is not the active tab of its group, as well as for
         * one that is not docked at all: in both cases there is nowhere to draw it, and a caller
         * that checks for an empty rectangle handles both without asking which.
         *
         * @param id Panel id.
         * @return Its body rectangle.
         */
        [[nodiscard]] UiRect panelBounds(std::string_view id) const;

        /**
         * @brief Where a panel's tab sits, or an empty rectangle when it is not docked.
         * @param id Panel id.
         * @return Its tab rectangle.
         */
        [[nodiscard]] UiRect panelTabBounds(std::string_view id) const;

        /**
         * @brief Declares that something in the shell is taking typed input this frame.
         *
         * Studio has no text fields of its own yet, and a hosted panel that does -- the Dear ImGui
         * panels during the migration, and the Studio fields of STUDIO-03024 after it -- has to be
         * able to say so, or every unmodified shortcut fires while the user types. A text-field
         * widget declares the same thing on the router directly; both are honoured, because
         * neither clears the other within a frame.
         *
         * @param active True while typed input is being consumed.
         */
        void setTextInputActive(bool active) { textInputActive_ = active; }

        /** @brief Whether typed input is being consumed this frame. */
        [[nodiscard]] bool isTextInputActive() const { return textInputActive_; }

        /** @brief Sets the left-aligned status bar text. */
        void setStatusLeft(std::string text) { statusLeft_ = std::move(text); }
        /** @brief Sets the right-aligned status bar text. */
        void setStatusRight(std::string text) { statusRight_ = std::move(text); }

        /** @brief What the status bar says on the left: what the user is working on. */
        [[nodiscard]] const std::string& statusLeft() const { return statusLeft_; }

        /** @brief What the status bar says on the right: what the build is running on. */
        [[nodiscard]] const std::string& statusRight() const { return statusRight_; }

        /** @brief The theme in use. */
        [[nodiscard]] const StudioTheme& theme() const { return frame_.theme(); }

        /**
         * @brief Replaces the theme. Between frames only.
         * @param theme New theme.
         */
        void setTheme(StudioTheme theme) { frame_.setTheme(std::move(theme)); }

        // --- The frame -----------------------------------------------------------------------------

        /**
         * @brief Runs one complete frame: build, layout, input, draw, retain.
         * @param input This frame's raw input.
         */
        void renderFrame(const UiInputState& input);

        /** @brief The geometry produced by the last frame. */
        [[nodiscard]] const UiDrawData& drawData() const { return frame_.drawData(); }

        /** @brief The cursor shape the platform should show. */
        [[nodiscard]] StudioCursor cursor() const { return frame_.cursor(); }

        /** @brief The region geometry resolved by the last frame. */
        [[nodiscard]] const StudioShellLayout& layout() const { return layout_; }

        // --- State and introspection -----------------------------------------------------------------

        /** @brief Index of the open menu, or -1 when none is. */
        [[nodiscard]] int openMenu() const { return openMenu_; }

        /**
         * @brief Opens a menu, or closes whatever is open.
         *
         * Exposed so a screenshot test can capture an open menu, and so `--shell-preview` can show
         * one. Behaviour is identical to the user having clicked the title.
         *
         * @param index Menu to open, or -1 to close.
         */
        void setOpenMenu(int index);

        /** @brief Index of the keyboard-highlighted row in the open menu, or -1. */
        [[nodiscard]] int highlightedMenuEntry() const { return highlightedEntry_; }

        /** @brief Whether a menu is open and therefore blocking the panels beneath it. */
        [[nodiscard]] bool isMenuOpen() const { return openMenu_ >= 0; }

        /**
         * @brief The action ids invoked during the last frame, in order.
         *
         * The shell records rather than reports through a callback, because a frame can invoke
         * more than one action and because a test wants to assert on the sequence.
         */
        [[nodiscard]] const std::vector<std::string>& invokedActions() const { return invoked_; }

        /** @brief The action ids whose invocation the registry refused, and why. */
        [[nodiscard]] const std::vector<std::string>& refusedActions() const { return refused_; }

        // --- Resolved geometry -----------------------------------------------------------------------
        //
        // Exposed because "where is the Save item" is a question with exactly one right answer and
        // several places that need it: a test driving the menu, a screenshot harness, and later a
        // platform accessibility bridge that has to report the same rectangles the pointer hits.
        // Recomputing it anywhere else would be a second source of truth.

        /**
         * @brief Where a menu title sits in the bar.
         * @param index Menu index.
         * @return Its rectangle, or an empty one when the index is out of range.
         */
        [[nodiscard]] UiRect menuTitleBounds(std::size_t index) const;

        /** @brief Where the open menu's list sits, or an empty rectangle when none is open. */
        [[nodiscard]] UiRect menuPopupBounds() const { return menuPopup_; }

        /** @brief Number of rows in the open menu, separators included. */
        [[nodiscard]] std::size_t menuRowCount() const { return menuRows_.size(); }

        /**
         * @brief Where a row of the open menu sits.
         * @param index Row index.
         * @return Its rectangle, or an empty one when the index is out of range.
         */
        [[nodiscard]] UiRect menuRowBounds(std::size_t index) const;

        /**
         * @brief The action a row of the open menu invokes.
         * @param index Row index.
         * @return The action id, @ref kStudioMenuSeparatorId for a rule, or empty when out of range.
         */
        [[nodiscard]] std::string_view menuRowActionId(std::size_t index) const;

        /** @brief Number of toolbar entries, separators included. */
        [[nodiscard]] std::size_t toolbarEntryCount() const { return toolbarEntries_.size(); }

        /**
         * @brief Where a toolbar entry sits.
         * @param index Entry index.
         * @return Its rectangle, or an empty one when the index is out of range.
         */
        [[nodiscard]] UiRect toolbarEntryBounds(std::size_t index) const;

        /**
         * @brief The action a toolbar entry invokes.
         * @param index Entry index.
         * @return The action id, @ref kStudioMenuSeparatorId for a rule, or empty when out of range.
         */
        [[nodiscard]] std::string_view toolbarEntryActionId(std::size_t index) const;

        /** @brief Studio's default menu bar: File, Edit, View, Project, Build, Play, Tools, Window, Help. */
        [[nodiscard]] static std::vector<StudioMenuDefinition> defaultMenus();

        /** @brief Studio's default toolbar. */
        [[nodiscard]] static std::vector<std::string> defaultToolbar();

        /** @brief The input layer a menu popup routes in. Panels sit at layer zero. */
        static constexpr int kMenuLayer = 1;

    private:
        /** @brief Where one menu title sits in the bar. */
        struct MenuTitleGeometry
        {
            UiRect bounds;
        };

        /** @brief One resolved row of an open menu. */
        struct MenuRowGeometry
        {
            UiRect bounds;
            /** @brief Action id, or @ref kStudioMenuSeparatorId. */
            std::string id;
            bool separator = false;
            bool enabled = true;
        };

        /** @brief One resolved toolbar entry. */
        struct ToolbarEntryGeometry
        {
            UiRect bounds;
            std::string id;
            bool separator = false;
        };

        void buildContent();
        void computeLayout(float width, float height);
        [[nodiscard]] float tabStripHeight() const;
        void describe();

        void describeMenuBar();
        void describeMenuPopup();
        void describeToolbar();
        void describeDocks();
        void describeSplitters();
        void describeStatusBar();
        void describeViewportBody(const UiRect& body);

        void handleMenuKeyboard();
        void dispatchShortcuts();

        /** @brief Moves the menu highlight by @p delta rows, skipping separators and disabled rows. */
        void moveHighlight(int delta);

        /** @brief Opens @p index and seeds the highlight, without going through a click. */
        void openMenuAt(int index);

        StudioFrame frame_;
        /**
         * @brief Declared after the frame so it outlives it.
         *
         * The frame holds a borrowed pointer to this atlas, and members are destroyed in reverse
         * declaration order -- so the atlas must be declared *after* the frame to be destroyed
         * before it, which is the order that keeps the pointer valid for the frame's whole life.
         */
        StudioFontAtlas fonts_;
        StudioActionRegistry actions_;
        StudioDockTree dock_;
        std::vector<std::pair<std::string, StudioPanelContent>> panelContent_;
        std::vector<StudioPanelDescriptor> panels_;

        std::vector<StudioMenuDefinition> menus_;
        std::vector<std::string> toolbar_;

        /** @brief Tab rectangles resolved this frame, by panel id. */
        std::vector<std::pair<std::string, UiRect>> tabBounds_;

        std::string statusLeft_;
        std::string statusRight_;

        StudioShellLayout layout_;
        std::vector<MenuTitleGeometry> menuTitles_;
        std::vector<MenuRowGeometry> menuRows_;
        std::vector<ToolbarEntryGeometry> toolbarEntries_;
        UiRect menuPopup_;

        int openMenu_ = -1;
        int highlightedEntry_ = -1;
        bool textInputActive_ = false;
        /** @brief True once the open menu has acted on a key this frame; nothing else may. */
        bool keyboardConsumed_ = false;

        std::vector<std::string> invoked_;
        std::vector<std::string> refused_;
    };
} // namespace CNA::Studio
