// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Ui/EditorUi.hpp
 * @brief The toolkit-independent UI surface every panel is written against.
 *
 * No panel, command or plugin may call Dear ImGui -- or Qt, or anything else -- directly. They
 * call this interface, and exactly one implementation binds it to a real toolkit (ANALYSIS.md
 * decision D-02). The cost is one indirection per widget, which is irrelevant at editor frame
 * rates. The benefit is threefold:
 *
 * 1. the toolkit choice stays reversible after the editor has real panels in it;
 * 2. the whole panel layer is unit-testable against NullEditorUi, with no window and no GPU;
 * 3. a plugin compiled against this header keeps working across a toolkit change.
 *
 * The interface is deliberately immediate-mode, because that is the cheaper direction to adapt:
 * wrapping a retained toolkit (Qt) in an immediate-mode facade is routine, while the reverse
 * requires inventing state the immediate-mode toolkit never had.
 */

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "CNA/Editor/Core/EditorMath.hpp"
#include "CNA/Editor/Core/PropertyValue.hpp"
#include "CNA/Editor/Core/Uuid.hpp"
#include "CNA/Editor/Ui/UiDrawData.hpp"
#include "CNA/Editor/Ui/UiInputState.hpp"

namespace CNA::Editor
{
    /** @brief Severity of a console message, used for filtering and colouring. */
    enum class LogSeverity
    {
        Trace,
        Info,
        Warning,
        Error
    };

    /** @brief Returns the short display name of @p severity, e.g. "warn". */
    const char* toString(LogSeverity severity);

    /** @brief Where a panel is docked by default. The implementation may ignore this. */
    enum class DockSide
    {
        Left,
        Right,
        Bottom,
        Center
    };

    /** @brief A rectangle in panel-local or screen coordinates, in pixels. */
    struct UiRegion
    {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;

        [[nodiscard]] bool isEmpty() const { return width <= 0.0f || height <= 0.0f; }
    };

    /**
     * @brief Everything the viewport panel needs to know about one frame of pointer interaction.
     *
     * Returned as one struct from a single call rather than as a scatter of queries, because the
     * viewport needs all of it together and each piece is only meaningful relative to the others:
     * a wheel delta matters only while hovered, a drag delta only while dragging, and a click only
     * when it lands on the image rather than on the panel around it.
     */
    struct UiImageInteraction
    {
        bool hovered = false;

        /** @brief Set on the frame a left click completes over the image, without a drag. */
        bool clicked = false;

        /** @brief Set on the frame the left button goes down over the image. */
        bool leftPressed = false;

        /**
         * @brief True while the left button is held after a press that began over the image.
         *
         * Deliberately independent of @c hovered: a gizmo drag that wanders off the panel must
         * keep going, and must end only when the button is actually released. Tying it to hover
         * would drop the entity wherever the cursor happened to cross the panel edge.
         */
        bool leftDown = false;

        /** @brief Set on the frame such a press is released, wherever the cursor then is. */
        bool leftReleased = false;

        /** @brief Cursor position relative to the image's top-left, in pixels. */
        float localMouseX = 0.0f;
        float localMouseY = 0.0f;

        /** @brief Wheel movement this frame, in notches. Non-zero only while hovered. */
        float wheel = 0.0f;

        /** @brief True while a pan drag is in progress. */
        bool dragging = false;

        /**
         * @brief True while the right or middle button is held over the image.
         *
         * Carried separately from @c dragging, which cannot say *which* button is panning. The 3D
         * viewport needs to know: right-drag turns the camera in place and middle-drag orbits it,
         * and those are different gestures with different results (ED-400). The middle button is
         * carried because a game may want it -- the editor itself only asks about the right one.
         */
        bool rightDown = false;
        bool middleDown = false;

        /** @brief Cursor movement since the previous frame, while dragging. */
        float dragDeltaX = 0.0f;
        float dragDeltaY = 0.0f;

        /**
         * @brief Modifiers held this frame.
         *
         * On the *interaction* rather than read from a keyboard API, because what they mean depends
         * on what the pointer is doing: Ctrl over a gizmo snaps the drag, Ctrl over the scene adds
         * to the selection. A panel that had to ask a global "is Ctrl down" would be reading state
         * that has nothing to do with the press it is handling.
         */
        bool control = false;
        bool shift = false;
    };

    /** @brief How the console panel wants its messages shown. */
    struct UiLogViewOptions
    {
        /** @brief Messages below this severity are hidden. */
        LogSeverity minimumSeverity = LogSeverity::Trace;

        /**
         * @brief Keep the newest message in view.
         *
         * Turned off, the view stays where the user put it. That is the whole point of a
         * scroll-lock: reading an error is impossible if every new frame's logging drags the
         * view away from it.
         */
        bool autoScroll = true;
    };

    /** @brief What happened to a tree node during one frame. */
    struct UiTreeNodeResult
    {
        /** @brief True when the node is expanded and its children should be drawn. */
        bool expanded = false;

        /** @brief True on the frame the node is clicked. */
        bool clicked = false;

        /** @brief True on the frame the node is double-clicked, which starts a rename. */
        bool doubleClicked = false;
    };

    /** @brief What happened to a text field during one frame. */
    struct UiTextFieldResult
    {
        /** @brief True when the text was edited this frame. */
        bool changed = false;

        /**
         * @brief True when the edit ended -- Enter pressed, or focus moved elsewhere.
         *
         * The two are one signal on purpose: clicking away from a rename field is as much a
         * "yes, that is the name" as pressing Enter is, and treating it as a cancel loses work.
         */
        bool committed = false;
    };

    /**
     * @brief The immediate-mode UI surface.
     *
     * Every method that draws a widget returns whether the user interacted with it during this
     * frame, so a panel reads as ordinary control flow. Editing widgets take their value by
     * reference and report a change, which is what lets a panel turn an edit into an
     * EditorCommand at exactly the right moment.
     */
    class EditorUi
    {
    public:
        virtual ~EditorUi() = default;

        /** @brief Returns a short name for the bound toolkit, e.g. "imgui" or "null". */
        [[nodiscard]] virtual const char* getBackendName() const = 0;

        /** @brief Returns false once the user has asked to close the editor. */
        [[nodiscard]] virtual bool isRunning() const = 0;

        /**
         * @brief Returns true while a widget is being dragged or typed into.
         *
         * What the undo stack needs to tell two slider drags apart: they are identical by every
         * property a merge key can see and differ only in that the user let go in between. The UI
         * is the only layer that knows, so it says.
         *
         * Defaulted to false, which means "no interaction is in progress" -- the safe answer for a
         * headless UI, since it makes every edit its own undo entry rather than silently merging
         * edits that should not have been.
         */
        [[nodiscard]] virtual bool isAnyItemActive() const { return false; }

        /**
         * @brief Asks the editor to close after this frame.
         *
         * On the interface rather than only on the implementations, because a *task* can finish --
         * `--compare-backends` runs to a result and then has nothing more to do -- and it has no
         * business knowing which toolkit is bound. Defaulted to nothing so an implementation that
         * cannot be closed from inside is still a valid one.
         */
        virtual void requestExit() {}

        /** @brief Begins a frame. Returns false when the application should exit. */
        virtual bool beginFrame() = 0;

        /** @brief Ends and presents the frame. */
        virtual void endFrame() = 0;

        /** @brief Establishes the dock space the panels attach to. */
        virtual void beginDockSpace() = 0;
        virtual void endDockSpace() = 0;

        /**
         * @brief Begins a dockable panel window.
         * @return False when the panel is collapsed or hidden; the caller must then skip its
         *         contents but must still call endPanel().
         */
        virtual bool beginPanel(const std::string& title, DockSide preferredSide) = 0;
        virtual void endPanel() = 0;

        /** @brief Draws a non-interactive line of text. */
        virtual void text(const std::string& value) = 0;

        /** @brief Draws a button; returns true on the frame it is clicked. */
        virtual bool button(const std::string& label) = 0;

        /** @brief Draws a checkbox; returns true when @p value changed this frame. */
        virtual bool checkbox(const std::string& label, bool& value) = 0;

        /**
         * @brief Draws the right editing widget for @p value, chosen from its PropertyType.
         *
         * This is the one call the inspector makes per property, which is what keeps the inspector
         * generic over component types it was never compiled against.
         *
         * @param label Field label.
         * @param value Edited in place.
         * @param enumOptions Allowed names when the value is an enum; ignored otherwise.
         * @param readOnly When true the widget is shown but cannot be changed.
         * @return True when the user changed the value this frame.
         */
        virtual bool propertyField(const std::string& label,
                                   PropertyValue& value,
                                   const std::vector<std::string>& enumOptions = {},
                                   bool readOnly = false) = 0;

        /**
         * @brief Draws one node of a tree.
         * @param id Stable identity for the node, so expansion state survives reordering.
         * @param label Displayed text.
         * @param selected Whether the node is currently selected.
         * @param leaf When true the node draws no expander.
         */
        virtual UiTreeNodeResult treeNode(const Uuid& id,
                                          const std::string& label,
                                          bool selected,
                                          bool leaf) = 0;

        /**
         * @brief Draws a tree node identified by a string, for rows that have no Uuid.
         *
         * A folder in the asset browser is the case: folders are derived from the tracked paths
         * and have no identity of their own, so the path itself is what makes one row distinct
         * from another.
         *
         * The default expands everything that is not a leaf, which is what a headless run wants:
         * the whole tree is walked, so a crash in a deeply nested branch is caught by CI rather
         * than by the first user to expand it.
         */
        virtual UiTreeNodeResult treeNode(const std::string& id,
                                          const std::string& label,
                                          bool selected,
                                          bool leaf)
        {
            (void)id;
            (void)label;
            (void)selected;

            UiTreeNodeResult result;
            result.expanded = !leaf;
            return result;
        }

        virtual void treePop() = 0;

        /**
         * @brief Draws a single-line text field.
         *
         * @param id Stable identity for the field.
         * @param text Edited in place.
         * @param takeFocus Ask for keyboard focus, for a field that has only just appeared.
         */
        virtual UiTextFieldResult inputText(const std::string& id, std::string& text, bool takeFocus = false)
        {
            (void)id;
            (void)text;
            (void)takeFocus;
            return UiTextFieldResult{};
        }

        /** @brief Returns the modifier keys held this frame. */
        [[nodiscard]] virtual UiKeyModifiers getModifiers() const { return UiKeyModifiers{}; }

        /**
         * @brief Offers the widget just drawn as a drag source carrying @p payload.
         *
         * @param type A short key naming what is being dragged, e.g. "entity" or "asset". A drop
         *        target accepts one type, so a scene entity cannot be dropped onto a texture slot.
         * @param payload The dragged value, as text. A Uuid in every current use.
         * @param label What to show under the cursor while dragging.
         */
        virtual void setDragSource(const std::string& type,
                                   const std::string& payload,
                                   const std::string& label)
        {
            (void)type;
            (void)payload;
            (void)label;
        }

        /**
         * @brief Accepts a drop of @p type onto the widget just drawn.
         * @return The payload on the frame a drop completes, otherwise std::nullopt.
         */
        [[nodiscard]] virtual std::optional<std::string> acceptDrop(const std::string& type)
        {
            (void)type;
            return std::nullopt;
        }

        /**
         * @brief Opens a right-click context menu attached to the widget just drawn.
         * @return False when the menu is closed; the caller must then skip its items but must
         *         still not call endContextMenu().
         */
        virtual bool beginContextMenu(const std::string& id) { (void)id; return false; }
        virtual void endContextMenu() {}

        /** @brief Begins a menu in the main menu bar. */
        virtual bool beginMenu(const std::string& label) = 0;
        virtual void endMenu() = 0;

        /**
         * @brief Draws a menu item.
         * @param shortcut Display-only accelerator text, e.g. "Ctrl+Z".
         * @param enabled When false the item is greyed out and cannot be chosen.
         * @return True on the frame it is chosen.
         */
        virtual bool menuItem(const std::string& label,
                              const std::string& shortcut = {},
                              bool enabled = true) = 0;

        /**
         * @brief Returns the space left in the current panel, in panel-local pixels.
         *
         * The viewport asks for this so it can render its scene at exactly the panel's size --
         * rendering at a fixed size and stretching would make the grid non-square and picking
         * disagree with what is on screen.
         */
        [[nodiscard]] virtual UiRegion getContentRegion() const { return UiRegion{}; }

        /**
         * @brief Draws @p texture filling @p width by @p height, and reports interaction with it.
         *
         * @param id Stable identity for the widget.
         * @param texture Renderer-side texture id, from the scene renderer.
         */
        /**
         * @param flipVertically Sample the texture bottom-up. Needed for render targets on
         *        backends whose textures originate at the bottom-left; see
         *        EditorViewport::isRenderTextureFlippedVertically().
         */
        virtual UiImageInteraction image(const std::string& id,
                                         UiTextureId texture,
                                         float width,
                                         float height,
                                         bool flipVertically = false)
        {
            (void)id;
            (void)texture;
            (void)width;
            (void)height;
            (void)flipVertically;
            return UiImageInteraction{};
        }

        /**
         * @brief Draws one sub-rectangle of @p texture, scaled to @p width by @p height.
         *
         * For a sheet: one animation frame, one tile. Given in *texels* rather than in normalised
         * coordinates, because everything on this side of the boundary -- the animation clip, the
         * tilemap, the importer's recorded pixel size -- already speaks texels, and converting in
         * one place is one place to get it wrong rather than several.
         *
         * @param sourceSize The texture's full size in texels. Zero in either axis draws nothing:
         *        without it there is no way to turn a texel rectangle into UVs, and guessing would
         *        show a frame from somewhere other than where the user pointed.
         */
        virtual void imageRegion(const std::string& id,
                                 UiTextureId texture,
                                 const EditorRectangle& source,
                                 const EditorVector2& sourceSize,
                                 float width,
                                 float height)
        {
            (void)id;
            (void)texture;
            (void)source;
            (void)sourceSize;
            (void)width;
            (void)height;
        }

        /**
         * @brief Returns true on the frame @p key goes down with exactly @p modifiers held.
         *
         * "Exactly" rather than "at least": Ctrl+Shift+Z is redo in most editors, and a shortcut
         * layer that ignored the extra Shift would fire the undo bound to Ctrl+Z as well.
         *
         * An implementation must report false while a text field has the keyboard, or the editor
         * becomes unusable the first time someone renames an entity -- W would switch gizmo mode
         * instead of typing a letter, and Delete would remove the entity being renamed.
         */
        /**
         * @brief Returns true while @p key is held, ignoring modifiers.
         *
         * Distinct from isShortcutPressed(), which fires once on the press. Flying a camera needs
         * the held state -- a fly control driven by press events moves in single steps -- and
         * nothing else in the editor does, which is why this arrived with ED-400 rather than
         * with the shortcut system.
         */
        [[nodiscard]] virtual bool isKeyDown(UiKey key) const
        {
            (void)key;
            return false;
        }

        [[nodiscard]] virtual bool isShortcutPressed(UiKey key, UiKeyModifiers modifiers = {})
        {
            (void)key;
            (void)modifiers;
            return false;
        }

        /**
         * @brief Constrains the next widget to @p width pixels.
         *
         * A hint, not a guarantee: an implementation that has no say over widget width may ignore
         * it. Needed because the default for an editing widget is "fill the line", which is right
         * in an inspector column and wrong in a toolbar, where it pushes everything after it off
         * to the far edge.
         */
        virtual void setNextItemWidth(float width) { (void)width; }

        /** @brief Draws a horizontal rule. */
        /**
         * @brief Brings the panel titled @p title to the front of whatever tab bar it is docked in.
         *
         * Applies on the *next* frame the panel draws and then stops -- a request, not a state. A
         * toolkit that cannot do this ignores it, which costs a caller nothing: everything that
         * asks is asking so that a screenshot shows a particular panel, and a screenshot of the
         * wrong tab is a wrong picture rather than a broken editor.
         */
        virtual void requestPanelFocus(const std::string& title) { (void)title; }

        virtual void separator() = 0;

        /** @brief Places the next widget on the same line as the previous one. */
        virtual void sameLine() = 0;

        /** @brief Appends a message to the console panel. */
        virtual void log(LogSeverity severity, const std::string& message) = 0;

        /**
         * @brief Draws the accumulated console messages.
         *
         * Called by the console panel. The messages live in the implementation because the scroll
         * position and severity colouring are toolkit state; the panel decides *that* a console
         * exists and what it should show, and passes the latter in.
         */
        virtual void drawLogView(const UiLogViewOptions& options = {}) { (void)options; }

        /**
         * @brief Returns the console's messages as one block of text, newest last.
         *
         * Used by the console's Copy button. Filtered the same way the view is, so what lands on
         * the clipboard is what the user can see -- copying hidden messages would be a surprise.
         */
        [[nodiscard]] virtual std::string getLogText(LogSeverity minimumSeverity = LogSeverity::Trace) const
        {
            (void)minimumSeverity;
            return {};
        }

        /** @brief Discards every console message. */
        virtual void clearLog() {}

        /** @brief Puts @p text on the system clipboard. */
        virtual void setClipboardText(const std::string& text) { (void)text; }
    };

    /**
     * @brief An EditorUi that draws nothing and interacts with nobody.
     *
     * This is not a placeholder for a missing implementation -- it is a first-class one. It makes
     * the whole panel layer testable in CI with no display, and it is what `cna-editor --headless`
     * runs on, which is how the editor's own scene and asset operations get exercised on a build
     * machine that has no GPU.
     *
     * Every widget reports "no interaction", every panel reports "visible", and log messages are
     * collected in memory where a test can assert on them.
     */
    class NullEditorUi : public EditorUi
    {
    public:
        /** @brief One captured log message. */
        struct LogEntry
        {
            LogSeverity severity = LogSeverity::Info;
            std::string message;
        };

        [[nodiscard]] const char* getBackendName() const override { return "null"; }
        [[nodiscard]] bool isRunning() const override { return running_; }

        bool beginFrame() override;
        void endFrame() override;

        void beginDockSpace() override {}
        void endDockSpace() override {}

        bool beginPanel(const std::string& title, DockSide preferredSide) override;
        void endPanel() override {}

        void text(const std::string& value) override { (void)value; }
        bool button(const std::string& label) override { (void)label; return false; }
        bool checkbox(const std::string& label, bool& value) override { (void)label; (void)value; return false; }

        bool propertyField(const std::string& label,
                           PropertyValue& value,
                           const std::vector<std::string>& enumOptions = {},
                           bool readOnly = false) override;

        // Overriding one overload would hide the other; the string-keyed form is inherited as is,
        // and expands everything that is not a leaf, which is what a headless run wants.
        using EditorUi::treeNode;

        UiTreeNodeResult treeNode(const Uuid& id,
                                  const std::string& label,
                                  bool selected,
                                  bool leaf) override;
        void treePop() override {}

        bool beginMenu(const std::string& label) override { (void)label; return false; }
        void endMenu() override {}
        bool menuItem(const std::string& label, const std::string& shortcut = {}, bool enabled = true) override;

        void separator() override {}
        void sameLine() override {}

        [[nodiscard]] bool isShortcutPressed(UiKey key, UiKeyModifiers modifiers = {}) override;

        /**
         * @brief Arms a shortcut so the next matching isShortcutPressed() reports true.
         *
         * The armed shortcut is consumed by that query rather than cleared at a frame boundary,
         * because tests step the editor by calling renderFrame() directly -- there is no beginFrame()
         * in that path to hang the clearing off, and a shortcut that stayed armed would fire on
         * every subsequent frame.
         */
        void pressShortcut(UiKey key, UiKeyModifiers modifiers = {});

        /**
         * @brief Reports a fixed 1280x720 content region.
         *
         * Non-zero on purpose: a headless run should exercise the viewport's real sizing and
         * rendering path, and a zero region would make every panel silently skip its content --
         * turning the headless smoke test into a no-op exactly where it is most useful.
         */
        [[nodiscard]] UiRegion getContentRegion() const override { return UiRegion{0.0f, 0.0f, 1280.0f, 720.0f}; }

        void log(LogSeverity severity, const std::string& message) override;

        [[nodiscard]] std::string getLogText(LogSeverity minimumSeverity = LogSeverity::Trace) const override;
        void clearLog() override { log_.clear(); }
        void setClipboardText(const std::string& text) override { clipboard_ = text; }

        /** @brief Returns whatever was last copied. Lets a test assert on the Copy button. */
        [[nodiscard]] const std::string& getClipboardText() const { return clipboard_; }

        /** @brief Asks the next beginFrame() to report exit. Used by `--frames N` and by tests. */
        void requestExit() override { running_ = false; }

        /** @brief Returns every message logged so far. */
        [[nodiscard]] const std::vector<LogEntry>& getLog() const { return log_; }

        /** @brief Returns the number of frames begun. */
        [[nodiscard]] std::uint64_t getFrameCount() const { return frameCount_; }

        /** @brief Returns the titles of the panels drawn during the most recent frame. */
        [[nodiscard]] const std::vector<std::string>& getLastFramePanels() const { return lastFramePanels_; }

    private:
        /** @brief A shortcut armed by pressShortcut() and not yet consumed. */
        struct PendingShortcut
        {
            UiKey key = UiKey::None;
            UiKeyModifiers modifiers;
        };

        bool running_ = true;
        std::uint64_t frameCount_ = 0;
        std::vector<PendingShortcut> shortcuts_;
        std::vector<LogEntry> log_;
        std::string clipboard_;
        std::vector<std::string> currentFramePanels_;
        std::vector<std::string> lastFramePanels_;
    };
}
