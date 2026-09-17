// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Project/ProjectValidation.hpp
 * @brief What is wrong with a project that opened, said so that somebody can fix it.
 *
 * `plan.md` STUDIO-08012.
 *
 * ### Opening and validating are separate, and deliberately so
 *
 * A project whose startup scene is missing, whose renderer no longer exists or whose language this
 * build cannot author **still opens**. That is not leniency: a broken project is exactly the
 * project somebody needs the editor for, and an editor that refuses to open it has removed the
 * only tool that could have fixed it. So loading reports what it could not read and validation
 * reports what is wrong, and neither of them refuses.
 *
 * ### Actionable means naming the fix, not the symptom
 *
 * Each diagnostic carries the thing it is about and, where there is one, the concrete next step.
 * "Invalid project" is the message that makes somebody try the same thing again; "the startup
 * scene `Scenes/Main.cnascene` is not in this project — set one in the project settings" is not.
 */

#include <string>
#include <vector>

namespace CNA::Studio
{
    class Project;
    class StudioLanguageRegistry;

    /** @brief How much a diagnostic matters. */
    enum class StudioProjectDiagnosticSeverity
    {
        /** @brief Worth knowing. The project works. */
        Information,

        /** @brief Something will not work until it is fixed, but Studio can open the project. */
        Warning,

        /** @brief Something is wrong that stops a major workflow, such as building. */
        Error
    };

    /** @brief Returns the display name of @p severity. */
    [[nodiscard]] const char* toString(StudioProjectDiagnosticSeverity severity);

    /** @brief One thing that is wrong with a project, and what to do about it. */
    struct StudioProjectDiagnostic
    {
        StudioProjectDiagnosticSeverity severity = StudioProjectDiagnosticSeverity::Warning;

        /** @brief What it is about: `"startupScene"`, `"renderer"`, `"language"`, and so on. */
        std::string subject;

        /** @brief What is wrong. */
        std::string message;

        /** @brief The concrete next step, when there is one. May be empty. */
        std::string remedy;

        /** @brief The message and the remedy as one line, for a log or a notification. */
        [[nodiscard]] std::string toLine() const;
    };

    /**
     * @brief Returns everything wrong with @p project, worst first.
     *
     * Pure apart from reading the project's own files: no clock, no network, no process. That is
     * what lets every diagnostic below be provoked in a unit test rather than only seen by hand.
     *
     * @param project A project that has already been loaded.
     * @param languages The languages this build implements, for the language diagnostic.
     * @return The diagnostics, errors first.
     */
    [[nodiscard]] std::vector<StudioProjectDiagnostic> validateStudioProject(
        const Project& project, const StudioLanguageRegistry& languages);
}
