// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Project/ProjectTemplate.hpp
 * @brief What a project template is, and where the ones this Studio offers come from.
 *
 * `plan.md` STUDIO-08005.
 *
 * ### A template is data plus a file tree, and nothing else
 *
 * `STUDIO-08005`'s acceptance is that adding a template does not require changing Studio, and it
 * is meant literally: a template is a directory with a `template.json` in it and a `content/`
 * subtree beside it. Nothing about a template is compiled, registered, or listed in a C++ table.
 * Dropping a directory into a template search path is the whole of adding one — and because the
 * standalone build test (`STUDIO-08011`) enumerates the same search path, a template added that
 * way is a template CI builds and runs.
 *
 * The alternative — a `switch` over a template enum, or a function per template — is the same
 * mistake as the language chain `STUDIO-02080` was written against, one level down. Four templates
 * ship today; the fifth must not be a code change.
 *
 * ### What a template does *not* contain
 *
 * No build file, no entry point, no runtime. Those are the *language's* answer and come from the
 * project's `StudioLanguageAdapter` (`STUDIO-02083`), so that one template works for every
 * language that declares it can host it. A template carrying a `CMakeLists.txt` would be a
 * template that is secretly about C++, and there would be four copies of it.
 *
 * ### Determinism
 *
 * The content tree is copied byte for byte, UUIDs included. Two projects created from one template
 * therefore share their scene and entity ids, which is harmless — those ids are scoped to a scene
 * — and it is what makes project creation reproducible. Generating fresh ids would make the same
 * template produce a different tree every time, which is a worse property than the one it fixes.
 */

#include <string>
#include <vector>

#include "CNA/Studio/Project/Project.hpp"

namespace CNA::Studio
{
    /** @brief Which viewport a project created from a template opens in. */
    enum class StudioTemplateView
    {
        /** @brief The 2D viewport, which is what a sprite game wants. */
        TwoD,

        /** @brief The 3D world viewport (`STUDIO-11014`). */
        ThreeD
    };

    /** @brief Returns the stable textual name of @p view as written in `template.json`. */
    [[nodiscard]] const char* toString(StudioTemplateView view);

    /** @brief Parses the name produced by toString(); defaults to StudioTemplateView::TwoD. */
    [[nodiscard]] StudioTemplateView parseStudioTemplateView(std::string_view text);

    /**
     * @brief One template, as read from a `template.json`.
     *
     * Every field is either shown to the user or written into the project being created. None of
     * it says how anything is built.
     */
    struct StudioProjectTemplate
    {
        /** @brief Stable id, e.g. `"empty-3d"`. The directory name, and what `--template=` takes. */
        std::string id;

        /** @brief What the Project Hub shows, e.g. `"Empty 3D"`. */
        std::string name;

        /** @brief One or two sentences under the name. */
        std::string description;

        /** @brief Which Studio features a project from this template opts into. */
        ProjectKind kind = ProjectKind::CnaNative;

        /**
         * @brief Language ids this template can be created in. Empty means every language.
         *
         * Checked against the chosen adapter's `supportsProjectKind` as well: a template may be
         * happy with a language that cannot host its project kind, and both answers have to agree
         * before the template is offered.
         */
        std::vector<std::string> languages;

        /** @brief The renderer a project from this template starts on, in Studio's lower-case form. */
        std::string renderer;

        /** @brief The platform a project from this template starts on, e.g. `"sdl3"`. */
        std::string platform;

        /** @brief Which viewport the created project opens in. */
        StudioTemplateView view = StudioTemplateView::TwoD;

        /** @brief Project-relative path of the scene the game starts on. */
        std::string startupScene;

        /** @brief Where the Hub puts it in the list. Lower sorts first; ties break on @ref id. */
        int order = 100;

        /** @brief Absolute path of the directory this template was read from. */
        std::string directory;

        /** @brief Whether this template can be created in the language with @p languageId. */
        [[nodiscard]] bool supportsLanguage(std::string_view languageId) const;
    };

    /** @brief What reading a template directory produced. */
    struct StudioTemplateLoadResult
    {
        /** @brief The template, when it could be read. */
        StudioProjectTemplate value;

        /** @brief Empty on success; otherwise what was wrong with the manifest. */
        std::string errorMessage;

        [[nodiscard]] bool succeeded() const { return errorMessage.empty(); }
    };

    /**
     * @brief Reads one template directory.
     *
     * @param directory A directory holding `template.json` and a `content/` subtree.
     * @return The template, or why it could not be read.
     */
    [[nodiscard]] StudioTemplateLoadResult loadStudioProjectTemplate(const std::string& directory);

    /**
     * @brief The templates this Studio offers, and where they were found.
     *
     * A value, not a service: built by whoever assembles Studio and handed to what needs it.
     */
    class StudioTemplateCatalogue
    {
    public:
        /** @brief The subdirectory of a template directory whose tree is copied into a project. */
        static constexpr const char* kContentDirectory = "content";

        /** @brief The manifest file every template directory must hold. */
        static constexpr const char* kManifestFile = "template.json";

        /**
         * @brief Reads every template directory under @p searchPath and adds what it finds.
         *
         * A directory without a manifest is skipped in silence — a search path is somewhere users
         * put things, and refusing to start because of a stray directory would be the wrong
         * trade. A directory *with* a manifest that cannot be read is a problem and is reported,
         * because somebody meant that one to work.
         *
         * @param searchPath A directory holding template directories. Missing is not an error.
         * @return The manifests that failed, each already naming its directory.
         */
        std::vector<std::string> addSearchPath(const std::string& searchPath);

        /** @brief Adds @p templateValue, replacing any with the same id. */
        void add(StudioProjectTemplate templateValue);

        /** @brief Every template, ordered by `order` then `id`. */
        [[nodiscard]] const std::vector<StudioProjectTemplate>& all() const { return templates_; }

        /** @brief Returns the template with @p id, or null. */
        [[nodiscard]] const StudioProjectTemplate* find(std::string_view id) const;

        /** @brief The templates that can be created in @p languageId, in list order. */
        [[nodiscard]] std::vector<const StudioProjectTemplate*> forLanguage(
            std::string_view languageId) const;

        [[nodiscard]] bool empty() const { return templates_.empty(); }

    private:
        std::vector<StudioProjectTemplate> templates_;
    };

    /**
     * @brief Where a build of Studio looks for templates, most specific first.
     *
     * Two kinds of entry, and they answer different questions. Beside the executable is where an
     * *installed* Studio keeps the templates it shipped with. The source tree is where a
     * *developer's* build finds them, baked in at configure time so that running `cna-studio` out
     * of a build directory offers the same templates a user gets — which is also what lets CI
     * build every one of them without an install step.
     *
     * @param executablePath `argv[0]`, or empty when it is not known.
     * @return Directories to search, in order. Ones that do not exist are included; the catalogue
     *         skips them.
     */
    [[nodiscard]] std::vector<std::string> studioTemplateSearchPaths(
        std::string_view executablePath);
}
