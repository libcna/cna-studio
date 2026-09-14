// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Viewport/CnaEditorHost.hpp"

#include <algorithm>
#include <exception>
#include <vector>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GameWindow.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include "CNA/Editor/Ui/ImGuiEditorUi.hpp"
#include "CNA/Editor/Viewport/CnaSceneRenderer.hpp"
#include "CNA/Editor/Viewport/EditorAudio.hpp"
#include "CNA/Editor/Viewport/CnaUiPlatform.hpp"
#include "CNA/Editor/Viewport/CnaUiRenderer.hpp"

namespace Xna = Microsoft::Xna::Framework;
namespace XnaGraphics = Microsoft::Xna::Framework::Graphics;

namespace CNA::Editor
{
    namespace
    {
    /**
     * @brief The CNA Game that owns the editor's window, device and frame loop.
     *
     * Kept entirely inside this translation unit: see the header for why the public interface is a
     * free function rather than this class.
     */
    class CnaEditorHost final : public Xna::Game
    {
    public:
        CnaEditorHost(const CnaEditorHostOptions& options, std::unique_ptr<EditorApplication> application);
        ~CnaEditorHost() override;

        CnaEditorHost(const CnaEditorHost&) = delete;
        CnaEditorHost& operator=(const CnaEditorHost&) = delete;

        [[nodiscard]] std::uint64_t getFrameCount() const;

        /** @brief Returns the totals accumulated across every frame drawn. */
        [[nodiscard]] const UiRenderStats& getTotalStats() const;

        /** @brief Returns the drawing area the most recent frame used, in pixels. */
        [[nodiscard]] float getLastDisplayWidth() const;
        [[nodiscard]] float getLastDisplayHeight() const;

        /** @brief Returns true when a screenshot was requested and written. */
        [[nodiscard]] bool wasScreenshotWritten() const;

    protected:
        void Initialize() override;
        void LoadContent() override;
        void Update(Xna::GameTime& gameTime) override;
        void Draw(const Xna::GameTime& gameTime) override;

    private:
        void captureScreenshotIfRequested();

        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

    struct CnaEditorHost::Impl
    {
        CnaEditorHostOptions options;
        std::unique_ptr<EditorApplication> application;
        std::unique_ptr<Xna::GraphicsDeviceManager> graphics;
        std::unique_ptr<CnaUiPlatform> platform;
        std::unique_ptr<CnaUiRenderer> renderer;

        /** @brief The application's UI, downcast once at construction rather than every frame. */
        ImGuiEditorUi* ui = nullptr;

        UiRenderStats lastStats;
        UiRenderStats totalStats;
        float lastDisplayWidth = 0.0f;
        float lastDisplayHeight = 0.0f;
        bool screenshotWritten = false;
        std::uint64_t frameCount = 0;
        bool contentLoaded = false;
    };

    CnaEditorHost::CnaEditorHost(const CnaEditorHostOptions& options,
                                 std::unique_ptr<EditorApplication> application)
        : impl_(std::make_unique<Impl>())
    {
        impl_->options = options;
        impl_->application = std::move(application);
        impl_->ui = dynamic_cast<ImGuiEditorUi*>(&impl_->application->getUi());

        impl_->graphics = std::make_unique<Xna::GraphicsDeviceManager>(this);
        impl_->graphics->setPreferredBackBufferWidthProperty(options.windowWidth);
        impl_->graphics->setPreferredBackBufferHeightProperty(options.windowHeight);

        getWindowProperty().setTitleProperty(options.windowTitle);
        getWindowProperty().setAllowUserResizingProperty(true);

        // The editor draws its own cursor for gizmos and resize handles later on, but until then
        // the OS cursor is what the user aims with.
        setIsMouseVisibleProperty(true);
    }

    CnaEditorHost::~CnaEditorHost()
    {
        // Order matters. The renderer holds Texture2D objects created on the device, so it must
        // release them before the device goes; and the UI must stop existing before the renderer,
        // since the renderer's texture map is keyed on ids the UI hands out.
        if (impl_->ui != nullptr) { impl_->ui->saveLayout(); }
        if (impl_->renderer != nullptr) { impl_->renderer->shutdown(); }
        impl_->application.reset();
        impl_->renderer.reset();
        impl_->platform.reset();
    }

    std::uint64_t CnaEditorHost::getFrameCount() const { return impl_->frameCount; }

    const UiRenderStats& CnaEditorHost::getTotalStats() const { return impl_->totalStats; }

    float CnaEditorHost::getLastDisplayWidth() const { return impl_->lastDisplayWidth; }

    float CnaEditorHost::getLastDisplayHeight() const { return impl_->lastDisplayHeight; }

    bool CnaEditorHost::wasScreenshotWritten() const { return impl_->screenshotWritten; }

    void CnaEditorHost::Initialize()
    {
        // Constructed here rather than in the constructor: CnaUiPlatform subscribes to
        // TextInputEXT, and CNA's input layer is only meaningfully alive once the game has been
        // initialised.
        impl_->platform = std::make_unique<CnaUiPlatform>();

        if (impl_->ui != nullptr)
        {
            UiClipboardHooks hooks;
            if (CnaUiPlatform::hasClipboard())
            {
                hooks.getText = &CnaUiPlatform::getClipboardText;
                hooks.setText = &CnaUiPlatform::setClipboardText;
            }
            impl_->ui->setClipboardHooks(hooks);

            if (!impl_->options.layoutPath.empty()) { impl_->ui->loadLayout(impl_->options.layoutPath); }
        }

        Game::Initialize();
    }

    void CnaEditorHost::LoadContent()
    {
        impl_->renderer = std::make_unique<CnaUiRenderer>();
        impl_->renderer->initialize(getGraphicsDeviceProperty());

        // The scene viewport can only exist once there is a device and a UI renderer to share its
        // render target through, so it is installed here rather than at construction. Replacing
        // the application's viewport in place keeps every panel written against the abstraction.
        impl_->application->setViewport(
            createCnaEditorViewport(getGraphicsDeviceProperty(),
                                    impl_->application->getContext().getAssets(),
                                    impl_->application->getContext().getComponentRegistry(),
                                    *impl_->renderer));

        // Installed here for the same reason the viewport is: it needs the asset database, which
        // only exists once a project is open.
        impl_->application->setAudio(
            createCnaEditorAudio(impl_->application->getContext().getAssets()));

        impl_->contentLoaded = true;

        impl_->application->getContext().log(
            LogSeverity::Info,
            "Editor window ready on the " + CnaUiRenderer::getBackendName() + " backend");

        Game::LoadContent();
    }

    void CnaEditorHost::Update(Xna::GameTime& gameTime)
    {
        Game::Update(gameTime);

        if (impl_->ui == nullptr || impl_->platform == nullptr) { return; }

        // The device's viewport, not the window's client bounds. The viewport is the surface
        // actually being drawn into, which is the authoritative size for a renderer: on a scaled
        // or letterboxed presentation the two differ, and on a backend that renders without a
        // window at all (SOFTWARE, HEADLESS) the window reports 0x0 while the back buffer is
        // perfectly real. Window bounds remain the fallback for the reverse case.
        const XnaGraphics::Viewport& viewport = getGraphicsDeviceProperty().getViewportProperty();
        float displayWidth = static_cast<float>(viewport.getWidthProperty());
        float displayHeight = static_cast<float>(viewport.getHeightProperty());

        if (displayWidth <= 0.0f || displayHeight <= 0.0f)
        {
            const Xna::Rectangle bounds = getWindowProperty().getClientBoundsProperty();
            displayWidth = static_cast<float>(bounds.Width);
            displayHeight = static_cast<float>(bounds.Height);
        }

        const auto deltaSeconds =
            static_cast<float>(gameTime.getElapsedGameTimeProperty().getTotalSecondsProperty());

        UiInputState input = impl_->platform->poll(displayWidth, displayHeight, deltaSeconds);
        impl_->lastDisplayWidth = input.displayWidth;
        impl_->lastDisplayHeight = input.displayHeight;
        impl_->ui->setInput(input);

        if (!impl_->ui->beginFrame())
        {
            // The UI reports exit when the user chooses File > Exit or the window closes.
            Exit();
            return;
        }

        impl_->application->renderFrame(deltaSeconds);
        impl_->ui->endFrame();
        ++impl_->frameCount;

        // Textures are uploaded here, in the update phase, because Dear ImGui marks a request
        // satisfied the moment it is issued and under a fixed-timestep loop many Update frames run
        // without a matching Draw. Uploading in Draw would silently lose every texture produced by
        // such a frame. See CnaUiRenderer::applyTextureRequests.
        if (impl_->renderer != nullptr)
        {
            const UiRenderStats textureStats = impl_->renderer->applyTextureRequests(impl_->ui->getDrawData());

            impl_->totalStats.texturesCreated += textureStats.texturesCreated;
            impl_->totalStats.texturesUpdated += textureStats.texturesUpdated;
            impl_->totalStats.texturesDestroyed += textureStats.texturesDestroyed;
        }

        // Text input is started only while a field has focus: leaving it on would keep a mobile
        // on-screen keyboard up for the whole session, and on desktop it needlessly routes every
        // keystroke through the IME.
        impl_->platform->setTextInputActive(impl_->ui->wantsTextInput());

        if (impl_->options.frameLimit > 0
            && impl_->frameCount >= static_cast<std::uint64_t>(impl_->options.frameLimit))
        {
            // Hold the exit until a requested screenshot has been taken. Exit() stops the loop
            // before the next Draw, so exiting the moment the frame count is reached would mean
            // the capture -- which can only run in Draw, where the back buffer exists -- never
            // happens at all.
            const bool waitingForScreenshot =
                !impl_->options.screenshotPath.empty() && !impl_->screenshotWritten;
            if (!waitingForScreenshot) { Exit(); }
        }
    }

    void CnaEditorHost::Draw(const Xna::GameTime& gameTime)
    {
        // A dark neutral background rather than XNA's CornflowerBlue: the dock space covers the
        // whole window anyway, and the blue would only ever be visible for one frame at start-up.
        getGraphicsDeviceProperty().Clear(Xna::Color(30, 30, 32, 255));

        if (impl_->contentLoaded && impl_->ui != nullptr && impl_->renderer != nullptr)
        {
            impl_->lastStats = impl_->renderer->renderGeometry(impl_->ui->getDrawData());

            impl_->totalStats.drawCalls += impl_->lastStats.drawCalls;
            impl_->totalStats.triangles += impl_->lastStats.triangles;
            impl_->totalStats.clippedAway += impl_->lastStats.clippedAway;
        }

        captureScreenshotIfRequested();

        Game::Draw(gameTime);
    }

    void CnaEditorHost::captureScreenshotIfRequested()
    {
        if (impl_->options.screenshotPath.empty() || impl_->screenshotWritten) { return; }

        // Only once the frame budget is spent. Capturing earlier would catch the editor
        // mid-warm-up, before the font atlas exists and before the dock layout has settled.
        if (impl_->options.frameLimit <= 0
            || impl_->frameCount < static_cast<std::uint64_t>(impl_->options.frameLimit))
        {
            return;
        }

        XnaGraphics::GraphicsDevice& device = getGraphicsDeviceProperty();
        const XnaGraphics::Viewport& viewport = device.getViewportProperty();
        const int width = viewport.getWidthProperty();
        const int height = viewport.getHeightProperty();
        if (width <= 0 || height <= 0) { return; }

        const auto pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        // Color has no default constructor (docs/SPIKE-IMGUI-CNA.md gap G-01), so the buffer is
        // filled rather than merely sized.
        std::vector<Xna::Color> pixels(pixelCount, Xna::Color(0, 0, 0, 255));

        try
        {
            device.GetBackBufferData(pixels.data(), static_cast<int>(pixelCount));

            XnaGraphics::Texture2D capture{device, width, height};
            capture.SetData(pixels.data(), static_cast<int>(pixelCount));
            capture.SaveAsPng(impl_->options.screenshotPath);
            impl_->screenshotWritten = true;

            impl_->application->getContext().log(
                LogSeverity::Info, "Wrote screenshot to " + impl_->options.screenshotPath);
        }
        catch (const std::exception& exception)
        {
            // A backend that cannot read its back buffer is a limitation, not a crash: the editor
            // has already drawn the frame, and losing the capture must not lose the session.
            impl_->application->getContext().log(
                LogSeverity::Warning,
                std::string{"Screenshot failed: "} + exception.what());
            impl_->screenshotWritten = true;
        }
    }
    }  // namespace

    CnaEditorHostResult runEditorInWindow(const CnaEditorHostOptions& options,
                                          std::unique_ptr<EditorApplication> application)
    {
        CnaEditorHostResult result;
        result.backend = CnaUiRenderer::getBackendName();

        if (!application)
        {
            result.exitCode = 1;
            result.errorMessage = "no application supplied";
            return result;
        }

        // Checked here rather than asserted deep in the frame loop: a UI with no geometry to
        // render would open a window and leave it blank, which is a far worse way to learn about
        // the mistake than a message.
        if (dynamic_cast<ImGuiEditorUi*>(&application->getUi()) == nullptr)
        {
            result.exitCode = 1;
            result.errorMessage =
                "runEditorInWindow needs an ImGuiEditorUi; this application has a '"
                + std::string{application->getUi().getBackendName()} + "' UI, which produces no "
                "geometry for the renderer to draw";
            application->getContext().log(LogSeverity::Error, result.errorMessage);
            return result;
        }

        CnaEditorHost host{options, std::move(application)};
        host.Run();

        result.frames = host.getFrameCount();
        result.drawCalls = host.getTotalStats().drawCalls;
        result.triangles = host.getTotalStats().triangles;
        result.textures = host.getTotalStats().texturesCreated;
        result.textureUpdates = host.getTotalStats().texturesUpdated;
        result.clippedAway = host.getTotalStats().clippedAway;
        result.displayWidth = host.getLastDisplayWidth();
        result.displayHeight = host.getLastDisplayHeight();
        result.screenshotWritten = host.wasScreenshotWritten();
        return result;
    }

    std::string getHostBackendName() { return CnaUiRenderer::getBackendName(); }
}
