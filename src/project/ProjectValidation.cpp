// SPDX-License-Identifier: MS-PL
/**
 * @file ProjectValidation.cpp
 * @brief The diagnostics a project gets when it opens.
 *
 * `plan.md` STUDIO-08012.
 */

#include "CNA/Studio/Project/ProjectValidation.hpp"

#include <algorithm>
#include <filesystem>
#include <string_view>
#include <system_error>

#include "CNA/Studio/Project/LanguageAdapter.hpp"
#include "CNA/Studio/Project/Project.hpp"
#include "CNA/Studio/Project/RendererCatalog.hpp"
#include "CNA/Studio/Project/TargetProfile.hpp"

namespace CNA::Studio
{
    const char* toString(StudioProjectDiagnosticSeverity severity)
    {
        switch (severity)
        {
            case StudioProjectDiagnosticSeverity::Information: return "information";
            case StudioProjectDiagnosticSeverity::Warning: return "warning";
            case StudioProjectDiagnosticSeverity::Error: return "error";
        }
        return "warning";
    }

    std::string StudioProjectDiagnostic::toLine() const
    {
        return remedy.empty() ? message : message + " " + remedy;
    }

    std::vector<StudioProjectDiagnostic> validateStudioProject(
        const Project& project, const StudioLanguageRegistry& languages)
    {
        std::vector<StudioProjectDiagnostic> diagnostics;
        const auto add = [&](StudioProjectDiagnosticSeverity severity, std::string subject,
                             std::string message, std::string remedy = {}) {
            diagnostics.push_back({severity, std::move(subject), std::move(message),
                                   std::move(remedy)});
        };

        std::error_code code;

        // --- The root ---------------------------------------------------------------------------
        if (project.getRootPath().empty())
        {
            add(StudioProjectDiagnosticSeverity::Error, "root",
                "This project has no directory.",
                "It was probably loaded from JSON rather than from a file.");
            return diagnostics;
        }

        // --- The language -----------------------------------------------------------------------
        // First, because everything about building depends on the answer, and because a project in
        // a language this build cannot author is the one case where a diagnostic has to say what
        // Studio *can* still do rather than only what it cannot.
        if (languages.forProject(project) == nullptr)
        {
            add(StudioProjectDiagnosticSeverity::Error, "language",
                "This project is written in the '" + project.getLanguage()
                    + "' language, which this build of CNA Studio has no support for.",
                "Scenes and assets can still be edited; building and packaging cannot.");
        }

        // --- The directories --------------------------------------------------------------------
        // An XnaCompatible project has no scenes by definition (docs/ARCHITECTURE.md §8): it keeps
        // its own game loop and Studio offers it assets, content preview and Play. Asking it for a
        // Scenes directory and a startup scene would be forcing the entity model on exactly the
        // project kind that exists so it cannot be forced.
        const bool hasScenes = project.getKind() != ProjectKind::XnaCompatible;

        for (const auto& [subject, relative, what] :
             {std::tuple{"sceneDirectory", project.getSceneDirectory(), "scenes"},
              std::tuple{"assetDirectory", project.getAssetDirectory(), "assets"}})
        {
            if (!hasScenes && std::string_view{subject} == "sceneDirectory") { continue; }

            if (relative.empty())
            {
                add(StudioProjectDiagnosticSeverity::Warning, subject,
                    std::string{"This project names no directory for its "} + what + ".",
                    "Set one in the project settings.");
                continue;
            }
            if (!std::filesystem::is_directory(project.resolvePath(relative), code))
            {
                add(StudioProjectDiagnosticSeverity::Warning, subject,
                    "The " + std::string{what} + " directory '" + relative
                        + "' is not in this project.",
                    "Create it, or point the project settings at where the " + std::string{what}
                        + " actually are.");
            }
        }

        // --- The startup scene ------------------------------------------------------------------
        if (!hasScenes)
        {
            // Nothing to say. Not even a remark: a project kind behaving exactly as documented is
            // not news, and a diagnostics pane that lists it teaches people to stop reading.
        }
        else if (project.getStartupScene().empty())
        {
            add(StudioProjectDiagnosticSeverity::Warning, "startupScene",
                "This project names no startup scene, so a built game has nothing to load.",
                "Set one in the project settings.");
        }
        else if (!std::filesystem::is_regular_file(
                     project.resolvePath(project.getStartupScene()), code))
        {
            add(StudioProjectDiagnosticSeverity::Error, "startupScene",
                "The startup scene '" + project.getStartupScene() + "' is not in this project.",
                "Open the scene you meant and set it as the startup scene, or restore the file.");
        }

        // --- The target profiles ------------------------------------------------------------------
        const std::vector<StudioTargetProfile>& profiles = project.getTargetProfiles();
        if (profiles.empty())
        {
            add(StudioProjectDiagnosticSeverity::Error, "targetProfiles",
                "This project declares no build target, so there is nothing to build.",
                "Add one in the Build panel.");
        }

        for (const StudioTargetProfile& profile : profiles)
        {
            // The profile model's own validation, forwarded rather than re-implemented: a second
            // opinion about which renderer identities exist is a second thing to keep current.
            //
            // On a copy, because `validateStudioTargetProfile` migrates a legacy renderer name in
            // place and this function only reports. Rewriting the open project as a side effect of
            // *looking* at it would make a diagnostics pane that silently edits.
            StudioTargetProfile inspected = profile;
            for (const StudioProfileProblem& problem :
                 validateStudioTargetProfile(inspected).problems)
            {
                const StudioProjectDiagnosticSeverity severity =
                    problem.severity == StudioProfileSeverity::Error
                        ? StudioProjectDiagnosticSeverity::Error
                        : (problem.severity == StudioProfileSeverity::Warning
                               ? StudioProjectDiagnosticSeverity::Warning
                               : StudioProjectDiagnosticSeverity::Information);

                add(severity, problem.axis,
                    "Target '" + profile.name + "': " + problem.message,
                    problem.severity == StudioProfileSeverity::Migration
                        ? "Saving the project writes the current name."
                        : std::string{});
            }
        }

        // --- The renderer the project names outside its profiles --------------------------------
        if (!project.getDefaultGraphicsBackend().empty()
            && findRenderer(project.getDefaultGraphicsBackend()) == nullptr
            && findLegacyRendererAlias(project.getDefaultGraphicsBackend()) == nullptr)
        {
            add(StudioProjectDiagnosticSeverity::Warning, "renderer",
                "'" + project.getDefaultGraphicsBackend()
                    + "' is not a renderer CNA has, so Play would fall back to a different one.",
                "Choose a renderer on the project's build target.");
        }

        // Errors first, then warnings, then remarks. Within a severity the order is the order they
        // were found, which is the order somebody reads the project settings in.
        std::stable_sort(diagnostics.begin(), diagnostics.end(),
            [](const StudioProjectDiagnostic& lhs, const StudioProjectDiagnostic& rhs) {
                return static_cast<int>(lhs.severity) > static_cast<int>(rhs.severity);
            });
        return diagnostics;
    }
}
