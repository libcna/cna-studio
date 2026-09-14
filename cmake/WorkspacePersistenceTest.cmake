# SPDX-License-Identifier: MS-PL
#
# The workspace layout survives a real process exit (plan.md STUDIO-05014).
#
#   cmake -DSTUDIO=<cna-studio> -DWORK_DIR=<dir> -P WorkspacePersistenceTest.cmake
#
# Runs the native shell three times against one layout file:
#
#   1. with nothing stored, which must report `workspace default and stored`;
#   2. again, which must now report `workspace restored`, because run 1 wrote it;
#   3. over a deliberately truncated file, which must fall back to the default arrangement,
#      say why, and still exit cleanly.
#
# Two processes rather than one, because that is the thing being tested. A unit test can prove the
# store round-trips in memory; only a second process can prove Studio actually writes on the way out
# and reads on the way in -- and the failure this catches, a shell that saves nothing and silently
# starts fresh every time, is invisible to any test that never exits.

foreach(_required STUDIO WORK_DIR)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "WorkspacePersistenceTest.cmake needs ${_required}")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
set(_layout "${WORK_DIR}/workspace.json")

function(run_shell _expected _label)
    execute_process(
        COMMAND "${STUDIO}" --ui=studio --frames=5 "--workspace=${_layout}"
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _output
        ERROR_VARIABLE _error)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "${_label}: the shell exited ${_result}:\n${_output}\n${_error}")
    endif()
    if(NOT _output MATCHES "${_expected}")
        message(FATAL_ERROR
            "${_label}: expected the shell to report '${_expected}'.\nIt said:\n${_output}\n${_error}")
    endif()
    set(_lastError "${_error}" PARENT_SCOPE)
endfunction()

# 1. Nothing stored: the default arrangement, written out on exit.
run_shell("workspace default and stored" "first run")
if(NOT EXISTS "${_layout}")
    message(FATAL_ERROR "first run: the shell exited without writing ${_layout}")
endif()

# 2. The arrangement run 1 stored comes back. This is the assertion the whole case exists for.
run_shell("workspace restored and stored" "second run")

# 3. A file nobody can read must cost the arrangement and nothing else -- Studio still starts, on
#    the default layout, and says why. A corrupt layout file is never a reason not to open.
file(WRITE "${_layout}" "{\"fileVersion\": 1, \"layout\": {\"root\": ")
run_shell("workspace default and stored" "corrupt-layout run")
if(NOT _lastError MATCHES "could not be read")
    message(FATAL_ERROR
        "corrupt-layout run: the shell recovered but did not say why:\n${_lastError}")
endif()

# And it repaired the file on the way out, so the next start is clean again.
run_shell("workspace restored and stored" "run after recovery")

message(STATUS "workspace-persistence: passed -- stored, restored, and survived a corrupt file")
