// SPDX-License-Identifier: MS-PL

/**
 * @file third_party/cgltf/cgltf_impl.cpp
 * @brief The one translation unit that instantiates cgltf's implementation.
 *
 * cgltf is header-only: including `cgltf.h` gives declarations, and exactly one source file in the
 * program must define `CGLTF_IMPLEMENTATION` before including it to get the definitions. Upstream
 * ships no such file, so this one is ours -- it is the only file in `third_party/cgltf/` that is
 * not a verbatim copy of upstream, which is why it carries this repository's licence header rather
 * than cgltf's.
 *
 * It lives here, in its own target compiled with warnings off, for the reason the vendored ImGui
 * does: third-party code is not held to this project's `-Wall -Wextra -Wpedantic -Werror`, because
 * its warnings are not actionable here and would drown out ours.
 *
 * It includes `cgltf_prefixed.h` rather than `cgltf.h`, and must: CNA vendors the same cgltf and
 * a build linking both would otherwise fail with duplicate definitions. See that header.
 */

#define CGLTF_IMPLEMENTATION
#include "cgltf_prefixed.h"
