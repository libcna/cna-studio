// SPDX-License-Identifier: MS-PL
/**
 * @file Main.cpp
 * @brief The cna-studio entry point.
 *
 * Decides which UI the process gets: the native Studio shell in a real window, the same shell
 * rendered headless with no window at all, or an error naming what the binary was built without.
 * The Dear ImGui prototype this used to also choose between is gone (STUDIO-07030).
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <filesystem>
#include <iostream>
#include <memory>

#include "CNA/Studio/Project/Project.hpp"
#include "CNA/Studio/Project/ProjectExport.hpp"
#include "CNA/Studio/Project/RendererCatalog.hpp"
#include "CNA/Studio/Project/StudioHostRequirements.hpp"
#include "CNA/Studio/ShellPanels/StudioShellActions.hpp"
#include "CNA/Studio/ShellPanels/StudioShellPanels.hpp"
#include "CNA/Studio/StudioOptions.hpp"

#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/Scene/SceneDocument.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/StudioStartupDocument.hpp"
#include "CNA/Studio/UiCore/StudioDrawList.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"
#include "CNA/Studio/UiCore/StudioShellLayout.hpp"
#include "CNA/Studio/UiCore/StudioTheme.hpp"
#include "CNA/Studio/UiCore/StudioUiBenchmark.hpp"
#include "CNA/Studio/UiCore/StudioWorkspaceStore.hpp"
#include "CNA/Studio/UiCore/UiSoftwareRasterizer.hpp"

#if defined(CNA_STUDIO_HAS_CNA)
#    include "CNA/Studio/Viewport/CnaStudioShellHost.hpp"
#endif

namespace
{
    /**
     * @brief Rasterises whatever the shell last described, writes the PNG, and reports it.
     *
     * Shared by the whole-shell capture and by `--shell-panel-only`, which describe different
     * things and then finish identically: by the time either arrives here the geometry is already
     * in the draw data, so the only difference between them is what was described into it.
     *
     * With no `--shell-preview` path -- `--headless`'s own way through this function
     * (STUDIO-07049) -- the frame is still rasterised and checked, just not written anywhere: a
     * smoke test wants to know the shell drew cleanly, not a file to open afterward.
     *
     * @param options Parsed command line, for the output path and the size to report.
     * @param theme The theme the shell was built with, for the background and the report line.
     * @param shell The shell holding the described frame.
     * @param textures Atlas uploads gathered across the frames, which the rasteriser samples.
     * @return Process exit code: 0 written; 4 there was nothing to write or the write failed;
     *         5 the frame refused an operation somewhere, which is a Studio defect rather than a
     *         bad command line and must not produce a picture that looks fine.
     */
    int writeShellPreview(const CNA::Studio::StudioOptions& options,
                          const CNA::Studio::StudioTheme& theme,
                          CNA::Studio::StudioShell& shell,
                          CNA::Studio::UiTextureTable& textures)
    {
        const CNA::Studio::ImageBuffer image =
            CNA::Studio::rasterizeUiDrawData(shell.drawData(),
                                             theme.color(CNA::Studio::StudioColorRole::AppBackground),
                                             textures);
        if (!image.isWellFormed())
        {
            std::cerr << "cna-studio: the shell produced no image at "
                      << options.shellPreviewWidth << "x" << options.shellPreviewHeight << "\n";
            return 4;
        }

        // `STUDIO-35080`. Before the file is written, not after: a blank capture must not leave a
        // picture behind for somebody to look at and believe.
        //
        // The windowed capture has had this since `STUDIO-04015`; the *preview* -- the only visual
        // harness this project has without a GPU, and the one every golden image comes from -- did
        // not. It could rasterise a frame of nothing, write a perfectly valid PNG and exit zero,
        // which is the same defect one harness down and is worse there, because it is the harness
        // that runs on every commit.
        if (options.screenshotMinColors > 0)
        {
            const std::size_t colors = CNA::Studio::countDistinctColors(
                image, static_cast<std::size_t>(options.screenshotMinColors));
            if (colors < static_cast<std::size_t>(options.screenshotMinColors))
            {
                std::cerr << "cna-studio: the shell preview holds only " << colors
                          << " distinct colours, and " << options.screenshotMinColors
                          << " were required -- the frame was described but nothing was drawn in "
                             "it. No file was written.\n";
                return 4;
            }
        }

        // No path is what `--headless` asks for (STUDIO-07049): a frame drawn and checked, same as
        // above, but with nothing to photograph it for -- the smoke test wants a clean exit, not a
        // file.
        if (!options.shellPreviewPath.empty())
        {
            if (!CNA::Studio::writeImageAsPng(image, options.shellPreviewPath))
            {
                std::cerr << "cna-studio: could not write '" << options.shellPreviewPath << "'\n";
                return 4;
            }

            const CNA::Studio::UiDrawData& data = shell.drawData();
            std::size_t vertices = 0;
            std::size_t commands = 0;
            for (const CNA::Studio::UiDrawList& list : data.lists)
            {
                vertices += list.vertices.size();
                commands += list.commands.size();
            }

            std::cout << "cna-studio: shell preview " << image.width << "x" << image.height
                      << ", theme '" << theme.name() << "', scale " << theme.scale()
                      << ", " << commands << " draw calls, " << vertices << " vertices, "
                      << shell.frame().interactionCount() << " interactive widgets, cursor "
                      << CNA::Studio::studioCursorName(shell.cursor())
                      << " -> " << options.shellPreviewPath << "\n";
        }

        if (shell.frame().phaseViolations() > 0)
        {
            // The frame refused an operation somewhere. That is a Studio defect rather than a bad
            // command line, and it must not produce a picture that looks fine.
            for (const std::string& violation : shell.frame().phaseViolationLog())
            {
                std::cerr << "cna-studio: frame phase violation: " << violation << "\n";
            }
            return 5;
        }
        return 0;
    }

    /**
     * @brief Renders the native Studio shell to a PNG and reports what it produced.
     *
     * Headless by construction: the shell's geometry is CNA-free and is rasterised on the CPU, so
     * this works on a build machine with no GPU and no display -- the same property that lets the
     * shell have screenshot tests before graphical CI exists.
     *
     * @param options Parsed command line.
     * @return Process exit code.
     */
    /**
     * @brief One benchmark scenario: a name, a setup, and what each frame feeds the shell.
     *
     * `STUDIO-04028`. A scenario is a *shape of frame* rather than a screenshot: the question is
     * what a real Studio frame costs a render backend, and the answers differ by an order of
     * magnitude between an idle shell and a scrolling outliner over a large scene.
     */
    struct UiBenchmarkScenario
    {
        std::string name;
        std::string what;

        /** @brief Which panel this scenario is about. Raised before the timed frames, and the
         *         run refuses if it cannot be: a scenario that quietly measured whichever panel
         *         happened to be on top would be a number about the default layout. */
        std::string panel;

        /** @brief Arranges the document before the timed frames. */
        std::function<void(CNA::Studio::StudioShell&, CNA::Studio::StudioContext&)> setUp;

        /**
         * @brief Adjusts the input for frame @p index, so a scenario can type, scroll or resize.
         * @return The input for that frame.
         */
        std::function<CNA::Studio::UiInputState(CNA::Studio::UiInputState, int)> driveFrame;
    };

    /** @brief What one scenario measured. */
    struct UiBenchmarkRow
    {
        std::string name;
        std::string what;
        int frames = 0;
        CNA::Studio::StudioUiFrameCost total;
        double medianMicroseconds = 0.0;
        double minMicroseconds = 0.0;
    };

    /** @brief Adds @p count entities to @p scene under a shallow hierarchy. */
    void fillScene(CNA::Studio::SceneDocument& scene, int count)
    {
        // Cleared first, so `outliner-2000` holds two thousand entities rather than two thousand
        // and whatever the starting document had. The benchmark now opens the same document the
        // shell opens -- a scene with a camera in it -- and a scenario whose name is a count has
        // to be able to be read as one.
        scene.clear();

        CNA::Studio::Uuid parent;
        for (int i = 0; i < count; ++i)
        {
            CNA::Studio::StudioEntity entity{CNA::Studio::Uuid::generate(),
                                             "Entity " + std::to_string(i)};
            // Every eighth entity starts a new branch, so the outliner has real depth and real
            // indent guides rather than one flat list -- which is cheaper to draw and would make
            // the number flattering.
            if (i % 8 != 0) { entity.setParentId(parent); }
            const CNA::Studio::Uuid id = entity.getId();
            scene.addEntity(std::move(entity));
            if (i % 8 == 0) { parent = id; }
        }
    }

    /** @brief Adds @p count assets to @p assets, spread across the kinds the grid has icons for. */
    void fillAssets(CNA::Studio::AssetDatabase& assets, int count)
    {
        static const char* const kKinds[] = {".png", ".ogg", ".gltf", ".cnascene", ".cnaprefab",
                                             ".ttf", ".frag", ".txt"};
        for (int i = 0; i < count; ++i)
        {
            CNA::Studio::AssetRecord record;
            record.id = CNA::Studio::Uuid::generate();
            // Across folders, because the grid's breadcrumb and its per-folder culling are both
            // part of what a large content browser costs and a flat directory exercises neither.
            record.sourcePath = "Content/Folder" + std::to_string(i % 12) + "/asset"
                              + std::to_string(i) + kKinds[static_cast<std::size_t>(i) % 8];
            record.type = CNA::Studio::AssetDatabase::guessTypeFromExtension(record.sourcePath);
            record.importerId = CNA::Studio::AssetDatabase::defaultImporterFor(record.type);
            (void)assets.add(std::move(record));
        }
    }

    /** @brief The scenarios, in the order they are reported. */
    std::vector<UiBenchmarkScenario> uiBenchmarkScenarios(const CNA::Studio::StudioOptions& options)
    {
        using CNA::Studio::StudioContext;
        using CNA::Studio::StudioShell;
        using CNA::Studio::UiInputState;

        std::vector<UiBenchmarkScenario> scenarios;

        scenarios.push_back(UiBenchmarkScenario{
            "baseline", "the shell as it opens -- the floor every other row is read against",
            "", [](StudioShell&, StudioContext&) {}, {}});
        scenarios.push_back(UiBenchmarkScenario{
            "outliner-2000", "a 2000-entity scene with the World Outliner raised",
            "outliner",
            [](StudioShell&, StudioContext& context) { fillScene(context.getScene(), 2000); },
            {}});

        scenarios.push_back(UiBenchmarkScenario{
            "outliner-scrolling", "the same scene, scrolled a notch every frame",
            "outliner",
            [](StudioShell&, StudioContext& context) { fillScene(context.getScene(), 2000); },
            [](UiInputState input, int frame) {
                // Over the outliner, which is where the wheel has to be for the scroll to land.
                input.mouseX = 160.0f;
                input.mouseY = 300.0f;
                input.mouseInWindow = true;
                input.wheelY = (frame % 40 < 20) ? -1.0f : 1.0f;
                return input;
            }});

        scenarios.push_back(UiBenchmarkScenario{
            "content-grid", "1500 assets in the Content Browser's card grid",
            "content",
            [](StudioShell&, StudioContext& context) { fillAssets(context.getAssets(), 1500); },
            {}});

        scenarios.push_back(UiBenchmarkScenario{
            "details-components", "an entity carrying eight components, in the Details panel",
            "details",
            [](StudioShell&, StudioContext& context) {
                CNA::Studio::StudioEntity entity{CNA::Studio::Uuid::generate(), "Heavy"};
                for (const char* kind : {"Transform", "SpriteRenderer", "Camera", "AudioSource",
                                         "Rigidbody", "Collider", "Light", "Script"})
                {
                    CNA::Studio::StudioComponent component{kind};
                    component.setProperty("enabled", CNA::Studio::PropertyValue{true});
                    component.setProperty("name", CNA::Studio::PropertyValue{std::string{kind}});
                    component.setProperty("weight", CNA::Studio::PropertyValue{1.5f});
                    entity.addComponent(std::move(component));
                }
                const CNA::Studio::Uuid id = entity.getId();
                context.getScene().addEntity(std::move(entity));
                context.select(id);
            },
            {}});

        scenarios.push_back(UiBenchmarkScenario{
            "keystrokes", "a character a frame with the Output Log raised and nothing focused "
                          "-- the routing cost, which every frame of real typing also pays",
            "output", [](StudioShell&, StudioContext&) {},
            [](UiInputState input, int frame) {
                input.characters.push_back(
                    static_cast<char16_t>(u'a' + static_cast<char16_t>(frame % 26)));
                return input;
            }});

        scenarios.push_back(UiBenchmarkScenario{
            "atlas-growth", "text drawn in glyphs the atlas has not rasterised yet",
            "outliner",
            [](StudioShell&, StudioContext& context) {
                // Latin, Greek and Cyrillic: the three scripts the shipped faces actually carry,
                // so this exercises atlas growth rather than the replacement box.
                static const char* const kScripts[] = {
                    "ABCDEFGHIJKLMNOPQRSTUVWXYZ", "abcdefghijklmnopqrstuvwxyz",
                    "\xce\x91\xce\x92\xce\x93\xce\x94\xce\x95\xce\x96\xce\x97\xce\x98",
                    "\xd0\x90\xd0\x91\xd0\x92\xd0\x93\xd0\x94\xd0\x95\xd0\x96\xd0\x97",
                    "0123456789!@#$%^&*()[]{}<>?/\\|~`"};
                for (int i = 0; i < 120; ++i)
                {
                    CNA::Studio::StudioEntity entity{
                        CNA::Studio::Uuid::generate(),
                        std::string{kScripts[static_cast<std::size_t>(i) % 5]} + std::to_string(i)};
                    context.getScene().addEntity(std::move(entity));
                }
            },
            {}});

        scenarios.push_back(UiBenchmarkScenario{
            "resize", "the window changing size every frame, which relayouts everything",
            "outliner",
            [](StudioShell&, StudioContext& context) { fillScene(context.getScene(), 300); },
            [](UiInputState input, int frame) {
                // A sweep rather than an alternation between two sizes, which any cache keyed on
                // the last size would answer for free.
                input.displayWidth = 1280.0f + static_cast<float>(frame % 64) * 10.0f;
                input.displayHeight = 720.0f + static_cast<float>(frame % 48) * 8.0f;
                return input;
            }});

        if (options.uiBenchmark.empty() || options.uiBenchmark == "all") { return scenarios; }

        std::vector<UiBenchmarkScenario> selected;
        for (UiBenchmarkScenario& scenario : scenarios)
        {
            if (scenario.name.find(options.uiBenchmark) != std::string::npos)
            {
                selected.push_back(std::move(scenario));
            }
        }
        return selected;
    }

    /**
     * @brief Runs the UI render benchmark and prints one row per scenario.
     *
     * `STUDIO-04028`. Needs no CNA, no GPU and no window, which is the point: a benchmark that
     * only runs in the expensive CNA job is a benchmark nobody runs. What it measures is what each
     * backend *asks the device to do* rather than how long the device takes -- see
     * `StudioUiBenchmark.hpp` for why that is the right unit for the question `STUDIO-04027` asks.
     *
     * @param options Parsed command line.
     * @return Process exit code: 0 measured; 2 the selection matched no scenario.
     */
    int runUiBenchmark(const CNA::Studio::StudioOptions& options)
    {
        const std::vector<UiBenchmarkScenario> scenarios = uiBenchmarkScenarios(options);
        if (scenarios.empty())
        {
            std::cerr << "cna-studio: --ui-benchmark=" << options.uiBenchmark
                      << " matched no scenario.\n";
            return 2;
        }

        std::vector<UiBenchmarkRow> rows;
        for (const UiBenchmarkScenario& scenario : scenarios)
        {
            // A fresh shell per scenario. Sharing one would carry the previous scenario's atlas,
            // retained widget state and scroll offsets into the next, and the atlas alone would
            // make whichever scenario ran second look free.
            CNA::Studio::StudioTheme theme = CNA::Studio::StudioTheme::dark();
            CNA::Studio::StudioShell shell{theme};
            CNA::Studio::StudioContext context;
            CNA::Studio::StudioLog log;
            context.setLogSink([&log](CNA::Studio::LogSeverity severity, const std::string& message) {
                log.append(severity, message);
            });

            const CNA::Studio::StudioStartupDocument opened =
                CNA::Studio::openStudioStartupDocument(context, options.projectPath,
                                                       options.scenePath);
            if (!opened.succeeded())
            {
                std::cerr << "cna-studio: " << opened.error << "\n";
                return 2;
            }

            (void)CNA::Studio::bindStudioShellActions(shell, context, log);
            CNA::Studio::StudioShellPanels panels{shell, context, log};
            panels.poll(0.0);

            if (scenario.setUp) { scenario.setUp(shell, context); }

            // Raised, and refused loudly when it cannot be. A scenario named after a panel that
            // silently measured whichever one the default layout puts on top would be a number
            // about the layout -- and it would keep being one after the panel was renamed. This
            // project has been here before: `--shell-invoke` read the return of `find()` and threw
            // away the return of `invoke()`, and a whole session went looking for a tool overlay
            // that had simply never been armed.
            if (!scenario.panel.empty() && !shell.activatePanel(scenario.panel))
            {
                std::cerr << "cna-studio: scenario '" << scenario.name << "' wants the '"
                          << scenario.panel << "' panel and this shell has no such panel open.\n";
                return 2;
            }

            CNA::Studio::UiInputState base;
            base.displayWidth = static_cast<float>(options.shellPreviewWidth);
            base.displayHeight = static_cast<float>(options.shellPreviewHeight);
            base.mouseInWindow = false;
            base.mouseX = -1.0f;
            base.mouseY = -1.0f;

            // One untimed frame first. The first frame of any shell rasterises the whole font
            // atlas and builds every retained-state entry, so timing it would measure start-up and
            // report it as the steady-state cost of drawing a panel.
            shell.renderFrame(base);

            UiBenchmarkRow row;
            row.name = scenario.name;
            row.what = scenario.what;
            row.frames = options.uiBenchmarkFrames;

            std::vector<double> samples;
            samples.reserve(static_cast<std::size_t>(options.uiBenchmarkFrames));

            for (int frame = 0; frame < options.uiBenchmarkFrames; ++frame)
            {
                CNA::Studio::UiInputState input = base;
                if (scenario.driveFrame) { input = scenario.driveFrame(input, frame); }

                const auto start = std::chrono::steady_clock::now();
                shell.renderFrame(input);
                const auto finish = std::chrono::steady_clock::now();

                samples.push_back(
                    std::chrono::duration<double, std::micro>(finish - start).count());
                CNA::Studio::studioUiAccumulateCost(
                    row.total, CNA::Studio::studioUiFrameCost(shell.drawData()));
            }

            // Median rather than mean, and the minimum beside it. A scheduler preemption in one
            // frame moves a mean and cannot move a median, and the minimum is the closest thing to
            // "what this costs when nothing else is happening" that a shared machine can report.
            std::sort(samples.begin(), samples.end());
            row.medianMicroseconds = samples[samples.size() / 2];
            row.minMicroseconds = samples.front();
            rows.push_back(std::move(row));
        }

        std::cout << "cna-studio: UI render benchmark (STUDIO-04028), "
                  << options.uiBenchmarkFrames << " frames per scenario at "
                  << options.shellPreviewWidth << "x" << options.shellPreviewHeight << "\n"
                  << "Per frame. 'classic KB' and 'modern KB' are the geometry bytes each UI "
                     "render backend\nputs on the bus for the same frame -- what is submitted, "
                     "not how long a GPU takes.\n\n";

        std::cout << std::left << std::setw(20) << "scenario" << std::right
                  << std::setw(10) << "us(med)" << std::setw(10) << "us(min)"
                  << std::setw(10) << "draws" << std::setw(10) << "verts"
                  << std::setw(9) << "tex" << std::setw(9) << "clip"
                  << std::setw(12) << "classic KB" << std::setw(11) << "modern KB"
                  << std::setw(8) << "ratio" << "\n";
        std::cout << std::string(109, '-') << "\n";

        for (const UiBenchmarkRow& row : rows)
        {
            const double frames = static_cast<double>(row.frames);
            const double classicKb = static_cast<double>(row.total.classicGpuBytes) / frames / 1024.0;
            const double modernKb = static_cast<double>(row.total.modernGpuBytes) / frames / 1024.0;

            std::cout << std::left << std::setw(20) << row.name << std::right
                      << std::setw(10) << std::fixed << std::setprecision(1) << row.medianMicroseconds
                      << std::setw(10) << row.minMicroseconds
                      << std::setw(10) << static_cast<double>(row.total.drawCalls) / frames
                      << std::setw(10) << static_cast<double>(row.total.vertices) / frames
                      << std::setw(9) << static_cast<double>(row.total.textureChanges) / frames
                      << std::setw(9) << static_cast<double>(row.total.clipChanges) / frames
                      << std::setw(12) << classicKb
                      << std::setw(11) << modernKb
                      << std::setw(7) << std::setprecision(2)
                      << (modernKb > 0.0 ? classicKb / modernKb : 0.0) << "x"
                      << "\n";
        }

        std::cout << "\n";
        for (const UiBenchmarkRow& row : rows)
        {
            std::cout << "  " << row.name << " -- " << row.what << "\n";
            if (row.total.texturesCreated + row.total.texturesUpdated > 0)
            {
                std::cout << "      " << row.total.texturesCreated << " texture creations, "
                          << row.total.texturesUpdated << " updates, "
                          << row.total.textureBytesUploaded / 1024 << " KB of pixels over the run\n";
            }
            // What Studio *hands CNA* rather than what reaches the bus: CNA repacks each 56-byte
            // VertexPositionColorTexture into a 24-byte stream before uploading, so the copy
            // Studio pays for is more than twice what the GPU sees. Both backends pay it and the
            // ratio between them is the same, which is why the table reports the bus figure and
            // this reports the other one rather than the table carrying four columns.
            const double perFrame = static_cast<double>(row.frames);
            std::cout << "      handed to CNA: "
                      << static_cast<double>(row.total.classicSubmittedBytes) / perFrame / 1024.0
                      << " KB classic, "
                      << static_cast<double>(row.total.modernSubmittedBytes) / perFrame / 1024.0
                      << " KB modern\n";
        }
        return 0;
    }

    int renderShellPreview(const CNA::Studio::StudioOptions& options)
    {
        // Checked before anything else opens: comparing means decoding the captures, decoding
        // needs a graphics device, and this function's whole reason to exist is running with none.
        // `--headless` reaches here now too (STUDIO-07049), so the check moved up from the console
        // UI's own path rather than being duplicated at both of this function's entry points.
        if (options.compareBackends)
        {
            std::cerr << "cna-studio: --compare-backends needs a graphics device, so it cannot run "
                         "headless or on the null UI. Run it on a build with -DCNA_STUDIO_WITH_CNA=ON "
                         "and a display.\n";
            return 3;
        }

        CNA::Studio::StudioTheme theme = options.shellPreviewTheme == "light"
            ? CNA::Studio::StudioTheme::light()
            : CNA::Studio::StudioTheme::dark();
        // Zero means the flag was not given, and a preview has no user preferences to fall back
        // on -- so it takes the same 100% the flag used to default to.
        theme.setScale(options.shellPreviewScale > 0.0
                           ? static_cast<float>(options.shellPreviewScale) : 1.0f);

        CNA::Studio::StudioShell shell{theme};

        // The ported panels, bound exactly as the real editor binds them. Without this the
        // preview photographs five empty rectangles where the panels are -- and the preview is
        // the only visual test the project has on a machine with no GPU and no display.
        CNA::Studio::StudioContext context;
        CNA::Studio::StudioLog log;
        context.setLogSink([&log](CNA::Studio::LogSeverity severity, const std::string& message) {
            log.append(severity, message);
        });

        // The same document a Studio with no project opens (STUDIO-07047): a scene with a camera
        // in it, because one with no camera renders nothing and reads as a broken editor. The
        // preview photographs what the editor shows, so it opens what the editor opens -- through
        // the same function the editor calls, `--scene` included (STUDIO-07053).
        const CNA::Studio::StudioStartupDocument opened =
            CNA::Studio::openStudioStartupDocument(context, options.projectPath,
                                                   options.scenePath);
        if (!opened.succeeded())
        {
            std::cerr << "cna-studio: " << opened.error << "\n";
            return 2;
        }

        // The same two bindings the real editor makes, in the same order: the document commands
        // and then the panels. A preview that bound one and not the other would photograph a File
        // menu whose Save is greyed out, which is a picture of this function rather than of Studio.
        (void)CNA::Studio::bindStudioShellActions(shell, context, log);

        CNA::Studio::StudioShellPanels panels{shell, context, log};

        // The viewport's own camera, bound even with no graphics device: without it the viewport's
        // commands -- `--view=3d` among them -- are found and refused rather than run, which is
        // exactly what left `--view=3d` and `--orbit` reaching only the prototype (STUDIO-07049).
        // Bare value objects rather than a real scene viewport, because a headless preview has no
        // device to build one from and none of these flags need it to draw anything -- they need
        // only the camera state a real session would also be changing.
        CNA::Studio::StudioCamera2D camera2D;
        CNA::Studio::StudioCamera3D camera3D;
        panels.setViewportServices(
            camera2D, camera3D,
            [](const CNA::Studio::Uuid&) { return CNA::Studio::StudioVector2{32.0f, 32.0f}; });

        // And the same discovery, for the same reason: what Play can launch and what the Backends
        // panel can compare are decided by which cna-player binaries are beside this executable,
        // so a preview that skipped it would photograph a Studio poorer than the one being run.
        if (!options.executablePath.empty())
        {
            panels.setPlayerBuilds(CNA::Studio::discoverPlayerBuilds(
                std::filesystem::path{options.executablePath}.parent_path().generic_string()));
        }

        // Through the panels, which is what fills the status bar in the real editor -- a preview
        // that composed its own status line would photograph a bar this Studio never draws.
        panels.poll(0.0);
        shell.status().renderer = "none, headless preview";
        if (options.projectPath.empty())
        {
            // Said once, so a capture of the empty shell is legible rather than looking like the
            // panels failed to draw.
            log.append(CNA::Studio::LogSeverity::Info,
                       "Shell preview. Pass --project=PATH to fill the panels from a real project.");
        }

        // `--select=NAME` puts something in the Details panel. By name, because that is what a
        // person types; the document looks entities up by id, so the walk is here.
        if (!options.selectEntity.empty())
        {
            const CNA::Studio::StudioEntity* wanted = nullptr;
            for (const CNA::Studio::StudioEntity& candidate : context.getScene().getEntities())
            {
                if (candidate.getName() == options.selectEntity) { wanted = &candidate; break; }
            }
            if (wanted == nullptr)
            {
                std::cerr << "cna-studio: no entity called '" << options.selectEntity
                          << "' in this scene.\n";
                return 2;
            }
            context.select(wanted->getId());
        }

        // `--select-asset=PATH` puts an asset in the Details panel, which is the only way a still
        // capture can reach the asset inspector: it is opened by clicking a Content Browser row.
        if (!options.selectAsset.empty())
        {
            const CNA::Studio::AssetRecord* wanted =
                context.getAssets().findByPath(options.selectAsset);
            if (wanted == nullptr)
            {
                std::cerr << "cna-studio: no asset at '" << options.selectAsset
                          << "' in this project.\n";
                return 2;
            }
            context.selectAsset(wanted->id);
        }

        // What this build actually is, rather than what the UI core can say for itself.
        shell.setAboutLines({std::string{"CNA Studio "} + CNA_STUDIO_VERSION,
                             "An editor for CNA games.",
                             "UI: Studio native, headless preview.",
                             "Renderer: " + shell.status().renderer});

        // `--shell-float=IDS` undocks panels, because a floating window is arranged by dragging
        // and a still capture cannot drag. Before --panel, so a floated panel can also be raised.
        for (std::size_t start = 0; start < options.shellPreviewFloat.size();)
        {
            const std::size_t comma = options.shellPreviewFloat.find(',', start);
            const std::string id = options.shellPreviewFloat.substr(
                start, comma == std::string::npos ? std::string::npos : comma - start);
            start = comma == std::string::npos ? options.shellPreviewFloat.size() : comma + 1;
            if (id.empty()) { continue; }
            if (!shell.floatPanel(id))
            {
                std::cerr << "cna-studio: no panel called '" << id << "' is open.\n";
                return 2;
            }
        }

        // `--panel=ID` raises a panel, so a capture can show one that shares a tab bar. The same
        // flag the real editor uses, rather than a preview-only spelling nobody would remember.
        if (!options.focusPanel.empty() && !shell.activatePanel(options.focusPanel))
        {
            std::cerr << "cna-studio: no panel called '" << options.focusPanel << "' is open.\n";
            return 2;
        }

        // "Window>Panels" rather than just "Window": a submenu is reached by hovering, and a
        // capture harness that could only open a top-level menu could never photograph one.
        std::vector<std::string> menuPath;
        if (!options.shellPreviewOpenMenu.empty())
        {
            std::string remaining = options.shellPreviewOpenMenu;
            for (std::size_t cut = remaining.find('>'); ; cut = remaining.find('>'))
            {
                if (cut == std::string::npos) { menuPath.push_back(remaining); break; }
                menuPath.push_back(remaining.substr(0, cut));
                remaining = remaining.substr(cut + 1);
            }

            const auto& menus = shell.menus();
            int index = -1;
            for (std::size_t i = 0; i < menus.size(); ++i)
            {
                if (menus[i].title == menuPath.front()) { index = static_cast<int>(i); }
            }
            if (index < 0)
            {
                std::cerr << "cna-studio: no menu titled '" << menuPath.front()
                          << "'. This shell has:";
                for (const CNA::Studio::StudioMenuDefinition& menu : menus)
                {
                    std::cerr << " " << menu.title;
                }
                std::cerr << "\n";
                return 2;
            }
            shell.setOpenMenu(index);
        }

        // `--shell-notify=LIST` raises toasts. A notification is what something *finishing in the
        // background* looks like, which is the one state a still capture cannot reach by pressing
        // anything, so the preview needs a way to say "pretend the build just failed".
        for (std::size_t start = 0; start < options.shellPreviewNotify.size();)
        {
            const std::size_t comma = options.shellPreviewNotify.find(',', start);
            std::string entry = options.shellPreviewNotify.substr(
                start, comma == std::string::npos ? std::string::npos : comma - start);
            start = comma == std::string::npos ? options.shellPreviewNotify.size() : comma + 1;
            if (entry.empty()) { continue; }

            // Bar-separated fields so a detail may contain the punctuation a sentence contains.
            std::vector<std::string> fields;
            for (std::size_t at = 0;;)
            {
                const std::size_t bar = entry.find('|', at);
                if (bar == std::string::npos) { fields.push_back(entry.substr(at)); break; }
                fields.push_back(entry.substr(at, bar - at));
                at = bar + 1;
            }

            if (fields.size() < 2)
            {
                std::cerr << "cna-studio: --shell-notify wants "
                             "SEVERITY|TITLE[|DETAIL[|ACTION]], got '" << entry << "'\n";
                return 2;
            }

            CNA::Studio::StudioNotification notification;
            notification.title = fields[1];
            if (fields.size() > 2) { notification.detail = fields[2]; }
            if (fields.size() > 3) { notification.actionId = fields[3]; }

            if (fields[0] == "info") { notification.severity = CNA::Studio::StudioNotificationSeverity::Info; }
            else if (fields[0] == "success") { notification.severity = CNA::Studio::StudioNotificationSeverity::Success; }
            else if (fields[0] == "warning") { notification.severity = CNA::Studio::StudioNotificationSeverity::Warning; }
            else if (fields[0] == "error") { notification.severity = CNA::Studio::StudioNotificationSeverity::Error; }
            else
            {
                std::cerr << "cna-studio: --shell-notify severity must be info, success, warning "
                             "or error, got '" << fields[0] << "'\n";
                return 2;
            }

            if (!notification.actionId.empty()
                && shell.actions().find(notification.actionId) == nullptr)
            {
                std::cerr << "cna-studio: --shell-notify names no command called '"
                          << notification.actionId << "'.\n";
                return 2;
            }

            shell.notifications().post(std::move(notification));
        }

        // `--view=3d` switches the viewport before anything else touches it (STUDIO-07049) -- the
        // same command a press of 3 or `--shell-invoke=studio.view.3d` runs, just asked for by its
        // own flag.
        if (options.threeDimensionalView)
        {
            shell.invoke("studio.view.3d");
            if (!shell.refusedActions().empty())
            {
                std::cerr << "cna-studio: --view=3d did nothing -- "
                          << shell.refusedActions().front() << ".\n";
                return 2;
            }
        }

        // After the switch, not before: the switch frames the scene once on its own, and setting
        // the angles first would have that framing overwrite them straight back out.
        if (options.orbitDegrees)
        {
            constexpr float kDegreesToRadians = 3.14159265358979323846f / 180.0f;
            camera3D.setYaw(options.orbitDegrees->x * kDegreesToRadians);
            camera3D.setPitch(options.orbitDegrees->y * kDegreesToRadians);
        }

        // `--shell-invoke=ID` runs a command, so a capture can show what it put on the screen --
        // a modal dialog above all, which is reached by a menu item and answered by a keystroke.
        if (!options.shellPreviewInvoke.empty())
        {
            if (shell.actions().find(options.shellPreviewInvoke) == nullptr)
            {
                std::cerr << "cna-studio: no command called '" << options.shellPreviewInvoke
                          << "'.\n";
                return 2;
            }
            shell.invoke(options.shellPreviewInvoke);

            // Existing is not the same as doing something. Most of the registry is declared with
            // no handler and given one by whatever binds it, so a command a headless build never
            // binds -- every viewport command, with no camera to drive -- is found, invoked, and
            // quietly does nothing. A screenshot tool that answers a request with the picture it
            // would have produced anyway is worse than one that refuses: the capture looks like
            // the feature failing. The shell already records the refusal; this reads it.
            if (!shell.refusedActions().empty())
            {
                std::cerr << "cna-studio: --shell-invoke did nothing -- "
                          << shell.refusedActions().front() << ".\n";
                return 2;
            }
        }

        CNA::Studio::UiInputState input;
        input.displayWidth = static_cast<float>(options.shellPreviewWidth);
        input.displayHeight = static_cast<float>(options.shellPreviewHeight);
        input.mouseX = static_cast<float>(options.shellPreviewPointerX);
        input.mouseY = static_cast<float>(options.shellPreviewPointerY);
        input.mouseInWindow = options.shellPreviewPointerX >= 0.0
                           && options.shellPreviewPointerY >= 0.0;
        input.setMouseDown(CNA::Studio::UiMouseButton::Left, options.shellPreviewMouseDown);

        // Two frames, not one. The first establishes the input snapshot the second diffs against,
        // and hover resolved on a frame with no predecessor is hover nobody has moved onto yet --
        // so a one-frame preview would capture every control at rest however the pointer is placed.
        // The atlas is requested on the frame it is rasterised and never again, so a table built
        // from the last frame alone would have no font and every glyph would draw as a solid
        // rectangle. Kept across the frames instead, the way a real renderer keeps an upload.
        CNA::Studio::UiTextureTable textures;

        // `--shell-panel-only=ID` draws that panel and nothing else, filling the window. A panel
        // taller than the dock it lives in is unreviewable in a shell capture -- the strip at the
        // bottom shows four rows of the Preferences page -- and this is what
        // `StudioShell::describePanelContent` exists for.
        if (!options.shellPreviewPanelOnly.empty())
        {
            if (!shell.hasPanelContent(options.shellPreviewPanelOnly))
            {
                std::cerr << "cna-studio: no panel called '" << options.shellPreviewPanelOnly
                          << "' has content to draw.\n";
                return 2;
            }

            const CNA::Studio::UiRect whole{0.0f, 0.0f, input.displayWidth, input.displayHeight};
            for (int pass = 0; pass < 2; ++pass)
            {
                CNA::Studio::runStudioFrame(shell.frame(), input, [&](CNA::Studio::StudioFrame& f) {
                    if (f.isDrawPass())
                    {
                        f.drawList().fillRect(
                            whole, f.theme().color(CNA::Studio::StudioColorRole::PanelBackground));
                    }
                    (void)shell.describePanelContent(options.shellPreviewPanelOnly, f, whole);
                });
                textures.apply(shell.drawData());
            }

            return writeShellPreview(options, theme, shell, textures);
        }

        shell.renderFrame(input);
        textures.apply(shell.drawData());
        shell.renderFrame(input);

        if (options.shellPreviewRightClick)
        {
            // A press and a release. The menu opens on the press; holding the button through the
            // capture would photograph a gesture nobody makes.
            CNA::Studio::UiInputState pressed = input;
            pressed.setMouseDown(CNA::Studio::UiMouseButton::Right, true);
            shell.renderFrame(pressed);
            textures.apply(shell.drawData());
            shell.renderFrame(input);
            textures.apply(shell.drawData());

            if (!shell.isContextMenuOpen())
            {
                std::cerr << "cna-studio: no context menu appeared. --shell-right-click needs "
                             "--shell-pointer over something that offers one.\n";
                return 2;
            }
        }

        // One level per step, each needing a frame to lay out before the next can be found in it.
        for (std::size_t depth = 1; depth < menuPath.size(); ++depth)
        {
            const std::size_t level = depth - 1;
            int row = -1;
            for (std::size_t i = 0; i < shell.menuRowCount(level); ++i)
            {
                if (shell.menuRowActionId(level, i) == menuPath[depth]) { row = static_cast<int>(i); }
            }
            if (row < 0)
            {
                std::cerr << "cna-studio: no submenu named '" << menuPath[depth]
                          << "'. That menu has:";
                for (std::size_t i = 0; i < shell.menuRowCount(level); ++i)
                {
                    const std::string_view name = shell.menuRowActionId(level, i);
                    if (!name.empty() && name != CNA::Studio::kStudioMenuSeparatorId)
                    {
                        std::cerr << " " << name;
                    }
                }
                std::cerr << "\n";
                return 2;
            }
            shell.openSubmenu(level, row);
            shell.renderFrame(input);
            textures.apply(shell.drawData());
        }

        if (options.shellPreviewTooltip)
        {
            // Frames, not a flag. A tooltip appears after the pointer has *rested*, so the only
            // way to capture one is to let the clock run -- reaching into the shell to set a
            // "tooltip showing" field would capture a state no amount of hovering produces.
            // The +3 covers the frame that establishes the hover (the clock only starts once the
            // pointer has been seen over the control) and leaves a frame in hand, so a delay that
            // divides exactly into sixtieths is not decided by a floating-point comparison.
            const int frames = static_cast<int>(
                std::ceil(shell.frame().tooltipDelay() * 60.0f)) + 3;
            for (int i = 0; i < frames; ++i)
            {
                shell.renderFrame(input);
                textures.apply(shell.drawData());
            }

            if (!shell.frame().tooltip().visible())
            {
                std::cerr << "cna-studio: no tooltip appeared. --shell-tooltip needs "
                             "--shell-pointer over a control that offers one.\n";
                return 2;
            }
        }

        if (!options.shellPreviewDragPanel.empty())
        {
            // A drag is the one interaction state a single input snapshot cannot express: it needs
            // a press in one place and a pointer in another. Synthesised here as the gesture a
            // person makes -- press the tab, then move -- rather than by reaching into the shell
            // and setting a drag field, because a capture of a state no gesture can produce is a
            // capture of something that does not happen.
            // A drag needs somewhere to drag *to*. Without a pointer in the window the gesture
            // starts and resolves to no drop target, so the capture is a picture of a drag with
            // nothing to show -- which would pass for a picture of the shell at rest.
            if (!input.mouseInWindow)
            {
                std::cerr << "cna-studio: --shell-drag needs --shell-pointer=X,Y inside the "
                             "window: that is where the panel is being dragged to.\n";
                return 2;
            }

            const CNA::Studio::UiRect tab = shell.panelTabBounds(options.shellPreviewDragPanel);
            if (tab.isEmpty())
            {
                std::cerr << "cna-studio: no open panel called '" << options.shellPreviewDragPanel
                          << "' to drag.\n";
                return 2;
            }

            CNA::Studio::UiInputState press = input;
            press.mouseX = tab.centerX();
            press.mouseY = tab.centerY();
            press.mouseInWindow = true;
            press.setMouseDown(CNA::Studio::UiMouseButton::Left, false);
            shell.renderFrame(press);

            press.setMouseDown(CNA::Studio::UiMouseButton::Left, true);
            shell.renderFrame(press);

            textures.apply(shell.drawData());

            CNA::Studio::UiInputState moved = press;
            moved.mouseX = input.mouseX;
            moved.mouseY = input.mouseY;
            shell.renderFrame(moved);
            textures.apply(shell.drawData());

            if (!shell.dockDrag().active())
            {
                std::cerr << "cna-studio: dragging '" << options.shellPreviewDragPanel
                          << "' started no drag. --shell-drag needs --shell-pointer somewhere "
                             "further than the drag threshold from the tab.\n";
                return 2;
            }
        }

        return writeShellPreview(options, theme, shell, textures);
    }

    /**
     * @brief Prints the Studio host capability contract.
     *
     * Capabilities, never renderer names: a renderer becomes eligible the moment it can do what
     * Studio needs, and no Studio source file changes when CNA adds one.
     */
    void printHostCapabilityContract()
    {
        std::cout << "CNA Studio host capability contract\n\n"
                     "Studio asks the live graphics device what it can do, by CNA capability name,\n"
                     "and never which renderer it is. A renderer becomes eligible to host Studio\n"
                     "the moment it reports these, with no change to Studio.\n\n";

        const auto severity = [](CNA::Studio::StudioRequirementSeverity value) {
            return value == CNA::Studio::StudioRequirementSeverity::Required ? "[required]   "
                                                                            : "[recommended]";
        };

        std::cout << "Capabilities:\n\n";
        for (const CNA::Studio::StudioHostFeatureRequirement& requirement :
             CNA::Studio::studioHostFeatureRequirements())
        {
            std::cout << "  " << severity(requirement.severity) << " " << requirement.feature;
            if (!requirement.restrictedIsEnough) { std::cout << "  (a restricted subset is not enough)"; }
            std::cout << "\n      " << requirement.reason << "\n\n";
        }

        std::cout << "Limits:\n\n";
        for (const CNA::Studio::StudioHostLimitRequirement& requirement :
             CNA::Studio::studioHostLimitRequirements())
        {
            std::cout << "  " << severity(requirement.severity) << " " << requirement.limit
                      << " >= " << requirement.minimum << "\n      " << requirement.reason << "\n\n";
        }

        std::cout << "An unclassified answer counts as unmet for a required capability. It means\n"
                     "the renderer has not audited it, which is not the same as no -- and is still\n"
                     "not a yes. A tool that starts and then cannot draw is worse than one that\n"
                     "refuses with a reason.\n";
    }

    void printBackends()
    {
        std::cout << "CNA graphics backends known to Studio:\n\n";
        for (const CNA::Studio::RendererInfo& backend : CNA::Studio::getKnownRenderers())
        {
            const char* support = "runtime-only  ";
            switch (backend.hostSupport)
            {
                case CNA::Studio::RendererHostSupport::StudioHost: support = "studio        "; break;
                case CNA::Studio::RendererHostSupport::PreviewOnly: support = "preview-only  "; break;
                case CNA::Studio::RendererHostSupport::RuntimeOnly: support = "runtime-only  "; break;
            }
            std::cout << "  " << support << backend.commandLineName << "  (" << backend.cnaIdentity << ")\n"
                      << "      " << backend.displayName << " -- " << backend.note << "\n";
        }
        std::cout << "\nThese are the backends a cna-player build can use. Studio's own backend\n"
                     "is fixed at compile time by CNA_GRAPHICS_RENDERER.\n";
    }
}

int main(int argc, char** argv)
{
    const CNA::Studio::StudioOptions options = CNA::Studio::StudioOptions::parse(argc, argv);

    if (options.hasError)
    {
        std::cerr << "cna-studio: " << options.errorMessage << "\n\n"
                  << CNA::Studio::StudioOptions::getUsage();
        return 2;
    }
    if (options.showHelp)
    {
        std::cout << CNA::Studio::StudioOptions::getUsage();
        return 0;
    }
    if (options.showVersion)
    {
        std::cout << "cna-studio " << CNA_STUDIO_VERSION << "\n";
        return 0;
    }
    if (options.listBackends)
    {
        printBackends();
        return 0;
    }
    if (options.hostCapabilities)
    {
        printHostCapabilityContract();
#if !defined(CNA_STUDIO_HAS_CNA)
        // Said plainly rather than left as an absence. The contract above is the whole answer this
        // build can give: evaluating it needs a real device, and this binary has no window host.
        std::cout << "\nThis build has no window host, so there is no device to evaluate the\n"
                     "contract against. Rebuild with -DCNA_STUDIO_WITH_CNA=ON for the live verdict.\n";
        return 0;
#endif
    }

    // Export needs no window, no toolkit and no graphics device -- and, more to the point,
    // the thing it produces must need no Studio either. Handled here, before anything that
    // would open one, so the code path a script exercises is the short one.
    if (!options.exportPath.empty())
    {
        if (options.projectPath.empty())
        {
            std::cerr << "cna-studio: --export needs --project=PATH: there is nothing to export "
                         "without a project.\n";
            return 3;
        }

        CNA::Studio::Project project;
        const CNA::Studio::ProjectLoadResult loaded = project.loadFromFile(
            options.projectPath, &CNA::Studio::getProjectFormatMigrator());
        for (const std::string& warning : loaded.warnings)
        {
            std::cerr << "cna-studio: warning: " << warning << "\n";
        }
        if (!loaded.succeeded)
        {
            std::cerr << "cna-studio: cannot open '" << options.projectPath
                      << "': " << loaded.errorMessage << "\n";
            return 4;
        }

        CNA::Studio::StudioExportRequest request;
        request.outputDirectory = options.exportPath;
        request.overwrite = options.exportOverwrite;

        const CNA::Studio::StudioExportResult result =
            CNA::Studio::exportStandaloneProject(project, request);

        for (const std::string& warning : result.warnings)
        {
            std::cerr << "cna-studio: warning: " << warning << "\n";
        }
        if (!result.succeeded())
        {
            std::cerr << "cna-studio: export failed: " << result.errorMessage << "\n";
            return 5;
        }

        std::cout << "cna-studio: exported " << result.writtenFiles.size() << " files to '"
                  << options.exportPath << "'\n"
                  << "Build it with:\n"
                  << "  cmake -S " << options.exportPath << " -B " << options.exportPath
                  << "/build -DCNA_ROOT=/path/to/cna\n"
                  << "  cmake --build " << options.exportPath << "/build\n";
        return 0;
    }

    // Before the UI selection below, because the preview needs no window, no toolkit and no
    // graphics device at all. `--headless` reaches the same function once nothing else asked for
    // a real window (STUDIO-07049): this is already the native shell's own headless drawing, and
    // unconditional on `CNA_STUDIO_HAS_CNA`, so it is where `--headless` has to land once the
    // prototype -- the console UI it used to mean -- is gone. Explicit `--ui=imgui --headless`
    // still means the console UI: that is the fallback this same run has by name
    // (`CnaStudioFallsBackToImGuiWhenAsked`), and it names something other than "studio" here.
    const bool nativeHeadless =
        options.headless && (options.uiBackend.empty() || options.uiBackend == "studio");
    if (!options.shellPreviewPath.empty() || nativeHeadless)
    {
        return renderShellPreview(options);
    }

    // And neither does the benchmark, for the same reason and to more purpose: it is what decides
    // whether `STUDIO-04027` deletes the classic UI render backend or keeps it (`STUDIO-04028`).
    if (!options.uiBenchmark.empty())
    {
        return runUiBenchmark(options);
    }

    // Which UI opens when the user asked for none (`STUDIO-06015`). 'studio' unconditionally now:
    // it is the only named UI this binary has left to default to (STUDIO-07030), and a build with
    // no CNA graphics device says so below exactly as it always has -- the alternative that error
    // used to name was Dear ImGui, and now there is none, but the shape of the answer is the same.
    const std::string uiBackend =
        !options.uiBackend.empty() ? options.uiBackend : std::string{"studio"};

#if defined(CNA_STUDIO_HAS_CNA)
    // The native Studio UI in a real window, through a real CNA renderer. Kept a separate entry
    // point from the ImGui host rather than a branch inside it: the two draw entirely different
    // things, and the migration ends by deleting one of them -- which is far easier when there is
    // one to delete rather than a branch to unpick.
    if (uiBackend == "studio")
    {
        // Checked before the window opens, not after the loop ends. The native shell has no
        // headless mode to fall back on: without a frame limit it runs until the user closes the
        // window, so a run asked for a screenshot it can never reach would hang rather than fail.
        if (!options.screenshotPath.empty() && options.frameLimit <= 0)
        {
            std::cerr << "cna-studio: --screenshot needs --frames. The native shell runs until the "
                         "window is closed, and the capture is taken on the last frame.\n";
            return 3;
        }

        CNA::Studio::CnaStudioShellHostOptions hostOptions;
        hostOptions.reportCapabilities = options.hostCapabilities;
        hostOptions.checkCapabilitiesOnly = options.hostCapabilities;
        hostOptions.frameLimit = options.frameLimit;
        hostOptions.screenshotPath = options.screenshotPath;
        hostOptions.screenshotMinColors = options.screenshotMinColors;
        hostOptions.uiScale = static_cast<float>(options.shellPreviewScale);  // 0 means "not given"

        hostOptions.theme = options.shellPreviewTheme;

        // "none" rather than an empty string for off, because an empty --workspace= reads as a
        // mistake and defaulting it to the user's real file would be the wrong guess: a test that
        // meant to isolate itself would silently write over the developer's layout.
        hostOptions.focusPanel = options.focusPanel;
        hostOptions.projectPath = options.projectPath;
        hostOptions.scenePath = options.scenePath;
        hostOptions.pluginDirectory = options.pluginDirectory;
        hostOptions.selectEntity = options.selectEntity;
        hostOptions.invokeAction = options.shellPreviewInvoke;
        if (options.windowWidth > 0 && options.windowHeight > 0)
        {
            hostOptions.windowWidth = options.windowWidth;
            hostOptions.windowHeight = options.windowHeight;
        }
        hostOptions.executablePath = options.executablePath;
        if (options.workspacePath == "none") { hostOptions.workspacePath.clear(); }
        else if (!options.workspacePath.empty()) { hostOptions.workspacePath = options.workspacePath; }
        else { hostOptions.workspacePath = CNA::Studio::StudioWorkspaceStore::defaultPath(); }

        const CNA::Studio::CnaStudioShellHostResult result =
            CNA::Studio::runStudioShellInWindow(hostOptions);

        if (!result.errorMessage.empty())
        {
            std::cerr << "cna-studio: " << result.errorMessage << "\n";
        }
        if (!options.screenshotPath.empty() && !result.screenshotWritten)
        {
            // Silent when the capture was refused for being blank: that reason has already been
            // printed, and guessing at a second one sends the reader looking for a fault that is
            // not there.
            if (!result.screenshotTooFlat)
            {
                std::cerr << "cna-studio: no screenshot was written to '" << options.screenshotPath
                          << "'. --screenshot needs --frames, and the renderer must support reading "
                             "back its own back buffer.\n";
            }
            return 4;
        }
        if (!result.layoutProblem.empty())
        {
            std::cerr << "cna-studio: " << result.layoutProblem << "\n";
        }
        if (!result.costModelMismatch.empty())
        {
            // A benchmark that reports a number nobody has checked against the thing it measures
            // is worse than no benchmark: it is a number people quote. STUDIO-04028's model runs
            // with no device, so this run -- which has one -- is where it is held to account.
            std::cerr << "cna-studio: the UI benchmark's cost model disagrees with the renderer -- "
                      << result.costModelMismatch << ". See STUDIO-04028.\n";
            return 6;
        }
        if (options.frameLimit > 0)
        {
            // A window that opens, loops and closes having issued zero draw calls looks identical
            // to a working one from the outside, so a smoke test needs numbers to assert on.
            std::cout << "cna-studio: native shell on " << result.renderer << ", " << result.frames
                      << " frames, " << result.displayWidth << "x" << result.displayHeight
                      << " display, " << result.drawCalls << " draw calls, " << result.triangles
                      << " triangles";
            if (!result.statusLeft.empty())
            {
                std::cout << ", status '" << result.statusLeft << "'";
            }
            if (result.outlinerRowsTotal > 0)
            {
                std::cout << ", outliner showing " << result.outlinerRowsDrawn << " of "
                          << result.outlinerRowsTotal << " entities";
            }
            if (result.contentRowsTotal > 0)
            {
                std::cout << ", content showing " << result.contentRowsDrawn << " of "
                          << result.contentRowsTotal << " rows";
            }
            if (result.detailsRowsDrawn > 0)
            {
                std::cout << ", details showing " << result.detailsRowsDrawn << " rows";
            }
            // Said either way, because "the viewport drew the grid" and "the viewport drew the
            // scene" produce the same draw-call count and the same valid screenshot.
            std::cout << ", viewport "
                      << (result.viewportComposited ? "compositing the scene"
                                                    : "showing the placeholder");
            if (result.logRowsMatching > 0 || result.logRowsDrawn > 0)
            {
                std::cout << ", output log showing " << result.logRowsDrawn << " of "
                          << result.logRowsMatching << " messages";
            }
            if (result.pluginsDiscovered > 0)
            {
                // Said only when there was something to find, so an ordinary run is not told about
                // a feature it is not using -- and said with both numbers, because "found two,
                // started none" and "found none" are different problems with the same empty menu.
                std::cout << ", plugins " << result.pluginsActive << " of "
                          << result.pluginsDiscovered << " active giving "
                          << result.pluginMenuRows << " menu rows";
            }
            if (!hostOptions.workspacePath.empty())
            {
                // Said out loud because it is otherwise unobservable: a restored layout and a
                // default one draw the same number of triangles, so a test that only counted
                // geometry could not tell "remembered the arrangement" from "quietly did not".
                std::cout << ", workspace " << (result.layoutRestored ? "restored" : "default")
                          << (result.layoutStored ? " and stored" : " and not stored");
            }
            std::cout << "\n";
        }
        return result.exitCode;
    }
#else
    if (uiBackend == "studio")
    {
        std::cerr << "cna-studio: the native Studio UI needs a window and a CNA graphics device.\n"
                     "Rebuild with -DCNA_STUDIO_WITH_CNA=ON, or use --shell-preview=PATH to render "
                     "it headless.\n";
        return 3;
    }
#endif

    // Everything past this point is a UI backend with no real window: 'imgui', kept only as a
    // name `--headless` still accepts (`CnaStudioFallsBackToImGuiWhenAsked`), and 'null', which
    // never had one. Both land on the same headless drawing `--headless` itself uses -- there is
    // nothing left to tell them apart by.
    if (uiBackend == "imgui")
    {
        if (options.headless) { return renderShellPreview(options); }

        // Asked for a real window under a name this binary no longer has an implementation for
        // (STUDIO-07030). Saying so beats opening the native shell under a name that did not ask
        // for it, or opening nothing and leaving the reason to be guessed at.
        std::cerr << "cna-studio: the Dear ImGui UI was removed. Use --ui=studio for the native "
                     "shell, or --headless for the console UI.\n";
        return 3;
    }

    if (uiBackend != "null")
    {
        std::cerr << "cna-studio: unknown UI backend '" << uiBackend
                  << "'. This binary provides 'studio' and 'null'.\n";
        return 3;
    }

    return renderShellPreview(options);
}
