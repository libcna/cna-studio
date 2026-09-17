// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Project/Cpp/CppProjectExport.hpp
 * @brief Writing a C++ project's own files: the ones that make a directory buildable without Studio.
 *
 * `plan.md` STUDIO-02051, STUDIO-02082, and the reduced form of `STUDIO-18020`.
 *
 * **The invariant this exists to defend.** CNA Studio produces CNA games, not CNA Studio games. A
 * C++ project Studio created or exported is an ordinary CNA C++ project: it configures with CMake,
 * compiles against a CNA checkout, runs, and at no point needs Studio to be installed, on the PATH,
 * or anywhere on the machine.
 *
 * That property is invisible on a developer's machine, where Studio's source tree is right there
 * and every accidental dependency on it resolves silently. It therefore cannot be maintained by
 * intent -- it has to be proved, by building the result with nothing but CMake, a compiler and CNA.
 * `STUDIO-02051` proves it for an export and `STUDIO-08011` for every project template, both by
 * doing it rather than by inspecting the tree.
 *
 * **Two callers, one set of generators.** A project created by the Project Hub and a project
 * written by `--export` need the same build file, the same entry point and the same project-owned
 * runtime; what differs is where the content is. Writing them twice would mean an export and a new
 * project that drift, and only one of the two has a build test.
 *
 * **What this is not.** Not cooking, not packaging, not staging, not a shipping-configuration
 * build -- those are Phase 18 and are ahead of this. This writes source: the game's `main`, its
 * project-owned runtime, its build file, and its content. Everything it emits is readable, and a
 * developer can open the result and build it by hand, which is the point.
 */

#include <string>
#include <vector>

#include "CNA/Studio/Project/LanguageAdapter.hpp"
#include "CNA/Studio/Project/ProjectPackaging.hpp"

namespace CNA::Studio
{
    class Project;

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
     * @brief Returns the files that make @p scaffold a buildable C++ project, in place.
     *
     * The same generators the export uses, pointed at a project that keeps its own layout:
     * `Scenes/` and `Assets/` stay where the editor writes them and the build stages them beside
     * the executable, so a project created in Studio builds and runs from its own directory
     * without being exported first.
     *
     * Deterministic and content-free: it writes no scene and copies no asset, because those are
     * the template's business and not the language's.
     *
     * @param scaffold What is being created.
     * @return The files, in a stable order.
     */
    [[nodiscard]] std::vector<StudioProjectFile> studioCppProjectFiles(
        const StudioProjectScaffold& scaffold);
}
