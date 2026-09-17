// SPDX-License-Identifier: MS-PL
/**
 * @file ProjectTemplate.cpp
 * @brief Reading template manifests, and finding the directories they live in.
 *
 * `plan.md` STUDIO-08005.
 */

#include "CNA/Studio/Project/ProjectTemplate.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <system_error>

#include "CNA/Studio/Core/Json.hpp"

namespace CNA::Studio
{
    const char* toString(StudioTemplateView view)
    {
        switch (view)
        {
            case StudioTemplateView::TwoD: return "2d";
            case StudioTemplateView::ThreeD: return "3d";
        }
        return "2d";
    }

    StudioTemplateView parseStudioTemplateView(std::string_view text)
    {
        return text == "3d" ? StudioTemplateView::ThreeD : StudioTemplateView::TwoD;
    }

    bool StudioProjectTemplate::supportsLanguage(std::string_view languageId) const
    {
        // An empty list means every language, which is the honest default for a template that is
        // nothing but scenes and assets. A template that genuinely needs one says so.
        if (languages.empty()) { return true; }
        return std::find(languages.begin(), languages.end(), languageId) != languages.end();
    }

    StudioTemplateLoadResult loadStudioProjectTemplate(const std::string& directory)
    {
        StudioTemplateLoadResult result;

        const std::filesystem::path root{directory};
        const std::filesystem::path manifestPath =
            root / StudioTemplateCatalogue::kManifestFile;

        std::ifstream stream{manifestPath, std::ios::binary};
        if (!stream)
        {
            result.errorMessage = "'" + manifestPath.generic_string() + "' cannot be read";
            return result;
        }

        const std::string text{std::istreambuf_iterator<char>{stream},
                               std::istreambuf_iterator<char>{}};
        const JsonParseResult parsed = Json::parse(text);
        if (!parsed.succeeded)
        {
            result.errorMessage = "'" + manifestPath.generic_string() + "' is not valid JSON: "
                                + parsed.errorMessage;
            return result;
        }
        if (!parsed.value.isObject())
        {
            result.errorMessage = "'" + manifestPath.generic_string() + "' is not a JSON object";
            return result;
        }

        StudioProjectTemplate value;
        value.directory = root.generic_string();

        // The directory name is the id unless the manifest says otherwise, so the two cannot
        // disagree by accident -- and a template moved to a different directory keeps working.
        value.id = parsed.value["id"].asString(root.filename().generic_string());
        if (value.id.empty())
        {
            result.errorMessage = "'" + manifestPath.generic_string() + "' has no id";
            return result;
        }

        value.name = parsed.value["name"].asString(value.id);
        value.description = parsed.value["description"].asString();
        value.kind = parseProjectKind(parsed.value["kind"].asString("CnaNative"));
        value.renderer = parsed.value["renderer"].asString();
        value.platform = parsed.value["platform"].asString();
        value.view = parseStudioTemplateView(parsed.value["view"].asString("2d"));
        value.startupScene = parsed.value["startupScene"].asString();
        value.order = parsed.value["order"].asInt(100);

        for (const JsonValue& language : parsed.value["languages"].getElements())
        {
            const std::string id = language.asString();
            if (!id.empty()) { value.languages.push_back(id); }
        }

        // The content tree is the other half of a template, and a manifest without one describes a
        // project with no scenes and no assets. Refused here rather than at creation, because the
        // Hub offering a template that produces an empty directory is worse than one that is not
        // offered at all.
        std::error_code code;
        if (!std::filesystem::is_directory(root / StudioTemplateCatalogue::kContentDirectory, code))
        {
            result.errorMessage = "template '" + value.id + "' has no '"
                                + StudioTemplateCatalogue::kContentDirectory + "' directory";
            return result;
        }

        result.value = std::move(value);
        return result;
    }

    void StudioTemplateCatalogue::add(StudioProjectTemplate templateValue)
    {
        const auto existing = std::find_if(templates_.begin(), templates_.end(),
            [&](const StudioProjectTemplate& registered) {
                return registered.id == templateValue.id;
            });

        // Replaced rather than appended, which is what makes the search-path order mean something:
        // a template the user put in their own directory wins over the one that shipped with the
        // same id, because their path is searched first.
        if (existing != templates_.end()) { *existing = std::move(templateValue); }
        else { templates_.push_back(std::move(templateValue)); }

        std::stable_sort(templates_.begin(), templates_.end(),
            [](const StudioProjectTemplate& lhs, const StudioProjectTemplate& rhs) {
                if (lhs.order != rhs.order) { return lhs.order < rhs.order; }
                return lhs.id < rhs.id;
            });
    }

    std::vector<std::string> StudioTemplateCatalogue::addSearchPath(const std::string& searchPath)
    {
        std::vector<std::string> problems;
        if (searchPath.empty()) { return problems; }

        std::error_code code;
        const std::filesystem::path root{searchPath};
        if (!std::filesystem::is_directory(root, code)) { return problems; }

        // Sorted, so that two templates with the same order and the same id are resolved the same
        // way on every machine. A directory iterator's order is the filesystem's, which is not.
        std::vector<std::filesystem::path> directories;
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator{root, code})
        {
            if (entry.is_directory(code)) { directories.push_back(entry.path()); }
        }
        std::sort(directories.begin(), directories.end());

        for (const std::filesystem::path& directory : directories)
        {
            // No manifest means it is not a template: a search path is somewhere people put things,
            // and refusing to start over a stray directory would be the wrong trade.
            if (!std::filesystem::is_regular_file(directory / kManifestFile, code)) { continue; }

            StudioTemplateLoadResult loaded =
                loadStudioProjectTemplate(directory.generic_string());
            if (!loaded.succeeded())
            {
                // A manifest that cannot be read *is* a problem: somebody meant that one to work.
                problems.push_back(loaded.errorMessage);
                continue;
            }

            // Only if nothing has claimed the id already. The first search path wins, which is
            // what makes "most specific first" mean anything.
            if (find(loaded.value.id) == nullptr) { add(std::move(loaded.value)); }
        }
        return problems;
    }

    const StudioProjectTemplate* StudioTemplateCatalogue::find(std::string_view id) const
    {
        const auto found = std::find_if(templates_.begin(), templates_.end(),
            [&](const StudioProjectTemplate& registered) { return registered.id == id; });
        return found == templates_.end() ? nullptr : &*found;
    }

    std::vector<const StudioProjectTemplate*> StudioTemplateCatalogue::forLanguage(
        std::string_view languageId) const
    {
        std::vector<const StudioProjectTemplate*> offered;
        for (const StudioProjectTemplate& value : templates_)
        {
            if (value.supportsLanguage(languageId)) { offered.push_back(&value); }
        }
        return offered;
    }

    std::vector<std::string> studioTemplateSearchPaths(std::string_view executablePath)
    {
        std::vector<std::string> paths;

        // Beside the executable, where an installed Studio keeps what it shipped with. Two
        // spellings because a Unix install puts them under `share/` and a self-contained directory
        // puts them next to the binary, and Studio should work when unpacked either way.
        if (!executablePath.empty())
        {
            std::error_code code;
            const std::filesystem::path binary =
                std::filesystem::absolute(std::filesystem::path{executablePath}, code);
            if (!code)
            {
                const std::filesystem::path directory = binary.parent_path();
                paths.push_back((directory / "templates").generic_string());
                paths.push_back((directory / ".." / "share" / "cna-studio" / "templates")
                                    .lexically_normal().generic_string());
            }
        }

        // And the source tree, baked in at configure time. Running `cna-studio` out of a build
        // directory then offers the same templates a user gets, which is what lets CI build every
        // one of them without an install step -- and what stops the templates being a thing only
        // an installed Studio has ever exercised.
#ifdef CNA_STUDIO_TEMPLATE_DIRECTORY
        paths.emplace_back(CNA_STUDIO_TEMPLATE_DIRECTORY);
#endif
        return paths;
    }
}
