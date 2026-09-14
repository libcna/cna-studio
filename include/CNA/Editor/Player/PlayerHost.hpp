// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Player/PlayerHost.hpp
 * @brief The player side of the editor bridge: what `cna-player` does with what the editor sends.
 *
 * `cna-player` lives in this repository rather than in CNA (ANALYSIS.md decision D-15). It is
 * built once per graphics backend, because CNA fixes its backend at compile time (finding F-01),
 * and the editor launches whichever build matches the backend the user asked to preview.
 *
 * This class is the *protocol and state* half, and it is deliberately CNA-free: it decides what a
 * message means, tracks the play/pause/step state, and reports back. Actually rendering the scene
 * is the CNA-linked half in `cna-player`'s own main loop. Splitting it this way is what lets the
 * whole message-handling surface be unit-tested headless -- see `tests/PlayerTests.cpp`.
 */

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "CNA/Editor/Assets/AssetDatabase.hpp"
#include "CNA/Editor/Project/Project.hpp"
#include "CNA/Editor/RuntimeBridge/EditorProtocol.hpp"
#include "CNA/Editor/Scene/SceneDocument.hpp"

namespace CNA::Editor
{
    /** @brief Whether the simulation is advancing. */
    enum class PlayState
    {
        /** @brief Advancing normally. */
        Running,
        /** @brief Held; only an explicit StepFrame advances it. */
        Paused,
        /** @brief The editor asked the player to exit. */
        Stopping
    };

    /** @brief Returns the display name of @p state. */
    const char* toString(PlayState state);

    /**
     * @brief Applies editor messages to a running game and reports back.
     *
     * Constructed with a project already loaded. `handle()` is called for each message the channel
     * delivers; anything the player wants to send back is returned rather than sent directly, so
     * that the transport stays injectable and the whole class stays testable with no socket.
     */
    class PlayerHost
    {
    public:
        /** @brief Sink for messages the host wants to send to the editor. */
        using Outbox = std::vector<EditorMessage>;

        /**
         * @brief One capture the editor asked for and the graphics half has still to take.
         *
         * Queued rather than answered on the spot, because only the CNA-linked loop can read a
         * back buffer -- and a `ScreenshotReady` sent before any pixels reached disk would be a lie
         * the editor has no way to detect. The request id travels with it so a reply can be
         * matched to its request even with several in flight.
         */
        struct ScreenshotRequest
        {
            std::uint64_t requestId = 0;
            std::string path;
        };

        PlayerHost();
        ~PlayerHost();

        PlayerHost(const PlayerHost&) = delete;
        PlayerHost& operator=(const PlayerHost&) = delete;

        /**
         * @brief Loads @p projectPath and prepares for messages.
         * @return False when the project cannot be read; getError() says why.
         */
        bool openProject(const std::string& projectPath);

        /**
         * @brief Handles one message from the editor.
         * @param message The received message.
         * @param outbox Replies are appended here.
         */
        void handle(const EditorMessage& message, Outbox& outbox);

        /**
         * @brief Advances one simulated frame if the state allows it.
         *
         * @return True when a frame should actually be drawn. False while paused, which is what
         *         lets the player idle cheaply instead of spinning.
         */
        bool tick();

        /** @brief Builds the Ready message announcing this build's backend and capabilities. */
        [[nodiscard]] EditorMessage makeReady(const std::string& backendName) const;

        [[nodiscard]] PlayState getPlayState() const { return playState_; }
        [[nodiscard]] const Project& getProject() const { return project_; }
        [[nodiscard]] const SceneDocument& getScene() const { return scene_; }

        /**
         * @brief The database and registry the graphics half draws through.
         *
         * Exposed because rendering needs both -- a sprite's texture is resolved by asset id, and a
         * component's declared defaults are the only thing that knows what a field means when the
         * file omits it -- and because the alternative is the graphics half scanning the project a
         * second time and disagreeing with this one about what it found.
         */
        [[nodiscard]] const AssetDatabase& getAssets() const { return assets_; }
        [[nodiscard]] const ComponentRegistry& getComponentRegistry() const { return components_; }
        [[nodiscard]] const std::string& getScenePath() const { return scenePath_; }
        [[nodiscard]] const std::string& getError() const { return error_; }

        /** @brief Returns the number of frames advanced since start-up. */
        [[nodiscard]] std::uint64_t getFrameCount() const { return frameCount_; }

        /** @brief Returns the entity the editor last asked to highlight. */
        [[nodiscard]] const Uuid& getHighlightedEntity() const { return highlightedEntity_; }

        /** @brief Returns true once the editor has asked the player to exit. */
        [[nodiscard]] bool shouldExit() const { return playState_ == PlayState::Stopping; }

        /**
         * @brief Returns the assets the editor has asked to reload, and clears the list.
         *
         * The CNA-linked half of `cna-player` drains this to drop whatever it has cached for those
         * ids. It is a list rather than a flag because the graphics half runs on its own schedule
         * and must not miss a reload that arrived between two of its frames.
         */
        std::vector<Uuid> takeReloadedAssets()
        {
            std::vector<Uuid> taken;
            taken.swap(reloadedAssets_);
            return taken;
        }

        /**
         * @brief Returns the captures the editor has asked for, and clears the list.
         *
         * Drained by the CNA-linked loop, which takes each one and answers it with
         * `makeScreenshotReply`. A build with no graphics drains it too and answers honestly that
         * it cannot capture -- an editor left waiting for a reply that will never come is worse
         * than one told no.
         */
        std::vector<ScreenshotRequest> takeScreenshotRequests()
        {
            std::vector<ScreenshotRequest> taken;
            taken.swap(screenshotRequests_);
            return taken;
        }

        /**
         * @brief Builds the reply to @p request.
         *
         * @param errorMessage Empty when the file was written; otherwise why it was not. Carried in
         *        the message rather than only logged, so the asking side can act on it.
         */
        [[nodiscard]] static EditorMessage makeScreenshotReply(const ScreenshotRequest& request,
                                                               const std::string& errorMessage = {});

        /**
         * @brief Tells the host how big the window it draws into is.
         *
         * Called by the CNA-linked loop, which owns the window; this half has no way to ask. The
         * size is what forwarded input is mapped into -- a game asking where the pointer is means
         * *its* window, not the editor panel the user was pointing at.
         */
        void setSurfaceSize(int width, int height);

        /**
         * @brief Returns the pointer and keyboard as last forwarded by the editor.
         *
         * Already mapped into this player's surface. This is where a game would read them once
         * there is a way to attach game code to an entity -- that question is open (see NEXT.md)
         * and deliberately not answered here. Until it is, the value's purpose is to be reported
         * back, so the editor can show what the game would see.
         */
        [[nodiscard]] const PlayerInputSnapshot& getInput() const { return input_; }

        /** @brief Returns whether a compatible handshake has been received. */
        [[nodiscard]] bool isHandshakeComplete() const { return handshakeComplete_; }

    private:
        void handleLoadScene(const EditorMessage& message, Outbox& outbox);
        void handleSetProperty(const EditorMessage& message, Outbox& outbox);
        void handleReloadAsset(const EditorMessage& message, Outbox& outbox);
        void handleInput(const EditorMessage& message, Outbox& outbox);

        /** @brief The pointer and keyboard the editor last forwarded, in this player's surface. */
        PlayerInputSnapshot input_;

        /** @brief The window's size, as the graphics half last reported it. */
        float surfaceWidth_ = 0.0f;
        float surfaceHeight_ = 0.0f;

        Project project_;
        SceneDocument scene_;
        ComponentRegistry components_;
        AssetDatabase assets_;
        std::string scenePath_;
        std::string error_;
        PlayState playState_ = PlayState::Running;
        Uuid highlightedEntity_;

        /** @brief Assets reloaded since the graphics half last drained the list. */
        std::vector<Uuid> reloadedAssets_;

        /** @brief Captures asked for and not yet taken. */
        std::vector<ScreenshotRequest> screenshotRequests_;
        std::uint64_t frameCount_ = 0;
        /** @brief Frames still owed from StepFrame requests while paused. */
        int pendingSteps_ = 0;
        bool handshakeComplete_ = false;
    };
}
