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
#include "CNA/Studio/UiCore/StudioDialog.hpp"
#include "CNA/Studio/UiCore/StudioDockTree.hpp"
#include "CNA/Studio/UiCore/StudioFontAtlas.hpp"
#include "CNA/Studio/UiCore/StudioFrame.hpp"
#include "CNA/Studio/UiCore/StudioShellLayout.hpp"
#include "CNA/Studio/UiCore/StudioWorkspaceStore.hpp"
#include "CNA/Studio/UiCore/StudioTheme.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace CNA::Studio
{
    /** @brief The id an entry uses to mean "a separator here", rather than an action. */
    inline constexpr std::string_view kStudioMenuSeparatorId = "-";

    /** @brief The label of the Window submenu the shell fills in with its registered panels. */
    inline constexpr std::string_view kStudioPanelMenuLabel = "Panels";

    /** @brief The id prefix of the show/hide command the shell registers for each panel. */
    inline constexpr std::string_view kStudioPanelActionPrefix = "studio.window.panel.";

    /** @brief The id prefix of the close command the shell registers for each panel. */
    inline constexpr std::string_view kStudioClosePanelActionPrefix = "studio.window.closePanel.";

    /** @brief The id prefix of the undock command the shell registers for each panel. */
    inline constexpr std::string_view kStudioFloatPanelActionPrefix = "studio.window.floatPanel.";

    /** @brief The id of the command that docks every floating window again. */
    inline constexpr std::string_view kStudioDockAllActionId = "studio.window.dockAll";

    /** @brief The label of the Layouts submenu the shell fills in with the saved arrangements. */
    inline constexpr std::string_view kStudioLayoutMenuLabel = "Layouts";

    /** @brief The id prefix of the command that applies one saved layout. */
    inline constexpr std::string_view kStudioApplyLayoutActionPrefix = "studio.window.layout.";

    /** @brief The id prefix of the command that deletes one saved layout. */
    inline constexpr std::string_view kStudioDeleteLayoutActionPrefix = "studio.window.deleteLayout.";

    /** @brief The id of the command that saves the current arrangement under a name. */
    inline constexpr std::string_view kStudioSaveLayoutAsActionId = "studio.window.saveLayoutAs";

    /** @brief One piece of background work the status bar reports on. */
    struct StudioStatusJob
    {
        /** @brief What is running, e.g. `"Building Linux x86_64 Debug"`. */
        std::string label;

        /**
         * @brief How far along, in `[0, 1]`, or negative when the length is not known.
         *
         * Negative is a real answer rather than a missing one: a player that has been launched is
         * running for as long as the user plays, and a bar pretending to know how long that is
         * would be lying in the one place the editor reports facts.
         */
        float progress = -1.0f;

        /** @brief A command that stops it, or empty. Drawn as a button when the command exists. */
        std::string stopActionId;
    };

    /**
     * @brief What the status bar says.
     *
     * Fields rather than two strings, because each part answers a different question and a caller
     * composing them into one line would decide the layout: what is open, whether it is saved,
     * what is running, what this project ships on, and what Studio itself is drawing on. The shell
     * lays them out, which is what keeps a build's progress bar in the same place whatever the
     * project is called.
     */
    struct StudioStatusModel
    {
        /** @brief The left-hand text: what the user is working on. */
        std::string message;

        /**
         * @brief Something wrong, shown in place of @ref message until it is cleared.
         *
         * Separate because @ref message is rebuilt from the document every poll, and a reason a
         * project would not open would be overwritten by the next frame -- leaving "No project
         * open", which is true and useless. Sticky, because the user may not have been looking.
         */
        std::string problem;

        /** @brief True when the document has unsaved changes. */
        bool modified = false;

        /** @brief The active build target, e.g. `"Linux x86_64 - opengles3 - Debug"`. */
        std::string target;

        /** @brief What Studio itself is drawing on, e.g. `"opengles3 on sdl3"`. */
        std::string renderer;

        /** @brief Background work. The first is the one shown. */
        std::vector<StudioStatusJob> jobs;
    };

    /**
     * @brief What the shell asks of whoever owns the layout file.
     *
     * A seam rather than a direct call, for the same reason the clipboard is one: the shell is
     * CNA-free and path-free, and where a workspace lives is the application's question. Unset
     * means "nothing is persisted", which a preview wants and which still lets the Layouts menu
     * work for the run it is in — a shell that refused to arrange itself because nobody gave it a
     * file would be worse than one that forgets on exit.
     */
    struct StudioWorkspaceServices
    {
        /** @brief Persists @p layout under @p name. Receives the reason on failure. */
        std::function<bool(const std::string& name, const JsonValue& layout,
                           std::string* problem)> saveNamed;

        /** @brief Removes the layout saved under @p name. Receives the reason on failure. */
        std::function<bool(const std::string& name, std::string* problem)> removeNamed;
    };

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

    /**
     * @brief One row of a menu: an action, a separator, or a submenu.
     *
     * Ids rather than labels for an action row: everything the row shows is read from the registry
     * when it is drawn, so a menu cannot fall out of step with the command it invokes. A submenu
     * is the one row that carries its own label, because it names a *grouping* rather than a
     * command and there is no registry entry to read it from.
     */
    struct StudioMenuEntry
    {
        StudioMenuEntry() = default;

        // Implicit on purpose. A menu is written as a list of action ids, and spelling every one
        // of them `StudioMenuEntry{"..."}` would cost every menu in the application its
        // readability to buy nothing at all.
        StudioMenuEntry(const char* actionId) : id(actionId) {}     // NOLINT(*-explicit-*)
        StudioMenuEntry(std::string actionId) : id(std::move(actionId)) {} // NOLINT(*-explicit-*)
        StudioMenuEntry(std::string_view actionId) : id(actionId) {} // NOLINT(*-explicit-*)

        /**
         * @brief A submenu row.
         * @param label Its title, e.g. `"Recent Projects"`.
         * @param rows  What it opens.
         */
        static StudioMenuEntry submenu(std::string label, std::vector<StudioMenuEntry> rows)
        {
            StudioMenuEntry entry;
            entry.label = std::move(label);
            entry.rows = std::move(rows);
            return entry;
        }

        /** @brief Action id, or @ref kStudioMenuSeparatorId. Empty for a submenu. */
        std::string id;
        /** @brief A submenu's own title. Empty for an action row, which reads its label from the registry. */
        std::string label;
        /** @brief What a submenu opens. */
        std::vector<StudioMenuEntry> rows;

        /** @brief True for a rule rather than a row. */
        [[nodiscard]] bool isSeparator() const { return id == kStudioMenuSeparatorId; }

        /**
         * @brief True for a row that opens a submenu.
         *
         * The *label* marks it, not the row list: a submenu that happens to be empty is still a
         * submenu, and drawing it as an action row named "" would be the worse answer.
         */
        [[nodiscard]] bool isSubmenu() const { return !label.empty(); }
    };

    /** @brief One menu in the application menu bar. */
    struct StudioMenuDefinition
    {
        /** @brief Title shown in the bar. */
        std::string title;
        /** @brief Its rows in order. */
        std::vector<StudioMenuEntry> entries;
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

        /** @brief The toolbar's action ids in order, with @ref kStudioMenuSeparatorId for a rule. */
        [[nodiscard]] const std::vector<std::string>& toolbar() const { return toolbar_; }

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

        /**
         * @brief Whether a panel has content bound.
         *
         * Exposed so a test can ask "does every panel draw something", which is the question a
         * grey rectangle with a tab on it cannot be distinguished from a panel whose content
         * failed by any other means.
         *
         * @param id Panel id.
         * @return True when something was bound and is callable.
         */
        [[nodiscard]] bool hasPanelContent(std::string_view id) const;

        /**
         * @brief Describes one panel's content into @p frame at @p bounds.
         *
         * The panel on its own, without the shell around it: a capture of one panel at full size,
         * a preview of what a panel would show, and the empty-state guard all want exactly this,
         * and each would otherwise have had to drive a whole shell and crop.
         *
         * The panel's own id scope and clip are pushed, as they are when the shell draws it, so a
         * panel described this way behaves identically to one in a dock.
         *
         * @param id Panel to describe.
         * @param frame The frame; must be in a describe pass.
         * @param bounds Where the content goes.
         * @return False when no panel of that id has content.
         */
        bool describePanelContent(std::string_view id, StudioFrame& frame, const UiRect& bounds);

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
         * @brief Where a dragged panel would land if it were dropped now.
         *
         * Five outcomes rather than one, because "put this panel somewhere" and "put this panel
         * *beside* that one" are different intentions and a docking model that only offered the
         * first would make every rearrangement a two-step operation.
         */
        enum class StudioDropZone : std::uint8_t
        {
            /** @brief Nothing under the pointer, or nothing being dragged. */
            None,
            /** @brief Into the target's tab group. */
            Tabs,
            Left,
            Right,
            Top,
            Bottom,
            /**
             * @brief Out of the dock tree entirely, into a floating window.
             *
             * Reached by releasing where no dock leaf is — over the menu bar, the status bar, or
             * another float. Undocking by dragging *somewhere else* rather than by a menu command
             * is what makes it discoverable: a user who has already dragged a tab across the
             * workspace has learned the gesture without being told it.
             */
            Float
        };

        /** @brief A drag in progress, and where it would land. */
        struct StudioDockDrag
        {
            /** @brief The panel being dragged. Empty when nothing is. */
            std::string panelId;

            /** @brief The leaf under the pointer, or @ref kInvalidDockNode. */
            StudioDockNodeId target = kInvalidDockNode;

            /** @brief What would happen on release. */
            StudioDropZone zone = StudioDropZone::None;

            /** @brief Where in the target's tab order, for @ref StudioDropZone::Tabs. */
            std::size_t tabIndex = 0;

            /**
             * @brief The float under the pointer, or @ref kInvalidFloatingDock.
             *
             * Set for @ref StudioDropZone::Tabs when the target is a float rather than a leaf, so
             * one drag can drop a panel into a floating window's tab strip as easily as into a
             * docked one — a float that could never gain a second tab would make every undock a
             * separate window.
             */
            std::size_t targetFloat = kInvalidFloatingDock;

            /** @brief The region the preview highlights. */
            UiRect preview;

            /** @brief Whether a drag is in progress. */
            [[nodiscard]] bool active() const { return !panelId.empty(); }
        };

        /** @brief The drag in progress, if any. */
        [[nodiscard]] const StudioDockDrag& dockDrag() const { return drag_; }

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
         * @brief Hands the shell the scene, already rendered, to composite into the viewport panel.
         *
         * The shell is CNA-free and cannot render a scene; whoever owns the device does it and
         * passes the result in. Set it every frame the scene is drawn, and clear it with
         * @ref kUiTextureNone when there is nothing to show — the viewport then draws the
         * placeholder grid, which is what a build with no device has always shown.
         *
         * @param texture The rendered scene, or @ref kUiTextureNone for none.
         * @param flipVertically Whether this renderer presents a sampled render target bottom-up
         *        (CNA gap G-03). The shell does not and must not know which renderer it is on.
         */
        void setViewportImage(UiTextureId texture, bool flipVertically = false)
        {
            viewportImage_ = texture;
            viewportImageFlipped_ = flipVertically;
        }

        /** @brief The scene texture the viewport is compositing, or @ref kUiTextureNone. */
        [[nodiscard]] UiTextureId viewportImage() const { return viewportImage_; }
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

        /**
         * @brief Sets the left-hand status bar text: what the user is working on.
         *
         * A convenience over @ref status, because "which project is open" and "why it would not
         * open" are the two things every host has to say and both go in the same place.
         */
        void setStatusLeft(std::string text) { status_.message = std::move(text); }

        /** @brief Replaces the whole status model. */
        void setStatus(StudioStatusModel status) { status_ = std::move(status); }

        /** @brief The status model, for a caller changing one field. */
        [[nodiscard]] StudioStatusModel& status() { return status_; }

        /** @brief What the status bar is saying. */
        [[nodiscard]] const StudioStatusModel& status() const { return status_; }

        /**
         * @brief Sets what the About dialog says.
         *
         * Supplied by whoever assembles Studio rather than compiled in here: the version, the
         * renderer and the platform are facts about a *build*, and a shell that carried its own
         * copy would be a second place they could be wrong. Ships with what the UI core itself
         * can honestly say, so About is never an empty box.
         *
         * @param lines One line each, in the order they are shown.
         */
        void setAboutLines(std::vector<std::string> lines) { aboutLines_ = std::move(lines); }

        /** @brief What the About dialog says. */
        [[nodiscard]] const std::vector<std::string>& aboutLines() const { return aboutLines_; }

        /** @brief What the status bar says on the left: what the user is working on. */
        [[nodiscard]] const std::string& statusLeft() const { return status_.message; }

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

        /**
         * @brief Index of the highlighted row in the *deepest* open popup, or -1.
         *
         * The deepest, because that is the one the arrow keys move and Enter chooses from. With no
         * submenu open it is the open menu's own highlight, which is what it has always meant.
         */
        [[nodiscard]] int highlightedMenuEntry() const;

        /**
         * @brief Index of the highlighted row in one open popup.
         * @param level 0 for the menu itself, 1 for its open submenu, and so on.
         * @return The row index, or -1 when nothing is highlighted or the level is not open.
         */
        [[nodiscard]] int highlightedMenuEntry(std::size_t level) const;

        /**
         * @brief Opens the submenu on row @p row of @p level, closing any deeper one.
         * @param level Popup holding the row.
         * @param row   Row index, or -1 to close whatever @p level had open.
         * @return True when the path changed.
         */
        bool openSubmenu(std::size_t level, int row);

        /**
         * @brief Which row of a popup has its submenu open.
         * @param level Popup index.
         * @return The row index, or -1.
         */
        [[nodiscard]] int openSubmenuRow(std::size_t level) const;

        /** @brief How long a submenu takes to switch when the pointer crosses a sibling row, in seconds. */
        void setSubmenuSwitchDelay(float seconds) { submenuSwitchDelay_ = std::max(0.0f, seconds); }

        /** @brief The submenu switch delay in seconds. */
        [[nodiscard]] float submenuSwitchDelay() const { return submenuSwitchDelay_; }

        /** @brief Whether a menu-bar menu is open. */
        [[nodiscard]] bool isMenuOpen() const { return openMenu_ >= 0; }

        /**
         * @brief Opens a context menu at a point.
         *
         * The same rows, the same submenus and the same keyboard traversal as a menu-bar menu:
         * a context menu differs only in where its first popup is anchored. Anything else would
         * be a second menu implementation, and the two would drift.
         *
         * @param rows What it offers.
         * @param x    Where the pointer was, in logical units.
         * @param y    Where the pointer was, in logical units.
         */
        void openContextMenu(std::vector<StudioMenuEntry> rows, float x, float y);

        /** @brief Whether a context menu is open. */
        [[nodiscard]] bool isContextMenuOpen() const { return contextOpen_; }

        /** @brief The rows the open context menu was given. */
        [[nodiscard]] const std::vector<StudioMenuEntry>& contextMenuRows() const
        {
            return contextRows_;
        }

        /** @brief Closes whichever popup chain is open, menu-bar or context. */
        void closePopup();

        // --- Modal dialogs (STUDIO-03040) ------------------------------------------------------

        /**
         * @brief Opens a modal dialog, replacing any other.
         *
         * The shell owns the one dialog because a modal is the application's state, not a panel's:
         * whichever command opened it, what it blocks is the whole window.
         *
         * @param request What the dialog says and offers.
         */
        void openDialog(StudioDialogRequest request);

        /** @brief Closes the dialog without answering it. */
        void closeDialog();

        /** @brief Whether a dialog is open. */
        [[nodiscard]] bool isDialogOpen() const { return dialogOpen_; }

        /** @brief What the open dialog says, or an empty request when none is. */
        [[nodiscard]] const StudioDialogRequest& dialog() const { return dialog_; }

        /**
         * @brief What the user did to the dialog on the last frame.
         *
         * Read after `renderFrame`. Whoever opened the dialog decides what an answer means — the
         * shell only carries it — so this is a report rather than a callback the shell would have
         * to own the lifetime of.
         */
        [[nodiscard]] const StudioDialogResult& dialogResult() const { return dialogResult_; }

        /**
         * @brief Where the open dialog is, for a test or a capture. Empty when none is.
         */
        [[nodiscard]] UiRect dialogBounds() const;

        // --- Saved layouts (STUDIO-05010) ------------------------------------------------------

        /**
         * @brief Hands the shell the arrangements the user has saved.
         *
         * Name *and* document, so applying one is a call rather than a round trip through the file
         * — and so a layout applied is exactly the one the menu named, whatever has happened to
         * the file since.
         *
         * @param layouts The saved arrangements. Reordered by name.
         */
        void setSavedLayouts(std::vector<StudioNamedLayout> layouts);

        /** @brief The saved arrangements, by name. */
        [[nodiscard]] const std::vector<StudioNamedLayout>& savedLayouts() const
        {
            return savedLayouts_;
        }

        /** @brief Sets the seam through which saved layouts reach disk. */
        void setWorkspaceServices(StudioWorkspaceServices services)
        {
            workspace_ = std::move(services);
        }

        /**
         * @brief Applies the layout saved under @p name.
         * @param name The saved layout.
         * @return Whether one of that name was applied.
         */
        bool applySavedLayout(std::string_view name);

        /**
         * @brief Saves the current arrangement under @p name, replacing one of the same name.
         * @param name What to call it.
         * @return Whether it was saved.
         */
        bool saveLayoutAs(std::string_view name);

        /**
         * @brief Removes the layout saved under @p name.
         * @param name The saved layout.
         * @return Whether one was removed.
         */
        bool deleteSavedLayout(std::string_view name);

        /**
         * @brief The id of the command that applies the layout saved under @p name.
         * @param name The saved layout.
         * @return `studio.window.layout.<name>`.
         */
        [[nodiscard]] static std::string applyLayoutActionId(std::string_view name);

        /**
         * @brief The id of the command that deletes the layout saved under @p name.
         * @param name The saved layout.
         * @return `studio.window.deleteLayout.<name>`.
         */
        [[nodiscard]] static std::string deleteLayoutActionId(std::string_view name);

        /** @brief Whether any popup chain is open, and therefore blocking the panels beneath it. */
        [[nodiscard]] bool isPopupOpen() const { return openMenu_ >= 0 || contextOpen_; }

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

        /**
         * @brief How many popups are open.
         *
         * 0 with no menu open, 1 for a menu with no submenu showing, and one more for each open
         * submenu.
         */
        [[nodiscard]] std::size_t menuLevelCount() const { return menuLevels_.size(); }

        /** @brief Where the open menu's list sits, or an empty rectangle when none is open. */
        [[nodiscard]] UiRect menuPopupBounds() const { return menuPopupBounds(0); }

        /**
         * @brief Where one open popup sits.
         * @param level 0 for the menu itself, 1 for its open submenu, and so on.
         * @return Its rectangle, or an empty one when that level is not open.
         */
        [[nodiscard]] UiRect menuPopupBounds(std::size_t level) const;

        /** @brief Number of rows in the open menu, separators included. */
        [[nodiscard]] std::size_t menuRowCount() const { return menuRowCount(0); }

        /**
         * @brief Number of rows in one open popup, separators included.
         * @param level Popup index.
         */
        [[nodiscard]] std::size_t menuRowCount(std::size_t level) const;

        /**
         * @brief Where a row of the open menu sits.
         * @param index Row index.
         * @return Its rectangle, or an empty one when the index is out of range.
         */
        [[nodiscard]] UiRect menuRowBounds(std::size_t index) const { return menuRowBounds(0, index); }

        /**
         * @brief Where a row of one open popup sits.
         * @param level Popup index.
         * @param index Row index.
         * @return Its rectangle, or an empty one when either index is out of range.
         */
        [[nodiscard]] UiRect menuRowBounds(std::size_t level, std::size_t index) const;

        /**
         * @brief The action a row of the open menu invokes.
         * @param index Row index.
         * @return The action id, @ref kStudioMenuSeparatorId for a rule, or empty when out of range.
         */
        [[nodiscard]] std::string_view menuRowActionId(std::size_t index) const
        {
            return menuRowActionId(0, index);
        }

        /**
         * @brief The action a row of one open popup invokes.
         * @param level Popup index.
         * @param index Row index.
         * @return The action id, @ref kStudioMenuSeparatorId for a rule, the submenu's own label
         *         for a submenu row, or empty when either index is out of range.
         */
        [[nodiscard]] std::string_view menuRowActionId(std::size_t level, std::size_t index) const;

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

        /**
         * @brief The show/hide command id the shell registers for a panel.
         * @param panelId The panel's stable id.
         * @return `"studio.window.panel."` followed by @p panelId.
         */
        [[nodiscard]] static std::string panelActionId(std::string_view panelId);

        /**
         * @brief The close command id the shell registers for a panel.
         * @param panelId The panel's stable id.
         * @return `"studio.window.closePanel."` followed by @p panelId.
         */
        [[nodiscard]] static std::string closePanelActionId(std::string_view panelId);

        /**
         * @brief The id of the command that undocks @p panelId into a floating window.
         * @param panelId The panel.
         * @return `studio.window.floatPanel.<id>`.
         */
        [[nodiscard]] static std::string floatPanelActionId(std::string_view panelId);

        /**
         * @brief Undocks a panel into a floating window of its own.
         *
         * Placed a little in from the workspace's top-left rather than under the pointer, because
         * this is the menu path rather than the drag: there is no pointer position that means
         * anything, and a window that appeared under the menu the user just used would cover it.
         *
         * @param id Panel to undock. Must be open.
         * @return True when it was open and is now floating.
         */
        bool floatPanel(std::string_view id);

        /**
         * @brief Docks every floating window back into the workspace.
         *
         * The way out of an arrangement that has got away from the user — a window dragged almost
         * off the screen, or half a dozen of them over each other. Recovery has to exist and has to
         * be one command, or the answer becomes "reset the layout", which costs them everything
         * else they arranged.
         *
         * @return How many windows were docked.
         */
        std::size_t dockAllFloating();

        /**
         * @brief The rows a panel tab's context menu offers.
         * @param panelId The panel whose tab was right-clicked.
         * @return Close, a rule, and the same panel list the Window menu carries.
         */
        [[nodiscard]] std::vector<StudioMenuEntry> tabContextMenu(std::string_view panelId) const;

        /** @brief Studio's default menu bar: File, Edit, View, Project, Build, Play, Tools, Window, Help. */
        [[nodiscard]] static std::vector<StudioMenuDefinition> defaultMenus();

        /** @brief Studio's default toolbar. */
        [[nodiscard]] static std::vector<std::string> defaultToolbar();

        /** @brief The input layer a menu popup routes in. Panels sit at layer zero. */
        static constexpr int kMenuLayer = 3;

        /**
         * @brief The layer the dock drop preview draws in.
         *
         * Above the panels it describes and below an open menu: a menu the user opened during a
         * drag is still the thing in front, and a preview drawn over it would obscure the only
         * control that could cancel the gesture.
         */
        static constexpr int kDockPreviewLayer = 3;

        /**
         * @brief The layer a tooltip draws in.
         *
         * Above everything, an open menu included: a tooltip describes whatever the pointer is
         * resting on, and the pointer may be resting on a menu row.
         */
        static constexpr int kTooltipLayer = 5;

        /**
         * @brief The layer floating windows draw and take input in.
         *
         * Between the docked workspace and an open menu. The router's layers are a *modal* stack
         * rather than a z-order — exactly one layer takes input at a time — so the shell raises
         * this one only while the pointer is over a float or a gesture that began on one is still
         * running. A float that blocked the workspace whenever it existed would make the panels
         * under it unusable; one that never blocked would let a button beneath it light up through
         * it, which is worse than either.
         *
         * The whole ordering, shared with the frame's own popups and modals, is written down at
         * @ref StudioFrame::kPopupLayer.
         */
        static constexpr int kFloatingLayer = 1;

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
            /** @brief Action id, @ref kStudioMenuSeparatorId, or empty for a submenu row. */
            std::string id;
            /** @brief A submenu row's own label. Empty for an action row. */
            std::string label;
            bool separator = false;
            bool submenu = false;
            bool enabled = true;
        };

        /** @brief One open popup: the menu itself, or a submenu of the popup above it. */
        struct MenuLevel
        {
            UiRect popup;
            std::vector<MenuRowGeometry> rows;
        };

        /** @brief One resolved toolbar entry. */
        struct ToolbarEntryGeometry
        {
            UiRect bounds;
            std::string id;
            bool separator = false;
        };

        /** @brief Registers (or refreshes) the show/hide command for one panel. */
        void registerPanelAction(const std::string& panelId);

        /** @brief Refills the Window menu's Panels submenu from the registered panels. */
        void rebuildPanelMenu();

        void buildContent();
        void computeLayout(float width, float height);
        [[nodiscard]] float tabStripHeight() const;
        void describe();

        void describeMenuBar();
        void describeMenuPopup();

        /** @brief Where a popup opens relative to what it belongs to. */
        enum class MenuPlacement
        {
            /** @brief Under the menu bar, aligned with a title. */
            Below,
            /** @brief Beside a parent row, which is what a submenu does. */
            Beside,
            /** @brief With its corner on a point, which is what a context menu does. */
            AtPoint
        };

        /**
         * @brief Lays one popup out from its rows.
         * @param entries   The rows to measure.
         * @param anchor    What it opens from: a menu title, a parent row, or a degenerate
         *                  rectangle at the pointer.
         * @param placement How it sits relative to @p anchor.
         * @return Its rectangle and rows.
         */
        [[nodiscard]] MenuLevel layOutMenuLevel(const std::vector<StudioMenuEntry>& entries,
                                                const UiRect& anchor,
                                                MenuPlacement placement) const;

        /** @brief The rows one open level draws, or nullptr when the path no longer resolves. */
        [[nodiscard]] const std::vector<StudioMenuEntry>* entriesForLevel(std::size_t level) const;
        void describeToolbar();
        void describeDocks();
        void describeFloating();
        void describeDialog();

        /**
         * @brief Describes one tab group: its strip, its tabs and the showing panel's content.
         *
         * Shared by docked leaves and floating windows, because they *are* the same thing in two
         * places: the same tabs, the same right-click menu, the same drag-out gesture. Two copies
         * would be two sets of behaviour to keep in step, and the one that drifted would be the
         * float — the one used least and noticed last.
         *
         * @param panels The group's panels, in tab order. Reordered by nothing here.
         * @param activePanel Which tab is showing; updated when another is clicked.
         * @param geometry The group's strip and body.
         * @param inFloat Whether this group is a floating window, which changes what its tab
         *        context menu offers and what a drag out of it means.
         */
        void describePanelGroup(std::vector<std::string>& panels, std::size_t& activePanel,
                                const StudioDockLeafGeometry& geometry, bool inFloat);

        /**
         * @brief One float's title bar and resize grip.
         *
         * Described with its own window rather than after all of them, so a window behind cannot
         * draw its grip over the window in front.
         *
         * @param index The float.
         */
        void describeFloatingHandles(std::size_t index);

        /** @brief Applies the move or resize in progress. Input pass only. */
        void applyFloatingGesture();
        void describeSplitters();
        void describeDockDrag();
        void describeTooltip();
        void resolveDropTarget();
        void applyDrop();
        void describeStatusBar();
        void describeViewportBody(const UiRect& body);

        void handleMenuKeyboard();
        void dispatchShortcuts();

        /** @brief Moves the deepest popup's highlight by @p delta rows, skipping separators and disabled rows. */
        void moveHighlight(int delta);

        /**
         * @brief Applies the pointer resting on a row to the open submenu at that level.
         * @param level Popup holding the row.
         * @param row   The row under the pointer.
         */
        void updateHoverSubmenu(std::size_t level, int row);

        /** @brief Opens the highlighted row's submenu, if it has one. @return True when one opened. */
        bool openHighlightedSubmenu();

        /** @brief Seeds @p level's highlight on its first (or last) choosable row, before it has laid out. */
        void moveHighlightInto(std::size_t level, int delta);

        /** @brief Closes the deepest open submenu. @return True when one closed. */
        bool closeDeepestSubmenu();

        /** @brief Chooses the deepest popup's highlighted row: invokes it, or opens its submenu. */
        void chooseHighlightedRow();

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
        StudioDockDrag drag_;
        std::vector<std::pair<std::string, StudioPanelContent>> panelContent_;
        std::vector<StudioPanelDescriptor> panels_;

        std::vector<StudioMenuDefinition> menus_;
        std::vector<std::string> toolbar_;

        /** @brief Tab rectangles resolved this frame, by panel id. */
        std::vector<std::pair<std::string, UiRect>> tabBounds_;

        /**
         * @brief The float whose title bar is being dragged, or @ref kInvalidFloatingDock.
         *
         * Held across frames because a move is a drag: the index is re-read every frame rather
         * than a pointer being kept, since raising a float renumbers every index above it.
         */
        std::size_t movingFloat_ = kInvalidFloatingDock;

        /** @brief The float's own geometry when the gesture began, so the delta cannot drift. */
        float movingFloatX_ = 0.0f;
        float movingFloatY_ = 0.0f;

        /** @brief The float being resized by its corner, or @ref kInvalidFloatingDock. */
        std::size_t resizingFloat_ = kInvalidFloatingDock;
        float resizingFloatWidth_ = 0.0f;
        float resizingFloatHeight_ = 0.0f;

        /**
         * @brief Whether the gesture in progress began on a floating window.
         *
         * Kept across frames for the same reason a widget keeps the mouse: a drag that has slid
         * off the window it started on is still that window's drag, and letting the blocking layer
         * fall back the moment the pointer leaves would hand the rest of the gesture to whatever
         * happens to be underneath.
         */
        bool floatGesture_ = false;

        /** @brief Which of the shell's own dialogs is open, so its answer can be acted on. */
        enum class PendingDialog : std::uint8_t
        {
            /** @brief None, or one the shell opened for somebody else to answer. */
            None,
            SaveLayoutAs,
            DeleteLayout
        };

        void rebuildLayoutMenu();
        void registerLayoutActions();
        void handleDialogAnswer();

        PendingDialog pending_ = PendingDialog::None;

        /** @brief What the pending dialog is about — a layout name. */
        std::string pendingArgument_;

        std::vector<StudioNamedLayout> savedLayouts_;
        StudioWorkspaceServices workspace_;

        /** @brief The one open dialog, its retained field and what the user last did to it. */
        StudioDialogRequest dialog_;
        StudioDialogState dialogState_;
        StudioDialogResult dialogResult_;
        bool dialogOpen_ = false;

        StudioStatusModel status_;
        std::vector<std::string> aboutLines_;

        StudioShellLayout layout_;
        std::vector<MenuTitleGeometry> menuTitles_;
        std::vector<MenuLevel> menuLevels_;
        std::vector<ToolbarEntryGeometry> toolbarEntries_;

        UiTextureId viewportImage_ = kUiTextureNone;
        bool viewportImageFlipped_ = false;

        int openMenu_ = -1;

        /**
         * @brief A context menu's rows, owned rather than referenced.
         *
         * Whoever opens one builds it from what was right-clicked, and that selection can change
         * -- or be deleted -- while the menu is up. Copying is what keeps the popup describing
         * what the user asked about rather than following the ground out from under itself.
         */
        std::vector<StudioMenuEntry> contextRows_;
        UiRect contextAnchor_;
        bool contextOpen_ = false;

        /**
         * @brief Which row of each popup has its submenu open, deepest last.
         *
         * A path rather than a pointer into the tree: menus are rebuilt from `menus_` every frame,
         * so anything holding a node would be a dangling reference the first time a menu changed
         * while it was open.
         */
        std::vector<int> submenuPath_;

        /** @brief The highlighted row of each open popup. Always `submenuPath_.size() + 1` long while a menu is open. */
        std::vector<int> highlight_;

        /** @brief The row the pointer is waiting on before the open submenu switches to it. */
        int submenuPendingRow_ = -1;
        std::size_t submenuPendingLevel_ = 0;
        float submenuPendingSeconds_ = 0.0f;
        float submenuSwitchDelay_ = 0.25f;
        bool textInputActive_ = false;
        /** @brief True once the open menu has acted on a key this frame; nothing else may. */
        bool keyboardConsumed_ = false;

        std::vector<std::string> invoked_;
        std::vector<std::string> refused_;
    };
} // namespace CNA::Studio
