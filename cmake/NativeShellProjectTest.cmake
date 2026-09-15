# SPDX-License-Identifier: MS-PL
#
# Opening a project through the native shell must leave it exactly as it was.
#
#   cmake -DSTUDIO=<cna-studio> -DEXAMPLES=<examples dir> -P NativeShellProjectTest.cmake
#
# Applying an importer fact or writing a sidecar the first time a project is opened is intended.
# Doing it on every open is not: it fills a repository with diffs nobody made, and the person who
# notices is whoever runs `git status` after simply *looking* at their game.
#
# Checked by hashing the tree before and after rather than by `git diff`, so the case works from a
# tarball and says which file changed rather than only that something did.

foreach(_required STUDIO EXAMPLES)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "NativeShellProjectTest.cmake needs ${_required}")
    endif()
endforeach()

set(_project "${EXAMPLES}/HelloSprites/HelloSprites.cnaproject")
if(NOT EXISTS "${_project}")
    message(FATAL_ERROR "no example project at ${_project}")
endif()

# Every file, not only the project file: opening scans the asset directory and may touch a sidecar.
file(GLOB_RECURSE _files "${EXAMPLES}/HelloSprites/*")
list(SORT _files)
if(_files STREQUAL "")
    message(FATAL_ERROR "the example project has no files; this case would pass by finding nothing")
endif()

set(_before "")
foreach(_file ${_files})
    file(SHA256 "${_file}" _hash)
    list(APPEND _before "${_file}=${_hash}")
endforeach()

execute_process(
    COMMAND "${STUDIO}" --ui=studio --workspace=none "--project=${_project}" --frames=10
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _output
    ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "the native shell exited ${_result}:\n${_output}\n${_error}")
endif()

set(_index 0)
foreach(_file ${_files})
    file(SHA256 "${_file}" _hash)
    list(GET _before ${_index} _was)
    if(NOT "${_file}=${_hash}" STREQUAL "${_was}")
        message(FATAL_ERROR "opening the project modified ${_file}")
    endif()
    math(EXPR _index "${_index} + 1")
endforeach()

# A file added or removed would leave the hashes above untouched and still be a change.
file(GLOB_RECURSE _after "${EXAMPLES}/HelloSprites/*")
list(SORT _after)
if(NOT _after STREQUAL "${_files}")
    message(FATAL_ERROR "opening the project added or removed files under ${EXAMPLES}/HelloSprites")
endif()

message(STATUS "native-shell-project: passed -- ${_index} files unchanged")
