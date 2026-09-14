// SPDX-License-Identifier: MS-PL
/**
 * @file StudioRuntimeSources.hpp
 * @brief The runtime source files an exported game carries, embedded in the Studio binary.
 *
 * `plan.md` STUDIO-02051, STUDIO-18021.
 *
 * A game authored in Studio is an ordinary CNA C++ project that builds and ships with Studio
 * uninstalled. It still needs *something* to read the scene files Studio wrote, and that something
 * is this: a small, self-contained runtime -- a JSON reader, a `Uuid`, and the header-only scene
 * loader over them.
 *
 * It travels **inside the exported project**, as ordinary project source the game compiles itself,
 * rather than as a library the build machine is expected to have. That is the difference between a
 * game that builds on a clean machine with a CNA checkout and one that quietly requires Studio to
 * be installed -- and the difference is invisible on the machine Studio was built on, which is why
 * it is guarded by a test rather than by intent (`STUDIO-02051`).
 *
 * The files are embedded at build time from the same sources Studio itself compiles, so the
 * exported runtime cannot drift from the writer that produced the scene. A second, committed copy
 * for export to read would be free to do exactly that.
 *
 * The paths keep Studio's own include layout (`CNA/Studio/Core/Json.hpp` and so on) so the loader's
 * `#include` lines work unchanged in the exported tree. Rewriting them on the way out would be one
 * more thing to get wrong, and would make the exported runtime harder to diff against its origin.
 */

#pragma once

#include <string_view>
#include <vector>

namespace CNA::Studio
{
    /** @brief One runtime file, and where it goes inside an exported project. */
    struct StudioRuntimeSource
    {
        /** @brief Path relative to the exported project root, using forward slashes. */
        std::string_view pathInProject;

        /** @brief The file's contents, byte for byte as Studio compiles them. */
        std::string_view contents;

        /** @brief Whether the exported project's CMakeLists must compile this file. */
        bool compiled = false;
    };

    /**
     * @brief Every file the exported runtime consists of.
     *
     * @return The runtime's headers and translation units, in a stable order.
     */
    [[nodiscard]] std::vector<StudioRuntimeSource> studioRuntimeSources();

    /** @brief The directory, inside an exported project, the runtime is written under. */
    inline constexpr std::string_view kStudioRuntimeDirectory = "Runtime";
}
