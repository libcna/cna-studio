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
                       --shell-size=1280x720 --shell-pointer=40,40 --shell-mouse-down)
set_tests_properties(CnaStudioShellPreviewPressedToolbar PROPERTIES TIMEOUT 60)

# A menu title that does not exist is an error naming the ones that do, rather than a picture of a
# closed menu that looks exactly like a working run.
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
    # The assertion is the draw-call count, not the exit code. A shell that built its widget tree
    # and then submitted nothing would open a window, clear it to the theme background, write a
    # perfectly valid screenshot of an empty frame and exit zero; requiring a non-zero count of
    # draw calls and triangles is what separates that from a shell that actually drew.
    add_test(NAME CnaStudioNativeShellWindowSmoke
        COMMAND cna-studio --ui=studio
            --frames=20
            --screenshot=${CMAKE_CURRENT_BINARY_DIR}/native-shell-smoke.png)
    set_tests_properties(CnaStudioNativeShellWindowSmoke PROPERTIES
        PASS_REGULAR_EXPRESSION "native shell on .+, [0-9]+ frames, [0-9]+x[0-9]+ display, [1-9][0-9]* draw calls, [1-9][0-9]* triangles")

    # The light theme through the same path. It is not a redundant run: the theme decides every
    # colour the renderer uploads, and a vertex-colour or blend-state mistake that happens to be
    # invisible on a dark background is exactly the kind of thing one theme catches and the other
    # does not.
    add_test(NAME CnaStudioNativeShellWindowSmokeLightTheme
        COMMAND cna-studio --ui=studio --shell-theme=light
            --frames=20
            --screenshot=${CMAKE_CURRENT_BINARY_DIR}/native-shell-smoke-light.png)
    set_tests_properties(CnaStudioNativeShellWindowSmokeLightTheme PROPERTIES
        PASS_REGULAR_EXPRESSION "native shell on .+, [0-9]+ frames, [0-9]+x[0-9]+ display, [1-9][0-9]* draw calls, [1-9][0-9]* triangles")

    # At 2x, where the atlas, the layout and the input mapping all change together. The unit tests
    # cover High-DPI geometry, but only a real device proves the scaled texture actually uploads.
    add_test(NAME CnaStudioNativeShellWindowSmokeHiDpi
        COMMAND cna-studio --ui=studio --shell-scale=2
            --frames=20
            --screenshot=${CMAKE_CURRENT_BINARY_DIR}/native-shell-smoke-2x.png)
    set_tests_properties(CnaStudioNativeShellWindowSmokeHiDpi PROPERTIES
        PASS_REGULAR_EXPRESSION "native shell on .+, [0-9]+ frames, [0-9]+x[0-9]+ display, [1-9][0-9]* draw calls, [1-9][0-9]* triangles")

    # Same reasoning as the ImGui smoke tests: SOFTWARE and HEADLESS need no window system, every
    # other renderer needs a real context, and forcing the dummy driver there fails for reasons
    # that have nothing to do with the shell.
    if(CNA_GRAPHICS_RENDERER STREQUAL "SOFTWARE" OR CNA_GRAPHICS_RENDERER STREQUAL "HEADLESS")
        set_tests_properties(CnaStudioNativeShellWindowSmoke
                             CnaStudioNativeShellWindowSmokeLightTheme
                             CnaStudioNativeShellWindowSmokeHiDpi
            PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy" TIMEOUT 180)
    else()
        set_tests_properties(CnaStudioNativeShellWindowSmoke
                             CnaStudioNativeShellWindowSmokeLightTheme
                             CnaStudioNativeShellWindowSmokeHiDpi
            PROPERTIES ENVIRONMENT "DISPLAY=${CNA_STUDIO_TEST_DISPLAY}"
                       LABELS "needs-display" TIMEOUT 180)
    endif()

    # The native shell has no headless mode: without a frame limit it runs until the window is
    # closed. A screenshot asked for on those terms can never be taken, so the run has to be
    # rejected before the window opens -- if it were not, this test would hang instead of failing.
    add_test(NAME CnaStudioNativeShellScreenshotNeedsFrameLimit
        COMMAND cna-studio --ui=studio
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
    # The real thing: open a window, draw the editor through CNA's public API, and write a PNG of
    # the result. A run that merely exits cleanly cannot tell a working editor from one that opened
    # a blank window, so the screenshot is the assertion -- if the editor drew nothing, the file
    # never appears and the test fails.
    #
    add_test(NAME CnaStudioWindowSmoke
        COMMAND cna-studio
            --project=${CMAKE_CURRENT_SOURCE_DIR}/../examples/HelloSprites/HelloSprites.cnaproject
            --frames=20
            --screenshot=${CMAKE_CURRENT_BINARY_DIR}/editor-smoke.png)

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
        COMMAND cna-studio --view=3d
            --project=${CMAKE_CURRENT_SOURCE_DIR}/../examples/HelloSprites/HelloSprites.cnaproject
            --frames=20
            --screenshot=${CMAKE_CURRENT_BINARY_DIR}/editor-smoke-3d.png)

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
            --screenshot=${CMAKE_CURRENT_BINARY_DIR}/player-smoke.png)

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
