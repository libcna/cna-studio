# SPDX-License-Identifier: MS-PL
#
# One binary rather than one per module: the suite is fast, and a single binary keeps
# `ctest --output-on-failure` output readable. Individual suites are registered separately so a
# failure names the area rather than just "CnaEditorTests".

add_executable(cna-editor-tests
    TestMain.cpp
    CoreTests.cpp
    SceneTests.cpp
    CommandTests.cpp
    ProjectAndAssetTests.cpp
    ModelImportTests.cpp
    ApplicationTests.cpp
    UiTests.cpp
    PlayerTests.cpp
    ViewportTests.cpp)

# A *real* shared library for ED-411's loading tests. Nothing about dlopen, symbol resolution,
# cross-runtime allocation or unload order can be checked against a test double -- a double would
# exercise the host's bookkeeping and none of the things that actually break.
#
# It links **none** of the editor's libraries, and that is the important part rather than a way
# around a linker error. A plugin that statically linked cna-editor-core would get its own copy of
# every registry and every global in it, so the component it registered would go into a
# ComponentRegistry the editor has never heard of -- and the failure would look like "initialize()
# ran and nothing happened". Instead it includes the headers and leaves the symbols undefined, and
# the host resolves them at load time from its own exported symbols. That is what ENABLE_EXPORTS on
# the test executable is for, and it is the same arrangement a real plugin has to use.
add_library(cna-editor-test-plugin MODULE plugin/TestPlugin.cpp)
target_link_libraries(cna-editor-test-plugin PRIVATE cna-editor-settings)
target_link_options(cna-editor-test-plugin PRIVATE "LINKER:--allow-shlib-undefined")
set_target_properties(cna-editor-tests PROPERTIES ENABLE_EXPORTS ON)
set_target_properties(cna-editor-test-plugin PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/plugins/testplugin"
    PREFIX "")

# The manifest beside it, written at configure time so the test has a directory to discover.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/plugins/testplugin/plugin.json"
"{\n"
"  \"id\": \"org.openeggbert.testplugin\",\n"
"  \"name\": \"Test Plugin\",\n"
"  \"version\": \"1.0.0\",\n"
"  \"editorApiVersion\": 1,\n"
"  \"library\": \"cna-editor-test-plugin${CMAKE_SHARED_MODULE_SUFFIX}\"\n"
"}\n")

add_dependencies(cna-editor-tests cna-editor-test-plugin)
target_compile_definitions(cna-editor-tests PRIVATE
    CNA_EDITOR_TEST_PLUGIN_DIR="${CMAKE_CURRENT_BINARY_DIR}/plugins")

target_include_directories(cna-editor-tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cna-editor-tests PRIVATE
    cna-editor-context
    cna-editor-plugins
    cna-editor-runtime-bridge
    cna-editor-player)

# PlayerTests spawns the real cna-player binary, so it must exist before the suite runs -- and
# the suite must know where it landed. A generator expression over the real target is exact;
# guessing from the test binary's own directory would silently turn the end-to-end bridge test
# into a no-op that always passes.
add_dependencies(cna-editor-tests cna-player)
target_compile_definitions(cna-editor-tests PRIVATE
    CNA_EDITOR_TEST_PLAYER_DIR="$<TARGET_FILE_DIR:cna-player>")

# The application's panel layer is exercised end-to-end by ApplicationTests.cpp, so the test
# binary compiles EditorApplication.cpp directly rather than linking the cna-editor executable.
target_sources(cna-editor-tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../src/app/EditorApplication.cpp"
    ${CNA_EDITOR_PANEL_SOURCES})

if(CNA_EDITOR_WITH_IMGUI)
    # The ImGui cases run the real editor UI headless and assert on the geometry it produces.
    target_link_libraries(cna-editor-tests PRIVATE cna-editor-ui-imgui)
    target_compile_definitions(cna-editor-tests PRIVATE CNA_EDITOR_HAS_IMGUI=1)
endif()

add_test(NAME CnaEditorTests COMMAND cna-editor-tests)
set_tests_properties(CnaEditorTests PROPERTIES TIMEOUT 120)

# A smoke test over the real binary: parses arguments, opens nothing, draws one headless frame.
# This is what catches a linker or start-up regression that the unit tests cannot see.
add_test(NAME CnaEditorHeadlessSmoke COMMAND cna-editor --headless)
set_tests_properties(CnaEditorHeadlessSmoke PROPERTIES TIMEOUT 60)

# The same run through the 3D viewport (ED-400). A separate case rather than a flag on the one
# above, because the two share no drawing at all: one composes sprites, the other builds a
# wireframe and projects it. A crash in the second would be invisible from the first.
add_test(NAME CnaEditorHeadlessSmoke3D
    COMMAND cna-editor --headless --view=3d
        --project=${CMAKE_CURRENT_SOURCE_DIR}/../examples/HelloSprites/HelloSprites.cnaproject)
set_tests_properties(CnaEditorHeadlessSmoke3D PROPERTIES TIMEOUT 60)

# A bad --view is an error rather than a silent 2D start: the flag exists to be set from a script
# that cannot see the window it asked for, and a typo there must not pass quietly.
add_test(NAME CnaEditorRejectsUnknownView COMMAND cna-editor --view=isometric)
set_tests_properties(CnaEditorRejectsUnknownView PROPERTIES WILL_FAIL TRUE)

# The same rule for --orbit, and for the same reason: it exists to make a 3D screenshot
# reproducible, so a run that quietly kept the default angle would produce a picture that looks
# fine and shows the wrong thing.
add_test(NAME CnaEditorRejectsMalformedOrbit COMMAND cna-editor --view=3d --orbit=35)
set_tests_properties(CnaEditorRejectsMalformedOrbit PROPERTIES WILL_FAIL TRUE)

# And the flag working, headless, over a project with a model in it -- which is also the smoke
# test for ED-405's mesh path: the wireframe builder walks a real imported glTF here rather than
# the bounding box it drew before.
add_test(NAME CnaEditorHeadlessSmoke3DOrbited
    COMMAND cna-editor --headless --view=3d --orbit=35,25
        --project=${CMAKE_CURRENT_SOURCE_DIR}/../examples/HelloSprites/HelloSprites.cnaproject)
set_tests_properties(CnaEditorHeadlessSmoke3DOrbited PROPERTIES TIMEOUT 60)

add_test(NAME CnaEditorHelp COMMAND cna-editor --help)
add_test(NAME CnaEditorListBackends COMMAND cna-editor --list-backends)

# --graphics must fail: CNA fixes its backend at compile time, so silently accepting the flag
# would teach a mental model the framework does not support.
add_test(NAME CnaEditorRejectsGraphicsFlag COMMAND cna-editor --graphics=vulkan)
set_tests_properties(CnaEditorRejectsGraphicsFlag PROPERTIES WILL_FAIL TRUE)

add_test(NAME CnaPlayerHelp COMMAND cna-player --help)

if(CNA_EDITOR_WITH_CNA AND CNA_EDITOR_WITH_IMGUI)
    # The real thing: open a window, draw the editor through CNA's public API, and write a PNG of
    # the result. A run that merely exits cleanly cannot tell a working editor from one that opened
    # a blank window, so the screenshot is the assertion -- if the editor drew nothing, the file
    # never appears and the test fails.
    #
    add_test(NAME CnaEditorWindowSmoke
        COMMAND cna-editor
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
    if(CNA_GRAPHICS_BACKEND STREQUAL "SOFTWARE" OR CNA_GRAPHICS_BACKEND STREQUAL "HEADLESS")
        set_tests_properties(CnaEditorWindowSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
            TIMEOUT 180)
    else()
        set(CNA_EDITOR_TEST_DISPLAY ":0" CACHE STRING
            "DISPLAY used by cna-editor ctests that need a real graphics context")
        set_tests_properties(CnaEditorWindowSmoke PROPERTIES
            ENVIRONMENT "DISPLAY=${CNA_EDITOR_TEST_DISPLAY}"
            LABELS "needs-display"
            TIMEOUT 180)
    endif()

    # The 3D viewport against a real device. It draws through a different path from the 2D one --
    # every line is a rotated quad rather than a sprite -- so a backend that cannot do that would
    # pass the smoke test above and fail here.
    add_test(NAME CnaEditorWindowSmoke3D
        COMMAND cna-editor --view=3d
            --project=${CMAKE_CURRENT_SOURCE_DIR}/../examples/HelloSprites/HelloSprites.cnaproject
            --frames=20
            --screenshot=${CMAKE_CURRENT_BINARY_DIR}/editor-smoke-3d.png)

    if(CNA_GRAPHICS_BACKEND STREQUAL "SOFTWARE")
        set_tests_properties(CnaEditorWindowSmoke3D PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
            TIMEOUT 180)
    else()
        set_tests_properties(CnaEditorWindowSmoke3D PROPERTIES
            ENVIRONMENT "DISPLAY=${CNA_EDITOR_TEST_DISPLAY}"
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

    if(CNA_GRAPHICS_BACKEND STREQUAL "SOFTWARE" OR CNA_GRAPHICS_BACKEND STREQUAL "HEADLESS")
        set_tests_properties(CnaSceneLoaderDemo PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
            TIMEOUT 180)
    else()
        set_tests_properties(CnaSceneLoaderDemo PROPERTIES
            ENVIRONMENT "DISPLAY=${CNA_EDITOR_TEST_DISPLAY}"
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

    if(CNA_GRAPHICS_BACKEND STREQUAL "SOFTWARE" OR CNA_GRAPHICS_BACKEND STREQUAL "HEADLESS")
        set_tests_properties(CnaPlayerWindowSmoke PROPERTIES
            ENVIRONMENT "SDL_VIDEODRIVER=dummy"
            TIMEOUT 180)
    else()
        set_tests_properties(CnaPlayerWindowSmoke PROPERTIES
            ENVIRONMENT "DISPLAY=${CNA_EDITOR_TEST_DISPLAY}"
            LABELS "needs-display"
            TIMEOUT 180)
    endif()

    # --screenshot without --frames has no final frame to capture, and silently writing nothing
    # would make the smoke test above pass for the wrong reason.
    add_test(NAME CnaEditorScreenshotNeedsFrameLimit
        COMMAND cna-editor
            --project=${CMAKE_CURRENT_SOURCE_DIR}/../examples/HelloSprites/HelloSprites.cnaproject
            --screenshot=${CMAKE_CURRENT_BINARY_DIR}/never-written.png
            --headless)
    set_tests_properties(CnaEditorScreenshotNeedsFrameLimit PROPERTIES TIMEOUT 60)
endif()

# Comparing backends means decoding the captures, and decoding needs a graphics device. A run that
# went ahead headless would report that every capture was unreadable, which is a confusing way to
# say "wrong configuration".
add_test(NAME CnaEditorComparisonNeedsADevice
    COMMAND cna-editor --compare-backends --headless)
set_tests_properties(CnaEditorComparisonNeedsADevice PROPERTIES WILL_FAIL TRUE TIMEOUT 60)

# cna-player requires a project: launching one without is a mistake worth failing on, not
# something to paper over with a default.
add_test(NAME CnaPlayerRequiresAProject COMMAND cna-player)
set_tests_properties(CnaPlayerRequiresAProject PROPERTIES WILL_FAIL TRUE)
