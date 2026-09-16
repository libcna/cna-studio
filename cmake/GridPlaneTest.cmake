# SPDX-License-Identifier: MS-PL
#
# STUDIO-07056 -- the 3D grid lies in the plane the user chose.
#
# Runs cna-studio twice in the 3D view over the same project at the same size, once with the grid
# on the scene's own plane and once on the ground plane, and requires the two captures to DIFFER.
#
# The inverse of the UI-render-backend A/B beside it, and for the inverse reason. There the two
# routes must agree because only the plumbing differs; here the two runs must disagree because the
# only thing that differs is the one thing being tested. A setting that is stored, loaded, given a
# menu row and read by nothing produces two identical captures -- and every unit test around it
# still passes, because each half works. This project has met that shape four times in this phase
# alone, most recently in STUDIO-11015, where three viewport preferences were written to disk and
# read by nothing.
#
# The preference rather than a flag, because that is how the setting actually reaches the editor
# and the acceptance condition includes surviving a restart. XDG_CONFIG_HOME points each run at its
# own directory, so neither can be contaminated by whatever is on the machine running this.
#
# Expected variables: CNA_STUDIO_EXE, CNA_STUDIO_PROJECT, CNA_STUDIO_OUT_DIR, CNA_STUDIO_SIZE,
# CNA_STUDIO_NEEDS_DISPLAY.

set(_sceneShot "${CNA_STUDIO_OUT_DIR}/grid-scene-plane.png")
set(_groundShot "${CNA_STUDIO_OUT_DIR}/grid-ground-plane.png")
set(_sceneHome "${CNA_STUDIO_OUT_DIR}/grid-config-scene")
set(_groundHome "${CNA_STUDIO_OUT_DIR}/grid-config-ground")

file(REMOVE "${_sceneShot}" "${_groundShot}")
file(REMOVE_RECURSE "${_sceneHome}" "${_groundHome}")

# Only the ground run gets a preferences file. The other is Studio's own default, which is the
# scene's plane -- written out it would assert the default twice rather than once.
file(WRITE "${_groundHome}/cna-studio/preferences.json"
     "{\"formatVersion\": 1, \"viewport\": {\"gridOnGroundPlane\": true}}\n")

function(_cna_studio_capture_grid home output)
    # `dummy` draws nothing and is exactly what a renderer with no shader stage needs to capture a
    # frame with no real display at all -- but it cannot create a GL context, so a backend that
    # runs one needs an actual (here, virtual) display instead, and Mesa's software rasterizer in
    # place of a GPU that a headless runner does not have.
    if(CNA_STUDIO_NEEDS_DISPLAY)
        set(_videoEnv "LIBGL_ALWAYS_SOFTWARE=1")
    else()
        set(_videoEnv "SDL_VIDEODRIVER=dummy")
    endif()

    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "XDG_CONFIG_HOME=${home}" "${_videoEnv}"
                "${CNA_STUDIO_EXE}" "--ui=studio" "--project=${CNA_STUDIO_PROJECT}"
                "--shell-invoke=studio.view.3d" "--window-size=${CNA_STUDIO_SIZE}"
                "--frames=12" "--screenshot=${output}" "--screenshot-min-colors=16"
                "--workspace=none"
        RESULT_VARIABLE _code OUTPUT_VARIABLE _out ERROR_VARIABLE _err)

    if(NOT _code EQUAL 0)
        message(FATAL_ERROR "the run for ${output} failed (exit ${_code}):\n${_out}\n${_err}")
    endif()
    if(NOT EXISTS "${output}")
        message(FATAL_ERROR "the run for ${output} wrote no capture:\n${_out}\n${_err}")
    endif()

    # The 3D view has to actually be showing, or both runs capture the 2D one and agree for a
    # reason that has nothing to do with the grid. `--shell-invoke` records a refusal rather than
    # failing, so this is checked rather than assumed.
    if(_out MATCHES "did nothing")
        message(FATAL_ERROR "switching to the 3D view was refused:\n${_out}")
    endif()
endfunction()

_cna_studio_capture_grid("${_sceneHome}" "${_sceneShot}")
_cna_studio_capture_grid("${_groundHome}" "${_groundShot}")

file(MD5 "${_sceneShot}" _sceneHash)
file(MD5 "${_groundShot}" _groundHash)

if(_sceneHash STREQUAL _groundHash)
    message(FATAL_ERROR
        "the grid plane preference changed nothing.\n"
        "  scene plane:  ${_sceneShot} (md5 ${_sceneHash})\n"
        "  ground plane: ${_groundShot} (md5 ${_groundHash})\n"
        "Both captures are kept. The preference is stored and loaded -- its round trip has its own "
        "unit test -- so an identical pair means it did not reach the wireframe: most likely the "
        "host is still building WireframeOptions with the default gridPlane, or the viewport state "
        "is no longer copied from the preferences each poll.")
endif()

message(STATUS "grid plane changes the 3D view: scene ${_sceneHash}, ground ${_groundHash}")
