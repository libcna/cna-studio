// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Scene/Tilemap.hpp"

#include <algorithm>
#include <cmath>

#include "CNA/Editor/Scene/BuiltinComponents.hpp"
#include "CNA/Editor/Scene/SceneDocument.hpp"

namespace CNA::Editor
{
    namespace
    {
        /** @brief Returns the tilemap component on @p entityId, or nullptr. */
        EditorComponent* findTilemap(SceneDocument& document, const Uuid& entityId)
        {
            EditorEntity* entity = document.findEntity(entityId);
            return entity != nullptr ? entity->findComponent(BuiltinComponentIds::kTilemap) : nullptr;
        }
    }

    std::int64_t TilemapGrid::at(int x, int y) const
    {
        if (!contains(x, y)) { return kEmptyTile; }

        const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(columns)
                                + static_cast<std::size_t>(x);
        return index < tiles.size() ? tiles[index] : kEmptyTile;
    }

    void TilemapGrid::set(int x, int y, std::int64_t tile)
    {
        if (!contains(x, y)) { return; }

        const std::size_t index = static_cast<std::size_t>(y) * static_cast<std::size_t>(columns)
                                + static_cast<std::size_t>(x);
        if (index >= tiles.size()) { tiles.resize(index + 1, kEmptyTile); }
        tiles[index] = tile;
    }

    PropertyValue TilemapGrid::toPropertyValue() const
    {
        PropertyValue::ListValue list;
        list.items.reserve(tiles.size());
        for (const std::int64_t tile : tiles) { list.items.emplace_back(tile); }
        return PropertyValue{std::move(list)};
    }

    TilemapGrid readTilemapGrid(const EditorComponent& component, const ComponentDescriptor* descriptor)
    {
        TilemapGrid grid;
        grid.columns = static_cast<int>(
            component.getPropertyOrDefault(TilemapKeys::kColumns, descriptor).get<std::int64_t>(0));
        grid.rows = static_cast<int>(
            component.getPropertyOrDefault(TilemapKeys::kRows, descriptor).get<std::int64_t>(0));

        grid.columns = std::max(grid.columns, 0);
        grid.rows = std::max(grid.rows, 0);

        const std::size_t expected =
            static_cast<std::size_t>(grid.columns) * static_cast<std::size_t>(grid.rows);

        const PropertyValue stored = component.getProperty(TilemapKeys::kTiles);
        if (stored.getType() == PropertyType::List)
        {
            for (const PropertyValue& item : stored.get<PropertyValue::ListValue>().items)
            {
                grid.tiles.push_back(item.get<std::int64_t>(kEmptyTile));
            }
        }

        // Padded or truncated rather than rejected: a hand-edited scene one row short should open
        // and be fixable, not refuse to load.
        grid.tiles.resize(expected, kEmptyTile);
        return grid;
    }

    TilemapGrid resizeTilemapGrid(const TilemapGrid& grid, int columns, int rows)
    {
        TilemapGrid resized;
        resized.columns = std::max(columns, 0);
        resized.rows = std::max(rows, 0);
        resized.tiles.assign(static_cast<std::size_t>(resized.columns) * static_cast<std::size_t>(resized.rows),
                             kEmptyTile);

        // Copied by coordinate, not by index. Resizing a flat list without remapping shifts every
        // row sideways, which turns "make the map one column wider" into "scramble the level".
        for (int y = 0; y < resized.rows; ++y)
        {
            for (int x = 0; x < resized.columns; ++x)
            {
                resized.set(x, y, grid.at(x, y));
            }
        }
        return resized;
    }

    TileCoordinate worldToTile(const WorldTransform& transform,
                               int tileWidth,
                               int tileHeight,
                               const EditorVector2& worldPoint)
    {
        // A zero or negative tile size would divide by zero, and a hand-edited scene can hold one.
        const float width = tileWidth > 0 ? static_cast<float>(tileWidth) * transform.scale.x : 0.0f;
        const float height = tileHeight > 0 ? static_cast<float>(tileHeight) * transform.scale.y : 0.0f;
        if (width == 0.0f || height == 0.0f) { return TileCoordinate{-1, -1}; }

        const float localX = (worldPoint.x - transform.position.x) / width;
        const float localY = (worldPoint.y - transform.position.y) / height;

        // Floor, not truncate: truncation folds -0.5 and +0.5 onto the same cell, so painting just
        // left of the origin would land on the first column instead of outside the map.
        return TileCoordinate{static_cast<int>(std::floor(localX)), static_cast<int>(std::floor(localY))};
    }

    PaintTilesCommand::PaintTilesCommand(SceneDocument& document,
                                         const ComponentRegistry& registry,
                                         Uuid entityId,
                                         std::uint64_t strokeId)
        : document_(&document), registry_(&registry), entityId_(entityId), strokeId_(strokeId)
    {
    }

    bool PaintTilesCommand::paint(int x, int y, std::int64_t tile)
    {
        const EditorComponent* component = findTilemap(*document_, entityId_);
        if (component == nullptr) { return false; }

        const TilemapGrid grid =
            readTilemapGrid(*component, registry_->find(BuiltinComponentIds::kTilemap));
        if (!grid.contains(x, y)) { return false; }

        // The value as it stands *now*, which is either the document's or one this command has
        // already recorded for the same cell during this stroke.
        std::int64_t current = grid.at(x, y);
        for (const Cell& recorded : cells_)
        {
            if (recorded.x == x && recorded.y == y) { current = recorded.newTile; }
        }
        if (current == tile) { return false; }

        for (Cell& recorded : cells_)
        {
            if (recorded.x != x || recorded.y != y) { continue; }

            // Already in this stroke: keep the value it had when the stroke started, so undo goes
            // back to before the stroke rather than to the middle of it.
            recorded.newTile = tile;
            return true;
        }

        cells_.push_back(Cell{x, y, grid.at(x, y), tile});
        return true;
    }

    void PaintTilesCommand::apply(bool useNewValue)
    {
        EditorComponent* component = findTilemap(*document_, entityId_);
        if (component == nullptr) { return; }

        // One read-modify-write for the whole stroke. Doing it per cell would rebuild the list once
        // per cell, which on a large map is the difference between a drag and a stall.
        TilemapGrid grid = readTilemapGrid(*component, registry_->find(BuiltinComponentIds::kTilemap));
        for (const Cell& cell : cells_)
        {
            grid.set(cell.x, cell.y, useNewValue ? cell.newTile : cell.oldTile);
        }
        component->setProperty(TilemapKeys::kTiles, grid.toPropertyValue());
    }

    void PaintTilesCommand::execute() { apply(true); }

    void PaintTilesCommand::undo() { apply(false); }

    std::string PaintTilesCommand::getDescription() const
    {
        return cells_.size() == 1 ? "Paint 1 tile"
                                  : "Paint " + std::to_string(cells_.size()) + " tiles";
    }

    std::string PaintTilesCommand::getMergeKey() const
    {
        // The stroke is part of the key. Without it two separate drags on the same tilemap would
        // merge into one undo entry, and the user would press Ctrl+Z once and lose both.
        return "tiles:" + entityId_.toString() + ":" + std::to_string(strokeId_);
    }

    bool PaintTilesCommand::mergeWith(const EditorCommand& newer)
    {
        const auto* other = dynamic_cast<const PaintTilesCommand*>(&newer);
        if (other == nullptr) { return false; }

        for (const Cell& cell : other->cells_)
        {
            const auto found = std::find_if(cells_.begin(), cells_.end(),
                                            [&cell](const Cell& recorded)
                                            { return recorded.x == cell.x && recorded.y == cell.y; });
            if (found != cells_.end())
            {
                // This stroke already touched that cell. Keep the value it had before the stroke
                // began -- that is what makes one Ctrl+Z undo the whole drag.
                found->newTile = cell.newTile;
                continue;
            }
            cells_.push_back(cell);
        }
        return true;
    }
}
