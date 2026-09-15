# SPDX-License-Identifier: MS-PL
#
# One binary rather than one per module: the suite is fast, and a single binary keeps
# `ctest --output-on-failure` output readable. Individual suites are registered separately so a
# failure names the area rather than just "CnaStudioTests".

add_executable(cna-studio-tests
    TestMain.cpp
    CoreTests.cpp
    SceneTests.cpp
    CommandTests.cpp
    ProjectAndAssetTests.cpp
    ModelImportTests.cpp
    ApplicationTests.cpp
    UiTests.cpp
    UiCoreTests.cpp
    StudioShellTests.cpp
    StudioFrameTests.cpp
    StudioShellInteractionTests.cpp
    StudioDockTests.cpp
    StudioHostCapabilityTests.cpp
    StudioFontTests.cpp
    StudioHighDpiTests.cpp
    StudioTargetProfileTests.cpp
    ProjectExportTests.cpp
    StudioWorkspaceStoreTests.cpp
    StudioLogPanelTests.cpp
    StudioOutlinerPanelTests.cpp
    StudioTextEditTests.cpp
    StudioDetailsPanelTests.cpp
    StudioShellActionTests.cpp
    StudioContentBrowserTests.cpp
    StudioDialogTests.cpp
    StudioDockDragTests.cpp
    StudioEmptyStateTests.cpp
    StudioMigrationInventoryTests.cpp
    StudioNotificationTests.cpp
    StudioPlayerInputTests.cpp
    StudioPluginMenuTests.cpp
    StudioPngTests.cpp
    StudioPreferencesTests.cpp
    StudioQuitTests.cpp
    StudioRecoveryTests.cpp
    StudioSceneSettingsTests.cpp
    StudioRenameTests.cpp
    StudioShortcutEditorTests.cpp
    StudioStatusBarTests.cpp
    StudioSavedLayoutTests.cpp
    StudioFloatingPanelTests.cpp
    StudioTilemapToolTests.cpp
    StudioTooltipTests.cpp
    StudioViewport3DTests.cpp
    StudioSubmenuTests.cpp
    StudioContextMenuTests.cpp
    StudioDropdownTests.cpp
    StudioDragDropTests.cpp
    StudioBuildPanelTests.cpp
    StudioProblemsPanelTests.cpp
    StudioHistoryPanelTests.cpp
    StudioLayersPanelTests.cpp
    StudioCapabilityParityTests.cpp
    StudioComparisonPanelTests.cpp
    StudioPlayModeTests.cpp
    StudioDiagnosticsPanelTests.cpp
    StudioViewportPanelTests.cpp
    ArchitectureGuardTests.cpp
    StudioInteractionTests.cpp
    PlayerTests.cpp
    ViewportTests.cpp)

# A *real* shared library for ED-411's loading tests. Nothing about dlopen, symbol resolution,
# cross-runtime allocation or unload order can be checked against a test double -- a double would
# exercise the host's bookkeeping and none of the things that actually break.
#
# It links **none** of the editor's libraries, and that is the important part rather than a way
# around a linker error. A plugin that statically linked cna-studio-core would get its own copy of
# every registry and every global in it, so the component it registered would go into a
# ComponentRegistry the editor has never heard of -- and the failure would look like "initialize()
# ran and nothing happened". Instead it includes the headers and leaves the symbols undefined, and
# the host resolves them at load time from its own exported symbols. That is what ENABLE_EXPORTS on
# the test executable is for, and it is the same arrangement a real plugin has to use.
add_library(cna-studio-test-plugin MODULE plugin/TestPlugin.cpp)
target_link_libraries(cna-studio-test-plugin PRIVATE cna-studio-settings)
target_link_options(cna-studio-test-plugin PRIVATE "LINKER:--allow-shlib-undefined")
set_target_properties(cna-studio-tests PROPERTIES ENABLE_EXPORTS ON)
set_target_properties(cna-studio-test-plugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/plugins/testplugin"
    PREFIX "")

# The manifest beside it, written at configure time so the test has a directory to discover.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/plugins/testplugin/plugin.json"
"{\n"
"  \"id\": \"org.openeggbert.testplugin\",\n"
"  \"name\": \"Test Plugin\",\n"
"  \"version\": \"1.0.0\",\n"
"  \"editorApiVersion\": 1,\n"
"  \"library\": \"cna-studio-test-plugin${CMAKE_SHARED_MODULE_SUFFIX}\"\n"
"}\n")

add_dependencies(cna-studio-tests cna-studio-test-plugin)
target_compile_definitions(cna-studio-tests PRIVATE
    CNA_STUDIO_TEST_PLUGIN_DIR="${CMAKE_CURRENT_BINARY_DIR}/plugins")

# The architecture guard tests scan the source tree, so they must be told where it is rather than
# guessing from the test binary's location -- a guess that silently finds nothing would make every
# guard pass vacuously.
target_compile_definitions(cna-studio-tests PRIVATE
    CNA_STUDIO_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

target_include_directories(cna-studio-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cna-studio-tests PRIVATE
    cna-studio-ui-core
    cna-studio-shell-panels
    cna-studio-context
    cna-studio-plugins
    cna-studio-runtime-bridge
    cna-studio-player)

# PlayerTests spawns the real cna-player binary, so it must exist before the suite runs -- and
# the suite must know where it landed. A generator expression over the real target is exact;
# guessing from the test binary's own directory would silently turn the end-to-end bridge test
# into a no-op that always passes.
add_dependencies(cna-studio-tests cna-player)
target_compile_definitions(cna-studio-tests PRIVATE
    CNA_STUDIO_TEST_PLAYER_DIR="$<TARGET_FILE_DIR:cna-player>")

# The application's panel layer is exercised end-to-end by ApplicationTests.cpp, so the test
# binary compiles StudioApplication.cpp directly rather than linking the cna-studio executable.
target_sources(cna-studio-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/app/StudioApplication.cpp"
    ${CNA_STUDIO_PANEL_SOURCES})

# The one module that links CNA. Linked into the tests so the seam itself -- the key map, the
# capability bridge -- has unit coverage rather than only being exercised through the window smoke
# tests, where a wrong mapping shows up as a shortcut that quietly does nothing.
if(CNA_STUDIO_WITH_CNA)
    target_link_libraries(cna-studio-tests PRIVATE cna-studio-viewport)
endif()

# The renderer/operating-system gate transcription (STUDIO-29007) can only be *checked* against CNA
# when there is a CNA checkout to read. Told where it is rather than left to guess: a guard that
# silently found nothing would pass vacuously, which is the failure mode these tests exist to avoid.
if(CNA_STUDIO_WITH_CNA AND EXISTS "${CNA_STUDIO_CNA_ROOT}/cmake/RendererSelection.cmake")
    target_compile_definitions(cna-studio-tests PRIVATE
        CNA_STUDIO_TEST_CNA_ROOT="${CNA_STUDIO_CNA_ROOT}")
endif()

if(CNA_STUDIO_WITH_IMGUI)
    # The ImGui cases run the real editor UI headless and assert on the geometry it produces.
    target_link_libraries(cna-studio-tests PRIVATE cna-studio-ui-imgui)
    target_compile_definitions(cna-studio-tests PRIVATE CNA_STUDIO_HAS_IMGUI=1)
endif()

add_test(NAME CnaStudioTests COMMAND cna-studio-tests)
set_tests_properties(CnaStudioTests PROPERTIES TIMEOUT 120)

# A smoke test over the real binary: parses arguments, opens nothing, draws one headless frame.
# This is what catches a linker or start-up regression that the unit tests cannot see.
add_test(NAME CnaStudioHeadlessSmoke COMMAND cna-studio --headless)
set_tests_properties(CnaStudioHeadlessSmoke PROPERTIES TIMEOUT 60)

# The same run through the 3D viewport (ED-400). A separate case rather than a flag on the one
# above, because the two share no drawing at all: one composes sprites, the other builds a
# wireframe and projects it. A crash in the second would be invisible from the first.
add_test(NAME CnaStudioHeadlessSmoke3D
    COMMAND cna-studio --headless --view=3d
        --project=${CMAKE_CURRENT_SOURCE_DIR}/../examples/HelloSprites/HelloSprites.cnaproject)
set_tests_properties(CnaStudioHeadlessSmoke3D PROPERTIES TIMEOUT 60)

# A bad --view is an error rather than a silent 2D start: the flag exists to be set from a script
# that cannot see the window it asked for, and a typo there must not pass quietly.
add_test(NAME CnaStudioRejectsUnknownView COMMAND cna-studio --view=isometric)
set_tests_properties(CnaStudioRejectsUnknownView PROPERTIES WILL_FAIL TRUE)

# The same rule for --orbit, and for the same reason: it exists to make a 3D screenshot
# reproducible, so a run that quietly kept the default angle would produce a picture that looks
# fine and shows the wrong thing.
add_test(NAME CnaStudioRejectsMalformedOrbit COMMAND cna-studio --view=3d --orbit=35)
set_tests_properties(CnaStudioRejectsMalformedOrbit PROPERTIES WILL_FAIL TRUE)

# And the flag working, headless, over a project with a model in it -- which is also the smoke
# test for ED-405's mesh path: the wireframe builder walks a real imported glTF here rather than
# the bounding box it drew before.
add_test(NAME CnaStudioHeadlessSmoke3DOrbited
    COMMAND cna-studio --headless --view=3d --orbit=35,25
        --project=${CMAKE_CURRENT_SOURCE_DIR}/../examples/HelloSprites/HelloSprites.cnaproject)
set_tests_properties(CnaStudioHeadlessSmoke3DOrbited PROPERTIES TIMEOUT 60)

# The native Studio shell, rendered end to end through the real binary and written as a PNG.
#
# Headless on purpose: the shell's geometry is CNA-free and is rasterised on the CPU, so this runs
# on a build machine with no GPU and no display. The assertion is the image -- a run that merely
# exited cleanly cannot tell a working shell from one that drew nothing.
add_test(NAME CnaStudioShellPreview
    COMMAND cna-studio --shell-preview=${CMAKE_CURRENT_BINARY_DIR}/shell-preview.png
                       --shell-size=1280x720)
set_tests_properties(CnaStudioShellPreview PROPERTIES TIMEOUT 60)

add_test(NAME CnaStudioShellPreviewLightTheme
    COMMAND cna-studio --shell-preview=${CMAKE_CURRENT_BINARY_DIR}/shell-preview-light.png
                       --shell-size=1280x720 --shell-theme=light)
set_tests_properties(CnaStudioShellPreviewLightTheme PROPERTIES TIMEOUT 60)

# Every DPI scale Studio supports, through the real binary. 125% and 175% are included because they
# are where a layout that happened to round cleanly at 150% and 200% falls apart, and they are the
# scales a Windows laptop actually ships on.
foreach(_scale "125" "150" "175" "200")
    add_test(NAME CnaStudioShellPreviewHiDpi${_scale}
        COMMAND cna-studio --shell-preview=${CMAKE_CURRENT_BINARY_DIR}/shell-preview-${_scale}.png
                           --shell-size=1600x900 --shell-scale=${_scale}e-2)
    set_tests_properties(CnaStudioShellPreviewHiDpi${_scale} PROPERTIES TIMEOUT 60)
endforeach()

# The interactive states. A shell captured only at rest leaves every hover, pressed, highlight and
# popup token untested by any image -- and those are exactly the tokens a theme gets wrong.
add_test(NAME CnaStudioShellPreviewOpenMenu
    COMMAND cna-studio --shell-preview=${CMAKE_CURRENT_BINARY_DIR}/shell-menu.png
                       --shell-size=1280x720 --shell-open-menu=File --shell-pointer=40,90)
set_tests_properties(CnaStudioShellPreviewOpenMenu PROPERTIES TIMEOUT 60)

add_test(NAME CnaStudioShellPreviewPressedToolbar
    COMMAND cna-studio --shell-preview=${CMAKE_CURRENT_BINARY_DIR}/shell-pressed.png
                       --shell-size=1280x720 --shell-pointer=20,40 --shell-mouse-down)
set_tests_properties(CnaStudioShellPreviewPressedToolbar PROPERTIES TIMEOUT 60)

# A menu title that does not exist is an error naming the ones that do, rather than a picture of a
# closed menu that looks exactly like a working run.
# The drop preview, which is the one interaction state a single input snapshot cannot express: a
# drag needs a press in one place and a pointer in another.
add_test(NAME CnaStudioShellPreviewDockDrag
    COMMAND cna-studio --shell-preview=${CMAKE_CURRENT_BINARY_DIR}/shell-preview-drag.png
                       --shell-size=1280x720 --shell-drag=layers
                       --shell-pointer=1000,400 --shell-mouse-down)
set_tests_properties(CnaStudioShellPreviewDockDrag PROPERTIES TIMEOUT 60)

# The ported panels with real content in them. Until the preview could bind them, every screenshot
# in this suite showed five empty rectangles where the panels are -- so nothing visual covered the
# panels at all, which is most of what a user looks at.
add_test(NAME CnaStudioShellPreviewPortedPanels
    COMMAND cna-studio --shell-preview=${CMAKE_CURRENT_BINARY_DIR}/shell-panels.png
                       --shell-size=1400x900
                       --project=${CMAKE_SOURCE_DIR}/examples/HelloSprites/HelloSprites.cnaproject)
set_tests_properties(CnaStudioShellPreviewPortedPanels PROPERTIES TIMEOUT 60)

# The Details panel with something selected, which is where every property editor actually shows:
# an asset picker, a colour swatch, an integer rectangle and an enumeration drop-down.
add_test(NAME CnaStudioShellPreviewDetailsPanel
    COMMAND cna-studio --shell-preview=${CMAKE_CURRENT_BINARY_DIR}/shell-details.png
                       --shell-size=1400x900 --select=Player
                       --project=${CMAKE_SOURCE_DIR}/examples/HelloSprites/HelloSprites.cnaproject)
set_tests_properties(CnaStudioShellPreviewDetailsPanel PROPERTIES TIMEOUT 60)

add_test(NAME CnaStudioRejectsSelectingAnEntityThatIsNotThere
    COMMAND cna-studio --shell-preview=never.png --select=Nonexistent
                       --project=${CMAKE_SOURCE_DIR}/examples/HelloSprites/HelloSprites.cnaproject)
set_tests_properties(CnaStudioRejectsSelectingAnEntityThatIsNotThere PROPERTIES WILL_FAIL TRUE)

# The Layers panel, which is the one view of a scene ordered by draw order rather than by hierarchy.
add_test(NAME CnaStudioShellPreviewLayersPanel
    COMMAND cna-studio --shell-preview=${CMAKE_CURRENT_BINARY_DIR}/shell-layers.png
                       --shell-size=1400x900 --panel=layers
                       --project=${CMAKE_SOURCE_DIR}/examples/HelloSprites/HelloSprites.cnaproject)
set_tests_properties(CnaStudioShellPreviewLayersPanel PROPERTIES TIMEOUT 60)

# The Diagnostics panel, whose whole value is what it says -- and which on a CNA-free build has to
# say "unknown" rather than leaving cells blank.
add_test(NAME CnaStudioShellPreviewDiagnosticsPanel
    COMMAND cna-studio --shell-preview=${CMAKE_CURRENT_BINARY_DIR}/shell-diagnostics.png
                       --shell-size=1400x900 --panel=diagnostics
                       --project=${CMAKE_SOURCE_DIR}/examples/HelloSprites/HelloSprites.cnaproject)
set_tests_properties(CnaStudioShellPreviewDiagnosticsPanel PROPERTIES TIMEOUT 60)

# The History panel, which is a third tab beside Details and therefore only capturable by raising
# it -- and whose one row on a freshly opened project is itself the thing worth photographing.
add_test(NAME CnaStudioShellPreviewHistoryPanel
    COMMAND cna-studio --shell-preview=${CMAKE_CURRENT_BINARY_DIR}/shell-history.png
                       --shell-size=1400x900 --panel=history
                       --project=${CMAKE_SOURCE_DIR}/examples/HelloSprites/HelloSprites.cnaproject)
set_tests_properties(CnaStudioShellPreviewHistoryPanel PROPERTIES TIMEOUT 60)

# The Problems panel, whose good state is emptiness -- so the capture is of a panel that says so
# rather than of a blank rectangle.
add_test(NAME CnaStudioShellPreviewProblemsPanel
    COMMAND cna-studio --shell-preview=${CMAKE_CURRENT_BINARY_DIR}/shell-problems.png
                       --shell-size=1400x900 --panel=problems
                       --project=${CMAKE_SOURCE_DIR}/examples/HelloSprites/HelloSprites.cnaproject)
set_tests_properties(CnaStudioShellPreviewProblemsPanel PROPERTIES TIMEOUT 60)

# The Build panel, which shares a tab bar and is therefore only capturable by raising it.
add_test(NAME CnaStudioShellPreviewBuildPanel
    COMMAND cna-studio --shell-preview=${CMAKE_CURRENT_BINARY_DIR}/shell-build.png
                       --shell-size=1400x900 --panel=build
                       --project=${CMAKE_SOURCE_DIR}/examples/HelloSprites/HelloSprites.cnaproject)
set_tests_properties(CnaStudioShellPreviewBuildPanel PROPERTIES TIMEOUT 60)

add_test(NAME CnaStudioRejectsRaisingAPanelThatIsNotOpen
    COMMAND cna-studio --shell-preview=never.png --panel=nosuchpanel)
set_tests_properties(CnaStudioRejectsRaisingAPanelThatIsNotOpen PROPERTIES WILL_FAIL TRUE)

add_test(NAME CnaStudioPreviewRejectsAProjectItCannotOpen
    COMMAND cna-studio --shell-preview=never.png --project=/definitely/not/here/Missing.cnaproject)
set_tests_properties(CnaStudioPreviewRejectsAProjectItCannotOpen PROPERTIES WILL_FAIL TRUE)

# A context menu on a panel tab, which is the shell's own first user of the mechanism.
add_test(NAME CnaStudioShellPreviewContextMenu
    COMMAND cna-studio --shell-preview=${CMAKE_CURRENT_BINARY_DIR}/shell-context.png
                       --shell-size=1280x720 --shell-pointer=133,533 --shell-right-click)
set_tests_properties(CnaStudioShellPreviewContextMenu PROPERTIES TIMEOUT 60)

# And right-clicking something that offers none is an error rather than a capture of the shell at
# rest passing for a capture of a context menu.
add_test(NAME CnaStudioRejectsAContextMenuThatNeverOpens
    COMMAND cna-studio --shell-preview=${CMAKE_CURRENT_BINARY_DIR}/no-context.png
                       --shell-size=1280x720 --shell-pointer=640,300 --shell-right-click)
set_tests_properties(CnaStudioRejectsAContextMenuThatNeverOpens PROPERTIES
    WILL_FAIL TRUE TIMEOUT 60)

# A submenu open beside its parent, which is the one menu state a top-level capture cannot reach.
add_test(NAME CnaStudioShellPreviewSubmenu
    COMMAND cna-studio --shell-preview=${CMAKE_CURRENT_BINARY_DIR}/shell-submenu.png
                       --shell-size=1280x720 "--shell-open-menu=Window>Panels"
                       --shell-pointer=1000,110)
set_tests_properties(CnaStudioShellPreviewSubmenu PROPERTIES TIMEOUT 60)

add_test(NAME CnaStudioRejectsUnknownShellSubmenu
    COMMAND cna-studio --shell-preview=never.png "--shell-open-menu=Window>Nonexistent")
set_tests_properties(CnaStudioRejectsUnknownShellSubmenu PROPERTIES WILL_FAIL TRUE)

# The icon toolbar is only as discoverable as its tooltips, so the tooltip is captured rather than
# assumed: the run fails outright if resting the pointer on a toolbar button produces none.
add_test(NAME CnaStudioShellPreviewTooltip
    COMMAND cna-studio --shell-preview=${CMAKE_CURRENT_BINARY_DIR}/shell-tooltip.png
                       --shell-size=1280x720 --shell-pointer=20,40 --shell-tooltip)
set_tests_properties(CnaStudioShellPreviewTooltip PROPERTIES TIMEOUT 60)

# And resting somewhere that offers none is an error rather than a picture of the shell at rest
# passing for a picture of a tooltip.
add_test(NAME CnaStudioRejectsATooltipThatNeverAppears
    COMMAND cna-studio --shell-preview=${CMAKE_CURRENT_BINARY_DIR}/no-tooltip.png
                       --shell-size=1280x720 --shell-pointer=640,400 --shell-tooltip)
set_tests_properties(CnaStudioRejectsATooltipThatNeverAppears PROPERTIES WILL_FAIL TRUE TIMEOUT 60)

# Dragging to a point too close to the tab starts nothing, and a capture of a drag that never
# began would be a picture of the shell at rest passing for a picture of a drag.
add_test(NAME CnaStudioRejectsADragThatStartsNothing
    COMMAND cna-studio --shell-preview=${CMAKE_CURRENT_BINARY_DIR}/never-dragged.png
                       --shell-size=1280x720 --shell-drag=layers --shell-mouse-down)
set_tests_properties(CnaStudioRejectsADragThatStartsNothing PROPERTIES WILL_FAIL TRUE TIMEOUT 60)

add_test(NAME CnaStudioRejectsDraggingAPanelThatIsNotOpen
    COMMAND cna-studio --shell-preview=${CMAKE_CURRENT_BINARY_DIR}/never-dragged2.png
                       --shell-size=1280x720 --shell-drag=nosuchpanel
                       --shell-pointer=1000,400 --shell-mouse-down)
set_tests_properties(CnaStudioRejectsDraggingAPanelThatIsNotOpen PROPERTIES
    WILL_FAIL TRUE TIMEOUT 60)

add_test(NAME CnaStudioRejectsUnknownShellMenu
    COMMAND cna-studio --shell-preview=never.png --shell-open-menu=Nonexistent)
set_tests_properties(CnaStudioRejectsUnknownShellMenu PROPERTIES WILL_FAIL TRUE)

add_test(NAME CnaStudioRejectsMalformedShellPointer
    COMMAND cna-studio --shell-preview=never.png --shell-pointer=40)
set_tests_properties(CnaStudioRejectsMalformedShellPointer PROPERTIES WILL_FAIL TRUE)

# A malformed size or theme is an error rather than a silent default, for the same reason --view is:
# these flags exist to be set from a script that cannot see the picture it asked for.
add_test(NAME CnaStudioRejectsMalformedShellSize
    COMMAND cna-studio --shell-preview=never.png --shell-size=1280)
set_tests_properties(CnaStudioRejectsMalformedShellSize PROPERTIES WILL_FAIL TRUE)

add_test(NAME CnaStudioRejectsUnknownShellTheme
    COMMAND cna-studio --shell-preview=never.png --shell-theme=neon)
set_tests_properties(CnaStudioRejectsUnknownShellTheme PROPERTIES WILL_FAIL TRUE)

# The host capability contract, printed from the real binary. Capability-driven eligibility is an
# architectural invariant (docs/ARCHITECTURE.md §4), and a contract nobody can read is one nobody
# will notice has quietly grown a renderer name in it.
add_test(NAME CnaStudioHostCapabilityContract COMMAND cna-studio --host-capabilities)
set_tests_properties(CnaStudioHostCapabilityContract PROPERTIES
    PASS_REGULAR_EXPRESSION "ThreeDimensionalPipeline"
    TIMEOUT 60)

add_test(NAME CnaStudioHelp COMMAND cna-studio --help)
add_test(NAME CnaStudioListBackends COMMAND cna-studio --list-backends)

# --graphics must fail: CNA fixes its backend at compile time, so silently accepting the flag
# would teach a mental model the framework does not support.
add_test(NAME CnaStudioRejectsGraphicsFlag COMMAND cna-studio --graphics=vulkan)
set_tests_properties(CnaStudioRejectsGraphicsFlag PROPERTIES WILL_FAIL TRUE)

add_test(NAME CnaPlayerHelp COMMAND cna-player --help)

if(CNA_STUDIO_WITH_CNA)
    # How many compile jobs the standalone export test gives the exported project's own build. It
    # builds CNA from source, so the default is generous; lower it on a machine where a second
    # full-throttle build alongside this one would thrash.
    set(CNA_STUDIO_EXPORT_TEST_JOBS "8" CACHE STRING
        "Parallel jobs for the standalone export test's build of the exported project")

    # Where the two ctests that need a real graphics context find one. Declared here rather than
    # inside one of the blocks below because both need it and either can be the one that runs:
    # the native shell needs CNA alone, the ImGui editor needs CNA and Dear ImGui.
    set(CNA_STUDIO_TEST_DISPLAY ":0" CACHE STRING
        "DISPLAY used by cna-studio ctests that need a real graphics context")

    # The native Studio shell in a real window, through a real CNA renderer -- the whole strangler
    # seam end to end: StudioShell describes, CnaUiPlatform fills UiInputState from CNA's input,
    # CnaUiRenderer draws UiDrawData through CNA's graphics API, and the back buffer comes back as
    # a PNG. Nothing below CTest can assert this; the headless preview renders the same draw list
    # through Studio's own software rasterizer, which would pass just as happily if CNA's side of
    # the seam were broken.
    #
    # Two assertions, not one, because each misses what the other catches. The draw-call count
    # separates a shell that submitted geometry from one that built its widget tree and submitted
    # nothing -- that one would open a window, clear it to the theme background, write a perfectly
    # valid screenshot of an empty frame and exit zero. And `--screenshot-min-colors` separates a
    # frame that *drew* from one that submitted geometry which rendered to nothing: a wrong blend
    # state, a clip rectangle that excludes the window, a vertex colour with no alpha, or a
    # renderer quietly dropping the calls all report every draw call and produce a blank picture.
    #
    # Sixteen because a blank frame has one colour, or two where something was cleared to another
    # shade, and a real shell frame has over a thousand -- antialiased text alone spreads at every
    # glyph edge. Nothing is gained by tuning a threshold with that much room either side.
    add_test(NAME CnaStudioNativeShellWindowSmoke
        COMMAND cna-studio --ui=studio --workspace=none
            --frames=20
            --screenshot=${CMAKE_CURRENT_BINARY_DIR}/native-shell-smoke.png
            --screenshot-min-colors=16)
    set_tests_properties(CnaStudioNativeShellWindowSmoke PROPERTIES
        PASS_REGULAR_EXPRESSION "native shell on .+, [0-9]+ frames, [0-9]+x[0-9]+ display, [1-9][0-9]* draw calls, [1-9][0-9]* triangles")

    # The light theme through the same path. It is not a redundant run: the theme decides every
    # colour the renderer uploads, and a vertex-colour or blend-state mistake that happens to be
    # invisible on a dark background is exactly the kind of thing one theme catches and the other
    # does not.
    add_test(NAME CnaStudioNativeShellWindowSmokeLightTheme
        COMMAND cna-studio --ui=studio --shell-theme=light --workspace=none
            --frames=20
            --screenshot=${CMAKE_CURRENT_BINARY_DIR}/native-shell-smoke-light.png
            --screenshot-min-colors=16)
    set_tests_properties(CnaStudioNativeShellWindowSmokeLightTheme PROPERTIES
        PASS_REGULAR_EXPRESSION "native shell on .+, [0-9]+ frames, [0-9]+x[0-9]+ display, [1-9][0-9]* draw calls, [1-9][0-9]* triangles")

    # At 2x, where the atlas, the layout and the input mapping all change together. The unit tests
    # cover High-DPI geometry, but only a real device proves the scaled texture actually uploads.
    add_test(NAME CnaStudioNativeShellWindowSmokeHiDpi
        COMMAND cna-studio --ui=studio --shell-scale=2 --workspace=none
            --frames=20
            --screenshot=${CMAKE_CURRENT_BINARY_DIR}/native-shell-smoke-2x.png
            --screenshot-min-colors=16)
    set_tests_properties(CnaStudioNativeShellWindowSmokeHiDpi PROPERTIES
        PASS_REGULAR_EXPRESSION "native shell on .+, [0-9]+ frames, [0-9]+x[0-9]+ display, [1-9][0-9]* draw calls, [1-9][0-9]* triangles")

    # The scene itself, composited into the native shell's viewport (STUDIO-04012). The assertion
    # is the words "compositing the scene", not the screenshot: a viewport drawing its placeholder
    # grid and one drawing the scene produce the same draw-call count and the same valid picture.
    add_test(NAME CnaStudioNativeShellCompositesTheScene
        COMMAND cna-studio --ui=studio --workspace=none --frames=20
            --project=${CMAKE_SOURCE_DIR}/examples/HelloSprites/HelloSprites.cnaproject
            --screenshot=${CMAKE_CURRENT_BINARY_DIR}/native-shell-viewport.png
            --screenshot-min-colors=16)
    set_tests_properties(CnaStudioNativeShellCompositesTheScene PROPERTIES
        PASS_REGULAR_EXPRESSION "viewport compositing the scene")

    # And with the viewport docked away, where it must say so rather than leaving last frame's
    # picture up: a stale viewport is worse than an empty one, because it looks live.
    add_test(NAME CnaStudioNativeShellSaysWhenTheViewportIsNotShowing
        COMMAND cna-studio --ui=studio --workspace=none --frames=20 --panel=output
            --project=${CMAKE_SOURCE_DIR}/examples/HelloSprites/HelloSprites.cnaproject)
    set_tests_properties(CnaStudioNativeShellSaysWhenTheViewportIsNotShowing PROPERTIES
        PASS_REGULAR_EXPRESSION "viewport (compositing the scene|showing the placeholder)")

    # Same reasoning as the ImGui smoke tests: SOFTWARE and HEADLESS need no window system, every
    # other renderer needs a real context, and forcing the dummy driver there fails for reasons
    # that have nothing to do with the shell.
    if(CNA_GRAPHICS_RENDERER STREQUAL "SOFTWARE" OR CNA_GRAPHICS_RENDERER STREQUAL "HEADLESS")
        set_tests_properties(CnaStudioNativeShellWindowSmoke
                             CnaStudioNativeShellWindowSmokeLightTheme
                             CnaStudioNativeShellWindowSmokeHiDpi
                             CnaStudioNativeShellCompositesTheScene
                             CnaStudioNativeShellSaysWhenTheViewportIsNotShowing
            PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy" TIMEOUT 180)
    else()
        set_tests_properties(CnaStudioNativeShellWindowSmoke
                             CnaStudioNativeShellWindowSmokeLightTheme
                             CnaStudioNativeShellWindowSmokeHiDpi
                             CnaStudioNativeShellCompositesTheScene
                             CnaStudioNativeShellSaysWhenTheViewportIsNotShowing
            PROPERTIES ENVIRONMENT "DISPLAY=${CNA_STUDIO_TEST_DISPLAY}"
                       LABELS "needs-display" TIMEOUT 180)
    endif()

    # The first ported panel, drawn on a real device (STUDIO-07005). Raised to the front, because
    # a panel sharing a tab strip with three others cannot be photographed otherwise -- and the
    # capture is the assertion: the ported panel draws considerably more geometry than the empty
    # surface it replaced, so a content function that silently stopped running would show up here
    # as a triangle count that fell back to the unported one.
    add_test(NAME CnaStudioNativeShellOutputLog
        COMMAND cna-studio --ui=studio --workspace=none --panel=output
            --frames=20
            --screenshot=${CMAKE_CURRENT_BINARY_DIR}/native-shell-output-log.png
            --screenshot-min-colors=16)
    set_tests_properties(CnaStudioNativeShellOutputLog PROPERTIES
        PASS_REGULAR_EXPRESSION "output log showing [1-9][0-9]* of [1-9][0-9]* messages")

    if(CNA_GRAPHICS_RENDERER STREQUAL "SOFTWARE" OR CNA_GRAPHICS_RENDERER STREQUAL "HEADLESS")
        set_tests_properties(CnaStudioNativeShellOutputLog PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy" TIMEOUT 180)
    else()
        set_tests_properties(CnaStudioNativeShellOutputLog PROPERTIES
            ENVIRONMENT "DISPLAY=${CNA_STUDIO_TEST_DISPLAY}" LABELS "needs-display" TIMEOUT 180)
    endif()

    # The native shell with a real editor behind it. The assertion is the status bar: a row count
    # says the Output Log drew something, and only this says the StudioContext opened the project
    # the shell was given and the shell is showing what it opened.
    add_test(NAME CnaStudioNativeShellWithAProject
        COMMAND cna-studio --ui=studio --workspace=none --panel=output
            --project=${CMAKE_CURRENT_SOURCE_DIR}/../examples/HelloSprites/HelloSprites.cnaproject
            --frames=20
            --screenshot=${CMAKE_CURRENT_BINARY_DIR}/native-shell-project.png
            --screenshot-min-colors=16)
    # Three assertions in one line, each of which only the whole stack can satisfy: the context
    # opened the project (the status bar), the World Outliner is reading the scene it opened (five
    # entities, which is what Level01 holds), and the Output Log is reading the editor's own
    # messages rather than a demonstration of them.
    set_tests_properties(CnaStudioNativeShellWithAProject PROPERTIES
        PASS_REGULAR_EXPRESSION "status 'HelloSprites.*Level01'.*outliner showing [1-9][0-9]* of 5 entities.*output log showing [1-9][0-9]* of [1-9][0-9]* messages")

    # The same run with something selected, so the Details panel has an entity to inspect. Four
    # ported panels' worth of state in one line, none of which any one of them could produce alone:
    # the context opened the project, the outliner read its scene, the selection reached the
    # inspector, and the inspector found the components on it.
    add_test(NAME CnaStudioNativeShellDetails
        COMMAND cna-studio --ui=studio --workspace=none --select=Player
            --project=${CMAKE_CURRENT_SOURCE_DIR}/../examples/HelloSprites/HelloSprites.cnaproject
            --frames=20
            --screenshot=${CMAKE_CURRENT_BINARY_DIR}/native-shell-details.png
            --screenshot-min-colors=16)
    set_tests_properties(CnaStudioNativeShellDetails PROPERTIES
        PASS_REGULAR_EXPRESSION "outliner showing [1-9][0-9]* of 5 entities.*details showing [1-9][0-9]* rows")

    if(CNA_GRAPHICS_RENDERER STREQUAL "SOFTWARE" OR CNA_GRAPHICS_RENDERER STREQUAL "HEADLESS")
        set_tests_properties(CnaStudioNativeShellDetails PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy" TIMEOUT 180)
    else()
        set_tests_properties(CnaStudioNativeShellDetails PROPERTIES
            ENVIRONMENT "DISPLAY=${CNA_STUDIO_TEST_DISPLAY}" LABELS "needs-display" TIMEOUT 180)
    endif()

    if(CNA_GRAPHICS_RENDERER STREQUAL "SOFTWARE" OR CNA_GRAPHICS_RENDERER STREQUAL "HEADLESS")
        set_tests_properties(CnaStudioNativeShellWithAProject PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy" TIMEOUT 180)
    else()
        set_tests_properties(CnaStudioNativeShellWithAProject PROPERTIES
            ENVIRONMENT "DISPLAY=${CNA_STUDIO_TEST_DISPLAY}" LABELS "needs-display" TIMEOUT 180)
    endif()

    # Opening a project through the native shell must not rewrite it, for the same reason the
    # headless path must not: a sidecar rewritten on every open fills a repository with diffs
    # nobody made.
    add_test(NAME CnaStudioNativeShellDoesNotModifyTheProject
        COMMAND "${CMAKE_COMMAND}"
            "-DSTUDIO=$<TARGET_FILE:cna-studio>"
            "-DEXAMPLES=${CMAKE_CURRENT_SOURCE_DIR}/../examples"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/NativeShellProjectTest.cmake")
    set_tests_properties(CnaStudioNativeShellDoesNotModifyTheProject PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy" TIMEOUT 180)

    # The workspace layout survives a real process exit. Two processes rather than one, because
    # that is the thing being tested: a unit test proves the store round-trips in memory, and only
    # a second process proves Studio writes on the way out and reads on the way in. The failure it
    # catches -- a shell that saves nothing and silently starts fresh every time -- is invisible to
    # any test that never exits.
    add_test(NAME CnaStudioWorkspacePersistence
        COMMAND "${CMAKE_COMMAND}"
            "-DSTUDIO=$<TARGET_FILE:cna-studio>"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/workspace-persistence"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/WorkspacePersistenceTest.cmake")
    set_tests_properties(CnaStudioWorkspacePersistence PROPERTIES TIMEOUT 300)

    if(CNA_GRAPHICS_RENDERER STREQUAL "SOFTWARE" OR CNA_GRAPHICS_RENDERER STREQUAL "HEADLESS")
        set_tests_properties(CnaStudioWorkspacePersistence PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy")
    else()
        set_tests_properties(CnaStudioWorkspacePersistence PROPERTIES
            ENVIRONMENT "DISPLAY=${CNA_STUDIO_TEST_DISPLAY}" LABELS "needs-display")
    endif()

    # The invariant the whole product rests on, checked by doing it: export the example project
    # into an empty directory, configure it with nothing but CMake and a CNA checkout, compile it,
    # and run it. Studio is not consulted after the export.
    #
    # An expensive test -- it builds CNA -- and it earns its cost. Every failure of this invariant
    # found so far has been one that reading the exported tree could not have caught: CNA's video
    # option is tri-state, so the boolean ON Studio passed made an exported game require FFmpeg;
    # and CNA builds its own tests and examples by default from a subdirectory, so the configure
    # failed before a line of the game compiled. Both looked correct on paper.
    #
    # Labelled, so a developer iterating on the UI can exclude it with -LE slow and CI cannot.
    add_test(NAME CnaStudioStandaloneExport
        COMMAND "${CMAKE_COMMAND}"
            "-DSTUDIO=$<TARGET_FILE:cna-studio>"
            "-DPROJECT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/../examples/HelloSprites/HelloSprites.cnaproject"
            "-DCNA_ROOT=${CNA_STUDIO_CNA_ROOT}"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/standalone-export"
            "-DRENDERER=${CNA_GRAPHICS_RENDERER}"
            "-DJOBS=${CNA_STUDIO_EXPORT_TEST_JOBS}"
            "-DGENERATOR=${CMAKE_GENERATOR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/StandaloneExportTest.cmake")
    set_tests_properties(CnaStudioStandaloneExport PROPERTIES
        LABELS "slow"
        TIMEOUT 5400)

    if(CNA_GRAPHICS_RENDERER STREQUAL "SOFTWARE" OR CNA_GRAPHICS_RENDERER STREQUAL "HEADLESS")
        set_tests_properties(CnaStudioStandaloneExport PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy")
    else()
        set_property(TEST CnaStudioStandaloneExport APPEND PROPERTY LABELS "needs-display")
        set_tests_properties(CnaStudioStandaloneExport PROPERTIES
            ENVIRONMENT "DISPLAY=${CNA_STUDIO_TEST_DISPLAY}")
    endif()

    # The default, with no --ui at all: `STUDIO-06015` made that the native shell, and a switch
    # nothing asserts is a switch a later change can undo without anybody noticing. The status line
    # is what tells the two apart -- the native shell reports what it is drawing on and how many
    # panels it filled, and the legacy editor reports neither.
    add_test(NAME CnaStudioDefaultsToTheNativeShell
        COMMAND cna-studio --workspace=none
            --project=${CMAKE_CURRENT_SOURCE_DIR}/../examples/HelloSprites/HelloSprites.cnaproject
            --frames=20
            --screenshot=${CMAKE_CURRENT_BINARY_DIR}/default-shell.png
            --screenshot-min-colors=16)
    set_tests_properties(CnaStudioDefaultsToTheNativeShell PROPERTIES
        PASS_REGULAR_EXPRESSION "native shell on .+, [0-9]+ frames"
        TIMEOUT 180)
    if(CNA_GRAPHICS_RENDERER STREQUAL "SOFTWARE" OR CNA_GRAPHICS_RENDERER STREQUAL "HEADLESS")
        set_tests_properties(CnaStudioDefaultsToTheNativeShell PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy")
    else()
        set_property(TEST CnaStudioDefaultsToTheNativeShell APPEND PROPERTY LABELS "needs-display")
        set_tests_properties(CnaStudioDefaultsToTheNativeShell PROPERTIES
            ENVIRONMENT "DISPLAY=${CNA_STUDIO_TEST_DISPLAY}")
    endif()

    # And the fallback still reachable by name, which is the other half of "default" meaning
    # anything: a flag that no longer worked would make this a removal rather than a switch.
    add_test(NAME CnaStudioFallsBackToImGuiWhenAsked
        COMMAND cna-studio --ui=imgui --headless
            --project=${CMAKE_CURRENT_SOURCE_DIR}/../examples/HelloSprites/HelloSprites.cnaproject)
    set_tests_properties(CnaStudioFallsBackToImGuiWhenAsked PROPERTIES TIMEOUT 60)

    # The native shell has no headless mode: without a frame limit it runs until the window is
    # closed. A screenshot asked for on those terms can never be taken, so the run has to be
    # rejected before the window opens -- if it were not, this test would hang instead of failing.
    add_test(NAME CnaStudioNativeShellScreenshotNeedsFrameLimit
        COMMAND cna-studio --ui=studio --workspace=none
            --screenshot=${CMAKE_CURRENT_BINARY_DIR}/native-shell-never-written.png)
    set_tests_properties(CnaStudioNativeShellScreenshotNeedsFrameLimit PROPERTIES
        WILL_FAIL TRUE TIMEOUT 60)
endif()

if(NOT CNA_STUDIO_WITH_CNA)
    # Without CNA there is no window and no graphics device, so --ui=studio has nothing to run in.
    # Failing with that explanation is the contract; silently falling back to the ImGui editor
    # would give a user who asked for the native shell a different program without saying so.
    add_test(NAME CnaStudioNativeShellNeedsCna COMMAND cna-studio --ui=studio)
    set_tests_properties(CnaStudioNativeShellNeedsCna PROPERTIES WILL_FAIL TRUE TIMEOUT 60)
endif()

if(CNA_STUDIO_WITH_CNA AND CNA_STUDIO_WITH_IMGUI)
    # The legacy editor, named explicitly. It was the default when these cases were written and is
    # not one since `STUDIO-06015`; asking for it by name is what keeps the migration fallback
    # covered rather than quietly retired, and is also what keeps these cases about the editor they
    # were written for rather than silently becoming a second set of native-shell tests.
    #
    # The real thing: open a window, draw the editor through CNA's public API, and write a PNG of
    # the result. A run that merely exits cleanly cannot tell a working editor from one that opened
    # a blank window, so the screenshot is the assertion -- if the editor drew nothing, the file
    # never appears and the test fails.
    #
    add_test(NAME CnaStudioWindowSmoke
        COMMAND cna-studio --ui=imgui
            --project=${CMAKE_CURRENT_SOURCE_DIR}/../examples/HelloSprites/HelloSprites.cnaproject
            --frames=20
            --screenshot=${CMAKE_CURRENT_BINARY_DIR}/editor-smoke.png
            --screenshot-min-colors=16)

    # How this test reaches a display depends on the backend, and getting it wrong makes the test
    # fail for reasons that have nothing to do with the editor.
    #
    # SOFTWARE and HEADLESS render without a window at all, so SDL's dummy video driver is enough
    # and the test runs anywhere. Every other backend needs a real graphics context: the dummy
    # driver cannot provide one, and forcing it there produces a failure that looks like a bug in
    # the editor and is not. Those get a DISPLAY instead, following the same CNA_TEST_DISPLAY
    # convention CNA itself uses for its window-creating ctests -- point it at an Xvfb server on a
    # build machine with no desktop.
    if(CNA_GRAPHICS_RENDERER STREQUAL "SOFTWARE" OR CNA_GRAPHICS_RENDERER STREQUAL "HEADLESS")
        set_tests_properties(CnaStudioWindowSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
            TIMEOUT 180)
    else()
        set_tests_properties(CnaStudioWindowSmoke PROPERTIES
            ENVIRONMENT "DISPLAY=${CNA_STUDIO_TEST_DISPLAY}"
            LABELS "needs-display"
            TIMEOUT 180)
    endif()

    # The 3D viewport against a real device. It draws through a different path from the 2D one --
    # every line is a rotated quad rather than a sprite -- so a backend that cannot do that would
    # pass the smoke test above and fail here.
    add_test(NAME CnaStudioWindowSmoke3D
        COMMAND cna-studio --ui=imgui --view=3d
            --project=${CMAKE_CURRENT_SOURCE_DIR}/../examples/HelloSprites/HelloSprites.cnaproject
            --frames=20
            --screenshot=${CMAKE_CURRENT_BINARY_DIR}/editor-smoke-3d.png
            --screenshot-min-colors=16)

    if(CNA_GRAPHICS_RENDERER STREQUAL "SOFTWARE")
        set_tests_properties(CnaStudioWindowSmoke3D PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
            TIMEOUT 180)
    else()
        set_tests_properties(CnaStudioWindowSmoke3D PROPERTIES
            ENVIRONMENT "DISPLAY=${CNA_STUDIO_TEST_DISPLAY}"
            LABELS "needs-display"
            TIMEOUT 180)
    endif()

    # The scene loader, exercised the way a shipped game would use it: a real device, the real
    # example project, and a check that the world transforms match what the editor's own code
    # produces. A loader that parsed correctly but composed a child's transform differently would
    # put a game's sprites somewhere other than where the editor drew them, and nothing short of
    # comparing two screenshots would show it.
    add_test(NAME CnaSceneLoaderDemo
        COMMAND scene-loader-demo
            --scene=${CMAKE_CURRENT_SOURCE_DIR}/../examples/HelloSprites/Scenes/Level01.cnascene
            --assets=${CMAKE_CURRENT_SOURCE_DIR}/../examples/HelloSprites
            --verbose)

    if(CNA_GRAPHICS_RENDERER STREQUAL "SOFTWARE" OR CNA_GRAPHICS_RENDERER STREQUAL "HEADLESS")
        set_tests_properties(CnaSceneLoaderDemo PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
            TIMEOUT 180)
    else()
        set_tests_properties(CnaSceneLoaderDemo PROPERTIES
            ENVIRONMENT "DISPLAY=${CNA_STUDIO_TEST_DISPLAY}"
            LABELS "needs-display"
            TIMEOUT 180)
    endif()

    # The player, doing the thing it exists to do: open a window, draw the example scene through
    # its own camera, and write a PNG of it. Until this existed the player ran the protocol and
    # drew nothing, which is a state no exit code could tell apart from a working one -- so the
    # assertion is the capture: the process exits non-zero when no file was written.
    add_test(NAME CnaPlayerWindowSmoke
        COMMAND cna-player
            --project=${CMAKE_CURRENT_SOURCE_DIR}/../examples/HelloSprites/HelloSprites.cnaproject
            --frames=20
            --screenshot=${CMAKE_CURRENT_BINARY_DIR}/player-smoke.png
            --screenshot-min-colors=16)

    if(CNA_GRAPHICS_RENDERER STREQUAL "SOFTWARE" OR CNA_GRAPHICS_RENDERER STREQUAL "HEADLESS")
        set_tests_properties(CnaPlayerWindowSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
            TIMEOUT 180)
    else()
        set_tests_properties(CnaPlayerWindowSmoke PROPERTIES
            ENVIRONMENT "DISPLAY=${CNA_STUDIO_TEST_DISPLAY}"
            LABELS "needs-display"
            TIMEOUT 180)
    endif()

    # --screenshot without --frames has no final frame to capture, and silently writing nothing
    # would make the smoke test above pass for the wrong reason.
    add_test(NAME CnaStudioScreenshotNeedsFrameLimit
        COMMAND cna-studio
            --project=${CMAKE_CURRENT_SOURCE_DIR}/../examples/HelloSprites/HelloSprites.cnaproject
            --screenshot=${CMAKE_CURRENT_BINARY_DIR}/never-written.png
            --headless)
    set_tests_properties(CnaStudioScreenshotNeedsFrameLimit PROPERTIES TIMEOUT 60)
endif()

# Comparing backends means decoding the captures, and decoding needs a graphics device. A run that
# went ahead headless would report that every capture was unreadable, which is a confusing way to
# say "wrong configuration".
add_test(NAME CnaStudioComparisonNeedsADevice
    COMMAND cna-studio --compare-backends --headless)
set_tests_properties(CnaStudioComparisonNeedsADevice PROPERTIES WILL_FAIL TRUE TIMEOUT 60)

# cna-player requires a project: launching one without is a mistake worth failing on, not
# something to paper over with a default.
add_test(NAME CnaPlayerRequiresAProject COMMAND cna-player)
set_tests_properties(CnaPlayerRequiresAProject PROPERTIES WILL_FAIL TRUE)
