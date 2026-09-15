// SPDX-License-Identifier: MS-PL
/**
 * @file CnaStudioShellHost.cpp
 * @brief The native Studio shell's window, input and presentation, through CNA's public API.
 */

#include "CNA/Studio/Viewport/CnaStudioShellHost.hpp"

#include <iostream>
#include <memory>
#include <vector>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GameWindow.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include "CNA/Studio/UiCore/StudioShell.hpp"
#include "CNA/Studio/ShellPanels/StudioOutlinerPanel.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/Ui/StudioLog.hpp"
#include "CNA/Studio/UiCore/StudioLogPanel.hpp"
#include "CNA/Studio/UiCore/StudioWorkspaceStore.hpp"
#include "CNA/Studio/UiCore/StudioTheme.hpp"
#include "CNA/Studio/Viewport/CnaCapabilityBridge.hpp"
#include "CNA/Studio/Viewport/CnaUiPlatform.hpp"
#include "CNA/Studio/Viewport/CnaUiRenderer.hpp"

namespace Xna = Microsoft::Xna::Framework;
namespace XnaGraphics = Microsoft::Xna::Framework::Graphics;

namespace CNA::Studio
{
    namespace
    {
        /**
         * @brief A CNA `Game` that draws the native Studio shell and nothing else.
         *
         * It holds a shell, a platform and a renderer, and contains no UI logic of its own. That is
         * the property worth protecting: every behavioural question about the shell is answerable
         * by the headless tests, and this file only has to be right about the window.
         */
        class CnaStudioShellGame final : public Xna::Game
        {
        public:
            explicit CnaStudioShellGame(const CnaStudioShellHostOptions& options)
                : options_(options)
            {
                graphics_ = std::make_unique<Xna::GraphicsDeviceManager>(this);
                graphics_->setPreferredBackBufferWidthProperty(options.windowWidth);
                graphics_->setPreferredBackBufferHeightProperty(options.windowHeight);

                // HiDef, not the Reach default. Reach refuses GetBackBufferData -- CNA gap G-07 --
                // so every screenshot and every golden image would throw, and the graphical smoke
                // test would prove nothing.
                graphics_->setGraphicsProfileProperty(XnaGraphics::GraphicsProfile::HiDef);

                StudioTheme theme = options.theme == "light" ? StudioTheme::light()
                                                             : StudioTheme::dark();
                theme.setScale(options.uiScale);
                shell_ = std::make_unique<StudioShell>(std::move(theme));

                // Restored before the window opens, so the first frame the user sees is already
                // their arrangement rather than the default one rearranging itself.
                if (!options.workspacePath.empty())
                {
                    const StudioWorkspaceStore store{options.workspacePath};
                    const StudioWorkspaceDocument stored = store.load();
                    layoutProblem_ = stored.problem;
                    if (stored.found)
                    {
                        std::string problem;
                        layoutRestored_ = shell_->loadLayout(stored.layout, &problem);
                        if (!problem.empty()) { layoutProblem_ = problem; }
                    }
                }

                // A real editor context, not a demonstration of one. Opened before the window so
                // the first frame already shows the project: a shell that opened empty and then
                // filled in would read as a shell that failed and recovered.
                //
                // The log sink is installed first, so whatever opening the project has to say --
                // an importer fact applied, an asset that would not parse -- lands in the Output
                // Log rather than being lost before anything was listening.
                context_ = std::make_unique<StudioContext>();
                context_->setLogSink([this](LogSeverity severity, const std::string& message) {
                    log_.append(severity, message);
                });

                if (!options.projectPath.empty())
                {
                    if (context_->openProject(options.projectPath))
                    {
                        shell_->setStatusLeft(context_->getProject().getName() + "  --  "
                                              + context_->getScene().getName());
                    }
                    else
                    {
                        // Reported, not fatal. An editor that refused to open because one project
                        // would not load leaves the user with no way to open a different one.
                        log_.append(LogSeverity::Error,
                                    "Could not open '" + options.projectPath + "'.");
                        shell_->setStatusLeft("Could not open " + options.projectPath);
                    }
                }

                // The World Outliner (STUDIO-07006), the second ported panel and the first that
                // reads the document model rather than a log.
                shell_->setPanelContent("outliner",
                    [this](StudioFrame& frame, const UiRect& bounds) {
                        const StudioOutlinerResult outliner =
                            studioOutlinerPanel(frame, bounds, *context_, outlinerState_);
                        if (frame.isDrawPass())
                        {
                            outlinerRowsDrawn_ = outliner.rowsDrawn;
                            outlinerRowsTotal_ = outliner.rowsTotal;
                        }
                        if (outliner.selectionChanged)
                        {
                            const std::vector<Uuid>& selection = context_->getSelection();
                            if (selection.empty())
                            {
                                log_.append(LogSeverity::Trace, "Selection cleared.");
                            }
                            else if (const StudioEntity* entity =
                                         context_->getScene().findEntity(selection.back()))
                            {
                                log_.append(LogSeverity::Trace, "Selected '" + entity->getName()
                                                                + "' (" + std::to_string(selection.size())
                                                                + " selected).");
                            }
                        }
                    });

                // The first ported panel (STUDIO-07005). Drawn by the Studio UI, from a log no UI
                // owns -- which is the whole shape of the strangler migration: the ImGui Console
                // reads the same model and keeps working until it is deleted.
                shell_->setPanelContent("output",
                    [this](StudioFrame& frame, const UiRect& bounds) {
                        const StudioLogPanelResult panelResult = studioLogPanel(frame, bounds, log_);
                        if (frame.isDrawPass())
                        {
                            logRowsDrawn_ = panelResult.rowsDrawn;
                            logRowsMatching_ = panelResult.rowsMatching;
                        }
                        if (panelResult.cleared) { log_.clear(); }
                        if (panelResult.copyRequested)
                        {
                            // CNA gap G-02: the clipboard is behind a default-off CNA option, so
                            // this degrades visibly rather than silently doing nothing.
                            if (CnaUiPlatform::hasClipboard())
                            {
                                CnaUiPlatform::setClipboardText(panelResult.copyText);
                                log_.append(LogSeverity::Info, "Copied the log to the clipboard.");
                            }
                            else
                            {
                                log_.append(LogSeverity::Warning,
                                            "This build has no clipboard: CNA's Devices module is "
                                            "off (CNA gap G-02). Rebuild CNA with CNA_DEVICES=ON.");
                            }
                        }
                    });

                if (!options.focusPanel.empty() && !shell_->activatePanel(options.focusPanel))
                {
                    log_.append(LogSeverity::Warning,
                                "No panel called '" + options.focusPanel + "' is open.");
                }

                setIsMouseVisibleProperty(true);
                getWindowProperty().setTitleProperty(options.windowTitle);
                getWindowProperty().setAllowUserResizingProperty(true);
            }

            [[nodiscard]] std::uint64_t frames() const { return frames_; }
            [[nodiscard]] std::size_t drawCalls() const { return drawCalls_; }
            [[nodiscard]] std::size_t triangles() const { return triangles_; }
            [[nodiscard]] float displayWidth() const { return displayWidth_; }
            [[nodiscard]] float displayHeight() const { return displayHeight_; }
            [[nodiscard]] bool screenshotWritten() const { return screenshotWritten_; }
            [[nodiscard]] const StudioHostEvaluation& capabilities() const { return capabilities_; }
            [[nodiscard]] const std::vector<std::string>& invoked() const { return invoked_; }
            [[nodiscard]] const std::string& statusLeft() const { return shell_->statusLeft(); }
            [[nodiscard]] std::size_t outlinerRowsDrawn() const { return outlinerRowsDrawn_; }
            [[nodiscard]] std::size_t outlinerRowsTotal() const { return outlinerRowsTotal_; }
            [[nodiscard]] std::size_t logRowsDrawn() const { return logRowsDrawn_; }
            [[nodiscard]] std::size_t logRowsMatching() const { return logRowsMatching_; }
            [[nodiscard]] const std::string& layoutProblem() const { return layoutProblem_; }
            [[nodiscard]] bool layoutRestored() const { return layoutRestored_; }

            /**
             * @brief Writes the arrangement back, if this session was asked to remember one.
             *
             * Called after Run() returns rather than from a destructor: saving is the kind of thing
             * that reports a problem, and a destructor is the one place that cannot.
             *
             * @param outProblem Receives the reason on failure.
             * @return Whether anything was written.
             */
            bool storeWorkspace(std::string* outProblem) const
            {
                if (options_.workspacePath.empty() || shell_ == nullptr) { return false; }
                const StudioWorkspaceStore store{options_.workspacePath};
                return store.save(shell_->saveLayout(), outProblem);
            }

        protected:
            void Initialize() override
            {
                platform_ = std::make_unique<CnaUiPlatform>();
                Game::Initialize();
            }

            void LoadContent() override
            {
                renderer_ = std::make_unique<CnaUiRenderer>();
                renderer_->initialize(getGraphicsDeviceProperty());

                capabilities_ = evaluateStudioHost(captureStudioCapabilitySnapshot(
                    getGraphicsDeviceProperty(), getHostPlatformName(),
                    /*modernApiAvailable=*/true));

                if (!capabilities_.canHostStudio)
                {
                    // STUDIO-02022, on the native shell too: refuse with the reason rather than
                    // open a window that cannot draw.
                    std::cerr << capabilities_.diagnostic();
                    Exit();
                    return;
                }

                shell_->setStatusRight("Renderer: " + CnaUiRenderer::getBackendName()
                                       + "   Platform: " + getHostPlatformName());

                // Real output, not a placeholder. The first question of every graphics bug report
                // is which renderer this build actually got, and the Output Log is where somebody
                // looks for it.
                log_.append(LogSeverity::Info,
                            "CNA Studio on the " + CnaUiRenderer::getBackendName()
                            + " renderer, " + getHostPlatformName() + " platform.");
                // Only what is *not* met. A console that recited twenty satisfied requirements
                // on every start would train the user to scroll past the one that matters.
                for (const StudioRequirementOutcome& outcome : capabilities_.unmetRecommended())
                {
                    log_.append(LogSeverity::Warning,
                                outcome.subject + " is not available: " + outcome.reason
                                + (outcome.detail.empty() ? "" : " (" + outcome.detail + ")"));
                }
                if (!layoutProblem_.empty())
                {
                    log_.append(LogSeverity::Warning, layoutProblem_);
                }
                else if (layoutRestored_)
                {
                    log_.append(LogSeverity::Info, "Restored the saved workspace layout.");
                }
                if (!CnaUiPlatform::hasClipboard())
                {
                    log_.append(LogSeverity::Warning,
                                "Clipboard unavailable: CNA's Devices module is off in this build "
                                "(CNA gap G-02).");
                }
                contentLoaded_ = true;
                Game::LoadContent();
            }

            void Update(Xna::GameTime& gameTime) override
            {
                Game::Update(gameTime);
                if (!contentLoaded_ || platform_ == nullptr) { return; }

                // The device's viewport, not the window's client bounds: on SOFTWARE and HEADLESS
                // the window reports 0x0 while the back buffer is perfectly real, and on a scaled
                // presentation the two differ.
                const XnaGraphics::Viewport& viewport =
                    getGraphicsDeviceProperty().getViewportProperty();
                float width = static_cast<float>(viewport.getWidthProperty());
                float height = static_cast<float>(viewport.getHeightProperty());
                if (width <= 0.0f || height <= 0.0f)
                {
                    const Xna::Rectangle bounds = getWindowProperty().getClientBoundsProperty();
                    width = static_cast<float>(bounds.Width);
                    height = static_cast<float>(bounds.Height);
                }

                const auto deltaSeconds = static_cast<float>(
                    gameTime.getElapsedGameTimeProperty().getTotalSecondsProperty());

                const UiInputState input = platform_->poll(width, height, deltaSeconds);
                displayWidth_ = input.displayWidth;
                displayHeight_ = input.displayHeight;

                shell_->renderFrame(input);
                ++frames_;

                for (const std::string& action : shell_->invokedActions())
                {
                    invoked_.push_back(action);
                    if (action == "studio.file.quit") { Exit(); }
                }

                // Uploaded in Update, not Draw: under a fixed-timestep loop several Update frames
                // can run without a matching Draw, and a texture request satisfied in Draw would be
                // lost for every one of them. The same reasoning as CnaUiRenderer's ImGui path.
                if (renderer_ != nullptr)
                {
                    renderer_->applyTextureRequests(shell_->drawData());
                }

                platform_->setTextInputActive(shell_->isTextInputActive());

                if (input.quitRequested) { Exit(); return; }

                if (options_.frameLimit > 0
                    && frames_ >= static_cast<std::uint64_t>(options_.frameLimit))
                {
                    // Held until a requested screenshot has been taken: Exit() stops the loop
                    // before the next Draw, and the capture can only happen there.
                    const bool waiting =
                        !options_.screenshotPath.empty() && !screenshotAttempted_;
                    if (!waiting) { Exit(); }
                }
            }

            void Draw(const Xna::GameTime& gameTime) override
            {
                const StudioColor background =
                    shell_->theme().color(StudioColorRole::AppBackground);
                // Widened explicitly: Color takes either four ints or four floats, and the theme's
                // bytes promote to both, which makes the call ambiguous rather than obvious.
                getGraphicsDeviceProperty().Clear(Xna::Color(static_cast<int>(background.r),
                                                             static_cast<int>(background.g),
                                                             static_cast<int>(background.b), 255));

                if (contentLoaded_ && renderer_ != nullptr)
                {
                    const UiRenderStats stats = renderer_->renderGeometry(shell_->drawData());
                    drawCalls_ += stats.drawCalls;
                    triangles_ += stats.triangles;
                }

                captureScreenshotIfRequested();
                Game::Draw(gameTime);
            }

        private:
            void captureScreenshotIfRequested()
            {
                if (options_.screenshotPath.empty() || screenshotAttempted_) { return; }
                if (options_.frameLimit <= 0
                    || frames_ < static_cast<std::uint64_t>(options_.frameLimit))
                {
                    return;
                }

                XnaGraphics::GraphicsDevice& device = getGraphicsDeviceProperty();
                const XnaGraphics::Viewport& viewport = device.getViewportProperty();
                const int width = viewport.getWidthProperty();
                const int height = viewport.getHeightProperty();
                if (width <= 0 || height <= 0) { return; }

                const auto count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
                std::vector<Xna::Color> pixels(count, Xna::Color(0, 0, 0, 255));

                try
                {
                    device.GetBackBufferData(pixels.data(), static_cast<int>(count));
                    XnaGraphics::Texture2D capture{device, width, height};
                    capture.SetData(pixels.data(), static_cast<int>(count));
                    capture.SaveAsPng(options_.screenshotPath);
                    screenshotAttempted_ = true;
                    screenshotWritten_ = true;
                }
                catch (const std::exception& exception)
                {
                    // Two flags, not one. Setting "written" in the failure path makes a failed
                    // capture report success, which silently defeats the only assertion a
                    // graphical smoke test rests on -- the file appearing IS the test.
                    screenshotAttempted_ = true;
                    std::cerr << "cna-studio: screenshot failed: " << exception.what() << "\n";
                }
            }

            CnaStudioShellHostOptions options_;
            std::unique_ptr<Xna::GraphicsDeviceManager> graphics_;
            std::unique_ptr<CnaUiPlatform> platform_;
            std::unique_ptr<CnaUiRenderer> renderer_;
            std::unique_ptr<StudioShell> shell_;

            StudioHostEvaluation capabilities_;
            std::vector<std::string> invoked_;

            bool contentLoaded_ = false;
            bool screenshotAttempted_ = false;
            bool screenshotWritten_ = false;
            std::unique_ptr<StudioContext> context_;
            StudioTreeState outlinerState_;
            std::size_t outlinerRowsDrawn_ = 0;
            std::size_t outlinerRowsTotal_ = 0;
            StudioLog log_;
            std::size_t logRowsDrawn_ = 0;
            std::size_t logRowsMatching_ = 0;
            std::string layoutProblem_;
            bool layoutRestored_ = false;
            std::uint64_t frames_ = 0;
            std::size_t drawCalls_ = 0;
            std::size_t triangles_ = 0;
            float displayWidth_ = 0.0f;
            float displayHeight_ = 0.0f;
        };
    } // namespace

    CnaStudioShellHostResult runStudioShellInWindow(const CnaStudioShellHostOptions& options)
    {
        CnaStudioShellHostResult result;
        result.renderer = CnaUiRenderer::getBackendName();

        CnaStudioShellGame game{options};
        game.Run();

        result.frames = game.frames();
        result.drawCalls = game.drawCalls();
        result.triangles = game.triangles();
        result.displayWidth = game.displayWidth();
        result.displayHeight = game.displayHeight();
        result.screenshotWritten = game.screenshotWritten();
        result.capabilityReport = game.capabilities().report();
        result.rendererCanHostStudio = game.capabilities().canHostStudio;
        result.invokedActions = game.invoked();
        result.statusLeft = game.statusLeft();
        result.outlinerRowsDrawn = game.outlinerRowsDrawn();
        result.outlinerRowsTotal = game.outlinerRowsTotal();
        result.logRowsDrawn = game.logRowsDrawn();
        result.logRowsMatching = game.logRowsMatching();
        result.layoutRestored = game.layoutRestored();
        result.layoutProblem = game.layoutProblem();

        // After the loop, not in a destructor: this reports a problem, and a destructor is the one
        // place that cannot. A layout that could not be stored is worth saying out loud -- the user
        // arranged it, and next start will quietly not have it.
        std::string storeProblem;
        result.layoutStored = game.storeWorkspace(&storeProblem);
        if (!storeProblem.empty()) { result.layoutProblem = storeProblem; }

        if (!result.rendererCanHostStudio)
        {
            result.exitCode = 6;
            result.errorMessage = "the compiled renderer cannot host CNA Studio";
        }
        return result;
    }
} // namespace CNA::Studio
