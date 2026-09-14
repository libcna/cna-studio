// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Scene/Tilemap.hpp
 * @brief The tilemap grid, and painting into it as one undo entry per stroke.
 *
 * plan.md ED-301. The grid is a **flat list of indices** plus a column count, stored in an ordinary
 * `PropertyType::List` on an ordinary component -- no new serialised structure, no new property
 * type, and a `.cnascene` that contains one is readable by anything that could already read a
 * scene. A sparse form would scale better and diff worse; it is not worth reaching for before a
 * real map is measurably slow.
 *
 * The interesting decision here is the undo boundary. A paint stroke is *one* action to the user
 * however many cells it crosses, so it has to be one entry -- but two separate strokes must not
 * collapse into one, which is exactly what a merge key of "entity + property" would do. So the key
 * carries the stroke as well, and the panel starts a new stroke on each press.
 */

#include <cstdint>
#include <string>
#include <vector>

#include "CNA/Editor/Core/ComponentDescriptor.hpp"
#include "CNA/Editor/Core/EditorCommand.hpp"
#include "CNA/Editor/Core/PropertyValue.hpp"
#include "CNA/Editor/Core/Uuid.hpp"
#include "CNA/Editor/Scene/SceneTransform.hpp"

namespace CNA::Editor
{
    class SceneDocument;
    class EditorComponent;

    /** @brief Property names on `CNA.Tilemap`. */
    namespace TilemapKeys
    {
        inline constexpr const char* kTileSet = "tileSet";
        inline constexpr const char* kColumns = "columns";
        inline constexpr const char* kRows = "rows";
        inline constexpr const char* kTileWidth = "tileWidth";
        inline constexpr const char* kTileHeight = "tileHeight";
        inline constexpr const char* kSheetColumns = "sheetColumns";
        inline constexpr const char* kTiles = "tiles";
    }

    /**
     * @brief The index meaning "nothing here".
     *
     * Negative rather than zero, so that tile 0 -- the first tile in every sheet anyone draws -- is
     * usable. A format where the first tile cannot be painted is a format people work around.
     */
    inline constexpr std::int64_t kEmptyTile = -1;

    /**
     * @brief One tilemap's grid, read out of its component.
     *
     * Row-major: index `y * columns + x`. Out-of-range reads answer `kEmptyTile` rather than
     * failing, because the paint tool asks about cells under the cursor and the cursor goes outside
     * the map constantly.
     */
    struct TilemapGrid
    {
        int columns = 0;
        int rows = 0;
        std::vector<std::int64_t> tiles;

        [[nodiscard]] bool contains(int x, int y) const
        {
            return x >= 0 && y >= 0 && x < columns && y < rows;
        }

        [[nodiscard]] std::int64_t at(int x, int y) const;

        /** @brief Sets a cell. Ignores anything outside the grid. */
        void set(int x, int y, std::int64_t tile);

        /** @brief Returns the grid as the `tiles` property value. */
        [[nodiscard]] PropertyValue toPropertyValue() const;

        [[nodiscard]] bool isEmpty() const { return columns <= 0 || rows <= 0; }
    };

    /**
     * @brief Reads the grid from @p component, sized by its own `columns` and `rows`.
     *
     * A stored list that is the wrong length is padded or truncated rather than rejected: a
     * hand-edited scene with one row too few should open and be fixable, not refuse to load.
     */
    [[nodiscard]] TilemapGrid readTilemapGrid(const EditorComponent& component,
                                              const ComponentDescriptor* descriptor);

    /**
     * @brief Returns @p grid resized to @p columns x @p rows, keeping whatever overlaps.
     *
     * Kept by *coordinate*, not by index. Resizing a flat list without remapping shifts every row
     * sideways, which turns "make the map one column wider" into "scramble the level".
     */
    [[nodiscard]] TilemapGrid resizeTilemapGrid(const TilemapGrid& grid, int columns, int rows);

    /** @brief A cell address in a tilemap's own grid. */
    struct TileCoordinate
    {
        int x = 0;
        int y = 0;
    };

    /**
     * @brief Returns the tile @p worldPoint falls on, for a tilemap at @p transform.
     *
     * Tile (0, 0) sits at the entity's origin and the grid grows right and **down**, matching how
     * `SpriteBatch` addresses the screen and how every tile editor numbers its rows.
     *
     * Rotation is ignored on purpose: a rotated tilemap is a thing you can author but not paint
     * into sensibly, and pretending otherwise would put tiles somewhere other than under the
     * cursor. Scale is honoured, because zooming a map by scaling its entity is ordinary.
     */
    [[nodiscard]] TileCoordinate worldToTile(const WorldTransform& transform,
                                             int tileWidth,
                                             int tileHeight,
                                             const EditorVector2& worldPoint);

    /**
     * @brief One paint stroke: any number of cells, one undo entry.
     *
     * Cells are recorded with the value they had, so undo restores exactly what was there -- rather
     * than a whole-grid snapshot per stroke, which on a large map would put a copy of the level in
     * the undo stack for every drag.
     */
    class PaintTilesCommand final : public EditorCommand
    {
    public:
        /** @brief One changed cell and both of its values. */
        struct Cell
        {
            int x = 0;
            int y = 0;
            std::int64_t oldTile = kEmptyTile;
            std::int64_t newTile = kEmptyTile;
        };

        /**
         * @param document The scene holding the tilemap; must outlive this command.
         * @param registry Supplies the tilemap descriptor's defaults.
         * @param entityId The entity carrying `CNA.Tilemap`.
         * @param strokeId Distinguishes this stroke from the next one. Two commands merge only
         *        when their stroke ids match, which is what keeps two drags from becoming one
         *        undo entry.
         */
        PaintTilesCommand(SceneDocument& document,
                          const ComponentRegistry& registry,
                          Uuid entityId,
                          std::uint64_t strokeId);

        /**
         * @brief Records a cell to be set to @p tile.
         *
         * Reads the current value as the undo state, so this must be called *before* the command
         * is executed. A cell already holding @p tile, or outside the grid, is ignored -- an undo
         * entry that changes nothing is one the user cannot see the effect of.
         *
         * @return True when the cell was recorded.
         */
        bool paint(int x, int y, std::int64_t tile);

        /** @brief Returns true when at least one cell was recorded. */
        [[nodiscard]] bool isValid() const { return !cells_.empty(); }

        /** @brief Returns the cells this command will change. */
        [[nodiscard]] const std::vector<Cell>& getCells() const { return cells_; }

        void execute() override;
        void undo() override;
        [[nodiscard]] std::string getDescription() const override;
        [[nodiscard]] std::string getMergeKey() const override;
        bool mergeWith(const EditorCommand& newer) override;

    private:
        /** @brief Applies every cell, taking @p useNewValue's side, in one read-modify-write. */
        void apply(bool useNewValue);

        SceneDocument* document_;
        const ComponentRegistry* registry_;
        Uuid entityId_;
        std::uint64_t strokeId_ = 0;
        std::vector<Cell> cells_;
    };
}
