// SPDX-License-Identifier: MS-PL
/**
 * @file ProjectExport.hpp
 * @brief Writes a Studio project out as a standalone CNA game that does not know Studio exists.
 *
 * `plan.md` STUDIO-02051, and the reduced form of `STUDIO-18020`.
 *
 * **The invariant this exists to defend.** CNA Studio produces CNA games, not CNA Studio games. An
 * exported project is an ordinary CNA C++ project: it configures with CMake, compiles against a
 * CNA checkout, runs, and at no point needs Studio to be installed, on the PATH, or anywhere on the
 * machine.
 *
 * That property is invisible on a developer's machine, where Studio's source tree is right there
 * and every accidental dependency on it resolves silently. It therefore cannot be maintained by
 * intent -- it has to be proved, by exporting into an empty directory and building the result with
 * nothing but CMake, a compiler and CNA. `STUDIO-02051` is that proof, run from the start rather
 * than at the end of the packaging workstream, because an invariant only checked once the feature
 * that depends on it is finished is an invariant that has already rotted.
 *
 * **What this is not.** Not cooking, not packaging, not staging, not a shipping-configuration
 * build -- those are Phase 18 and are ahead of this. This writes source: the game's `main`, its
 * project-owned runtime, its build file, and its content. Everything it emits is readable, and a
 * developer can open the result and build it by hand, which is the point.
 */

#pragma once

#include <string>
#include <vector>

namespace CNA::Studio
{
    class Project;

    /** @brief What to export, and where to put it. */
    struct StudioExportRequest
    {
        /** @brief Directory the exported project is written into. Created if absent. */
        std::string outputDirectory;

        /**
         * @brief Name of the generated CMake project and its executable.
         *
         * Defaults to the project's own name, reduced to an identifier. A CMake target cannot be
         * called `Hello Sprites`, and a game whose executable is named after a project whose name
         * happens to contain a space is not a reason to fail the export.
         */
        std::string targetName;

        /**
         * @brief Overwrite files already in @ref outputDirectory rather than refusing.
         *
         * Off by default. Export writes a whole tree, and a tree written over a directory somebody
         * chose by mistake is not something an undo can help with.
         */
        bool overwrite = false;
    };

    /** @brief What an export produced, and why it failed if it did. */
    struct StudioExportResult
    {
        /** @brief Every file written, relative to the output directory, in write order. */
        std::vector<std::string> writtenFiles;

        /** @brief Problems that did not stop the export, such as an asset that would not copy. */
        std::vector<std::string> warnings;

        /** @brief Empty when the export succeeded. */
        std::string errorMessage;

        [[nodiscard]] bool succeeded() const { return errorMessage.empty(); }
    };

    /**
     * @brief Writes @p project out as a standalone CNA game.
     *
     * The result contains, and contains nothing else:
     *
     * - `CMakeLists.txt` -- the game's own build, which asks only for a CNA checkout;
     * - `Source/Main.cpp` -- a CNA `Game` that loads the startup scene and draws it;
     * - `Runtime/` -- the project-owned scene runtime (@ref studioRuntimeSources);
     * - `Content/` -- the scenes and assets, copied;
     * - `README.md` -- how to build it, written for someone who has never run Studio.
     *
     * @param project The project to export. Its root path must exist.
     * @param request Where to write, and under what name.
     * @return The files written, or the reason nothing was.
     */
    [[nodiscard]] StudioExportResult exportStandaloneProject(const Project& project,
                                                             const StudioExportRequest& request);

    /**
     * @brief Reduces @p name to something CMake will accept as a target name.
     *
     * Exposed because the export test asserts on the executable's name, and duplicating the rule
     * in the test would let the two drift.
     *
     * @param name Any project name.
     * @return An identifier: alphanumerics and underscores, never starting with a digit.
     */
    [[nodiscard]] std::string studioExportTargetName(std::string_view name);
}
