// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Panels/HierarchyPanel.hpp
 * @brief The scene tree: selection, renaming, reparenting and the entity context menu.
 */

#include <string>

#include "CNA/Editor/Panels/EditorPanel.hpp"

namespace CNA::Editor
{
    /**
     * @brief Draws the scene hierarchy and edits its structure.
     *
     * Structural changes are recorded while drawing and applied afterwards. A reparent reorders
     * the child lists the recursion is walking and a delete invalidates them outright, so running
     * either mid-traversal would leave the tree walking a document that is changing underneath it.
     */
    class HierarchyPanel final : public EditorPanel
    {
    public:
        using EditorPanel::EditorPanel;

        void draw() override;

        /** @brief Puts @p entityId's row into rename mode. Also reachable from F2 and the menu. */
        void beginRename(const Uuid& entityId);

    private:
        /** @brief Recursively draws @p entityId and its children. */
        void drawNode(const Uuid& entityId);

        /** @brief Draws the right-click menu for one node. */
        void drawContextMenu(const Uuid& entityId);

        /** @brief Draws the in-place rename field in place of @p entityId's row. */
        void drawRenameField(const Uuid& entityId);

        /** @brief Runs whatever the tree asked for, once it has finished drawing. */
        void applyPendingAction();

        /** @brief Payload type for an entity dragged within the hierarchy. */
        static constexpr const char* kEntityDragType = "entity";

        /** @brief Payload type for an asset dragged out of the browser. */
        static constexpr const char* kAssetDragType = "asset";

        /** @brief Where a prefab made from a selection is written, project-relative. */
        static constexpr const char* kPrefabDirectory = "Assets/Prefabs";

        /** @brief What the tree asked for while it was drawing. */
        enum class Action
        {
            None,
            Reparent,
            Delete,
            /** @brief Turn the entity into a prefab, and itself into an instance of it. */
            CreatePrefab,
            /** @brief Instantiate a prefab asset under the entity. */
            InstantiatePrefab
        };

        struct PendingAction
        {
            Action kind = Action::None;
            Uuid entityId;
            Uuid parentId;

            /** @brief The prefab asset to instantiate, for Action::InstantiatePrefab. */
            Uuid assetId;
        };

        PendingAction pending_;

        /** @brief The entity whose row is currently a text field, if any. */
        Uuid renamingEntity_;
        std::string renameBuffer_;
        bool renameNeedsFocus_ = false;
    };
}
