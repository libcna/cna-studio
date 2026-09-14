// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Panels/AssetBrowserPanel.hpp"

#include <filesystem>

#include "CNA/Editor/Assets/AssetCommands.hpp"
#include "CNA/Editor/Assets/MaterialDocument.hpp"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "CNA/Editor/Assets/AssetCommands.hpp"
#include "CNA/Editor/EditorContext.hpp"

namespace CNA::Editor
{
    void AssetBrowserPanel::draw()
    {
        if (!ui_.beginPanel("Assets", DockSide::Bottom)) { ui_.endPanel(); return; }

        const AssetDatabase& assets = context_.getAssets();

        ui_.setNextItemWidth(220.0f);
        ui_.inputText("##assetFilter", filter_);
        ui_.sameLine();

        const AssetFolder root = buildAssetTree(assets, filter_);

        if (filter_.empty())
        {
            ui_.text(std::to_string(assets.getCount()) + " assets");
        }
        else
        {
            // The count of what survived the filter, not of everything -- the number is only
            // useful as an answer to what was just typed.
            ui_.text(std::to_string(root.getTotalAssetCount()) + " of "
                     + std::to_string(assets.getCount()) + " assets");
        }

        // The one thing this browser *creates* rather than discovers (ED-403). A material is the
        // editor's own document, so there is no file for a user to bring in from elsewhere -- and
        // an inspector that edits materials with no way to make one is a form nobody can reach.
        ui_.sameLine();
        if (context_.hasProject() && ui_.button("New Material"))
        {
            createMaterial();
        }

        ui_.separator();

        if (assets.getCount() == 0)
        {
            ui_.text("This project has no assets yet.");
        }
        else if (root.isEmpty())
        {
            // Said plainly. An empty tree looks identical to a broken one otherwise.
            ui_.text("Nothing matches '" + filter_ + "'.");
        }
        else
        {
            drawFolder(root, true);
        }

        ui_.endPanel();
        applyPendingMove();
    }

    void AssetBrowserPanel::drawFolder(const AssetFolder& folder, bool isRoot)
    {
        // The root has no row of its own -- it is the panel. Its contents are drawn flat.
        if (!isRoot)
        {
            const UiTreeNodeResult node =
                ui_.treeNode("folder:" + folder.path,
                             folder.name + "  (" + std::to_string(folder.getTotalAssetCount()) + ")",
                             false, false);

            // Dropping an asset onto a folder moves it there. The drag comes from an asset row in
            // this same panel, and the payload is the id, so the move never depends on a path.
            if (const std::optional<std::string> dropped = ui_.acceptDrop(kAssetDragType))
            {
                const Uuid draggedId = Uuid::parse(*dropped);
                if (const AssetRecord* record = context_.getAssets().find(draggedId))
                {
                    moveAssetId_ = draggedId;
                    moveDestination_ = joinAssetPath(folder.path, assetFileName(record->sourcePath));
                }
            }

            if (!node.expanded) { return; }
        }

        for (const AssetFolder& child : folder.folders) { drawFolder(child, false); }
        for (const Uuid& assetId : folder.assets) { drawAsset(assetId); }

        if (!isRoot) { ui_.treePop(); }
    }

    void AssetBrowserPanel::drawAsset(const Uuid& assetId)
    {
        const AssetRecord* record = context_.getAssets().find(assetId);
        if (record == nullptr) { return; }

        const std::string fileName = assetFileName(record->sourcePath);

        if (renamingAsset_ == assetId)
        {
            const UiTextFieldResult result =
                ui_.inputText("##rename-" + assetId.toString(), renameBuffer_, renameNeedsFocus_);

            // Only the first frame asks for focus; asking every frame would make the field
            // impossible to leave, because it would take the keyboard back the instant anything
            // else claimed it.
            renameNeedsFocus_ = false;
            if (!result.committed) { return; }

            renamingAsset_ = Uuid{};

            // An empty name is a slip, not an instruction, and a name with a separator in it is a
            // move disguised as a rename -- which the folder drop already does, unambiguously.
            if (renameBuffer_.empty() || renameBuffer_ == fileName) { return; }
            if (renameBuffer_.find('/') != std::string::npos
                || renameBuffer_.find('\\') != std::string::npos)
            {
                context_.log(LogSeverity::Warning,
                             "A name cannot contain a path separator. Drag the asset onto a folder "
                             "to move it.");
                return;
            }

            moveAssetId_ = assetId;
            moveDestination_ = joinAssetPath(assetDirectory(record->sourcePath), renameBuffer_);
            return;
        }

        drawThumbnail(*record);

        const std::string label = std::string{toString(record->type)} + "  " + fileName;
        const UiTreeNodeResult row =
            ui_.treeNode(assetId, label, context_.getSelectedAsset() == assetId, true);

        ui_.setDragSource(kAssetDragType, assetId.toString(), label);

        if (ui_.beginContextMenu("asset-" + assetId.toString()))
        {
            // Right-clicking a row acts on that row, so it becomes the selection first. Acting on
            // something other than what was right-clicked is the classic context-menu bug.
            context_.selectAsset(assetId);

            if (ui_.menuItem("Rename", "F2"))
            {
                renamingAsset_ = assetId;
                renameBuffer_ = fileName;
                renameNeedsFocus_ = true;
            }
            ui_.endContextMenu();
        }

        if (row.doubleClicked)
        {
            renamingAsset_ = assetId;
            renameBuffer_ = fileName;
            renameNeedsFocus_ = true;
        }
        else if (row.clicked)
        {
            // Selecting an asset is what puts its import settings in the inspector, which is the
            // only place they can be edited.
            context_.selectAsset(assetId);
        }
    }

    void AssetBrowserPanel::createMaterial()
    {
        // Numbered until the name is free rather than overwriting: pressing the button twice is
        // two materials, which is what pressing it twice means everywhere else.
        const std::string directory = context_.getAssets().resolvePath("Assets/Materials");
        std::string name = "Material";
        std::string path = directory + "/" + name + ".cnamaterial";

        for (int suffix = 1; std::filesystem::exists(path) && suffix < 1000; ++suffix)
        {
            name = "Material" + std::to_string(suffix);
            path = directory + "/" + name + ".cnamaterial";
        }

        MaterialDocument material;
        material.name = name;

        auto command = std::make_unique<SetMaterialCommand>(path, material, "created");
        SetMaterialCommand* raw = command.get();
        context_.execute(std::move(command));

        if (!raw->succeeded())
        {
            context_.log(LogSeverity::Warning, "Could not write '" + path + "'.");
            return;
        }

        // The scan is what gives the new file its id and sidecar, and until it runs the material
        // exists on disk and not in the database -- which is a state no other part of the editor
        // is written to expect.
        context_.getAssets().scan(context_.getProject().getAssetDirectory());
        context_.log(LogSeverity::Info, "Created material '" + name + "'.");
    }

    void AssetBrowserPanel::drawThumbnail(const AssetRecord& record)
    {
        // A model is drawn rather than decoded (ED-406). Its geometry comes from the same cache the
        // 3D view draws from, so a model already on screen costs nothing extra -- and a model that
        // has not been imported yet simply has no thumbnail, which is what the browser already
        // shows for an image it cannot read.
        if (record.importerId == ImporterIds::kModel)
        {
            const MeshProvider meshes = context_.makeMeshProvider();
            const MeshData* mesh = meshes ? meshes(record.id) : nullptr;
            if (mesh == nullptr || mesh->isEmpty()) { return; }

            const UiTextureId model = actions_.getViewport().getModelThumbnail(
                record.id, *mesh, static_cast<int>(kThumbnailExtent));
            if (model == kUiTextureNone) { return; }

            // Square, because the render target is: a model has no aspect ratio the way an image
            // does, and framing it inside the square is the camera's job rather than the layout's.
            ui_.image("##thumb-" + record.id.toString(), model, kThumbnailExtent, kThumbnailExtent);
            ui_.sameLine();
            return;
        }

        const UiTextureId texture = actions_.getViewport().getAssetThumbnail(record.id);
        if (texture == kUiTextureNone) { return; }

        // Fitted inside a square box using the pixel size the importer already recorded, so a
        // wide sprite is not squashed into a square. An asset whose size is unknown -- a format
        // the editor cannot measure -- falls back to the box, which is no worse than nothing.
        float width = kThumbnailExtent;
        float height = kThumbnailExtent;

        const EditorVector2 pixels =
            PropertyValue::fromJson(record.importerSettings["pixelSize"], PropertyType::Vector2)
                .get<EditorVector2>();

        if (pixels.x > 0.0f && pixels.y > 0.0f)
        {
            const float scale = kThumbnailExtent / std::max(pixels.x, pixels.y);
            width = pixels.x * scale;
            height = pixels.y * scale;
        }

        ui_.image("##thumb-" + record.id.toString(), texture, width, height);
        ui_.sameLine();
    }

    void AssetBrowserPanel::applyPendingMove()
    {
        const Uuid assetId = moveAssetId_;
        const std::string destination = moveDestination_;
        moveAssetId_ = Uuid{};
        moveDestination_.clear();

        if (!assetId.isValid() || destination.empty()) { return; }

        const AssetRecord* record = context_.getAssets().find(assetId);
        if (record == nullptr || record->sourcePath == destination) { return; }

        const std::string from = record->sourcePath;
        auto command = std::make_unique<MoveAssetCommand>(context_.getAssets(), assetId, destination);
        if (!command->isValid())
        {
            context_.log(LogSeverity::Warning, "Cannot move '" + from + "': " + command->getError());
            return;
        }

        MoveAssetCommand& reference = *command;
        context_.execute(std::move(command));

        // The command retires itself when the filesystem refuses, so this distinguishes "moved"
        // from "tried and could not" rather than reporting both as success.
        if (reference.isValid())
        {
            context_.log(LogSeverity::Info, "Moved '" + from + "' to '" + destination + "'.");
        }
        else
        {
            context_.log(LogSeverity::Warning,
                         "Cannot move '" + from + "': " + reference.getError());
        }
    }
}
