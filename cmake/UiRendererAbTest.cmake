# SPDX-License-Identifier: MS-PL
#
# STUDIO-04025 -- the A/B verification that lets the modern UI renderer become the default.
#
# Runs cna-studio twice over the same project at the same size, once on each UI render backend,
# and compares the two captures. The comparison is the whole test: two backends that agree on
# draw-call counts and disagree on pixels is exactly the failure a counts-only assertion misses,
# and it is the failure a rewritten GPU path is most likely to have.
#
# Why byte equality rather than a tolerance. Both backends submit the same geometry, in the same
# order, with the same blend, sampler and scissor state, to the same renderer -- only the program
# and the buffer route differ, and neither changes where a triangle lands or what colour it is.
# On one renderer that is an exact contract and worth asserting exactly; ACROSS renderers it would
# not be, which is why this runs on whichever renderer the build has rather than comparing two.
#
# Expected variables: CNA_STUDIO_EXE, CNA_STUDIO_PROJECT, CNA_STUDIO_OUT_DIR, CNA_STUDIO_SIZE.

set(_modern "${CNA_STUDIO_OUT_DIR}/ab-modern.png")
set(_compat "${CNA_STUDIO_OUT_DIR}/ab-compat.png")
file(REMOVE "${_modern}" "${_compat}")

# --host-capabilities prints the chosen backend and exits, so the identity check above is a second,
# cheaper run rather than a parse of the capture run's own output.
function(_cna_studio_assert_backend flag reported)
    execute_process(
        COMMAND "${CNA_STUDIO_EXE}" "--ui-renderer=${flag}" --host-capabilities
        OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _code)
    if(NOT _out MATCHES "UI renderer: ${reported}")
        message(FATAL_ERROR
            "asked for the ${flag} UI renderer and this host reports otherwise.\n${_out}${_err}")
    endif()
endfunction()

# Both, before either capture. Without this the comparison would pass trivially on a host that
# refused the modern backend and quietly ran the classic one twice -- which is the single most
# likely way for an A/B test to become a tautology.
_cna_studio_assert_backend(modern modern)
_cna_studio_assert_backend(compat compatibility)

execute_process(
    COMMAND "${CNA_STUDIO_EXE}" "--ui-renderer=modern" "--project=${CNA_STUDIO_PROJECT}"
            "--window-size=${CNA_STUDIO_SIZE}" "--frames=12" "--screenshot=${_modern}"
            "--screenshot-min-colors=16" "--workspace=none"
    RESULT_VARIABLE _modernCode OUTPUT_VARIABLE _modernOut ERROR_VARIABLE _modernErr)
execute_process(
    COMMAND "${CNA_STUDIO_EXE}" "--ui-renderer=compat" "--project=${CNA_STUDIO_PROJECT}"
            "--window-size=${CNA_STUDIO_SIZE}" "--frames=12" "--screenshot=${_compat}"
            "--screenshot-min-colors=16" "--workspace=none"
    RESULT_VARIABLE _compatCode OUTPUT_VARIABLE _compatOut ERROR_VARIABLE _compatErr)

if(NOT _modernCode EQUAL 0)
    message(FATAL_ERROR "the modern backend failed (exit ${_modernCode}):\n${_modernOut}\n${_modernErr}")
endif()
if(NOT _compatCode EQUAL 0)
    message(FATAL_ERROR "the compatibility backend failed (exit ${_compatCode}):\n${_compatOut}\n${_compatErr}")
endif()
if(NOT EXISTS "${_modern}" OR NOT EXISTS "${_compat}")
    message(FATAL_ERROR "one of the two backends wrote no capture.")
endif()

file(SIZE "${_modern}" _modernSize)
file(SIZE "${_compat}" _compatSize)
file(MD5 "${_modern}" _modernHash)
file(MD5 "${_compat}" _compatHash)

if(NOT _modernHash STREQUAL _compatHash)
    message(FATAL_ERROR
        "the two UI render backends drew different frames.\n"
        "  modern:        ${_modern} (${_modernSize} bytes, md5 ${_modernHash})\n"
        "  compatibility: ${_compat} (${_compatSize} bytes, md5 ${_compatHash})\n"
        "Both are kept for comparison. They submit the same geometry with the same state to the "
        "same renderer, so a difference is a defect in one of the two draw routes -- most likely "
        "the vertex offset, the projection's row/column order, or a state one of them forgets to "
        "restore.")
endif()

message(STATUS "UI render backends agree: ${_modernSize} bytes, md5 ${_modernHash}")
