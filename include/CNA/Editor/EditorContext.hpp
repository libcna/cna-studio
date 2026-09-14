// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/EditorContext.hpp
 * @brief The single object every panel, command and plugin is handed.
 *
 * EditorContext owns the open project, the open scene, the component registry, the asset
 * database, the undo stack and the selection. Panels hold a reference to it and nothing else,
 * which keeps the panel layer free of globals and makes a panel testable by handing it a context
 * built over a scratch document.
 *
 * It deliberately does *not* own the UI: the context is what the headless build uses, and pulling
 * a toolkit dependency in here would undo the separation EditorUi exists to create.
 */

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "CNA/Editor/Assets/AssetDatabase.hpp"
#include "CNA/Editor/Assets/AssetImporters.hpp"
#include "CNA/Editor/Assets/MaterialDocument.hpp"
#include "CNA/Editor/Plugins/PluginExtensions.hpp"
#include "CNA/Editor/Assets/MeshCache.hpp"
#include "CNA/Editor/Scene/SceneModels.hpp"
#include "CNA/Editor/Core/ComponentDescriptor.hpp"
#include "CNA/Editor/Core/EditorCommand.hpp"
#include "CNA/Editor/Core/Uuid.hpp"
#include "CNA/Editor/Project/Project.hpp"
#include "CNA/Editor/Scene/SceneDocument.hpp"
#include "CNA/Editor/Ui/EditorUi.hpp"

namespace CNA::Editor
{
    /**
     * @brief The editor's shared, non-UI state.
     *
     * Selection is a list rather than a single id because multi-select is not something to retrofit:
     * every command that acts on "the selection" has to be written against a set from the start, or
     * it will be rewritten later.
     */
    class EditorContext
    {
    public:
        /** @brief A console sink. The application installs one that forwards into the UI. */
        using LogSink = std::function<void(LogSeverity, const std::string&)>;

        EditorContext();

        [[nodiscard]] Project& getProject() { return project_; }
        [[nodiscard]] const Project& getProject() const { return project_; }

        [[nodiscard]] SceneDocument& getScene() { return scene_; }
        [[nodiscard]] const SceneDocument& getScene() const { return scene_; }

        [[nodiscard]] ComponentRegistry& getComponentRegistry() { return components_; }
        [[nodiscard]] const ComponentRegistry& getComponentRegistry() const { return components_; }

        /**
         * @brief Returns the importer settings schemas.
         *
         * A separate registry from the component one: an importer id and a component type id are
         * different namespaces, and a project that named a component "CNA.TextureImporter" should
         * not silently become editable as an importer.
         */
        /** @brief Panels and menu commands contributed by plugins (ED-412). */
        [[nodiscard]] PluginExtensionRegistry& getPluginExtensions() { return pluginExtensions_; }
        [[nodiscard]] const PluginExtensionRegistry& getPluginExtensions() const
        {
            return pluginExtensions_;
        }

        [[nodiscard]] ComponentRegistry& getImporterRegistry() { return importers_; }
        [[nodiscard]] const ComponentRegistry& getImporterRegistry() const { return importers_; }

        [[nodiscard]] AssetDatabase& getAssets() { return assets_; }
        [[nodiscard]] const AssetDatabase& getAssets() const { return assets_; }

        /**
         * @brief Returns the imported-model cache (plan.md ED-405).
         *
         * Here rather than in the viewport, beside the sprite sizes it resembles, for one reason:
         * a `MeshData` needs no CNA. Keeping it in the context means the *standalone* build draws
         * real models -- `--headless --view=3d` and the CI wireframe tests included -- and that
         * when ED-402 arrives it uploads what this already holds instead of reading every file a
         * second way.
         */
        [[nodiscard]] MeshCache& getMeshes() { return meshes_; }
        [[nodiscard]] const MeshCache& getMeshes() const { return meshes_; }

        /** @brief Returns a mesh provider bound to this context's cache and asset database. */
        [[nodiscard]] MeshProvider makeMeshProvider()
        {
            return meshes_.makeProvider(assets_);
        }

        /**
         * @brief Returns a `MaterialProvider` that reads `.cnamaterial` assets (ED-403).
         *
         * Read on each ask rather than cached, unlike meshes. A material is a few dozen bytes of
         * JSON against a mesh's megabytes, and the cache a mesh needs exists to stop a whole-file
         * parse happening sixty times a second -- the same cache here would buy microseconds and
         * cost a second thing to invalidate when a file changes on disk.
         */
        [[nodiscard]] MaterialProvider makeMaterialProvider();

        [[nodiscard]] CommandHistory& getHistory() { return history_; }
        [[nodiscard]] const CommandHistory& getHistory() const { return history_; }

        /** @brief Returns true when a project is open. */
        [[nodiscard]] bool hasProject() const { return !project_.getFilePath().empty(); }

        /** @brief Returns the path of the currently open scene file, or an empty string. */
        [[nodiscard]] const std::string& getScenePath() const { return scenePath_; }

        /**
         * @brief Opens a `.cnaproject`, scans its assets and loads its startup scene.
         *
         * A failure to load the startup scene is reported but does not fail the open: a project
         * whose scene file is broken is exactly the project a user needs the editor for.
         *
         * @return False only when the project file itself cannot be read.
         */
        bool openProject(const std::string& path);

        /** @brief Loads a scene file into the context, clearing the undo history. */
        bool openScene(const std::string& path);

        /** @brief Saves the open scene back to getScenePath(), or to @p path when supplied. */
        bool saveScene(const std::string& path = {});

        /** @brief Replaces the open scene with an empty one holding a single camera entity. */
        void newScene(std::string name = "Untitled");

        /** @brief Notified of each command as it is executed. */
        using CommandObserver = std::function<void(const EditorCommand&)>;

        /**
         * @brief Installs the command observer. Passing an empty function removes it.
         *
         * Exists so the application can mirror a document change into a running player without the
         * context having to know what a player is. It is called on every execute(), including the
         * ones that merge into the previous entry -- a gizmo drag whose intermediate steps never
         * reached the player would arrive there as one jump at the end.
         */
        void setCommandObserver(CommandObserver observer) { commandObserver_ = std::move(observer); }

        /** @brief Runs @p command through the undo stack. */
        void execute(std::unique_ptr<EditorCommand> command, MergePolicy policy = MergePolicy::NewEntry);

        /** @brief Returns the selected entity ids, in selection order. */
        [[nodiscard]] const std::vector<Uuid>& getSelection() const { return selection_; }

        /** @brief Returns the first selected id, or the nil Uuid when nothing is selected. */
        [[nodiscard]] Uuid getPrimarySelection() const;

        /** @brief Returns true when @p id is selected. */
        [[nodiscard]] bool isSelected(const Uuid& id) const;

        /** @brief Replaces the selection with @p id, or clears it when @p id is nil. */
        void select(const Uuid& id);

        /** @brief Returns the asset the inspector is showing, or the nil Uuid. */
        [[nodiscard]] const Uuid& getSelectedAsset() const { return selectedAsset_; }

        /**
         * @brief Selects an asset for inspection, clearing any entity selection.
         *
         * The inspector shows one thing at a time. Two independent selections would leave the
         * user unable to tell which of them the panel is about, and the answer would change
         * depending on which they touched last anyway.
         */
        void selectAsset(const Uuid& id);

        /** @brief Replaces the selection wholesale. */
        void setSelection(std::vector<Uuid> ids);

        /** @brief Adds @p id to the selection when absent, removes it when present. */
        void toggleSelection(const Uuid& id);

        /** @brief Clears the selection. */
        void clearSelection();

        /**
         * @brief Drops selected ids that no longer exist in the scene.
         *
         * Called after any command that may delete entities. Without it, the inspector would keep
         * showing a deleted entity, and the next command would target a missing id.
         */
        void pruneSelection();

        /** @brief Installs the console sink. Passing an empty function silences logging. */
        void setLogSink(LogSink sink) { logSink_ = std::move(sink); }

        /** @brief Emits a console message. Safe to call with no sink installed. */
        void log(LogSeverity severity, const std::string& message) const;

    private:
        Project project_;
        SceneDocument scene_;
        ComponentRegistry components_;
        PluginExtensionRegistry pluginExtensions_;
        ComponentRegistry importers_;
        AssetDatabase assets_;
        MeshCache meshes_;
        CommandHistory history_;
        std::vector<Uuid> selection_;
        Uuid selectedAsset_;
        std::string scenePath_;
        LogSink logSink_;
        CommandObserver commandObserver_;
    };
}
