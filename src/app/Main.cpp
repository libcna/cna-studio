// SPDX-License-Identifier: MS-PL
/**
 * @file Main.cpp
 * @brief The cna-studio entry point.
 *
 * Only this file decides which concrete StudioUi and StudioViewport the application gets. Keeping
 * that decision in exactly one place is what lets the same StudioApplication run under a real
 * toolkit, under the null UI in CI, and under a future Qt implementation.
 */

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>

#include "CNA/Studio/Project/Project.hpp"
#include "CNA/Studio/Project/ProjectExport.hpp"
#include "CNA/Studio/Project/RendererCatalog.hpp"
#include "CNA/Studio/Project/StudioHostRequirements.hpp"
#include "CNA/Studio/StudioApplication.hpp"
#include "CNA/Studio/UiCore/StudioDrawList.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"
#include "CNA/Studio/UiCore/StudioShellLayout.hpp"
#include "CNA/Studio/UiCore/StudioTheme.hpp"
#include "CNA/Studio/UiCore/StudioWorkspaceStore.hpp"
#include "CNA/Studio/UiCore/UiSoftwareRasterizer.hpp"
#include "CNA/Studio/RuntimeBridge/BackendComparison.hpp"

#if defined(CNA_STUDIO_HAS_IMGUI)
#    include "CNA/Studio/Ui/ImGuiStudioUi.hpp"
#endif

// The window host needs both CNA and Dear ImGui. Without it the editor still runs headless, which
// is what CI and `--headless` use.
#if defined(CNA_STUDIO_HAS_HOST)
#    include "CNA/Studio/Viewport/CnaStudioHost.hpp"
#endif

#if defined(CNA_STUDIO_HAS_CNA)
#    include "CNA/Studio/Viewport/CnaStudioShellHost.hpp"
#endif

namespace
{
    /** @brief Prints every log message to stdout. Used by headless runs. */
    class ConsoleStudioUi final : public CNA::Studio::NullStudioUi
    {
    public:
        void log(CNA::Studio::LogSeverity severity, const std::string& message) override
        {
            NullStudioUi::log(severity, message);
            std::ostream& stream =
                severity == CNA::Studio::LogSeverity::Error ? std::cerr : std::cout;
            stream << "[" << CNA::Studio::toString(severity) << "] " << message << "\n";
        }
    };

#if defined(CNA_STUDIO_HAS_HOST)
    /**
     * @brief Returns where ImGui's dock layout `.ini` should live.
     *
     * Under the user's config directory, not the project: where somebody puts their panels is a
     * property of the person, not of the game they happen to be editing.
     */
    std::string resolveLayoutPath()
    {
        const char* configHome = std::getenv("XDG_CONFIG_HOME");
        const char* home = std::getenv("HOME");
        const char* appData = std::getenv("APPDATA");

        std::filesystem::path base;
        if (configHome != nullptr && *configHome != '\0') { base = configHome; }
        else if (appData != nullptr && *appData != '\0') { base = appData; }
        else if (home != nullptr && *home != '\0') { base = std::filesystem::path{home} / ".config"; }
        else { return {}; }

        const std::filesystem::path directory = base / "cna-studio";
        std::error_code errorCode;
        std::filesystem::create_directories(directory, errorCode);
        if (errorCode) { return {}; }

        return (directory / "layout.ini").generic_string();
    }
#endif

#if defined(CNA_STUDIO_HAS_HOST)
    /**
     * @brief Prints a comparison run's verdict, one line per backend.
     *
     * Beside the window host, because a build with no host cannot compare anything: decoding a
     * capture needs a graphics device, and a function nobody can reach is a warning waiting to
     * happen.
     */
    void printComparison(const CNA::Studio::BackendComparison& comparison)
    {
        std::cout << "cna-studio: backend comparison against '" << comparison.getReferenceBackend()
                  << "'\n";

        for (const CNA::Studio::ComparisonEntry& entry : comparison.getEntries())
        {
            std::cout << "  " << entry.backend << (entry.isReference ? "  (reference)" : "") << "\n";

            if (!entry.errorMessage.empty())
            {
                std::cout << "      " << entry.errorMessage << "\n";
                continue;
            }
            if (entry.isReference) { std::cout << "      " << entry.capturePath << "\n"; continue; }

            if (!entry.difference.comparable)
            {
                std::cout << "      cannot compare: " << entry.difference.incomparableReason << "\n";
                continue;
            }

            std::cout << "      " << entry.difference.differingPixels << " of "
                      << entry.difference.totalPixels << " pixels differ, largest channel difference "
                      << entry.difference.maxChannelDelta << "\n";
            if (!entry.differencePath.empty())
            {
                std::cout << "      " << entry.differencePath << "\n";
            }
        }

        if (!comparison.getError().empty())
        {
            std::cerr << "cna-studio: " << comparison.getError() << "\n";
        }
    }
#endif

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
    int renderShellPreview(const CNA::Studio::StudioOptions& options)
    {
        CNA::Studio::StudioTheme theme = options.shellPreviewTheme == "light"
            ? CNA::Studio::StudioTheme::light()
            : CNA::Studio::StudioTheme::dark();
        theme.setScale(static_cast<float>(options.shellPreviewScale));

        CNA::Studio::StudioShell shell{theme};

        if (!options.shellPreviewOpenMenu.empty())
        {
            const auto& menus = shell.menus();
            int index = -1;
            for (std::size_t i = 0; i < menus.size(); ++i)
            {
                if (menus[i].title == options.shellPreviewOpenMenu) { index = static_cast<int>(i); }
            }
            if (index < 0)
            {
                std::cerr << "cna-studio: no menu titled '" << options.shellPreviewOpenMenu
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
        shell.renderFrame(input);
        shell.renderFrame(input);

        const CNA::Studio::ImageBuffer image =
            CNA::Studio::rasterizeUiDrawData(shell.drawData(),
                                             theme.color(CNA::Studio::StudioColorRole::AppBackground));
        if (!image.isWellFormed())
        {
            std::cerr << "cna-studio: the shell produced no image at "
                      << options.shellPreviewWidth << "x" << options.shellPreviewHeight << "\n";
            return 4;
        }

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
#if !defined(CNA_STUDIO_HAS_HOST)
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
    // graphics device at all.
    if (!options.shellPreviewPath.empty())
    {
        return renderShellPreview(options);
    }

#if defined(CNA_STUDIO_HAS_CNA)
    // The native Studio UI in a real window, through a real CNA renderer. Kept a separate entry
    // point from the ImGui host rather than a branch inside it: the two draw entirely different
    // things, and the migration ends by deleting one of them -- which is far easier when there is
    // one to delete rather than a branch to unpick.
    if (options.uiBackend == "studio")
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
        hostOptions.frameLimit = options.frameLimit;
        hostOptions.screenshotPath = options.screenshotPath;
        hostOptions.uiScale = static_cast<float>(options.shellPreviewScale);
        hostOptions.theme = options.shellPreviewTheme;

        // "none" rather than an empty string for off, because an empty --workspace= reads as a
        // mistake and defaulting it to the user's real file would be the wrong guess: a test that
        // meant to isolate itself would silently write over the developer's layout.
        hostOptions.focusPanel = options.focusPanel;
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
            std::cerr << "cna-studio: no screenshot was written to '" << options.screenshotPath
                      << "'. --screenshot needs --frames, and the renderer must support reading "
                         "back its own back buffer.\n";
            return 4;
        }
        if (!result.layoutProblem.empty())
        {
            std::cerr << "cna-studio: " << result.layoutProblem << "\n";
        }
        if (options.frameLimit > 0)
        {
            // A window that opens, loops and closes having issued zero draw calls looks identical
            // to a working one from the outside, so a smoke test needs numbers to assert on.
            std::cout << "cna-studio: native shell on " << result.renderer << ", " << result.frames
                      << " frames, " << result.displayWidth << "x" << result.displayHeight
                      << " display, " << result.drawCalls << " draw calls, " << result.triangles
                      << " triangles";
            if (result.logRowsMatching > 0 || result.logRowsDrawn > 0)
            {
                std::cout << ", output log showing " << result.logRowsDrawn << " of "
                          << result.logRowsMatching << " messages";
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
    if (options.uiBackend == "studio")
    {
        std::cerr << "cna-studio: the native Studio UI needs a window and a CNA graphics device.\n"
                     "Rebuild with -DCNA_STUDIO_WITH_CNA=ON, or use --shell-preview=PATH to render "
                     "it headless.\n";
        return 3;
    }
#endif

    // This is the one place that decides which concrete StudioUi and StudioViewport the
    // application gets. Everything else -- panels, commands, plugins -- is written against the
    // abstractions and does not change when this does (ANALYSIS.md decision D-02).
    const bool useImGui = !options.headless && options.uiBackend != "null";

#if !defined(CNA_STUDIO_HAS_IMGUI)
    if (useImGui)
    {
        std::cerr << "cna-studio: this binary was built with -DCNA_STUDIO_WITH_IMGUI=OFF, so the "
                     "'" << options.uiBackend << "' UI is unavailable.\n"
                     "Run with --headless to use the console UI.\n";
        return 3;
    }
#else
    if (useImGui && options.uiBackend != "imgui")
    {
        std::cerr << "cna-studio: unknown UI backend '" << options.uiBackend
                  << "'. This binary provides 'studio', 'imgui' and 'null'.\n";
        return 3;
    }

#    if defined(CNA_STUDIO_HAS_HOST)
    if (useImGui)
    {
        auto application = std::make_unique<CNA::Studio::StudioApplication>(
            std::make_unique<CNA::Studio::ImGuiStudioUi>(),
            std::make_unique<CNA::Studio::NullStudioViewport>());

        if (!application->initialize(options)) { return 1; }

        // Set before the application is handed to the host, which owns it from then on: by the
        // time runStudioInWindow returns there is nothing left to ask.
        bool comparisonReported = false;
        bool backendsAgree = false;
        if (options.compareBackends)
        {
            application->setComparisonReport([&](const CNA::Studio::BackendComparison& comparison) {
                comparisonReported = true;
                backendsAgree = comparison.allBackendsAgree();
                printComparison(comparison);
            });
        }

        CNA::Studio::CnaStudioHostOptions hostOptions;
        hostOptions.reportCapabilities = options.hostCapabilities;
        hostOptions.checkCapabilitiesOnly = options.hostCapabilities;
        hostOptions.frameLimit = options.frameLimit;
        hostOptions.layoutPath = resolveLayoutPath();
        hostOptions.screenshotPath = options.screenshotPath;
        hostOptions.windowTitle = application->getContext().hasProject()
                                      ? "CNA Studio -- " + application->getContext().getProject().getName()
                                      : "CNA Studio";

        const CNA::Studio::CnaStudioHostResult result =
            CNA::Studio::runStudioInWindow(hostOptions, std::move(application));

        if (!result.errorMessage.empty()) { std::cerr << "cna-studio: " << result.errorMessage << "\n"; }

        // STUDIO-02022: a distinct exit code, so a build matrix can tell "this renderer cannot host
        // Studio" from "Studio crashed" without parsing a message.
        if (!result.rendererCanHostStudio)
        {
            return CNA::Studio::kCnaStudioHostUnsupportedRendererExitCode;
        }
        if (options.hostCapabilities) { return 0; }

        if (!options.screenshotPath.empty() && !result.screenshotWritten)
        {
            std::cerr << "cna-studio: no screenshot was written to '" << options.screenshotPath
                      << "'. --screenshot needs --frames, and the backend must support reading "
                         "back its own back buffer.\n";
            return 4;
        }

        if (options.compareBackends)
        {
            // The exit code *is* the assertion, which is the whole point of the harness: a build
            // server asserts on it without reading a word of the output.
            if (!comparisonReported)
            {
                std::cerr << "cna-studio: the backend comparison never produced a verdict.\n";
                return 5;
            }
            if (!backendsAgree)
            {
                std::cerr << "cna-studio: the backends do not agree.\n";
                return 5;
            }
            std::cout << "cna-studio: every backend drew the same picture.\n";
            return 0;
        }

        // Printed only for a scripted run. A window that opens, loops and closes having issued
        // zero draw calls looks identical to a working editor from the outside, so a smoke test
        // needs the numbers to assert on.
        if (options.frameLimit > 0)
        {
            std::cout << "cna-studio: backend " << result.backend << ", " << result.frames
                      << " frames, " << result.displayWidth << "x" << result.displayHeight
                      << " display, " << result.drawCalls << " draw calls, " << result.triangles
                      << " triangles, " << result.textures << " textures created, "
                      << result.textureUpdates << " texture updates, " << result.clippedAway
                      << " commands clipped away\n";
        }
        return result.exitCode;
    }
#    else
    if (useImGui)
    {
        // The ImGui UI is built and produces real geometry -- that is what the headless tests
        // assert on -- but presenting it needs a window and a CNA graphics device, which live in
        // cna-studio-viewport. Saying so plainly beats opening a blank window.
        std::cerr << "cna-studio: the ImGui UI is built, but this binary has no window host.\n"
                     "Rebuild with -DCNA_STUDIO_WITH_CNA=ON to get one, or run with --headless.\n";
        return 3;
    }
#    endif
#endif

    if (options.compareBackends)
    {
        // Comparing means decoding the captures, and decoding needs a graphics device. Saying so
        // beats running the whole thing and reporting that every capture was unreadable.
        std::cerr << "cna-studio: --compare-backends needs a graphics device, so it cannot run "
                     "headless or on the null UI. Run it on a build with -DCNA_STUDIO_WITH_CNA=ON "
                     "and a display.\n";
        return 3;
    }

    CNA::Studio::StudioApplication application{std::make_unique<ConsoleStudioUi>(),
                                               std::make_unique<CNA::Studio::NullStudioViewport>()};

    if (!application.initialize(options)) { return 1; }

    // The null UI never reports "the user closed the window", so an unbounded run() would spin
    // forever with nothing to look at. One frame is the useful default -- and is exactly what
    // makes `--headless` a usable smoke test. `--frames=N` overrides it.
    if (options.frameLimit <= 0)
    {
        application.renderFrame();
        return 0;
    }

    return application.run();
}
