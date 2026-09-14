// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Ui/ImGuiEditorUi.hpp
 * @brief The Dear ImGui implementation of EditorUi.
 *
 * This is the *only* file in the editor that knows Dear ImGui exists (ANALYSIS.md decision D-02).
 * It takes a UiInputState in and produces a UiDrawData out, so it depends on neither a windowing
 * library nor a graphics API -- which means it builds and runs with no CNA checkout, and its
 * output can be asserted on in CI with no window and no GPU.
 *
 * Pairing it with `CnaUiPlatform` (input) and `CnaUiRenderer` (output) is what turns it into a
 * real editor window; both live in `cna-editor-viewport` because both need CNA.
 */

#include <memory>
#include <string>
#include <vector>

#include "CNA/Editor/Ui/EditorUi.hpp"
#include "CNA/Editor/Ui/UiDrawData.hpp"
#include "CNA/Editor/Ui/UiInputState.hpp"

namespace CNA::Editor
{
    /** @brief How ImGuiEditorUi should obtain and release clipboard text. */
    struct UiClipboardHooks
    {
        /** @brief Returns the clipboard's text. Empty when unavailable. */
        std::string (*getText)() = nullptr;

        /** @brief Sets the clipboard's text. */
        void (*setText)(const std::string& text) = nullptr;
    };

    /**
     * @brief Draws the editor with Dear ImGui, into a toolkit-independent UiDrawData.
     *
     * Usage per frame:
     * @code
     * ui.setInput(platform.pollInput());
     * if (!ui.beginFrame()) { break; }
     * application.renderFrame();
     * ui.endFrame();
     * renderer.render(ui.getDrawData());
     * @endcode
     */
    class ImGuiEditorUi final : public EditorUi
    {
    public:
        ImGuiEditorUi();
        ~ImGuiEditorUi() override;

        ImGuiEditorUi(const ImGuiEditorUi&) = delete;
        ImGuiEditorUi& operator=(const ImGuiEditorUi&) = delete;

        [[nodiscard]] const char* getBackendName() const override { return "imgui"; }
        [[nodiscard]] bool isRunning() const override;

        bool beginFrame() override;
        void endFrame() override;

        void beginDockSpace() override;
        void endDockSpace() override;

        void requestPanelFocus(const std::string& title) override;

        bool beginPanel(const std::string& title, DockSide preferredSide) override;
        void endPanel() override;

        void text(const std::string& value) override;
        bool button(const std::string& label) override;
        bool checkbox(const std::string& label, bool& value) override;

        bool propertyField(const std::string& label,
                           PropertyValue& value,
                           const std::vector<std::string>& enumOptions = {},
                           bool readOnly = false) override;

        UiTreeNodeResult treeNode(const Uuid& id,
                                  const std::string& label,
                                  bool selected,
                                  bool leaf) override;
        UiTreeNodeResult treeNode(const std::string& id,
                                  const std::string& label,
                                  bool selected,
                                  bool leaf) override;
        void treePop() override;

        UiTextFieldResult inputText(const std::string& id, std::string& text, bool takeFocus = false) override;

        [[nodiscard]] UiKeyModifiers getModifiers() const override;

        void setDragSource(const std::string& type,
                           const std::string& payload,
                           const std::string& label) override;
        [[nodiscard]] std::optional<std::string> acceptDrop(const std::string& type) override;

        bool beginContextMenu(const std::string& id) override;
        void endContextMenu() override;

        bool beginMenu(const std::string& label) override;
        void endMenu() override;
        bool menuItem(const std::string& label, const std::string& shortcut = {}, bool enabled = true) override;

        [[nodiscard]] bool isKeyDown(UiKey key) const override;
        [[nodiscard]] bool isShortcutPressed(UiKey key, UiKeyModifiers modifiers = {}) override;

        void setNextItemWidth(float width) override;

        void separator() override;
        void sameLine() override;
        void drawLogView(const UiLogViewOptions& options = {}) override;
        [[nodiscard]] std::string getLogText(LogSeverity minimumSeverity = LogSeverity::Trace) const override;
        void clearLog() override;
        void setClipboardText(const std::string& text) override;

        [[nodiscard]] UiRegion getContentRegion() const override;
        void imageRegion(const std::string& id,
                         UiTextureId texture,
                         const EditorRectangle& source,
                         const EditorVector2& sourceSize,
                         float width,
                         float height) override;

        UiImageInteraction image(const std::string& id,
                                 UiTextureId texture,
                                 float width,
                                 float height,
                                 bool flipVertically = false) override;

        void log(LogSeverity severity, const std::string& message) override;

        /** @brief Supplies the input for the next frame. Call before beginFrame(). */
        void setInput(const UiInputState& input);

        /**
         * @brief Returns the geometry produced by the most recent endFrame().
         *
         * Valid until the next endFrame(). Texture request pixel pointers are owned by ImGui and
         * are only valid for this frame -- the renderer must copy, not retain.
         *
         * Texture ids are allocated here rather than by the renderer, so a request always arrives
         * with its id already set and the renderer never has to report one back. See the comment
         * on capturePendingTextures for why any other arrangement deadlocks against ImGui's own
         * assertions -- and for the obligation that puts on the caller: these requests must be
         * consumed on every frame that produces draw data, not only on frames that get drawn.
         */
        [[nodiscard]] const UiDrawData& getDrawData() const;

        /** @brief Installs clipboard hooks. Without them, copy and paste do nothing. */
        void setClipboardHooks(const UiClipboardHooks& hooks);

        /** @brief Asks the editor to exit after the current frame. */
        [[nodiscard]] bool isAnyItemActive() const override;

        void requestExit() override;

        /**
         * @brief Returns true when a text field has keyboard focus.
         *
         * The platform layer starts and stops OS text input from this. Leaving text input on
         * permanently would keep a mobile on-screen keyboard up for the whole session, and on
         * desktop it needlessly routes every keystroke through the IME.
         */
        [[nodiscard]] bool wantsTextInput() const;

        /** @brief Returns true when the UI, rather than the game viewport, wants the mouse. */
        [[nodiscard]] bool wantsMouseCapture() const;

        /** @brief Returns the console messages accumulated so far. */
        [[nodiscard]] const std::vector<std::pair<LogSeverity, std::string>>& getLog() const;

        /**
         * @brief Loads ImGui's `.ini` layout from @p path, if it exists.
         *
         * Called before the first frame. The dock layout is *not* stored in the project: where a
         * user puts their panels is a property of the user, not of the game they are editing.
         */
        void loadLayout(const std::string& path);

        /** @brief Writes the current dock layout to the path given to loadLayout(). */
        void saveLayout() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
