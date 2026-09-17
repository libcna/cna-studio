// SPDX-License-Identifier: MS-PL
/**
 * @file ProjectCreation.cpp
 * @brief Refusing a new project for a reason, and writing one when there is none.
 *
 * `plan.md` STUDIO-08004, STUDIO-08006 … STUDIO-08010.
 */

#include "CNA/Studio/Project/ProjectCreation.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "CNA/Studio/Project/TargetProfile.hpp"

namespace CNA::Studio
{
    namespace
    {
        /** @brief Whether @p directory exists and holds anything at all. */
        bool holdsSomething(const std::filesystem::path& directory)
        {
            std::error_code code;
            if (!std::filesystem::exists(directory, code)) { return false; }
            if (!std::filesystem::is_directory(directory, code)) { return true; }
            return !std::filesystem::is_empty(directory, code);
        }

        /**
         * @brief Whether a file can actually be created under @p directory.
         *
         * By writing one, not by reading permission bits. Permissions are one of several reasons a
         * write fails -- a read-only mount, a full disk, a directory somebody else owns -- and a
         * check that models only the first is a check that passes right before the failure it was
         * there to predict.
         */
        bool canWriteInto(const std::filesystem::path& directory, std::string& reason)
        {
            std::error_code code;

            // Everything this has to undo, recorded before it is done. `create_directories` makes
            // parents as well as the leaf, so removing only the leaf would leave half a path
            // behind -- and the Hub calls this on every keystroke, so half a path per keystroke.
            std::vector<std::filesystem::path> created;
            for (std::filesystem::path walk = directory;
                 !walk.empty() && !std::filesystem::exists(walk, code);
                 walk = walk.parent_path())
            {
                created.push_back(walk);
                if (walk.parent_path() == walk) { break; }
            }

            std::filesystem::create_directories(directory, code);
            if (code)
            {
                reason = "'" + directory.generic_string() + "' cannot be created: " + code.message();
                return false;
            }

            const std::filesystem::path probe = directory / ".cna-studio-write-probe";
            bool wrote = false;
            {
                std::ofstream stream{probe, std::ios::binary | std::ios::trunc};
                wrote = static_cast<bool>(stream);
            }
            std::filesystem::remove(probe, code);

            // Innermost first, which is the only order `remove` can succeed in.
            for (const std::filesystem::path& path : created)
            {
                std::filesystem::remove(path, code);
                code.clear();
            }

            if (!wrote) { reason = "'" + directory.generic_string() + "' cannot be written to"; }
            return wrote;
        }

        /** @brief Writes @p contents to @p path, creating parents. */
        bool writeFile(const std::filesystem::path& path, std::string_view contents,
                       std::string& errorMessage)
        {
            std::error_code code;
            std::filesystem::create_directories(path.parent_path(), code);
            if (code)
            {
                errorMessage = "cannot create '" + path.parent_path().generic_string() + "': "
                             + code.message();
                return false;
            }

            // Binary, so a generated source file does not gain line endings on Windows that it did
            // not have in the template, and so a second creation produces identical bytes.
            std::ofstream stream{path, std::ios::binary | std::ios::trunc};
            if (!stream)
            {
                errorMessage = "cannot write '" + path.generic_string() + "'";
                return false;
            }
            stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
            if (!stream)
            {
                errorMessage = "failed while writing '" + path.generic_string() + "'";
                return false;
            }
            return true;
        }

        /**
         * @brief Copies a template's content tree into a project, in a deterministic order.
         *
         * @param from The template's `content/` directory.
         * @param to The project root.
         * @param written Every file copied, appended as a project-relative path.
         * @param warnings Anything that could not be copied, appended.
         */
        void copyContent(const std::filesystem::path& from, const std::filesystem::path& to,
                         std::vector<std::string>& written, std::vector<std::string>& warnings)
        {
            std::error_code code;
            if (!std::filesystem::is_directory(from, code)) { return; }

            // Collected and sorted rather than copied as the iterator yields them: a directory
            // iterator's order is the filesystem's, and `writtenFiles` is something tests and the
            // Hub's summary read.
            std::vector<std::filesystem::path> files;
            for (const std::filesystem::directory_entry& entry :
                 std::filesystem::recursive_directory_iterator{from, code})
            {
                if (entry.is_regular_file(code)) { files.push_back(entry.path()); }
            }
            std::sort(files.begin(), files.end());

            for (const std::filesystem::path& file : files)
            {
                const std::filesystem::path relative =
                    std::filesystem::relative(file, from, code);
                if (code)
                {
                    warnings.push_back("cannot place '" + file.generic_string()
                                       + "' relative to the template");
                    code.clear();
                    continue;
                }

                const std::filesystem::path destination = to / relative;
                std::filesystem::create_directories(destination.parent_path(), code);
                if (code)
                {
                    warnings.push_back("cannot create '"
                                       + destination.parent_path().generic_string() + "'");
                    code.clear();
                    continue;
                }

                std::filesystem::copy_file(file, destination,
                                           std::filesystem::copy_options::overwrite_existing, code);
                if (code)
                {
                    warnings.push_back("cannot copy '" + relative.generic_string() + "'");
                    code.clear();
                    continue;
                }
                written.push_back(relative.generic_string());
            }
        }
    }

    std::string studioProjectDirectoryName(std::string_view name)
    {
        std::string directory;
        for (const char character : name)
        {
            const auto value = static_cast<unsigned char>(character);

            // Path separators and control characters only. Spaces and punctuation are left alone:
            // a project called `Hello Sprites` should get a directory called `Hello Sprites`, the
            // way every other application on the machine would.
            if (character == '/' || character == '\\' || value < 0x20) { continue; }
            directory.push_back(character);
        }

        while (!directory.empty() && directory.back() == ' ') { directory.pop_back(); }
        while (!directory.empty() && directory.front() == ' ') { directory.erase(directory.begin()); }

        // Trailing dots are legal on POSIX and rejected by Windows, and a project created on one
        // machine has to open on the other.
        while (!directory.empty() && directory.back() == '.') { directory.pop_back(); }
        return directory;
    }

    std::vector<StudioNewProjectProblem> validateStudioNewProject(
        const StudioNewProjectRequest& request,
        const StudioTemplateCatalogue& templates,
        const StudioLanguageRegistry& languages)
    {
        std::vector<StudioNewProjectProblem> problems;

        // --- The name ---------------------------------------------------------------------------
        if (request.name.empty())
        {
            problems.push_back({"name", "A project needs a name."});
        }
        else if (studioProjectDirectoryName(request.name).empty())
        {
            problems.push_back({"name",
                "'" + request.name + "' leaves nothing usable as a directory name once path "
                "separators are removed."});
        }
        else
        {
            // Reserved on Windows whatever the extension, and a project created on Linux that
            // cannot be checked out on Windows is a trap that springs on somebody else's machine.
            std::string upper;
            for (const char character : studioProjectDirectoryName(request.name))
            {
                if (character == '.') { break; }
                upper.push_back(
                    static_cast<char>(std::toupper(static_cast<unsigned char>(character))));
            }
            for (const char* reserved : {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4",
                                         "LPT1", "LPT2", "LPT3", "LPT4"})
            {
                if (upper == reserved)
                {
                    problems.push_back({"name",
                        "'" + request.name + "' is a reserved device name on Windows, so this "
                        "project could not be checked out there."});
                    break;
                }
            }
        }

        // --- The language -----------------------------------------------------------------------
        const std::string languageId =
            request.languageId.empty() ? languages.defaultLanguageId() : request.languageId;
        const StudioLanguageAdapter* language = languages.find(languageId);
        if (language == nullptr)
        {
            problems.push_back({"language",
                languageId.empty()
                    ? std::string{"This build of Studio can author in no language at all."}
                    : "This build of Studio has no support for the '" + languageId + "' language."});
        }

        // --- The template -----------------------------------------------------------------------
        const StudioProjectTemplate* templateValue = templates.find(request.templateId);
        if (request.templateId.empty())
        {
            problems.push_back({"template", "Choose a template."});
        }
        else if (templateValue == nullptr)
        {
            problems.push_back({"template",
                "There is no template called '" + request.templateId + "'."});
        }
        else if (language != nullptr)
        {
            // Both answers have to agree, and they are different questions: a template may decline
            // a language, and a language may decline the project kind the template asks for.
            if (!templateValue->supportsLanguage(languageId))
            {
                problems.push_back({"template",
                    "The '" + templateValue->name + "' template is not offered in "
                    + language->descriptor().displayName + "."});
            }
            else if (!language->supportsProjectKind(templateValue->kind))
            {
                problems.push_back({"template",
                    language->descriptor().displayName + " cannot host a "
                    + std::string{toString(templateValue->kind)} + " project, which is what the '"
                    + templateValue->name + "' template creates."});
            }
        }

        // --- The directory ----------------------------------------------------------------------
        if (request.directory.empty())
        {
            problems.push_back({"directory", "Choose where to create the project."});
        }
        else
        {
            const std::filesystem::path directory{request.directory};
            if (!directory.is_absolute())
            {
                problems.push_back({"directory",
                    "'" + request.directory + "' is not an absolute path."});
            }
            else if (holdsSomething(directory))
            {
                std::error_code code;
                problems.push_back({"directory",
                    std::filesystem::is_directory(directory, code)
                        ? "'" + request.directory + "' already has something in it. A new project "
                          "is created in an empty directory, so nothing of yours is overwritten."
                        : "'" + request.directory + "' is a file."});
            }
            else
            {
                // Checked by writing, and everything the check created is removed again --
                // validation that left directories behind would make typing in the Hub's path
                // field create one per keystroke.
                std::string reason;
                if (!canWriteInto(directory, reason))
                {
                    problems.push_back({"directory", reason});
                }
            }
        }

        return problems;
    }

    StudioNewProjectResult createStudioProject(const StudioNewProjectRequest& request,
                                               const StudioTemplateCatalogue& templates,
                                               const StudioLanguageRegistry& languages)
    {
        StudioNewProjectResult result;

        // Nothing is written until every check has passed. A creation that fails halfway leaves a
        // directory that is neither a project nor empty, and the user is left working out which
        // files were theirs.
        result.problems = validateStudioNewProject(request, templates, languages);
        if (!result.problems.empty()) { return result; }

        const std::string languageId =
            request.languageId.empty() ? languages.defaultLanguageId() : request.languageId;
        const StudioLanguageAdapter* language = languages.find(languageId);
        const StudioProjectTemplate* templateValue = templates.find(request.templateId);
        if (language == nullptr || templateValue == nullptr) { return result; }

        const std::filesystem::path root{request.directory};

        std::error_code code;
        std::filesystem::create_directories(root, code);
        if (code)
        {
            result.problems.push_back(
                {"directory", "cannot create '" + root.generic_string() + "': " + code.message()});
            return result;
        }

        // --- The project file -------------------------------------------------------------------
        // Created under the sanitised name so the `.cnaproject` file is one every filesystem
        // accepts, then given the name the user actually typed. `createDefault` composes the file
        // path from the name it is handed, and the two are not the same question: what the project
        // is called is the user's, what the file is called has to survive a checkout on Windows.
        Project project =
            Project::createDefault(studioProjectDirectoryName(request.name), root.generic_string());
        project.setName(request.name);
        project.setKind(templateValue->kind);
        project.setLanguage(languageId);
        // Set from the template, and *cleared* when the template names none. `createDefault`
        // fills in a conventional scene path, which is right for a project that has scenes and is
        // a dangling reference for one that does not -- an XNA-compatible project would otherwise
        // be created already reporting a missing startup scene, which is a generator bug shown to
        // the user as their fault.
        project.setStartupScene(templateValue->startupScene);

        // Which viewport it opens in (STUDIO-11014). Carried from the template because the
        // template is the only thing that knows whether it made a 3D world or a 2D one; an Empty
        // 3D project that opened on the 2D grid would need the user to press 3 before it looked
        // like anything at all.
        project.setDefaultView(toString(templateValue->view));

        // The renderer and platform the template was authored for (STUDIO-08010). Written onto the
        // active target profile as well as the legacy `defaultGraphicsBackend`, because the profile
        // is what Build and Play read and the other is what an older Studio reads.
        std::vector<StudioTargetProfile> profiles = project.getTargetProfiles();
        if (!profiles.empty())
        {
            if (!templateValue->renderer.empty()) { profiles.front().renderer = templateValue->renderer; }
            if (!templateValue->platform.empty()) { profiles.front().platform = templateValue->platform; }
            (void)project.setTargetProfiles(profiles);
        }
        if (!templateValue->renderer.empty())
        {
            project.setDefaultGraphicsBackend(studioRendererCnaIdentity(templateValue->renderer));
        }

        // --- The template's content -------------------------------------------------------------
        copyContent(std::filesystem::path{templateValue->directory}
                        / StudioTemplateCatalogue::kContentDirectory,
                    root, result.writtenFiles, result.warnings);

        // --- The language's files ---------------------------------------------------------------
        StudioProjectScaffold scaffold;
        scaffold.projectName = request.name;
        scaffold.rootPath = root.generic_string();
        scaffold.kind = templateValue->kind;
        scaffold.startupScene = templateValue->startupScene;
        scaffold.sceneDirectory = project.getSceneDirectory();
        scaffold.assetDirectory = project.getAssetDirectory();
        scaffold.renderer = templateValue->renderer;

        for (const StudioProjectFile& file : language->projectFiles(scaffold))
        {
            // A template that shipped its own copy of a file the language also writes keeps it.
            // The template is the more specific answer, and silently overwriting what it provided
            // would make a template unable to customise its own entry point.
            const std::filesystem::path destination = root / file.pathInProject;
            std::error_code code;
            if (std::filesystem::exists(destination, code))
            {
                result.warnings.push_back("the template provides its own '" + file.pathInProject
                                          + "', so the one " + language->descriptor().displayName
                                          + " would have written was not used");
                continue;
            }

            std::string errorMessage;
            if (!writeFile(destination, file.contents, errorMessage))
            {
                result.problems.push_back({"directory", errorMessage});
                return result;
            }
            result.writtenFiles.push_back(file.pathInProject);
        }

        // --- And the project file last ----------------------------------------------------------
        // Last on purpose: a directory with a `.cnaproject` in it is one the Hub will list as a
        // project, so it appears only once everything it points at is there.
        std::string errorMessage;
        if (!project.saveToFile({}, &errorMessage))
        {
            result.problems.push_back({"directory", errorMessage});
            return result;
        }

        result.projectFilePath = project.getFilePath();
        result.writtenFiles.push_back(
            std::filesystem::path{result.projectFilePath}.filename().generic_string());
        return result;
    }
}
