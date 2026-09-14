// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Viewport/CnaPlayerHost.hpp"

#include <exception>
#include <memory>
#include <vector>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GameWindow.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include "CNA/Editor/Scene/GameCamera.hpp"
#include "CNA/Editor/Viewport/CnaSceneRenderer.hpp"
#include "CNA/Editor/Viewport/CnaUiRenderer.hpp"

namespace Xna = Microsoft::Xna::Framework;
namespace XnaGraphics = Microsoft::Xna::Framework::Graphics;

namespace CNA::Editor
{
    namespace
    {
        /** @brief The CNA Game that owns the player's window, device and frame loop. */
        class CnaPlayerGame final : public Xna::Game
        {
        public:
            CnaPlayerGame(const CnaPlayerHostOptions& options,
                          PlayerHost& host,
                          PlayerFrameHook frameHook,
                          PlayerMessageSink sink)
                : options_(options), host_(host), frameHook_(std::move(frameHook)), sink_(std::move(sink))
            {
                graphics_ = std::make_unique<Xna::GraphicsDeviceManager>(this);
                graphics_->setPreferredBackBufferWidthProperty(options.windowWidth);
                graphics_->setPreferredBackBufferHeightProperty(options.windowHeight);

                getWindowProperty().setTitleProperty(options.windowTitle);
                getWindowProperty().setAllowUserResizingProperty(true);

                // A game shows the OS cursor unless it draws its own; this one draws none.
                setIsMouseVisibleProperty(true);
            }

            ~CnaPlayerGame() override
            {
                // The renderer holds textures created on the device, so it has to let go of them
                // before the device does.
                renderer_.shutdown();
            }

            CnaPlayerGame(const CnaPlayerGame&) = delete;
            CnaPlayerGame& operator=(const CnaPlayerGame&) = delete;

            [[nodiscard]] std::uint64_t getFrameCount() const { return frameCount_; }
            [[nodiscard]] const SceneRenderStats& getLastStats() const { return lastStats_; }
            [[nodiscard]] bool wasScreenshotWritten() const { return screenshotWritten_; }

        protected:
            void LoadContent() override
            {
                renderer_.initialize(getGraphicsDeviceProperty(), host_.getAssets(),
                                     host_.getComponentRegistry());
                contentLoaded_ = true;
                Game::LoadContent();
            }

            void Update(Xna::GameTime& gameTime) override
            {
                Game::Update(gameTime);

                // Before the socket is pumped, so an input message arriving this frame is mapped
                // into the size the window actually is rather than into whatever it was last
                // frame. The device is the only thing that knows, and only this half has it.
                const XnaGraphics::Viewport& surface =
                    getGraphicsDeviceProperty().getViewportProperty();
                host_.setSurfaceSize(surface.getWidthProperty(), surface.getHeightProperty());

                // The caller's socket first: a message that arrived this frame should affect this
                // frame, not the next one. A paused player still pumps, or Resume would never be
                // heard by the process it was sent to.
                if (frameHook_ && !frameHook_()) { Exit(); return; }
                if (host_.shouldExit()) { Exit(); return; }

                // An asset the editor changed. Dropping the texture is all that is needed: the next
                // frame that draws a sprite using it loads it again from disk.
                for (const Uuid& assetId : host_.takeReloadedAssets())
                {
                    renderer_.invalidateTexture(assetId);
                }

                // tick() decides whether the simulation advances; drawing happens either way,
                // because a paused game still shows the frame it was paused on.
                host_.tick();
            }

            void Draw(const Xna::GameTime& gameTime) override
            {
                XnaGraphics::GraphicsDevice& device = getGraphicsDeviceProperty();
                const XnaGraphics::Viewport& viewport = device.getViewportProperty();
                const int width = viewport.getWidthProperty();
                const int height = viewport.getHeightProperty();

                // The scene's own camera decides both the view and the clear colour -- this is the
                // game's picture, so nothing here is the editor's choice.
                const GameView view = computeGameView(
                    host_.getScene(),
                    EditorVector2{static_cast<float>(width), static_cast<float>(height)});

                device.Clear(Xna::Color(static_cast<int>(view.clearColor.r),
                                        static_cast<int>(view.clearColor.g),
                                        static_cast<int>(view.clearColor.b),
                                        static_cast<int>(view.clearColor.a)));

                if (contentLoaded_)
                {
                    lastStats_ = renderer_.renderGameView(host_.getScene(), view.camera, width, height);
                }

                ++frameCount_;

                // After drawing, never before: a capture taken ahead of the frame it is meant to
                // record would show the previous one.
                serveScreenshotRequests();
                captureFinalScreenshotIfRequested();

                Game::Draw(gameTime);

                if (options_.frameLimit > 0
                    && frameCount_ >= static_cast<std::uint64_t>(options_.frameLimit))
                {
                    Exit();
                }
            }

        private:
            /** @brief Answers every capture the editor asked for over the bridge. */
            void serveScreenshotRequests()
            {
                for (const PlayerHost::ScreenshotRequest& request : host_.takeScreenshotRequests())
                {
                    const std::string error = writeScreenshot(request.path);
                    if (sink_) { sink_(PlayerHost::makeScreenshotReply(request, error)); }
                }
            }

            /** @brief Writes the command-line screenshot once the frame budget is spent. */
            void captureFinalScreenshotIfRequested()
            {
                if (options_.screenshotPath.empty() || screenshotAttempted_) { return; }
                if (options_.frameLimit <= 0
                    || frameCount_ < static_cast<std::uint64_t>(options_.frameLimit))
                {
                    return;
                }

                // Attempted and written are two different facts. Marking it attempted either way
                // stops a backend that cannot read its back buffer from retrying on every
                // remaining frame; reporting *written* separately is what lets the caller exit
                // non-zero, which is the whole assertion a smoke test makes.
                const std::string error = writeScreenshot(options_.screenshotPath);
                screenshotAttempted_ = true;
                screenshotWritten_ = error.empty();
            }

            /** @brief Writes the back buffer to @p path. Returns an error, or an empty string. */
            std::string writeScreenshot(const std::string& path)
            {
                XnaGraphics::GraphicsDevice& device = getGraphicsDeviceProperty();
                const XnaGraphics::Viewport& viewport = device.getViewportProperty();
                const int width = viewport.getWidthProperty();
                const int height = viewport.getHeightProperty();
                if (width <= 0 || height <= 0) { return "the back buffer has no size to capture"; }

                const auto pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

                // Color has no default constructor (docs/SPIKE-IMGUI-CNA.md gap G-01), so the
                // buffer is filled rather than merely sized.
                std::vector<Xna::Color> pixels(pixelCount, Xna::Color(0, 0, 0, 255));

                try
                {
                    device.GetBackBufferData(pixels.data(), static_cast<int>(pixelCount));

                    XnaGraphics::Texture2D capture{device, width, height};
                    capture.SetData(pixels.data(), static_cast<int>(pixelCount));
                    capture.SaveAsPng(path);
                    return {};
                }
                catch (const std::exception& exception)
                {
                    // A backend that cannot read its back buffer is a limitation, not a crash: the
                    // frame was drawn, and losing the capture must not lose the session.
                    return exception.what();
                }
            }

            CnaPlayerHostOptions options_;
            PlayerHost& host_;
            PlayerFrameHook frameHook_;
            PlayerMessageSink sink_;

            std::unique_ptr<Xna::GraphicsDeviceManager> graphics_;
            CnaSceneRenderer renderer_;
            SceneRenderStats lastStats_;

            std::uint64_t frameCount_ = 0;
            bool contentLoaded_ = false;
            bool screenshotAttempted_ = false;
            bool screenshotWritten_ = false;
        };
    }

    CnaPlayerHostResult runPlayerInWindow(const CnaPlayerHostOptions& options,
                                          PlayerHost& host,
                                          const PlayerFrameHook& frameHook,
                                          const PlayerMessageSink& sink)
    {
        CnaPlayerHostResult result;
        result.backend = CnaUiRenderer::getBackendName();

        try
        {
            CnaPlayerGame game{options, host, frameHook, sink};
            game.Run();

            result.frames = game.getFrameCount();
            result.spritesDrawn = game.getLastStats().spritesDrawn;
            result.tilesDrawn = game.getLastStats().tilesDrawn;
            result.screenshotWritten = game.wasScreenshotWritten();
        }
        catch (const std::exception& exception)
        {
            // No window, no device, no display -- a build machine, a headless container, an X
            // server that is not there. Reported rather than thrown, so the caller can fall back
            // to running the game with nothing drawn: the protocol half needs no graphics at all,
            // and a player that died here would take the editor's whole play session with it.
            result.exitCode = 5;
            result.errorMessage = exception.what();
        }
        return result;
    }
}
