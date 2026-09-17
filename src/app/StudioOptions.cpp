// SPDX-License-Identifier: MS-PL
/**
 * @file StudioOptions.cpp
 * @brief Parsing the command line, and the usage text that documents it.
 *
 * `plan.md` STUDIO-07048. Moved out of `StudioApplication.cpp` for the reason the header was: the
 * parser belongs to every entry point, and the prototype's application is being deleted.
 *
 * ### Two rules the parser holds itself to
 *
 * **Never throws and never exits.** A bad flag sets `hasError` and a message; `main` decides what
 * to do about it. A parser that called `exit` would be one no test could ask a question of.
 *
 * **Every flag is in the usage text.** `EveryShellPreviewFlagIsInTheUsageText` checks it, because
 * a flag nobody can discover is a flag that only its author can use.
 */

#include "CNA/Studio/StudioOptions.hpp"

#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>

namespace CNA::Studio
{
    namespace
    {
        /** @brief Splits "--name=value" into its parts. Returns false when there is no '='. */
        bool splitOption(std::string_view argument, std::string& name, std::string& value)
        {
            const std::size_t equals = argument.find('=');
            if (equals == std::string_view::npos) { return false; }
            name = std::string{argument.substr(0, equals)};
            value = std::string{argument.substr(equals + 1)};
            return true;
        }

        /**
         * @brief Parses `WIDTHxHEIGHT`, both positive.
         *
         * Shared by `--shell-size` and `--window-size`, so the two cannot come to disagree about
         * what a size looks like -- the kind of difference nobody notices until one of them
         * accepts something the other rejects.
         *
         * @param text What the user typed.
         * @param width Receives the width.
         * @param height Receives the height.
         * @return False when it is not two positive numbers around an `x`.
         */
        bool parseSize(const std::string& text, int& width, int& height)
        {
            const std::size_t separator = text.find('x');
            if (separator == std::string::npos) { return false; }

            try
            {
                const int parsedWidth = std::stoi(text.substr(0, separator));
                const int parsedHeight = std::stoi(text.substr(separator + 1));
                if (parsedWidth <= 0 || parsedHeight <= 0) { return false; }
                width = parsedWidth;
                height = parsedHeight;
                return true;
            }
            catch (const std::exception&) { return false; }
        }
    }

    StudioOptions StudioOptions::parse(int argc, const char* const* argv)
    {
        StudioOptions options;

        // argv[0] is how the editor finds the `cna-player-*` binaries beside it. Because CNA fixes
        // its backend at compile time (finding F-01), the set of backends play mode can offer is
        // the set of player executables actually installed next to this one.
        if (argc > 0 && argv[0] != nullptr) { options.executablePath = argv[0]; }

        for (int index = 1; index < argc; ++index)
        {
            const std::string_view argument{argv[index] != nullptr ? argv[index] : ""};
            if (argument.empty()) { continue; }

            std::string name;
            std::string value;

            if (argument == "--help" || argument == "-h") { options.showHelp = true; continue; }
            if (argument == "--version") { options.showVersion = true; continue; }
            if (argument == "--headless") { options.headless = true; continue; }
            if (argument == "--list-backends") { options.listBackends = true; continue; }
            if (argument == "--compare-backends") { options.compareBackends = true; continue; }
            if (argument == "--shell-mouse-down") { options.shellPreviewMouseDown = true; continue; }
            if (argument == "--shell-right-click") { options.shellPreviewRightClick = true; continue; }
            if (argument == "--host-capabilities") { options.hostCapabilities = true; continue; }
            if (argument == "--ui-benchmark") { options.uiBenchmark = "all"; continue; }
            if (argument == "--export-overwrite") { options.exportOverwrite = true; continue; }
            if (argument == "--shell-tooltip") { options.shellPreviewTooltip = true; continue; }

            if (splitOption(argument, name, value))
            {
                if (name == "--project") { options.projectPath = value; continue; }
                if (name == "--export") { options.exportPath = value; continue; }
                if (name == "--workspace") { options.workspacePath = value; continue; }
                if (name == "--select") { options.selectEntity = value; continue; }
                if (name == "--select-asset") { options.selectAsset = value; continue; }
                if (name == "--shell-drag") { options.shellPreviewDragPanel = value; continue; }
                if (name == "--scene") { options.scenePath = value; continue; }
                if (name == "--ui") { options.uiBackend = value; continue; }
                if (name == "--ui-renderer")
                {
                    // Retired by STUDIO-02074: validated and stored so a script that already
                    // passes it does not get an unknown-flag error, but nothing reads the value.
                    if (value != "auto" && value != "modern" && value != "compat")
                    {
                        options.hasError = true;
                        options.errorMessage =
                            "--ui-renderer expects auto, modern or compat, got '" + value + "'";
                    }
                    options.uiRenderer = value;
                    continue;
                }
                if (name == "--ui-benchmark") { options.uiBenchmark = value; continue; }
                if (name == "--ui-benchmark-frames")
                {
                    try { options.uiBenchmarkFrames = std::stoi(value); }
                    catch (const std::exception&) { options.uiBenchmarkFrames = 0; }
                    if (options.uiBenchmarkFrames <= 0)
                    {
                        options.hasError = true;
                        options.errorMessage =
                            "--ui-benchmark-frames expects a positive count, got '" + value + "'";
                    }
                    continue;
                }
                if (name == "--screenshot") { options.screenshotPath = value; continue; }
                if (name == "--shell-preview") { options.shellPreviewPath = value; continue; }
                if (name == "--shell-theme")
                {
                    if (value != "dark" && value != "light")
                    {
                        options.hasError = true;
                        options.errorMessage = "--shell-theme expects dark or light, got '" + value + "'";
                    }
                    options.shellPreviewTheme = value;
                    continue;
                }
                if (name == "--shell-size")
                {
                    // A malformed size is an error rather than a silent default, for the same
                    // reason --view is: this flag exists to be set from a script that cannot see
                    // the picture it asked for.
                    if (!parseSize(value, options.shellPreviewWidth, options.shellPreviewHeight))
                    {
                        options.hasError = true;
                        options.errorMessage = "--shell-size expects WIDTHxHEIGHT, got '" + value + "'";
                    }
                    continue;
                }
                if (name == "--shell-scale")
                {
                    try { options.shellPreviewScale = std::stod(value); }
                    catch (const std::exception&)
                    {
                        options.hasError = true;
                        options.errorMessage = "--shell-scale expects a number, got '" + value + "'";
                    }
                    if (options.shellPreviewScale <= 0.0)
                    {
                        options.hasError = true;
                        options.errorMessage = "--shell-scale must be greater than zero";
                    }
                    continue;
                }
                if (name == "--shell-pointer")
                {
                    // X,Y. Malformed is an error for the same reason --shell-size is: the flag
                    // exists to make a hover state reproducible from a script that cannot see it.
                    const std::size_t separator = value.find(',');
                    bool parsed = false;
                    if (separator != std::string::npos)
                    {
                        try
                        {
                            options.shellPreviewPointerX = std::stod(value.substr(0, separator));
                            options.shellPreviewPointerY = std::stod(value.substr(separator + 1));
                            parsed = true;
                        }
                        catch (const std::exception&) { parsed = false; }
                    }
                    if (!parsed)
                    {
                        options.hasError = true;
                        options.errorMessage = "--shell-pointer expects X,Y, got '" + value + "'";
                    }
                    continue;
                }
                if (name == "--shell-panel-only")
                {
                    options.shellPreviewPanelOnly = value;
                    continue;
                }
                if (name == "--window-size")
                {
                    int width = 0;
                    int height = 0;
                    if (!parseSize(value, width, height))
                    {
                        options.hasError = true;
                        options.errorMessage =
                            "--window-size expects WIDTHxHEIGHT, got '" + value + "'";
                        continue;
                    }
                    options.windowWidth = width;
                    options.windowHeight = height;
                    continue;
                }
                if (name == "--shell-notify")
                {
                    options.shellPreviewNotify = value;
                    continue;
                }
                if (name == "--shell-invoke")
                {
                    options.shellPreviewInvoke = value;
                    continue;
                }
                if (name == "--shell-float")
                {
                    options.shellPreviewFloat = value;
                    continue;
                }
                if (name == "--shell-open-menu")
                {
                    options.shellPreviewOpenMenu = value;
                    continue;
                }
                if (name == "--recovery-dir") { options.recoveryDirectory = value; continue; }
                if (name == "--autosave")
                {
                    try { options.autosaveSeconds = std::stod(value); }
                    catch (const std::exception&)
                    {
                        options.hasError = true;
                        options.errorMessage = "--autosave expects a number of seconds, got '" + value + "'";
                    }
                    if (options.autosaveSeconds < 0.0) { options.autosaveSeconds = 0.0; }
                    continue;
                }
                if (name == "--tolerance")
                {
                    try { options.comparisonTolerance = std::stoi(value); }
                    catch (const std::exception&)
                    {
                        options.hasError = true;
                        options.errorMessage = "--tolerance expects a number, got '" + value + "'";
                    }
                    continue;
                }
                if (name == "--view")
                {
                    // Only the two the viewport has. A typo here is worth an error rather than a
                    // silent 2D start, because the flag exists precisely to be checked from a
                    // script that cannot see the window it asked for.
                    if (value == "3d" || value == "3D") { options.threeDimensionalView = true; }
                    else if (value == "2d" || value == "2D") { options.threeDimensionalView = false; }
                    else
                    {
                        options.hasError = true;
                        options.errorMessage = "--view expects 2d or 3d, got '" + value + "'";
                    }
                    continue;
                }
                if (name == "--plugins")
                {
                    options.pluginDirectory = value;
                    continue;
                }
                if (name == "--panel")
                {
                    options.focusPanel = value;
                    continue;
                }
                if (name == "--orbit")
                {
                    // "yaw,pitch". Rejected rather than partly accepted: this flag exists to make
                    // a screenshot reproducible, and a run that silently kept the default angle
                    // would produce a picture that looks fine and shows the wrong thing.
                    const std::size_t comma = value.find(',');
                    bool parsed = comma != std::string::npos;
                    if (parsed)
                    {
                        try
                        {
                            options.orbitDegrees =
                                StudioVector2{std::stof(value.substr(0, comma)),
                                              std::stof(value.substr(comma + 1))};
                        }
                        catch (const std::exception&) { parsed = false; }
                    }
                    if (!parsed)
                    {
                        options.hasError = true;
                        options.errorMessage =
                            "--orbit expects YAW,PITCH in degrees, got '" + value + "'";
                    }
                    continue;
                }
                if (name == "--frames")
                {
                    try { options.frameLimit = std::stoi(value); }
                    catch (const std::exception&)
                    {
                        options.hasError = true;
                        options.errorMessage = "--frames expects a number, got '" + value + "'";
                    }
                    continue;
                }
                if (name == "--screenshot-min-colors")
                {
                    try
                    {
                        const int wanted = std::stoi(value);
                        if (wanted < 0) { throw std::out_of_range{"negative"}; }
                        options.screenshotMinColors = static_cast<std::size_t>(wanted);
                    }
                    catch (const std::exception&)
                    {
                        options.hasError = true;
                        options.errorMessage =
                            "--screenshot-min-colors expects a count, got '" + value + "'";
                    }
                    continue;
                }
                if (name == "--graphics")
                {
                    // Rejected rather than ignored, because silently accepting it would teach
                    // users a mental model CNA does not support (see StudioOptions::uiBackend).
                    options.hasError = true;
                    options.errorMessage =
                        "--graphics is not a Studio option: CNA selects its graphics backend at "
                        "compile time, so this Studio binary is fixed to the backend it was built "
                        "against. Pass --graphics to cna-player instead, or use --ui to choose the "
                        "Studio UI toolkit.";
                    continue;
                }
            }

            if (argument.rfind("--", 0) == 0)
            {
                options.hasError = true;
                options.errorMessage = "unknown option '" + std::string{argument} + "'";
                continue;
            }

            // A bare path is the project to open, which is what a file manager passes.
            if (options.projectPath.empty()) { options.projectPath = std::string{argument}; }
        }

        return options;
    }

    std::string StudioOptions::getUsage()
    {
        return
            "cna-studio -- professional authoring environment for CNA\n"
            "\n"
            "Usage:\n"
            "  cna-studio [options] [project.cnaproject]\n"
            "\n"
            "Options:\n"
            "  --project=PATH     Open this .cnaproject at start-up.\n"
            "  --scene=PATH       Open this .cnascene, overriding the project's startup scene.\n"
            "  --ui=NAME          UI toolkit: 'studio' (default on a CNA build) or 'null'.\n"
            "  --shell-preview=P  Render the native Studio shell to PNG at P and exit.\n"
            "  --shell-pointer=X,Y  Place the pointer, so hover states are capturable.\n"
            "  --shell-mouse-down   Hold the primary button, so pressed states are capturable.\n"
            "  --shell-right-click  Right-click at the pointer, opening a context menu.\n"
            "  --shell-open-menu=T  Open the menu titled T, e.g. File or Window>Panels.\n"
            "  --shell-float=IDS    Undock these panels into floating windows, comma separated.\n"
            "  --shell-invoke=ID    Invoke this command before capturing, e.g. studio.help.about.\n"
            "  --window-size=WxH  Size of the real window, for --ui=studio.\n"
            "  --shell-notify=LIST  Post notifications before capturing, comma separated, each\n"
            "                       SEVERITY|TITLE[|DETAIL[|ACTION]] where SEVERITY is info,\n"
            "                       success, warning or error.\n"
            "  --shell-panel-only=ID  Capture just this panel, filling the window, so a page\n"
            "                       taller than its dock can be reviewed.\n"
            "  --shell-drag=PANEL   Drag PANEL's tab to --shell-pointer, showing the drop\n"
            "                       preview. Needs --shell-pointer.\n"
            "  --shell-tooltip      Rest the pointer until a tooltip appears.\n"
            "  --ui=imgui         Retired name (STUDIO-07030); with --headless, same as not\n"
            "                     naming a UI at all, since there is no window to open under it.\n"
            "  --select-asset=P   Select the asset at project-relative path P.\n"
            "  --ui-benchmark[=S] Measure UI frame cost for scenarios matching S (default all)\n"
            "                     and exit. What each render backend is asked to submit.\n"
            "  --ui-benchmark-frames=N  Frames per scenario (default 120).\n"
            "  --ui-renderer=R    Retired (STUDIO-02074); accepted for scripts, changes\n"
            "                     nothing. A host that cannot run the modern UI renderer\n"
            "                     refuses to start regardless.\n"
            "  --workspace=PATH   Where --ui=studio remembers its layout. 'none' forgets it.\n"
            "  --select=NAME      Select this entity at start-up, for --ui=studio.\n"
            "  --export=DIR       Export the project as a standalone CNA game and exit.\n"
            "  --export-overwrite  Let --export write into a non-empty directory.\n"
            "  --host-capabilities  Report what Studio requires of a host renderer, evaluate it\n"
            "                       against this build's device where there is one, and exit.\n"
            "  --shell-size=WxH   Size of the shell preview. Default: 1920x1080.\n"
            "  --shell-scale=N    DPI scale of the shell preview. Default: 1.0.\n"
            "  --shell-theme=T    Shell preview theme: 'dark' or 'light'. Default: dark.\n"
            "  --headless         Run with no window, on the null UI. Implies --ui=null.\n"
            "  --view=2d|3d       Which viewport camera to start in. Defaults to 2d.\n"
            "  --orbit=YAW,PITCH  Orbit the 3D camera to these angles, in degrees. Needs --view=3d.\n"
            "  --frames=N         Exit after N frames. Useful for smoke tests.\n"
            "  --screenshot=PATH  Write a PNG of the final frame. Requires --frames.\n"
            "  --screenshot-min-colors=N  Fail if the captured frame holds fewer distinct\n"
            "                     colours than N, so a blank window fails rather than passing.\n"
            "  --autosave=SECONDS Crash-recovery snapshot interval. 0 disables. Default: 30.\n"
            "  --recovery-dir=DIR Where snapshots are kept. Default: the per-user state directory.\n"
            "  --list-backends    Print the CNA graphics backends Studio knows about.\n"
            "  --compare-backends Run the open scene on every installed cna-player build, compare\n"
            "                     the frames, print the result and exit non-zero if they differ.\n"
            "                     Needs a graphics device, so it does not combine with --headless.\n"
            "  --tolerance=N      Largest per-channel difference --compare-backends still counts as\n"
            "                     identical. Default: 2, because two backends are never bit-equal.\n"
            "  --panel=TITLE      Bring the panel with this exact title to the front, e.g. Assets.\n"
            "  --plugins=DIR      Where to look for plugins. Defaults to plugins/ beside Studio.\n"
            "  --version          Print the version and exit.\n"
            "  -h, --help         Print this help and exit.\n"
            "\n"
            "Note: there is no --graphics option. CNA selects its graphics backend at compile\n"
            "time, so this Studio binary is fixed to the backend it was built against. To preview\n"
            "a game on a different backend, launch the matching cna-player build:\n"
            "\n"
            "  cna-player --project=MyGame.cnaproject --graphics=software --studio-port=34781\n";
    }
}
