// SPDX-License-Identifier: MS-PL
/**
 * @file CnaStudioShellHost.cpp
 * @brief The native Studio shell's window, input and presentation, through CNA's public API.
 */

#include "CNA/Studio/Viewport/CnaStudioShellHost.hpp"

#include "CNA/Studio/UiCore/StudioUiBenchmark.hpp"

#include <algorithm>
#include <ctime>
#include <cstdint>
#include <filesystem>
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
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColorTexture.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include "CNA/Studio/Scene/SceneModels.hpp"
#include "CNA/Studio/Scene/SceneSprites3D.hpp"
#include "CNA/Studio/Scene/SceneWireframe.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"
#include "CNA/Studio/RuntimeBridge/PlayerProcess.hpp"
#include "CNA/Studio/Viewport/CnaSceneRenderer.hpp"
#include "CNA/Studio/ShellPanels/StudioShellPanels.hpp"
#include "CNA/Studio/ShellPanels/StudioDetailsPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioShellActions.hpp"
#include "CNA/Studio/ShellPanels/StudioOutlinerPanel.hpp"
#include "CNA/Studio/Scene/SceneCommands.hpp"
#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/Plugins/Plugin.hpp"
#include "CNA/Studio/Plugins/PluginStartup.hpp"
#include "CNA/Studio/ShellPanels/StudioPluginMenus.hpp"
#include "CNA/Studio/StudioStartupDocument.hpp"
#include "CNA/Studio/Ui/StudioLog.hpp"
#include "CNA/Studio/UiCore/StudioLogPanel.hpp"
#include "CNA/Studio/UiCore/StudioPreferences.hpp"
#include "CNA/Studio/UiCore/StudioWorkspaceStore.hpp"
#include "CNA/Studio/UiCore/StudioTheme.hpp"
#include "CNA/Studio/Viewport/CnaCapabilityBridge.hpp"
#include "CNA/Studio/Viewport/CnaUiPlatform.hpp"
#include "CNA/Studio/Viewport/StudioAudio.hpp"
#include "CNA/Studio/UiRenderer/StudioHostRenderer.hpp"
#include "CNA/Studio/UiRenderer/StudioModernUiRenderer.hpp"

namespace Xna = Microsoft::Xna::Framework;
namespace XnaGraphics = Microsoft::Xna::Framework::Graphics;

namespace
{
    /**
     * @brief Whether @p pixels is too flat to be a picture of anything, and says so if it is.
     *
     * Counted from the packed value rather than from the channels, because the question is only
     * whether two texels differ and packing is a fixed permutation of the same bits: no assumption
     * about channel order is needed, and one that was wrong would be invisible here.
     *
     * @param pixels The captured frame.
     * @param minimum How many distinct colours are required. Zero asks nothing.
     * @param path The file being written, for the message.
     * @return True when the capture should be treated as a failure.
     */
    bool captureIsBlank(const std::vector<Xna::Color>& pixels, std::size_t minimum,
                        const std::string& path)
    {
        if (minimum == 0) { return false; }

        std::vector<std::uint32_t> seen;
        seen.reserve(minimum);
        for (const Xna::Color& texel : pixels)
        {
            const auto packed = static_cast<std::uint32_t>(texel.getPackedValueProperty());
            if (std::find(seen.begin(), seen.end(), packed) != seen.end()) { continue; }
            seen.push_back(packed);
            if (seen.size() >= minimum) { return false; }
        }

        std::cerr << "cna-studio: " << path << " holds only " << seen.size()
                  << " distinct colours, and " << minimum << " were required -- the frame was "
                  << "captured but nothing was drawn in it.\n";
        return true;
    }
}

namespace CNA::Studio
{
    namespace
    {
        // STUDIO-04028. Every byte figure `--ui-benchmark` prints is one of these constants times
        // a vertex count, and both are stated as literals because `StudioUiBenchmark.hpp` is
        // CNA-free by design. A change to either layout would make all of them wrong at once,
        // silently and in the direction nobody checks. Here it is a compile error naming the
        // constant.
        //
        // The two differ by more than padding. Studio hands CNA a 56-byte
        // VertexPositionColorTexture whose data is twenty bytes: a Vector3 (12), a Color (24,
        // because CNA's Color carries a vtable of its own) and a Vector2 (8), plus eight for this
        // type's own vtable pointer. CNA repacks it to a 24-byte PositionColorTextureStream before
        // upload, which is also the stride the vertex declaration names -- so the bus sees 24
        // where Studio wrote 56.
        //
        // Recorded rather than worked around: the types are CNA's and the fix belongs there
        // (docs/CNA-GAPS.md G-11). A Studio-local vertex would be a second layout to keep in step
        // with CNA's declaration, which is worse than the waste.
        static_assert(sizeof(XnaGraphics::VertexPositionColorTexture)
                          == kStudioUiSubmittedVertexBytes,
                      "kStudioUiSubmittedVertexBytes no longer matches the vertex Studio hands "
                      "CNA, so every byte count --ui-benchmark reports is wrong (plan.md "
                      "STUDIO-04028).");

        /**
         * @brief Whether CNA still uploads the vertex stride `kStudioUiGpuVertexBytes` assumes.
         *
         * `STUDIO-04028`. Every GPU byte figure `--ui-benchmark` prints is that constant times a
         * vertex count, and the constant is a literal because `StudioUiBenchmark.hpp` is CNA-free.
         * The submitted-bytes half is pinned by the `static_assert` above; this half cannot be,
         * because the stride lives on a `VertexDeclaration` rather than in a type's size -- so it
         * is asked at run time, through the public declaration, by the host that is already
         * checking the model per frame (`checkCostModelAgrees` below). Moved here from the
         * now-deleted `CnaUiRenderer.cpp` (STUDIO-04027): the check was never about which backend
         * draws, only about what CNA uploads, and this is its only caller.
         *
         * @return True when the declaration's stride is what the benchmark assumes.
         */
        [[nodiscard]] bool studioUiGpuVertexStrideMatches()
        {
            // The GPU-side half of the same check, and a *runtime* one because the stride lives on
            // a VertexDeclaration rather than in a type's size. That is the public way to ask: the
            // packed stream behind it is CNA::Internal, which Studio does not reach into
            // (STUDIO-02032) -- and the declaration's stride is the authority anyway, because it
            // is what the device is told.
            return XnaGraphics::VertexPositionColorTexture::getVertexDeclarationStatic()
                       .getVertexStrideProperty() == static_cast<int>(kStudioUiGpuVertexBytes);
        }

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

                // The user's preferences before the theme, because they decide it. A command
                // line flag still wins: somebody passing --shell-theme is answering this one run,
                // and honouring the file over the flag would make the flag look broken.
                const StudioPreferencesStore preferencesStore{StudioPreferencesStore::defaultPath()};
                const StudioPreferencesDocument storedPreferences = preferencesStore.load();
                preferencesProblem_ = storedPreferences.problem;

                std::string themeName = storedPreferences.preferences.theme;
                float scale = storedPreferences.preferences.uiScale;
                if (!options.theme.empty()) { themeName = options.theme; }
                if (options.uiScale > 0.0f) { scale = options.uiScale; }

                StudioTheme theme = themeName == "light" ? StudioTheme::light()
                                                         : StudioTheme::dark();
                theme.setScale(scale);
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

                    // The arrangements the user saved under names, and the seam that writes them
                    // back. The shell keeps the documents so applying one is a call rather than a
                    // round trip through the file, and these two persist what it decides.
                    shell_->setSavedLayouts(stored.named);

                    const std::string workspacePath = options.workspacePath;
                    StudioWorkspaceServices services;
                    services.saveNamed = [workspacePath](const std::string& name,
                                                         const JsonValue& layout,
                                                                        std::string* problem) {
                        return StudioWorkspaceStore{workspacePath}.saveNamed(name, layout, problem);
                    };
                    services.removeNamed = [workspacePath](const std::string& name,
                                                                          std::string* problem) {
                        return StudioWorkspaceStore{workspacePath}.removeNamed(name, problem);
                    };
                    shell_->setWorkspaceServices(std::move(services));
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

                // The project, and then `--scene` over the top of the project's own startup
                // scene. Shared with the preview, the benchmark and the prototype, because this
                // shell used to write it out itself and had left `--scene` out of it: the flag was
                // parsed, documented and read by the prototype alone, so on the default UI it did
                // nothing at all (STUDIO-07053).
                const StudioStartupDocument opened = openStudioStartupDocument(
                    *context_, options.projectPath, options.scenePath);

                // Whatever *did* open goes in the status bar, including when the scene override
                // did not: a failed `--scene` leaves the project open on its own startup scene,
                // and a status bar that then said nothing would describe a Studio with no project
                // while the panels showed one.
                if (opened.projectOpened)
                {
                    shell_->setStatusLeft(context_->getProject().getName() + "  --  "
                                          + context_->getScene().getName());
                }

                if (!opened.succeeded())
                {
                    // Reported, not fatal. An editor that refused to open because one project
                    // would not load leaves the user with no way to open a different one -- and
                    // the same goes for a scene: the project is open and usable behind the
                    // message.
                    log_.append(LogSeverity::Error, opened.error);
                    shell_->status().problem = opened.error;
                }

                (void)bindStudioShellActions(*shell_, *context_, log_);

                // Every panel, bound in one place that does not need CNA -- so the headless
                // preview shows the same panels this window does (STUDIO-07017, over the panel
                // content seam of STUDIO-07016).
                StudioShellPanelServices services;

                // The Details panel's preview (STUDIO-07044). Constructed here rather than at
                // first press: it needs the asset database and nothing else, and a device is only
                // touched when a clip is actually played -- so a Studio started on a machine with
                // no sound card still opens, and says so on the button rather than at start-up.
                audio_ = createCnaStudioAudio(context_->getAssets());
                services.audio = audio_.get();

                // The sprite animation preview's pixels (STUDIO-07043). Through the member rather
                // than captured directly, because the scene viewport is created later -- the
                // panels are bound before there is a device, which is what lets the headless
                // preview bind the same ones.
                // What the watcher in `StudioShellPanels` cannot do itself: only the module with
                // a device holds a rendered texture to drop (STUDIO-30012).
                services.invalidateRenderedAsset = [this](const Uuid& assetId) {
                    if (sceneViewport_ != nullptr) { sceneViewport_->invalidateAsset(assetId); }
                };

                services.assetThumbnail = [this](const Uuid& assetId) {
                    return sceneViewport_ != nullptr ? sceneViewport_->getAssetThumbnail(assetId)
                                                     : kUiTextureNone;
                };

                // Pixels made on a worker, turned into a texture by the one module that has a
                // device (STUDIO-35041). The cache keyed on `key` lives in the viewport, because
                // the texture's lifetime is the device's business rather than the panel's.
                services.uploadThumbnail = [this](const Uuid& assetId,
                                                  const StudioThumbnail& thumbnail) {
                    return sceneViewport_ != nullptr
                        ? sceneViewport_->uploadThumbnail(assetId, thumbnail.key, thumbnail.width,
                                                          thumbnail.height, thumbnail.pixels)
                        : kUiTextureNone;
                };

                services.releaseThumbnail = [this](const Uuid& assetId) {
                    if (sceneViewport_ != nullptr) { sceneViewport_->releaseThumbnail(assetId); }
                };

                // What the material editor tells a user about their own build (STUDIO-07046).
                services.modelEffectName = [this] {
                    return sceneViewport_ != nullptr ? sceneViewport_->getModelEffectName()
                                                     : std::string{};
                };

                services.setClipboardText = [](const std::string& text) {
                    if (!CnaUiPlatform::hasClipboard()) { return false; }
                    CnaUiPlatform::setClipboardText(text);
                    return true;
                };
                panels_ = std::make_unique<StudioShellPanels>(*shell_, *context_, log_,
                                                              std::move(services));

                // The Project Hub's templates and the user's recent list (STUDIO-08001). Through
                // the same function the headless preview calls, so what CI photographs is what a
                // user sees rather than a Hub with different templates in it.
                bindStudioProjectHub(*panels_, options.executablePath, log_);

                // The preferences the theme already came from, and the seam that writes them back.
                panels_->preferences() = storedPreferences.preferences;
                // The seam rather than watching the invoked actions for an id. A host matching on
                // a command's name is a host reimplementing its behaviour outside the registry --
                // which is how this one came to close the window without asking about unsaved
                // changes, because the command it named never actually ran.
                shell_->setQuitHandler([this] { Exit(); });

                panels_->userPreferences().setSaveSink([](const StudioPreferences& preferences,
                                                              std::string* problem) {
                    return StudioPreferencesStore{StudioPreferencesStore::defaultPath()}
                        .save(preferences, problem);
                });

                // The project's plugins (STUDIO-07052). After the panels, because a plugin
                // registers its commands and panels into the context on activation and
                // `StudioShellPanels::pollPlugins` is what turns those into menu rows -- it reads
                // an extension *revision* rather than taking a callback, so loading before it
                // exists would leave the rows unbuilt until something else changed.
                //
                // Every failure is named per plugin, in the manifest's own words, through the same
                // routine the prototype uses. A shell that loaded plugins differently from the
                // editor beside it would be a second plugin contract nobody wrote down.
                pluginLoad_ = studioLoadPlugins(plugins_, *context_, options.pluginDirectory,
                                                options.executablePath);

                if (context_->hasProject())
                {
                    // Remembered here rather than inside StudioContext::openProject: the context is
                    // the CNA-free document model, and a list of recently opened files is not its
                    // business. A project opened with --project is as recent as one opened from the
                    // Hub, and a list that only knew about one of the two would be wrong in the way
                    // that is hardest to notice.
                    panels_->rememberProject(context_->getProject().getFilePath(),
                                             static_cast<std::int64_t>(std::time(nullptr)));

                    // And the view the project asks to open in (STUDIO-11014), which for a project
                    // created from Empty 3D is the 3D world viewport.
                    panels_->applyProjectDefaultViewOnOpen(*shell_);
                }
                else
                {
                    // No project: the Hub is the thing to be looking at, and it is the state a
                    // fresh Studio starts in.
                    (void)shell_->activatePanel("projecthub");
                }

                if (!options.selectEntity.empty())
                {
                    // By name, because that is what a person types. SceneDocument looks entities
                    // up by id, so the walk is here rather than being a lookup the document does
                    // not have and that nothing else has asked for.
                    const StudioEntity* wanted = nullptr;
                    for (const StudioEntity& candidate : context_->getScene().getEntities())
                    {
                        if (candidate.getName() == options.selectEntity) { wanted = &candidate; break; }
                    }

                    if (wanted != nullptr)
                    {
                        context_->select(wanted->getId());
                    }
                    else
                    {
                        log_.append(LogSeverity::Warning,
                                    "No entity called '" + options.selectEntity + "' in this scene.");
                    }
                }

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

            /** @brief Whether the capture was refused for holding too few colours. */
            [[nodiscard]] bool screenshotTooFlat() const { return screenshotTooFlat_; }

            /** @brief Where the benchmark's cost model first disagreed with the backend. */
            [[nodiscard]] const std::string& costModelMismatch() const { return costModelMismatch_; }
            [[nodiscard]] bool viewportComposited() const { return viewportComposited_; }
            [[nodiscard]] const StudioHostEvaluation& capabilities() const { return capabilities_; }
            [[nodiscard]] const std::vector<std::string>& invoked() const { return invoked_; }
            [[nodiscard]] const std::string& statusLeft() const { return shell_->statusLeft(); }
            /** @brief What the panels reported last frame, or zeroes before they were bound. */
            [[nodiscard]] StudioShellPanelCounts panelCounts() const
            {
                return panels_ != nullptr ? panels_->counts() : StudioShellPanelCounts{};
            }
            [[nodiscard]] std::size_t outlinerRowsDrawn() const { return panelCounts().outlinerRowsDrawn; }
            [[nodiscard]] std::size_t outlinerRowsTotal() const { return panelCounts().outlinerRowsTotal; }
            [[nodiscard]] std::size_t detailsRowsDrawn() const { return panelCounts().detailsRowsDrawn; }
            [[nodiscard]] std::size_t contentRowsDrawn() const { return panelCounts().contentRowsDrawn; }
            [[nodiscard]] std::size_t contentRowsTotal() const { return panelCounts().contentRowsTotal; }
            [[nodiscard]] std::size_t logRowsDrawn() const { return panelCounts().logRowsDrawn; }
            [[nodiscard]] std::size_t logRowsMatching() const { return panelCounts().logRowsMatching; }
            [[nodiscard]] const std::string& layoutProblem() const { return layoutProblem_; }
            [[nodiscard]] bool layoutRestored() const { return layoutRestored_; }

            [[nodiscard]] const StudioPluginLoad& pluginLoad() const { return pluginLoad_; }

            [[nodiscard]] std::size_t pluginMenuRows() const
            {
                return panels_ == nullptr ? 0 : panels_->counts().pluginMenuRows;
            }

            /**
             * @brief Shuts the plugins down while the context is still alive.
             *
             * Called after the loop rather than left to the destructor, for the reason the unload
             * exists at all: a plugin's `shutdown()` is handed the context, and a member destroyed
             * after the context would hand it a dangling reference. Declaration order alone would
             * get this right today and would stop doing so the moment somebody reorders the
             * members, which is not a thing a reviewer notices.
             */
            void unloadPlugins()
            {
                if (context_ == nullptr) { return; }

                // The shell's registry first, and the order is not a preference. Binding a plugin
                // command *copies* its `std::function` into `StudioActionRegistry`, and destroying
                // that copy runs a manager function living in the plugin's library -- so clearing
                // the registry after `dlclose` jumps into unmapped memory rather than failing to
                // find a command. It crashes in `~StudioShell`, which is both the hardest place to
                // read a backtrace from and the last place anybody looks for a plugin bug.
                //
                // Found by loading a plugin on this shell for the first time: the segmentation
                // fault was not in the loading, it was in the shutting down, and it had been
                // waiting in the design since the menus were written.
                if (shell_ != nullptr) { (void)studioClearPluginMenus(*shell_); }

                studioUnloadPlugins(plugins_, *context_);
            }

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
                // STUDIO-02071/02072: from what the build actually carries. The literal `true`
                // that used to sit here reported what this call site asserted rather than what CNA
                // was configured with.
                //
                // Asked *before* a renderer is created, because it is what decides whether one gets
                // made at all (STUDIO-02074): a host that cannot meet the profile refuses to start
                // rather than falling back to a second UI GPU stack.
                assessment_ = assessStudioHost(getGraphicsDeviceProperty(), getHostPlatformName());
                capabilities_ = assessment_.modern;

                renderer_ = makeUiRenderBackend();
                if (renderer_ != nullptr) { renderer_->initialize(getGraphicsDeviceProperty()); }

                // The contract said this host can run a shader; the shader is the proof. A modern
                // backend whose program CNA refused would otherwise draw a black window on a host
                // the report called capable, which reads as Studio being broken rather than as one
                // shader being rejected. There is nothing left to fall back to (STUDIO-02074), so
                // this is a refusal rather than a degraded run.
                if (auto* modern = dynamic_cast<StudioModernUiRenderer*>(renderer_.get());
                    modern != nullptr && !modern->isUsable())
                {
                    modernShaderProblem_ = modern->shaderDiagnostic();
                    assessment_.decision.choice = StudioUiBackendChoice::None;
                    assessment_.decision.reason =
                        "The modern UI renderer was required and this host refused Studio's UI "
                        "shader. " + modernShaderProblem_;
                }

                if (options_.reportCapabilities || !assessment_.canHostStudio())
                {
                    std::cout << capabilities_.report();
                    std::cout << "  UI renderer: "
                              << studioUiBackendChoiceName(assessment_.decision.choice) << " -- "
                              << assessment_.decision.reason << "\n";
                }

                if (!assessment_.canHostStudio())
                {
                    // STUDIO-02022, on the native shell too: refuse with the reason rather than
                    // open a window that cannot draw.
                    std::cerr << capabilities_.diagnostic();
                    std::cerr << assessment_.decision.reason << "\n";
                    Exit();
                    return;
                }

                if (options_.checkCapabilitiesOnly)
                {
                    // Asked what this build can do, not asked for an editor.
                    Exit();
                    return;
                }

                shell_->status().renderer =
                    studioHostCnaRendererName() + " on " + getHostPlatformName();

                // The same facts About shows, set here because this is where they are known: a
                // shell that carried its own copy of the renderer name would be a second place it
                // could be wrong, and About is exactly the dialog people quote in bug reports.
                shell_->setAboutLines({std::string{"CNA Studio "} + CNA_STUDIO_VERSION,
                                       "An editor for CNA games.",
                                       "UI: Studio native.",
                                       "Renderer: " + shell_->status().renderer});

                // Real output, not a placeholder. The first question of every graphics bug report
                // is which renderer this build actually got, and the Output Log is where somebody
                // looks for it.
                log_.append(LogSeverity::Info,
                            "CNA Studio on the " + studioHostCnaRendererName()
                            + " renderer, " + getHostPlatformName() + " platform.");
                log_.append(LogSeverity::Info,
                            "Modern graphics API: " + assessment_.modernApi.detail + ".");
                log_.append(LogSeverity::Info,
                            std::string{"UI renderer: "}
                            + std::string{studioUiBackendChoiceName(assessment_.decision.choice)}
                            + ".");
                if (assessment_.modernApi.versionMismatch)
                {
                    log_.append(LogSeverity::Warning,
                                "The CNAEXT engine layer's header and library report different "
                                "revisions. Rebuild CNA and Studio together.");
                }
                // Only what is *not* met. A console that recited twenty satisfied requirements
                // on every start would train the user to scroll past the one that matters.
                for (const StudioRequirementOutcome& outcome : capabilities_.unmetRecommended())
                {
                    log_.append(LogSeverity::Warning,
                                outcome.subject + " is not available: " + outcome.reason
                                + (outcome.detail.empty() ? "" : " (" + outcome.detail + ")"));
                }
                if (!preferencesProblem_.empty())
                {
                    // Said rather than swallowed: a user whose theme reverted deserves to know
                    // why, and the alternative is a Studio that silently looks like a fresh
                    // install every time it starts.
                    log_.append(LogSeverity::Warning, preferencesProblem_);
                    preferencesProblem_.clear();
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
                // The scene renderer, and what the Diagnostics panel reports. Both here rather
                // than in the constructor: there is no graphics device until LoadContent, and the
                // capability evaluation above is what the diagnostics are made of. The first
                // version of this ran in the constructor and segfaulted on the null device.
                sceneViewport_ = createCnaStudioViewport(getGraphicsDeviceProperty(),
                                                         context_->getAssets(),
                                                         context_->getComponentRegistry(),
                                                         *renderer_);

                // The camera the viewport panel drives, and the sprite sizes it picks against.
                // Handed over now that the device exists: the panels were bound before it did,
                // because binding needs no CNA and that is the whole point of the seam.
                panels_->setViewportServices(sceneViewport_->getCamera(),
                                             sceneViewport_->getCamera3D(),
                                             sceneViewport_->makeSizeProvider());

                StudioDiagnosticsInfo& diagnostics = panels_->diagnostics();
                diagnostics.uiBackend = "Studio native";
                diagnostics.renderer = capabilities_.rendererName;
                diagnostics.platform = capabilities_.platformName;
                diagnostics.modernApi = capabilities_.modernApiAvailable;
                diagnostics.host = capabilities_;
                diagnostics.viewportBackend = sceneViewport_->getBackendName();
                if (!options_.executablePath.empty())
                {
                    // Through the panels rather than straight into the diagnostics: Play chooses
                    // from the same list the Diagnostics panel reports, and two copies would be
                    // two chances to disagree about what this Studio can run.
                    panels_->setPlayerBuilds(discoverPlayerBuilds(
                        std::filesystem::path{options_.executablePath}.parent_path()
                            .generic_string()));
                }

                // `--shell-invoke` runs here, last, rather than in the constructor where the
                // rest of the options are read. Half the registry is declared with no handler and
                // given one by whatever binds it, and the viewport's commands -- the tools, the
                // gizmo modes, Focus -- are bound by the `setViewportServices` above, which needs
                // a graphics device and so cannot happen until now. Invoking before that found
                // the command, ran its absent handler and captured a window where nothing had
                // happened: a whole session spent looking for a missing overlay that had simply
                // never been armed.
                if (!options_.invokeAction.empty())
                {
                    if (shell_->actions().find(options_.invokeAction) == nullptr)
                    {
                        log_.append(LogSeverity::Warning,
                                    "No command called '" + options_.invokeAction + "'.");
                    }
                    else
                    {
                        shell_->invoke(options_.invokeAction);
                        // Existing is not doing. The shell records what it refused, so a command
                        // with no handler says so instead of looking like a feature that failed.
                        if (!shell_->refusedActions().empty())
                        {
                            log_.append(LogSeverity::Warning,
                                        "'" + options_.invokeAction + "' did nothing: "
                                            + shell_->refusedActions().front() + ".");
                        }
                    }
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

                // Once per frame, before the panel that reports on it: a build that advanced only
                // when its panel happened to be the visible tab would stall whenever the user
                // looked at something else.
                elapsedSeconds_ += static_cast<double>(deltaSeconds);
                panels_->poll(elapsedSeconds_);

                // Assets edited outside Studio (STUDIO-07051) are watched inside `panels_->poll`
                // above, which is also what refreshes the asset database's presence cache
                // (STUDIO-30012). It was polled here, in the CNA-backed host only, which meant the
                // cache's invalidation ran in one of the two builds -- and the headless preview and
                // every test are the other one.
                //
                // Still before the scene is rendered, because the frame that reports a change has
                // to be the frame that shows it: reporting an edit and then drawing the old art for
                // one more frame is a flicker nobody can explain.
                renderSceneIntoViewport();

                shell_->renderFrame(input);
                ++frames_;

                for (const std::string& action : shell_->invokedActions())
                {
                    invoked_.push_back(action);
                }

                // Uploaded in Update, not Draw: under a fixed-timestep loop several Update frames
                // can run without a matching Draw, and a texture request satisfied in Draw would be
                // lost for every one of them.
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

                    checkCostModelAgrees(stats);

                    // Reported to the Diagnostics panel per frame rather than accumulated there:
                    // "how heavy is a frame" is the question, and a running total answers a
                    // different one.
                    if (panels_ != nullptr)
                    {
                        panels_->diagnostics().drawCalls = stats.drawCalls;
                        panels_->diagnostics().triangles = stats.triangles;
                        panels_->diagnostics().frames = frames_;
                    }
                }

                captureScreenshotIfRequested();
                Game::Draw(gameTime);
            }

        private:
            /**
             * @brief Requires `studioUiFrameCost` to predict what the backend just reported.
             *
             * `STUDIO-04028`. The benchmark's cost model is a second implementation of the
             * modern backend's inner loop, written against the draw data so that it runs with no
             * CNA and no GPU — which is what makes it useful and what makes it capable of being
             * quietly wrong. Checked here, against the real renderer, on a real device, on every
             * frame of every automated run: a model nobody compared with reality is a second
             * implementation with no tests. The measurement this model produced is what
             * `STUDIO-04027` decided on: the classic backend it also used to model is deleted.
             *
             * Only while a frame limit is set, which is what a capture or a smoke test has and an
             * interactive session does not. The pass is cheap — one walk of the commands — but an
             * editor should not spend any of a user's frame checking its own benchmark.
             *
             * The first disagreement is kept rather than the last: after one, the rest are
             * consequences, and a report naming the final frame sends the reader to the wrong one.
             *
             * @param stats What the backend reported for the frame just drawn.
             */
            void checkCostModelAgrees(const UiRenderStats& stats)
            {
                if (options_.frameLimit <= 0 || !costModelMismatch_.empty()) { return; }

                if (!studioUiGpuVertexStrideMatches())
                {
                    costModelMismatch_ =
                        "the GPU vertex stride: CNA's vertex declaration no longer names "
                        + std::to_string(kStudioUiGpuVertexBytes)
                        + " bytes, so every GPU byte count --ui-benchmark reports is wrong";
                    return;
                }

                const StudioUiFrameCost predicted = studioUiFrameCost(shell_->drawData());
                const std::size_t predictedBytes = predicted.modernSubmittedBytes;

                const auto disagree = [this](std::string_view what, std::size_t model,
                                             std::size_t reported) {
                    if (model == reported) { return; }
                    if (!costModelMismatch_.empty()) { return; }
                    costModelMismatch_ = std::string{what} + ": the cost model says "
                                       + std::to_string(model) + ", the "
                                       + std::string{renderer_->name()} + " backend reported "
                                       + std::to_string(reported)
                                       + " (frame " + std::to_string(frames_) + ")";
                };

                disagree("draw calls", predicted.drawCalls, stats.drawCalls);
                disagree("triangles", predicted.triangles, stats.triangles);
                disagree("vertices", predicted.vertices, stats.vertices);
                disagree("indices", predicted.indices, stats.indices);
                disagree("clipped-away commands", predicted.clippedAway, stats.clippedAway);
                disagree("texture changes", predicted.textureChanges, stats.textureChanges);
                disagree("clip changes", predicted.clipChanges, stats.clipChanges);
                disagree("geometry bytes uploaded", predictedBytes, stats.geometryBytesUploaded);
            }

            /**
             * @brief Renders the scene into an offscreen target and hands it to the shell.
             *
             * Sized from the viewport panel's rectangle *as of the last frame*, because the shell
             * decides that rectangle while it describes the frame and the render has to happen
             * before it. One frame of latency after a resize, which shows as the scene stretching
             * for a frame rather than as anything a user would name.
             */
            void renderSceneIntoViewport()
            {
                if (sceneViewport_ == nullptr || context_ == nullptr) { return; }

                const UiRect body = shell_->panelBounds("viewport");
                const int width = static_cast<int>(body.width);
                const int height = static_cast<int>(body.height);
                if (width <= 0 || height <= 0)
                {
                    // Not open, or not the active tab. Cleared rather than left stale: a viewport
                    // showing last frame's picture while docked away is worse than one showing the
                    // grid, because it looks live.
                    shell_->setViewportImage(kUiTextureNone);
                    viewportComposited_ = false;
                    return;
                }

                const UiTextureId texture = panels_->viewportView() == StudioViewportView::ThreeD
                    ? renderSceneIn3D(width, height)
                    // The mode the toolbar chose, so the manipulator drawn is the one a drag will
                    // grab. Two sources of truth here would show a rotate ring and move the entity.
                    : sceneViewport_->render(context_->getScene(), width, height,
                                             context_->getSelection(), panels_->viewportMode(),
                                             panels_->viewportSpace(),
                                             panels_->animationPreview());

                shell_->setViewportImage(texture,
                                         sceneViewport_->isRenderTextureFlippedVertically());
                viewportComposited_ = texture != kUiTextureNone;
            }

            /**
             * @brief Renders the 3D view: the wireframe, the solid meshes and the sprites as quads.
             *
             * Every batch is built by a CNA-free, tested function and handed to the viewport to
             * upload, which is what keeps the decision of *what* to draw testable without a device
             * — the same division the prototype's 3D view uses, and the reason porting it was
             * wiring rather than invention.
             */
            UiTextureId renderSceneIn3D(int width, int height)
            {
                const StudioCamera3D& camera = sceneViewport_->getCamera3D();
                const SpriteSizeProvider sizes = sceneViewport_->makeSizeProvider();

                // The grid's plane is the user's (STUDIO-07056). The preference is a boolean
                // because `cna-studio-ui-core` does not link the scene module; the mapping onto
                // `GridPlane` belongs here, where both halves are in scope.
                WireframeOptions wireframeOptions;
                wireframeOptions.gridPlane = panels_->viewportGridOnGroundPlane()
                    ? GridPlane::Ground
                    : GridPlane::SceneXY;

                // What the shading mode asks for (`plan.md` STUDIO-11010). Worked out by a
                // CNA-free function so the decision is testable without a device; all that is left
                // here is turning three booleans into calls, and there is nothing in that to get
                // wrong.
                const StudioShadingPlan shading = studioShadingPlan(panels_->viewportShading());
                wireframeOptions.drawMeshEdges = shading.meshEdges;

                const WireframeResult wireframe = buildSceneWireframe(
                    context_->getScene(), camera, context_->getSelection(), sizes,
                    wireframeOptions);

                const SceneModelBatch models =
                    shading.solidModels
                        ? buildSceneModelBatch(context_->getScene(), camera,
                                               context_->makeMeshProvider(),
                                               context_->getSelection(),
                                               context_->makeMaterialProvider())
                        : SceneModelBatch{};

                // Sprites as quads in the scene's own plane. `SpriteBatch` cannot draw the
                // trapezoid a sprite becomes from an angle, which is why the 3D view has its own
                // path for them rather than reusing the 2D one.
                const SceneSpriteBatch3D sprites =
                    shading.spriteQuads
                        ? buildSceneSpriteQuads(context_->getScene(), camera, sizes,
                                                panels_->animationPreview(),
                                                context_->getSelection(),
                                                &context_->getComponentRegistry())
                        : SceneSpriteBatch3D{};

                return sceneViewport_->renderScene3D(models, sprites, wireframe.segments, width,
                                                     height);
            }

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

                    // Checked before the file is written, so a blank capture does not also leave
                    // a picture behind for somebody to look at and believe.
                    screenshotAttempted_ = true;
                    if (captureIsBlank(pixels, options_.screenshotMinColors,
                                       options_.screenshotPath))
                    {
                        screenshotTooFlat_ = true;
                        return;
                    }

                    XnaGraphics::Texture2D capture{device, width, height};
                    capture.SetData(pixels.data(), static_cast<int>(count));
                    capture.SaveAsPng(options_.screenshotPath);
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
            /** @brief Creates the backend the assessment chose, or nothing when it refused. */
            [[nodiscard]] std::unique_ptr<StudioUiRenderBackend> makeUiRenderBackend() const
            {
                switch (assessment_.decision.choice)
                {
                    case StudioUiBackendChoice::Modern:
                        return std::make_unique<StudioModernUiRenderer>();
                    case StudioUiBackendChoice::None:
                        break;
                }
                return nullptr;
            }

            std::unique_ptr<StudioUiRenderBackend> renderer_;
            StudioHostAssessment assessment_;

            /** @brief CNA's account of a refused UI shader, or empty. */
            std::string modernShaderProblem_;
            std::unique_ptr<StudioShell> shell_;

            StudioHostEvaluation capabilities_;
            std::vector<std::string> invoked_;

            bool contentLoaded_ = false;
            bool screenshotAttempted_ = false;
            bool screenshotWritten_ = false;
            bool screenshotTooFlat_ = false;

            /** @brief The first frame on which the benchmark's cost model and the backend
             *         disagreed, or empty. See `checkCostModelAgrees`. */
            std::string costModelMismatch_;
            bool viewportComposited_ = false;
            std::unique_ptr<StudioContext> context_;
            StudioLog log_;

            /**
             * @brief The project's plugins, declared after the context they are handed.
             *
             * Members are destroyed in reverse declaration order, so this one goes first -- but it
             * is never left to the destructor: a plugin's `shutdown()` takes the context, so the
             * unload is a named step that happens while the context is still alive. See
             * `unloadPlugins` below.
             */
            PluginHost plugins_;

            /** @brief What `studioLoadPlugins` found, so the run can report it. */
            StudioPluginLoad pluginLoad_;

            /**
             * @brief Watches the project's asset files for edits made outside Studio.
             *
             * Declared after the context whose database it reads. It holds no reference of its own
             * -- the database is handed to `poll` -- but the order still says which owns which.
             */

            /**
             * @brief The audio preview, declared before the panels that borrow it.
             *
             * Members are destroyed in reverse declaration order, and `panels_` holds a raw
             * pointer to this for the whole of its life.
             */
            std::unique_ptr<StudioAudio> audio_;

            /**
             * @brief Declared after the log and the context it borrows, so it is destroyed first.
             *
             * The panels hold references to both and the shell holds callables that capture them,
             * and members are destroyed in reverse declaration order.
             */
            std::unique_ptr<StudioShellPanels> panels_;

            /**
             * @brief Declared after the context it reads and the UI renderer it shares through.
             *
             * Members are destroyed in reverse declaration order, and this one hands textures to
             * the UI renderer and reads the context's assets for the whole of its life.
             */
            std::unique_ptr<StudioViewport> sceneViewport_;
            std::string layoutProblem_;
            std::string preferencesProblem_;
            bool layoutRestored_ = false;
            std::uint64_t frames_ = 0;

            /** @brief Monotonic seconds since start-up, for anything the panels time. */
            double elapsedSeconds_ = 0.0;
            std::size_t drawCalls_ = 0;
            std::size_t triangles_ = 0;
            float displayWidth_ = 0.0f;
            float displayHeight_ = 0.0f;
        };
    } // namespace

    CnaStudioShellHostResult runStudioShellInWindow(const CnaStudioShellHostOptions& options)
    {
        CnaStudioShellHostResult result;
        result.renderer = studioHostCnaRendererName();

        CnaStudioShellGame game{options};
        game.Run();

        // Before anything reads the results, and before `game` goes out of scope: a plugin's
        // shutdown takes the context, so it has to run while the context is still there.
        game.unloadPlugins();

        result.frames = game.frames();
        result.drawCalls = game.drawCalls();
        result.triangles = game.triangles();
        result.displayWidth = game.displayWidth();
        result.displayHeight = game.displayHeight();
        result.screenshotWritten = game.screenshotWritten();
        result.screenshotTooFlat = game.screenshotTooFlat();
        result.costModelMismatch = game.costModelMismatch();
        result.capabilityReport = game.capabilities().report();
        result.rendererCanHostStudio = game.capabilities().canHostStudio;
        result.invokedActions = game.invoked();
        result.statusLeft = game.statusLeft();
        result.pluginsDiscovered = game.pluginLoad().discovered;
        result.pluginsActive = game.pluginLoad().active;
        result.pluginMenuRows = game.pluginMenuRows();
        result.outlinerRowsDrawn = game.outlinerRowsDrawn();
        result.outlinerRowsTotal = game.outlinerRowsTotal();
        result.detailsRowsDrawn = game.detailsRowsDrawn();
        result.contentRowsDrawn = game.contentRowsDrawn();
        result.contentRowsTotal = game.contentRowsTotal();
        result.logRowsDrawn = game.logRowsDrawn();
        result.logRowsMatching = game.logRowsMatching();
        result.viewportComposited = game.viewportComposited();
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
