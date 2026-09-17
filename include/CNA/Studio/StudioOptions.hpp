// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/StudioOptions.hpp
 * @brief The parsed command line, and the usage text that documents it.
 *
 * `plan.md` STUDIO-07048. It used to live in `StudioApplication.hpp`, which is the header of the
 * prototype's application object — so every entry point that parses a command line, the native
 * shell and the headless shell preview included, included the object being deleted in order to
 * read a struct that has nothing to do with it.
 *
 * One struct for every entry point rather than one per UI, deliberately: `--project`, `--frames`
 * and `--screenshot` mean the same thing wherever they are given, and two parsers would be two
 * chances for `--frames=0` to mean different things depending on which UI happened to open.
 */

#include <optional>
#include <string>
#include <vector>

#include "CNA/Studio/Core/ImageDiff.hpp"
#include "CNA/Studio/Core/StudioMath.hpp"

namespace CNA::Studio
{
    /** @brief Parsed command line. */
    struct StudioOptions
    {
        /** @brief Path to a `.cnaproject` to open at start-up. */
        std::string projectPath;

        /** @brief Path to a `.cnascene` to open, overriding the project's startup scene. */
        std::string scenePath;

        /**
         * @brief Requested UI toolkit: "studio", "imgui" or "null". Empty means "not asked".
         *
         * Empty by default rather than naming one, because the answer depends on the build: a
         * Studio with a CNA device opens the native shell, and one without has no window to open
         * either UI in. `main` resolves it before anything reads it, so nothing downstream has to
         * know the difference between "not asked" and "asked for this".
         *
         * Note that this selects the *UI toolkit*, not the CNA graphics backend. CNA's backend is
         * fixed at compile time (ANALYSIS.md finding F-01), so `--graphics=` on the editor would
         * be a lie -- it appears instead on cna-player, where it chooses which player binary to
         * launch.
         */
        std::string uiBackend;

        /** @brief Run with no window, on NullStudioUi. */
        bool headless = false;

        /** @brief Exit after this many frames. Zero means run until the user quits. */
        int frameLimit = 0;

        /**
         * @brief Start with the viewport in its 3D camera (plan.md ED-400).
         *
         * On the command line because it is the only way to *see* the 3D view from a script: the
         * screenshot path takes a picture of whatever the editor is showing, and every UI feature
         * here has been verified by taking one.
         */
        bool threeDimensionalView = false;

        /**
         * @brief Yaw and pitch, in degrees, to orbit the 3D camera to before drawing.
         *
         * The same argument that put `--view=3d` here, taken one step further. That flag makes the
         * 3D view *reachable* from a script; this one makes it reachable from an angle. A 3D
         * feature photographed head-on is photographed in the one pose where it looks like the 2D
         * view -- which is exactly the pose that cannot tell a mesh from the flat rectangle a
         * sprite draws, and so cannot show whether ED-405's models arrived.
         *
         * Empty leaves the camera where start-up put it. Ignored without `--view=3d`, because the
         * 2D camera has no pitch to set.
         */
        std::optional<StudioVector2> orbitDegrees;

        /**
         * @brief A panel to bring to the front before drawing, by its exact title.
         *
         * The third flag on the same argument as `--view=3d` and `--orbit`: a docked panel sharing
         * a tab bar with five others cannot be photographed at all otherwise, because the tab that
         * happens to be in front is whichever docked last. Empty leaves the layout alone.
         */
        std::string focusPanel;

        /**
         * @brief Where to look for plugins. Defaults to `plugins/` beside the executable.
         *
         * The same convention player builds are discovered by, and for the same reason: an editor
         * and the things that extend it are installed together. On the command line so a test or a
         * developer can point at a build tree without installing anything.
         */
        std::string pluginDirectory;

        /**
         * @brief Write a PNG of the final frame here. Requires a frame limit.
         *
         * A smoke test that only checks the process exited cleanly cannot tell a working editor
         * from one that opened a blank window; an image can.
         */
        std::string screenshotPath;

        /**
         * @brief Fail the run when the captured frame holds fewer distinct colours than this.
         *
         * Zero, the default, asks nothing. The comment above says the image is the test, and it
         * was not quite: the file appears for a blank window too, and every graphical case
         * asserted on counts -- draw calls, triangles, rows -- which a window that rendered
         * nothing can still report. This is the assertion that was missing.
         *
         * A blank frame has one colour, or two where something was cleared to a different shade;
         * a frame of a real UI has hundreds, because antialiased text alone spreads at every glyph
         * edge. The threshold between those is not delicate, so a small number is enough and
         * nothing is gained by tuning it.
         */
        std::size_t screenshotMinColors = 0;

        /**
         * @brief Render the native Studio shell to this PNG and exit.
         *
         * The shell of `plan.md` Phase 6 is real geometry but not yet interactive, so it is
         * reachable as a preview rather than as `--ui=studio`: a flag that opened an unresponsive
         * window would be a worse lie than one that says what it does. It runs headless, because
         * the shell's geometry is CNA-free and is rasterised on the CPU -- which is also how its
         * screenshot tests run without a GPU.
         */
        std::string shellPreviewPath;

        /** @brief Width of the shell preview, in logical units. */
        int shellPreviewWidth = 1920;

        /** @brief Height of the shell preview, in logical units. */
        int shellPreviewHeight = 1080;

        /**
         * @brief DPI scale of the shell and the preview. 1.0 is 100%; zero means "not given".
         *
         * Zero rather than 1.0 as the default, because the user's preferences also answer this and
         * the flag has to be able to say "I did not". Defaulting to 1.0 would have made every run
         * look like somebody had passed `--shell-scale=1`, which would override the preference
         * silently.
         */
        double shellPreviewScale = 0.0;

        /** @brief Shell theme: `"dark"`, `"light"`, or empty for whatever the user prefers. */
        std::string shellPreviewTheme;

        /**
         * @brief Pointer position for the shell preview, in logical units.
         *
         * A preview with the pointer parked off-screen shows the shell at rest, which is exactly
         * the state in which every hover, pressed and highlight token is untested. Placing the
         * pointer makes those states capturable as golden images.
         */
        double shellPreviewPointerX = -1.0;

        /** @brief Pointer y for the shell preview, in logical units. */
        double shellPreviewPointerY = -1.0;

        /** @brief True to hold the primary mouse button down in the shell preview. */
        bool shellPreviewMouseDown = false;

        /**
         * @brief True to right-click at the pointer before capturing, opening a context menu.
         *
         * A press and a release, not a held button: a context menu opens on the press and the
         * capture wants the menu, not a button the user is still holding.
         */
        bool shellPreviewRightClick = false;

        /**
         * @brief Menu to open in the shell preview, e.g. `"File"` or `"Window>Panels"`.
         *
         * A `>`-separated path, because a submenu is reached by hovering and a harness that could
         * only open a top-level menu could never photograph one.
         */
        std::string shellPreviewOpenMenu;

        /**
         * @brief Panels to undock into floating windows before the shell preview is captured.
         *
         * Comma-separated ids. A floating window is arranged by dragging, which a still capture
         * cannot do — so without a flag the one arrangement CI could never photograph would be the
         * one most likely to be drawn wrong.
         */
        std::string shellPreviewFloat;

        /**
         * @brief A command to invoke before the shell preview is captured, e.g. `studio.help.about`.
         *
         * A modal dialog is reached by a menu item and answered by a keystroke, neither of which a
         * still capture can perform — so without a flag the one thing CI could never photograph
         * would be the thing that covers everything else.
         */
        std::string shellPreviewInvoke;

        /**
         * @brief Capture one panel filling the window, rather than the whole shell.
         *
         * A panel taller than the dock it lives in — the Preferences page, a long Details list —
         * is unreviewable in a shell capture: the strip at the bottom of the window shows four
         * rows of it. This draws that panel and nothing else, at whatever size was asked for.
         */
        std::string shellPreviewPanelOnly;

        /**
         * @brief Notifications to post before capturing, e.g. `error|Build failed|3 errors`.
         *
         * `SEVERITY|TITLE[|DETAIL[|ACTION]]`, comma separated. A toast is raised by something
         * finishing in the background, which is the one state a still capture cannot reach by
         * pressing anything.
         */
        std::string shellPreviewNotify;

        /**
         * @brief Window width for a real window, or zero for the host's default.
         *
         * Separate from `--shell-size`, which is the *headless* preview's raster size. Both UIs
         * open a real window through CNA and neither could be asked for one of a given size, which
         * made "the same screen at the same resolution on both" impossible to capture -- and that
         * comparison is the whole of `STUDIO-07023`.
         */
        int windowWidth = 0;

        /** @brief Window height for a real window, or zero for the host's default. */
        int windowHeight = 0;

        /**
         * @brief Report the Studio host capability contract and exit.
         *
         * Prints what Studio requires of a host renderer, and -- on a build with a window host --
         * evaluates it against the live device and prints the verdict before exiting. "Which of
         * Studio's requirements does this build's renderer actually meet" is the first question of
         * every graphics bug report, and it should not need a debugger to answer.
         */
        bool hostCapabilities = false;

        /**
         * @brief Parsed and validated, but no longer read by anything.
         *
         * `plan.md` STUDIO-02072, retired by `STUDIO-02074`. Used to choose between the modern
         * CNAEXT UI renderer and a classic fallback for a host that could not meet its profile.
         * `STUDIO-02074` retired that fallback: a host that cannot run the modern renderer refuses
         * to start regardless of what this names. Kept as a no-op, accepting the same three values
         * (`auto`, `modern`, `compat`), so a script that already passes it does not get an
         * unknown-flag error.
         */
        std::string uiRenderer = "auto";

        /**
         * @brief Select this asset by its project-relative path, for a capture or a smoke test.
         *
         * `STUDIO-07045`. The counterpart to `--select`, and needed for the same reason: the
         * Details panel's asset inspector is only reachable by clicking a row in the Content
         * Browser, and a still capture cannot click. A feature that can only be photographed by a
         * human driving a mouse is a feature nothing regression-tests.
         */
        std::string selectAsset;

        /**
         * @brief Run the UI render benchmark and exit, printing one row per scenario.
         *
         * `STUDIO-04028`. Empty means no; `all` runs every scenario, and any other value selects
         * the scenarios whose names contain it. On the command line rather than only in a test
         * because the number is for *comparing two runs* -- before and after a change, on two
         * machines, on two renderers -- and a measurement reachable only from a test binary is one
         * nobody takes twice.
         */
        std::string uiBenchmark;

        /**
         * @brief Frames per benchmark scenario.
         *
         * Enough that the timing is not one sample, and not so many that running the whole set
         * becomes something people skip.
         */
        int uiBenchmarkFrames = 120;

        /**
         * @brief Export the opened project as a standalone CNA game into this directory, and exit.
         *
         * On the command line because the invariant it serves has to be *provable* by a script:
         * "an exported project builds with Studio uninstalled" is a claim, and the only thing that
         * settles it is a test that exports into an empty directory and builds the result with
         * nothing but CMake, a compiler and CNA. A GUI-only export could not be checked that way,
         * and so would be a claim nobody ever tested (`STUDIO-02051`).
         */
        std::string exportPath;

        /** @brief Let `--export` write into a directory that already has files in it. */
        bool exportOverwrite = false;

        /**
         * @brief Where the native shell remembers its workspace arrangement.
         *
         * Defaults to the user's configuration directory. `--workspace=none` turns remembering off,
         * which is what a screenshot test wants: a capture whose layout depends on what the last
         * run happened to leave behind is a capture that compares against nothing.
         */
        std::string workspacePath;

        /** @brief Select the entity with this name at start-up. Used by `--ui=studio`. */
        std::string selectEntity;

        /**
         * @brief Drag this panel's tab to the preview pointer, so the drop preview is capturable.
         *
         * The one interaction state the other preview flags cannot reach: a drag needs a press on
         * one place and a pointer somewhere else, which no single input snapshot expresses. Empty
         * drags nothing.
         */
        std::string shellPreviewDragPanel;

        /**
         * @brief Hold the pointer still long enough for a tooltip to appear, for the capture.
         *
         * A tooltip is a *timed* state: it exists only after the pointer has rested. No single
         * input snapshot expresses that, so the preview has to run frames rather than describe a
         * moment -- the same reason `--shell-drag` exists.
         */
        bool shellPreviewTooltip = false;

        /**
         * @brief argv[0], used to find the `cna-player-*` binaries beside Studio.
         *
         * Play mode offers exactly the backends whose player executable is installed, which is a
         * direct consequence of CNA fixing its backend at compile time (ANALYSIS.md finding F-01).
         */
        std::string executablePath;

        /**
         * @brief Seconds between crash-recovery snapshots of an unsaved scene. Zero disables them.
         *
         * The reliable half of crash recovery is the part that runs before the crash: the snapshot
         * is already on disk when the process dies, and needs nothing from the dying process. This
         * is how much work a crash can cost.
         */
        double autosaveSeconds = 30.0;

        /**
         * @brief Where snapshots are kept. Empty means the per-user default.
         *
         * Set it to keep a sandboxed or portable run from writing outside its own directory.
         */
        std::string recoveryDirectory;

        /**
         * @brief Run a backend comparison instead of editing, and exit with its verdict.
         *
         * plan.md ED-511: the same run the Backends panel performs, driven from the command line so
         * a build server can assert that a scene renders identically on every installed backend.
         * The exit code is the assertion -- non-zero when they disagree or when the run could not
         * happen at all.
         *
         * It needs a graphics device, because comparing captures means decoding them, so it does
         * not combine with `--headless`.
         */
        bool compareBackends = false;

        /** @brief Largest per-channel difference `--compare-backends` still counts as identical. */
        int comparisonTolerance = kDefaultImageTolerance;

        /** @brief Print the backend table and exit. */
        bool listBackends = false;

        /** @brief Print usage and exit. */
        bool showHelp = false;

        /** @brief Print the version and exit. */
        bool showVersion = false;

        /** @brief Set when parsing failed; @c errorMessage says why. */
        bool hasError = false;
        std::string errorMessage;

        /** @brief Parses argv. Never throws and never exits the process. */
        static StudioOptions parse(int argc, const char* const* argv);

        /** @brief Returns the usage text. */
        static std::string getUsage();
    };
}
