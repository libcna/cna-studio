# SPDX-License-Identifier: MS-PL
#
# The standalone export test (plan.md STUDIO-02051), driven as a CMake script so one CTest case can
# run four steps and fail on the first that does not work.
#
#   cmake -DSTUDIO=<cna-studio> -DPROJECT_FILE=<x.cnaproject> -DCNA_ROOT=<cna checkout>
#         -DWORK_DIR=<empty dir> [-DRENDERER=SOFTWARE] [-DJOBS=N]
#         -P StandaloneExportTest.cmake
#
# Export the project into an empty directory, configure it with nothing but CMake and a CNA
# checkout, compile it, and run it. Studio is not consulted after the export: that is the point.
#
# This is the concrete form of the invariant the whole product rests on --
#
#     CNA Studio produces CNA games, not CNA Studio games.
#
# -- and it is deliberately a *build*, not an inspection. Every failure of the invariant found so
# far has been one that reading the exported tree could not have caught: CNA's video option being
# tri-state rather than boolean, so `ON` made an exported game require FFmpeg; CNA building its own
# tests and examples by default from a subdirectory, so the configure failed before compiling a
# line. Both looked correct on paper and neither compiled.

foreach(_required STUDIO PROJECT_FILE CNA_ROOT WORK_DIR)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "StandaloneExportTest.cmake needs ${_required}")
    endif()
endforeach()

if(NOT DEFINED RENDERER)
    set(RENDERER "SOFTWARE")
endif()
if(NOT DEFINED JOBS)
    set(JOBS 4)
endif()

set(_export "${WORK_DIR}/exported")
set(_build "${WORK_DIR}/exported-build")

# A previous run's tree would make the export refuse, and -- worse -- could let a stale exported
# file pass a test the current export would fail.
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

# ---------------------------------------------------------------------------------------------
# 1. Export
# ---------------------------------------------------------------------------------------------
message(STATUS "standalone-export: exporting ${PROJECT_FILE}")
execute_process(
    COMMAND "${STUDIO}" "--project=${PROJECT_FILE}" "--export=${_export}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "export failed (${_result}):\n${_output}\n${_error}")
endif()

if(NOT EXISTS "${_export}/CMakeLists.txt")
    message(FATAL_ERROR "the export wrote no CMakeLists.txt")
endif()

# ---------------------------------------------------------------------------------------------
# 2. Configure, with nothing but CMake and CNA
# ---------------------------------------------------------------------------------------------
# Deliberately no -DCNA_BUILD_TESTS, no -DCNA_BUILD_EXAMPLES and no feature options: whatever the
# exported project needs to configure, it has to ask for itself. Passing them here would test this
# script's knowledge of CNA rather than the export's.
message(STATUS "standalone-export: configuring on ${RENDERER}")
set(_generatorArguments "")
if(DEFINED GENERATOR AND NOT GENERATOR STREQUAL "")
    # The generator Studio itself was configured with, so the exported build is not silently a
    # slower one -- and so the executable lands where step 4 looks for it.
    set(_generatorArguments -G "${GENERATOR}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${_export}" -B "${_build}" ${_generatorArguments}
            "-DCNA_ROOT=${CNA_ROOT}" "-DCNA_GRAPHICS_RENDERER=${RENDERER}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "the exported project would not configure (${_result}):\n${_output}\n${_error}")
endif()

# ---------------------------------------------------------------------------------------------
# 3. Compile
# ---------------------------------------------------------------------------------------------
message(STATUS "standalone-export: building")
execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${_build}" --parallel ${JOBS}
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "the exported project would not compile (${_result}):\n${_output}\n${_error}")
endif()

# ---------------------------------------------------------------------------------------------
# 4. Run it
# ---------------------------------------------------------------------------------------------
# A game that built but loads nothing would pass every step above. The assertion is the line it
# prints: how many entities came out of the scene the editor wrote, and how many sprites it drew.
# By name, not by globbing the build directory: a glob finds Makefile, build.ninja and every
# other artefact a generator leaves lying about, and picking the wrong one produces a confusing
# failure that has nothing to do with the game. The generated CMakeLists names its own project,
# and the project name is the executable name by construction.
file(READ "${_export}/CMakeLists.txt" _generated)
if(NOT _generated MATCHES "project\\(([A-Za-z0-9_]+) LANGUAGES")
    message(FATAL_ERROR "the generated CMakeLists.txt has no project() this script can read")
endif()
set(_gameName "${CMAKE_MATCH_1}")

set(_game "${_build}/${_gameName}")
if(NOT EXISTS "${_game}")
    # Multi-configuration generators put it one directory deeper.
    file(GLOB _configured "${_build}/*/${_gameName}" "${_build}/${_gameName}.exe"
                          "${_build}/*/${_gameName}.exe")
    if(_configured STREQUAL "")
        message(FATAL_ERROR "the build produced no '${_gameName}' executable in ${_build}")
    endif()
    list(GET _configured 0 _game)
endif()

message(STATUS "standalone-export: running ${_game}")
execute_process(
    COMMAND "${_game}" --frames=10
    WORKING_DIRECTORY "${_build}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "the exported game would not run (${_result}):\n${_output}\n${_error}")
endif()

# "loaded 0 entities" is a game that opened, drew nothing and exited cleanly -- which is exactly
# what an exit code alone cannot tell from a working one.
if(NOT _output MATCHES "loaded ([1-9][0-9]*) entities, drew ([1-9][0-9]*) sprites")
    message(FATAL_ERROR
        "the exported game ran but did not report loading a scene and drawing it:\n${_output}\n${_error}")
endif()

message(STATUS "standalone-export: ${_output}")
message(STATUS "standalone-export: passed -- exported, configured, compiled and ran with no Studio")
