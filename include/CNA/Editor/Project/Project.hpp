// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Project/Project.hpp
 * @brief The `.cnaproject` file: what the editor needs to know about a game before opening it.
 *
 * A project has a *kind* (ProjectKind), and that choice is what keeps CNA from turning into a
 * mandatory engine (ANALYSIS.md decision D-10). A CnaNative project opts into scenes, entities and
 * the editor's component model. An XnaCompatible project does not: for those the editor is an
 * asset and content-pipeline tool plus a launcher, and the game keeps its own hand-written
 * `Game::Initialize`/`LoadContent`/`Update`/`Draw` structure with no editor concepts in it at all.
 */

#include <string>
#include <vector>

#include "CNA/Editor/Core/FormatMigration.hpp"
#include "CNA/Editor/Core/Json.hpp"

namespace CNA::Editor
{
    /** @brief Determines which editor features a project opts into. */
    enum class ProjectKind
    {
        /**
         * @brief Uses the editor's scene/entity/component model.
         *
         * Scenes, the inspector, gizmos, prefabs and the runtime bridge all apply.
         */
        CnaNative,

        /**
         * @brief A plain XNA-style CNA game with its own object model.
         *
         * The editor offers the asset browser, importer settings, content preview, backend
         * configuration and Play; it offers no scene editing, because there is no scene to edit.
         * A pure XNA port must never be forced through the entity model to use the tooling.
         */
        XnaCompatible
    };

    /** @brief Returns the stable textual name of @p kind as written into `.cnaproject`. */
    const char* toString(ProjectKind kind);

    /** @brief Parses the name produced by toString(); defaults to ProjectKind::CnaNative. */
    ProjectKind parseProjectKind(std::string_view text);

    /**
     * @brief How well a given CNA graphics backend works underneath the editor itself.
     *
     * This distinction exists because CNA selects its backend at *compile* time, so the editor
     * binary and the previewed game are built against different CNA builds (ANALYSIS.md finding
     * F-01). A backend can therefore be perfectly good for running a game while being unable to
     * host a docked editor UI at an arbitrary window size.
     */
    enum class BackendEditorSupport
    {
        /** @brief The editor UI itself can be hosted on this backend. */
        EditorSupported,

        /** @brief Usable for a preview/player process, not for the editor UI. */
        PreviewOnly,

        /** @brief Ships games only; no editor or preview role. */
        RuntimeOnly
    };

    /** @brief One entry in the table of CNA backends the editor knows about. */
    struct BackendInfo
    {
        /** @brief The value CNA's own CNA_GRAPHICS_BACKEND CMake option takes, e.g. "VULKAN". */
        std::string cmakeName;

        /** @brief Lower-case name used on the command line and in `.cnaproject`, e.g. "vulkan". */
        std::string commandLineName;

        std::string displayName;
        BackendEditorSupport support = BackendEditorSupport::RuntimeOnly;

        /** @brief Why the support level is what it is; shown in the backend configuration dialog. */
        std::string note;
    };

    /**
     * @brief Returns the backend table, mirroring CNA's cmake/BackendSelection.cmake.
     *
     * Kept as data in the editor rather than queried from CNA, because the editor must be able to
     * talk about a backend it was not itself built against -- which is the normal case, since it
     * launches player processes built from other CNA configurations.
     */
    const std::vector<BackendInfo>& getKnownBackends();

    /** @brief Returns the entry whose commandLineName is @p name, or nullptr. */
    const BackendInfo* findBackend(std::string_view name);

    /** @brief Outcome of loading a `.cnaproject`. */
    struct ProjectLoadResult
    {
        bool succeeded = false;
        std::string errorMessage;
        std::vector<std::string> warnings;
    };

    /**
     * @brief An open project.
     *
     * Paths stored here are relative to the project root and use forward slashes, so a
     * `.cnaproject` committed on one platform opens unchanged on another.
     */
    /**
     * @brief Returns the migration chain that upgrades a `.cnaproject` to the current version.
     *
     * Empty today. Run on every load regardless, so the first real migration is an addition to a
     * path that already works rather than a path nobody has exercised.
     */
    [[nodiscard]] const FormatMigrator& getProjectFormatMigrator();

    class Project
    {
    public:
        /** @brief The `formatVersion` this build writes, and the highest it can read. */
        static constexpr int kFormatVersion = 1;

        /** @brief The project file extension. */
        static constexpr const char* kFileExtension = ".cnaproject";

        [[nodiscard]] const std::string& getName() const { return name_; }
        void setName(std::string name) { name_ = std::move(name); }

        [[nodiscard]] ProjectKind getKind() const { return kind_; }
        void setKind(ProjectKind kind) { kind_ = kind; }

        /** @brief Absolute path of the directory containing the `.cnaproject` file. */
        [[nodiscard]] const std::string& getRootPath() const { return rootPath_; }

        /** @brief Absolute path of the `.cnaproject` file itself. */
        [[nodiscard]] const std::string& getFilePath() const { return filePath_; }

        /** @brief Project-relative path of the scene opened when the game starts. */
        [[nodiscard]] const std::string& getStartupScene() const { return startupScene_; }
        void setStartupScene(std::string path) { startupScene_ = std::move(path); }

        /** @brief Project-relative directory scanned by the AssetDatabase. Defaults to "Assets". */
        [[nodiscard]] const std::string& getAssetDirectory() const { return assetDirectory_; }
        void setAssetDirectory(std::string path) { assetDirectory_ = std::move(path); }

        /** @brief Project-relative directory holding `.cnascene` files. Defaults to "Scenes". */
        [[nodiscard]] const std::string& getSceneDirectory() const { return sceneDirectory_; }
        void setSceneDirectory(std::string path) { sceneDirectory_ = std::move(path); }

        /** @brief Command-line backend name the Play button prefers, e.g. "easygl". */
        [[nodiscard]] const std::string& getDefaultGraphicsBackend() const { return defaultGraphicsBackend_; }
        void setDefaultGraphicsBackend(std::string name) { defaultGraphicsBackend_ = std::move(name); }

        /** @brief Target platform triples the build/publish dialog offers. */
        [[nodiscard]] const std::vector<std::string>& getTargetPlatforms() const { return targetPlatforms_; }
        void setTargetPlatforms(std::vector<std::string> platforms) { targetPlatforms_ = std::move(platforms); }

        /**
         * @brief Returns the render layers, in back-to-front order.
         *
         * A property of the game, not of one level: a layer named in one scene and missing from
         * the next would make moving an entity between scenes silently change what it is. The
         * order is the meaning -- index 0 draws first -- so this is a list rather than a set.
         *
         * Never empty. A project with no layers could not have a valid entity, so the reader
         * substitutes the default rather than leaving a state nothing can point at.
         */
        [[nodiscard]] const std::vector<std::string>& getLayers() const { return layers_; }

        /**
         * @brief Returns the world-space step a snapped drag rounds to, or 0 for the visible grid.
         *
         * A project laid out on a 16-pixel tile grid wants to say so once rather than have every
         * user zoom until the drawn grid happens to agree. Zero is not "no snapping" -- Ctrl is
         * what turns snapping on -- it is "use the grid the viewport is drawing", which is what
         * the editor did before this setting existed and therefore what an older project means.
         */
        [[nodiscard]] float getGridSnap() const { return gridSnap_; }

        /** @brief Sets the snap step. Negative values are refused; zero restores the visible grid. */
        void setGridSnap(float step);

        /** @brief Replaces the layer list. An empty list is refused, leaving the previous one. */
        void setLayers(std::vector<std::string> layers);

        /** @brief The layer every entity starts on, and the one a project is created with. */
        static constexpr const char* kDefaultLayer = "Default";

        /** @brief CNA modules the game links, e.g. "cna-core", "cna-audio". */
        [[nodiscard]] const std::vector<std::string>& getModules() const { return modules_; }
        void setModules(std::vector<std::string> modules) { modules_ = std::move(modules); }

        /** @brief Plugin ids the editor should load for this project. */
        [[nodiscard]] const std::vector<std::string>& getPlugins() const { return plugins_; }
        void setPlugins(std::vector<std::string> plugins) { plugins_ = std::move(plugins); }

        /** @brief Serialises to the `.cnaproject` JSON documented in docs/FORMATS.md. */
        [[nodiscard]] JsonValue toJson() const;

        /** @brief Replaces this project's contents from @p json, without touching the paths. */
        ProjectLoadResult loadFromJson(const JsonValue& json, const FormatMigrator* migrator = nullptr);

        /** @brief Loads a `.cnaproject` from @p path and records the root and file paths. */
        ProjectLoadResult loadFromFile(const std::string& path, const FormatMigrator* migrator = nullptr);

        /** @brief Writes the project back to getFilePath(), or to @p path when one is supplied. */
        [[nodiscard]] bool saveToFile(const std::string& path = {}, std::string* errorMessage = nullptr);

        /** @brief Returns an absolute path for the project-relative @p relativePath. */
        [[nodiscard]] std::string resolvePath(std::string_view relativePath) const;

        /** @brief Fills in the standard directory layout for a new project named @p name. */
        static Project createDefault(std::string name, std::string rootPath);

    private:
        std::string name_ = "Untitled";
        ProjectKind kind_ = ProjectKind::CnaNative;
        std::string rootPath_;
        std::string filePath_;
        std::string startupScene_;
        std::string assetDirectory_ = "Assets";
        std::string sceneDirectory_ = "Scenes";
        std::string defaultGraphicsBackend_ = "easygl";
        std::vector<std::string> targetPlatforms_{"linux-x64"};
        std::vector<std::string> layers_{kDefaultLayer};

        /** @brief World units a snapped drag rounds to. Zero means the viewport's visible grid. */
        float gridSnap_ = 0.0f;
        std::vector<std::string> modules_{"cna-core"};
        std::vector<std::string> plugins_;
    };
}
