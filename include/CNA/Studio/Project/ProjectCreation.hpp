// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Project/ProjectCreation.hpp
 * @brief Creating a project: what is refused, and what is written when nothing is.
 *
 * `plan.md` STUDIO-08004, STUDIO-08006 … STUDIO-08010.
 *
 * ### Three contributors, and none of them knows about the others
 *
 * ```
 * template  ──▶ scenes, assets, the starting world, the renderer and platform it was authored for
 * language  ──▶ the build file, the entry point, the project-owned runtime
 * this file ──▶ the .cnaproject, the directory layout, and refusing before anything is written
 * ```
 *
 * That split is what `STUDIO-02080` and `STUDIO-08005` are both for. A template that carried a
 * `CMakeLists.txt` would be a template secretly about C++; a language that carried a starting scene
 * would be a language secretly about 3D.
 *
 * ### Refusal comes before creation, never during
 *
 * Every check runs before a single file is written. A creation that fails halfway leaves a
 * directory that is neither a project nor empty, and the user is left to work out which files were
 * theirs — so the validation is a separate function with its own tests, and creation calls it
 * first and stops.
 */

#include <string>
#include <vector>

#include "CNA/Studio/Project/LanguageAdapter.hpp"
#include "CNA/Studio/Project/Project.hpp"
#include "CNA/Studio/Project/ProjectTemplate.hpp"

namespace CNA::Studio
{
    /** @brief What to create, and where. */
    struct StudioNewProjectRequest
    {
        /** @brief The project's name, as the user typed it. */
        std::string name;

        /**
         * @brief Absolute path of the directory the project is created in.
         *
         * The directory itself, not its parent: the Hub composes `<location>/<name>` and shows the
         * result, so that what the user is told is where it goes is where it goes.
         */
        std::string directory;

        /** @brief Id of the template to create from. */
        std::string templateId;

        /** @brief Id of the language to create in. Empty means the registry's default. */
        std::string languageId;
    };

    /** @brief One reason a request was refused, or a remark about one that was not. */
    struct StudioNewProjectProblem
    {
        /** @brief Which field it is about: `"name"`, `"directory"`, `"template"` or `"language"`. */
        std::string field;

        /** @brief What is wrong, as a sentence a user can act on. */
        std::string message;
    };

    /** @brief What creating a project produced, or why nothing was. */
    struct StudioNewProjectResult
    {
        /** @brief Absolute path of the written `.cnaproject`, when one was written. */
        std::string projectFilePath;

        /** @brief Every file written, relative to the project root, in write order. */
        std::vector<std::string> writtenFiles;

        /** @brief Why nothing was written. Empty on success. */
        std::vector<StudioNewProjectProblem> problems;

        /** @brief Problems that did not stop creation, such as an asset that would not copy. */
        std::vector<std::string> warnings;

        [[nodiscard]] bool succeeded() const { return problems.empty() && !projectFilePath.empty(); }
    };

    /**
     * @brief Returns why @p request cannot be created, or nothing when it can.
     *
     * Every refusal names the field it is about and says what to do, because "invalid project" is
     * the message that makes somebody try the same thing again.
     *
     * @param request What the user asked for.
     * @param templates The templates on offer.
     * @param languages The languages this build implements.
     * @return The problems, in the order the fields appear in the dialog.
     */
    [[nodiscard]] std::vector<StudioNewProjectProblem> validateStudioNewProject(
        const StudioNewProjectRequest& request,
        const StudioTemplateCatalogue& templates,
        const StudioLanguageRegistry& languages);

    /**
     * @brief Returns @p name reduced to something usable as a directory name.
     *
     * Exposed so the Hub can show the path it is about to create as the user types, rather than
     * discovering at the last moment that the directory is not called what the project is.
     *
     * @param name Any project name.
     * @return The name with path separators and control characters removed, trimmed.
     */
    [[nodiscard]] std::string studioProjectDirectoryName(std::string_view name);

    /**
     * @brief Creates a project, or refuses without writing anything.
     *
     * @param request What to create.
     * @param templates The templates on offer.
     * @param languages The languages this build implements.
     * @return The project file that was written, or the problems that stopped it.
     */
    [[nodiscard]] StudioNewProjectResult createStudioProject(
        const StudioNewProjectRequest& request,
        const StudioTemplateCatalogue& templates,
        const StudioLanguageRegistry& languages);
}
