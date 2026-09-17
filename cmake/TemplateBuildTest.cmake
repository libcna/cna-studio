# SPDX-License-Identifier: MS-PL
#
# The template build test (plan.md STUDIO-08011), driven as a CMake script so one CTest case can
# run four steps and fail on the first that does not work.
#
#   cmake -DSTUDIO=<cna-studio> -DTEMPLATE=<id> -DCNA_ROOT=<cna checkout> -DWORK_DIR=<empty dir>
#         [-DRENDERER=SOFTWARE] [-DJOBS=N] [-DGENERATOR=...]
#         -P TemplateBuildTest.cmake
#
# Create a project from a template, configure it with nothing but CMake and a CNA checkout, compile
# it, and run it. Studio is not consulted after the project is created: that is the point.
#
# This is the concrete form of the invariant the whole product rests on --
#
#     CNA Studio produces CNA games, not CNA Studio games.
#
# -- and it is the form that matters most, because it is about *creation* rather than about export.
# `STUDIO-02051` already proves an exported project builds; every project a user actually makes
# comes out of the Project Hub instead, and until this existed that path had never been compiled by
# anything. A template that produced a tree which does not build would have been found by the first
# person to press New Project.
#
# Deliberately a build, not an inspection, for the reason STUDIO-02051 records: every failure of
# this invariant found so far has been one that reading the tree could not have caught.

foreach(_required STUDIO TEMPLATE CNA_ROOT WORK_DIR)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "TemplateBuildTest.cmake needs ${_required}")
    endif()
endforeach()

if(NOT DEFINED RENDERER)
    set(RENDERER "SOFTWARE")
endif()
if(NOT DEFINED JOBS)
    set(JOBS 4)
endif()

# A name with a space in it, on purpose. A generator that composed a CMake target name from the
# project name without reducing it produces a `CMakeLists.txt` that does not configure, and the
# obvious test name -- `Probe` -- is exactly the one that would never find out.
set(_projectName "Template Probe")
set(_project "${WORK_DIR}/project")
set(_build "${WORK_DIR}/project-build")

# A previous run's tree would make creation refuse -- and, worse, could let a stale file pass a test
# the current Studio would fail.
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

# -------------------------------------------------------------------------------------------------
# 1. Create the project, through the same code the Project Hub's button runs
# -------------------------------------------------------------------------------------------------
message(STATUS "template-build: creating '${TEMPLATE}'")
execute_process(
    COMMAND "${STUDIO}" "--new-project=${_project}" "--template=${TEMPLATE}"
            "--project-name=${_projectName}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "creating '${TEMPLATE}' failed (${_result}):\n${_output}\n${_error}")
endif()

if(NOT EXISTS "${_project}/CMakeLists.txt")
    message(FATAL_ERROR "the '${TEMPLATE}' template produced no CMakeLists.txt")
endif()

# The project file Studio would open. Named after the project rather than after the template, and
# checked here because a `.cnaproject` that is not where Studio expects it is a project that
# creates and then cannot be opened.
file(GLOB _projectFiles "${_project}/*.cnaproject")
if(_projectFiles STREQUAL "")
    message(FATAL_ERROR "the '${TEMPLATE}' template produced no .cnaproject")
endif()
list(GET _projectFiles 0 _projectFile)

# -------------------------------------------------------------------------------------------------
# 2. Studio can open what Studio just wrote
# -------------------------------------------------------------------------------------------------
# Cheap, and it closes the gap between "these bytes parse" and "the editor accepts them". A
# generator whose output the reader rejects would otherwise be found by a user rather than by CI.
message(STATUS "template-build: reopening ${_projectFile}")
execute_process(
    COMMAND "${STUDIO}" "--project=${_projectFile}" --headless --frames=2
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR
        "Studio would not reopen the project it just created (${_result}):\n${_output}\n${_error}")
endif()

# -------------------------------------------------------------------------------------------------
# 3. Configure, with nothing but CMake and CNA
# -------------------------------------------------------------------------------------------------
# Deliberately no -DCNA_BUILD_TESTS, no -DCNA_BUILD_EXAMPLES and no feature options: whatever the
# created project needs to configure, it has to ask for itself. Passing them here would test this
# script's knowledge of CNA rather than the template's.
message(STATUS "template-build: configuring on ${RENDERER}")
set(_generatorArguments "")
if(DEFINED GENERATOR AND NOT GENERATOR STREQUAL "")
    set(_generatorArguments -G "${GENERATOR}")
endif()
execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${_project}" -B "${_build}" ${_generatorArguments}
            "-DCNA_ROOT=${CNA_ROOT}" "-DCNA_GRAPHICS_RENDERER=${RENDERER}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR
        "the '${TEMPLATE}' project would not configure (${_result}):\n${_output}\n${_error}")
endif()

# -------------------------------------------------------------------------------------------------
# 4. Compile
# -------------------------------------------------------------------------------------------------
message(STATUS "template-build: building")
execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${_build}" --parallel ${JOBS}
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR
        "the '${TEMPLATE}' project would not compile (${_result}):\n${_output}\n${_error}")
endif()

# -------------------------------------------------------------------------------------------------
# 5. Run it
# -------------------------------------------------------------------------------------------------
# A game that built but loads nothing would pass every step above. The executable is found by the
# name the generated CMakeLists gives its own project, rather than by globbing the build directory:
# a glob finds Makefile, build.ninja and every other artefact a generator leaves lying about.
file(READ "${_project}/CMakeLists.txt" _generated)
if(NOT _generated MATCHES "project\\(([A-Za-z0-9_]+) LANGUAGES")
    message(FATAL_ERROR "the generated CMakeLists.txt has no project() this script can read")
endif()
set(_gameName "${CMAKE_MATCH_1}")

set(_game "${_build}/${_gameName}")
if(NOT EXISTS "${_game}")
    file(GLOB _configured "${_build}/*/${_gameName}" "${_build}/${_gameName}.exe"
                          "${_build}/*/${_gameName}.exe")
    if(_configured STREQUAL "")
        message(FATAL_ERROR "the build produced no '${_gameName}' executable in ${_build}")
    endif()
    list(GET _configured 0 _game)
endif()

message(STATUS "template-build: running ${_game}")
execute_process(
    COMMAND "${_game}" --frames=10
    WORKING_DIRECTORY "${_build}"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "the '${TEMPLATE}' game would not run (${_result}):\n${_output}\n${_error}")
endif()

# What the game must have *said*, which is the difference between a process that exited zero and one
# that did its job. The claim depends on the project kind, and the kind is read from the project
# Studio wrote rather than from a list here -- so a template added tomorrow is asserted correctly
# without this script being edited, which is the same property STUDIO-08005 asks of the catalogue.
file(READ "${_projectFile}" _projectJson)
if(_projectJson MATCHES "\"kind\"[ \t]*:[ \t]*\"XnaCompatible\"")
    # A hand-written game has no scene to load and no entity count to report. What it can say is
    # that its own Update ran, which is what separates it from a window that opened and closed.
    if(NOT _output MATCHES "ran ([1-9][0-9]*) frames")
        message(FATAL_ERROR
            "the '${TEMPLATE}' game ran but did not report running any frames:\n${_output}\n${_error}")
    endif()
else()
    # A CNA-native game loaded the scene the editor wrote, and says how much of it came out.
    # "loaded 0 entities" is a game that opened, drew nothing and exited cleanly -- which is
    # exactly what an exit code alone cannot tell from a working one.
    if(NOT _output MATCHES "loaded ([1-9][0-9]*) entities")
        message(FATAL_ERROR
            "the '${TEMPLATE}' game ran but did not report loading a scene:\n${_output}\n${_error}")
    endif()
endif()

message(STATUS "template-build: ${_output}")
message(STATUS "template-build: '${TEMPLATE}' passed -- created, reopened, configured, compiled "
               "and ran with no Studio")
