// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/UiCore/StudioTreeView.hpp
 * @brief A scrolling, selectable tree of rows, knowing nothing about what they are.
 *
 * `plan.md` STUDIO-03034.
 *
 * The second reusable thing a ported panel needed, after scrolling. Deliberately a *view* over rows
 * a caller flattens rather than a walker over somebody's data structure: the outliner shows a scene
 * graph, the content browser will show a directory, and a widget that knew about either would have
 * to learn about both. It takes a flat list with a depth per row, which is what a tree looks like
 * once it has been drawn.
 *
 * ### The caller flattens, the widget owns nothing but geometry
 *
 * Expansion is the caller's state, passed in as a @ref StudioTreeState. That is not ceremony: the
 * caller has to consult it anyway to decide which rows to flatten, and a widget that owned it would
 * mean asking the widget a question before it has been called. The widget reports what the user
 * clicked; the caller applies it.
 *
 * ### Rows are virtualised
 *
 * A scene with fifty thousand entities must cost what a scene with five costs. Only the rows on
 * screen are measured, laid out or drawn — the clip would stop the pixels either way, and only
 * culling stops the work.
 */

#pragma once

#include "CNA/Studio/UiCore/StudioIcons.hpp"
#include "CNA/Studio/UiCore/StudioFrame.hpp"
#include "CNA/Studio/UiCore/StudioWidgets.hpp"
#include "CNA/Studio/UiCore/UiRect.hpp"

#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace CNA::Studio
{
    /** @brief One visible row of a tree. */
    struct StudioTreeRow
    {
        /**
         * @brief Stable identity, opaque to the widget.
         *
         * Used for the row's widget id and for the expansion state, so a tree keeps its shape when
         * the rows around it change. A scene uses the entity's UUID; a file tree would use a path.
         */
        std::string id;

        /** @brief What the row says. */
        std::string label;

        /** @brief Something to the right of the label, drawn dimmed: a type, a count, a size. */
        std::string detail;

        /**
         * @brief The colour @ref detail is drawn in.
         *
         * Secondary text by default, because a detail column is usually a type or a count and
         * colouring those would be decoration. A report whose rows differ in *severity* is the
         * case it exists for: "error" and "warning" have to be told apart at a glance, and a list
         * that says which in grey words is a list the eye has to read line by line.
         */
        StudioColorRole detailRole = StudioColorRole::TextSecondary;

        /**
         * @brief What this row *is*, drawn before the label.
         *
         * `STUDIO-35030`. A list of names in one weight is a list the eye has to read line by
         * line; a list with a camera, a light and three meshes in it is one it can scan. `None`
         * draws nothing and takes no space, so a tree with no meaningful types — a property tree,
         * a diagnostics tree — is unchanged.
         */
        StudioIcon icon = StudioIcon::None;

        /**
         * @brief The colour @ref icon is drawn in.
         *
         * Secondary by default. An icon at the label's weight competes with the label, and the
         * label is what a user is reading; the icon is there to be recognised at a glance rather
         * than read. A row that wants a coloured icon — a warning, a broken reference — says so.
         */
        StudioColorRole iconRole = StudioColorRole::TextSecondary;

        /** @brief How far in the row is indented. Zero is a root. */
        int depth = 0;

        /** @brief Whether the row can be expanded. Decides whether it gets a disclosure triangle. */
        bool hasChildren = false;

        /** @brief Whether the row is part of the current selection. */
        bool selected = false;

        /**
         * @brief False to draw the row dimmed *and* make it unclickable.
         *
         * For a row there is genuinely nothing to do with. Rarer than it looks: see @ref muted.
         */
        bool enabled = true;

        /**
         * @brief Draw the row dimmed while leaving it fully interactive.
         *
         * The distinction matters more than it sounds. An asset whose source file has gone should
         * read as wrong at a glance *and* still be selectable — it is the row a user most needs to
         * click, because clicking it is how they find out what references it. Conflating "looks
         * wrong" with "cannot be touched" makes exactly the rows that need attention the ones
         * nothing can reach.
         */
        bool muted = false;

        /**
         * @brief Payload type this row can be dragged as, e.g. `"asset"`. Empty means it cannot.
         *
         * Declared on the row rather than wired up by the caller, for the same reason the label is:
         * a tree widget that had to be told separately which rows are draggable would be one more
         * list to keep in step with the rows themselves.
         */
        std::string dragType;

        /** @brief What a drag of this row carries. Defaults to @ref id when empty. */
        std::string dragValue;

        /**
         * @brief Payload types this row accepts a drop of. Empty accepts none.
         *
         * A *list*, because a row can mean more than one thing to a drag: an outliner row takes an
         * entity (to reparent it) and an asset (to put one in the scene under it), and a tree that
         * allowed one type per row would make the second of those a different widget.
         *
         * Offered in order and the first match wins, so a row that accepted two types a payload
         * could satisfy has a defined answer rather than whichever the loop reached.
         */
        std::vector<std::string> dropTypes;

        /**
         * @brief A toggle at the right-hand end of the row, or `None` for no toggle.
         *
         * `STUDIO-35060`. An outliner where hiding an entity means selecting it, finding the
         * Details panel and unticking a box is one where nobody hides anything — and hiding things
         * is how a large scene is worked on at all. The affordance has to be *on the row*.
         *
         * Drawn only while the row is hovered or the toggle is off, which is what every outliner
         * that has one does: a column of forty identical eyes is a column of noise, and the rows
         * that matter are the ones that are *not* in the default state.
         */
        StudioIcon toggleIcon = StudioIcon::None;

        /** @brief What to draw instead while @ref toggleOn is false. `None` keeps @ref toggleIcon. */
        StudioIcon toggleOffIcon = StudioIcon::None;

        /** @brief The toggle's state. */
        bool toggleOn = true;

        /** @brief Hover help for the toggle. */
        std::string toggleTooltip;
    };

    /**
     * @brief Which rows of a tree are open.
     *
     * A set of ids rather than a flag per row, so the answer survives the rows being rebuilt from
     * scratch every frame — which is what an immediate-mode caller does.
     */
    class StudioTreeState
    {
    public:
        /**
         * @brief Whether @p id is open.
         *
         * Defaults to open. A tree that starts entirely collapsed shows the user one line and
         * makes them work to discover that their scene has anything in it.
         *
         * @param id Row id.
         * @return True when its children should be flattened.
         */
        [[nodiscard]] bool isExpanded(std::string_view id) const
        {
            return collapsed_.find(std::string{id}) == collapsed_.end();
        }

        /**
         * @brief Opens or closes @p id.
         * @param id Row id.
         * @param expanded Whether it should be open.
         */
        void setExpanded(std::string_view id, bool expanded)
        {
            if (expanded) { collapsed_.erase(std::string{id}); }
            else { collapsed_.insert(std::string{id}); }
        }

        /** @brief Opens everything. */
        void expandAll() { collapsed_.clear(); }

        /** @brief How many rows are closed. */
        [[nodiscard]] std::size_t collapsedCount() const { return collapsed_.size(); }

        /**
         * @brief Starts editing @p id's label in place.
         *
         * Renaming where the name *is* — rather than in a dialog, or by going to another panel —
         * because the thing being renamed is the thing on the screen, and a dialog makes the user
         * check afterwards that they edited the row they meant.
         *
         * @param id Row to rename.
         * @param currentLabel What the field starts with, so an edit is a correction rather than
         *        retyping the name from nothing.
         */
        void beginRename(std::string_view id, std::string_view currentLabel)
        {
            renaming_ = std::string{id};
            renameText_ = std::string{currentLabel};
            renameStarting_ = true;
        }

        /** @brief Stops editing, keeping whatever the label was. */
        void cancelRename()
        {
            renaming_.clear();
            renameText_.clear();
            renameStarting_ = false;
        }

        /** @brief The row being renamed, or empty. */
        [[nodiscard]] const std::string& renaming() const { return renaming_; }

        /** @brief The text in the rename field. Written by the widget while editing. */
        [[nodiscard]] std::string& renameText() { return renameText_; }

        /** @brief Whether the field still needs focusing. Cleared by the widget once it has it. */
        [[nodiscard]] bool renameStarting() const { return renameStarting_; }

        /** @brief Records that the rename field now has focus. */
        void clearRenameStarting() { renameStarting_ = false; }

    private:
        // Collapsed rather than expanded, so the default is open and an unknown id needs no entry.
        std::set<std::string> collapsed_;

        std::string renaming_;
        std::string renameText_;
        bool renameStarting_ = false;
    };

    /** @brief What the user did to a tree this frame. */
    struct StudioTreeResult
    {
        /** @brief Index of the row whose disclosure triangle was clicked. Input pass only. */
        std::optional<std::size_t> toggled;

        /** @brief Index of the row that was clicked. Input pass only. */
        std::optional<std::size_t> clicked;

        /**
         * @brief Index of the row the secondary button was clicked on. Input pass only.
         *
         * Reported separately from @ref clicked and *without* changing the selection, because a
         * right-click asks a question about the row under the pointer: a tree that selected it
         * first would make "delete" act on the row the user was about to be shown a menu for,
         * which is right, and lose the multi-row selection they had, which is not.
         */
        std::optional<std::size_t> rightClicked;

        /** @brief Whether the click asked to add to the selection rather than replace it. */
        bool additive = false;

        /** @brief How many rows were actually drawn. */
        std::size_t rowsDrawn = 0;

        /** @brief Index of the row whose trailing toggle was clicked. Input pass only. */
        std::optional<std::size_t> toggledRowAction;

        /** @brief Index of the row a payload was dropped on. Input pass only. */
        std::optional<std::size_t> dropped;

        /** @brief What was dropped, on the frame it was. */
        std::string droppedValue;

        /** @brief Which of the row's accepted types the drop carried. */
        std::string droppedType;

        /** @brief Index of the row a drag started from, on the frame it started. Input pass only. */
        std::optional<std::size_t> dragStarted;

        /**
         * @brief Index of the row whose rename was committed, on the frame it was. Input pass only.
         */
        std::optional<std::size_t> renamed;

        /** @brief The new name, on the frame the rename committed. */
        std::string renamedTo;
    };

    /**
     * @brief Draws a flattened tree, scrolling and selectable.
     *
     * @param frame The frame.
     * @param bounds Area the tree occupies.
     * @param rows The visible rows, in display order.
     * @param state Expansion state; updated when a disclosure triangle is clicked.
     * @param emptyMessage Shown when @p rows is empty. An empty tree that explains itself is the
     *        difference between "nothing here" and "something is broken".
     * @return What the user did.
     */
    StudioTreeResult studioTreeView(StudioFrame& frame, const UiRect& bounds,
                                    const std::vector<StudioTreeRow>& rows,
                                    StudioTreeState& state,
                                    std::string_view emptyMessage = {});

    // ---------------------------------------------------------------------------------------
    // Building only the window (`plan.md` STUDIO-09016)
    // ---------------------------------------------------------------------------------------
    //
    // `studioTreeView` virtualises its *drawing* on its own: only the rows on screen are measured,
    // laid out or described. What it cannot do while it owns the scroll region is stop the caller
    // **building** the rows, and for a content browser over a hundred thousand assets that is the
    // expensive half — a hundred thousand `StudioTreeRow`s, each with three strings, constructed
    // twice a frame to show forty.
    //
    // The window can only be asked once the scroll region exists, because where the view sits is
    // what decides it. So a caller that wants to build only the window opens the region itself and
    // draws the rows into it, which is the same shape the content browser's grid already uses:
    //
    // ```
    // StudioScrollOptions options;
    // options.contentHeight = static_cast<float>(total) * studioTreeRowHeight(frame.theme());
    // const StudioScrollResult view = studioBeginScroll(frame, id, bounds, options);
    //
    // StudioTreeWindow window = studioTreeWindow(view, total, frame.theme());
    // const std::vector<StudioTreeRow> rows = buildOnly(window.firstRow, window.rowCount);
    //
    // const StudioTreeResult result = studioTreeRows(frame, view, rows, state, window);
    // studioEndScroll(frame);
    // ```
    //
    // Deliberately *not* a peek at the scroll offset before the region is opened. The offset a pass
    // resolves to depends on the wheel and on a scrollbar thumb being dragged, both of which are
    // consumed by `studioBeginScroll` on the input pass only — so a window asked beforehand would
    // answer differently in the two passes of a scrolling frame, and two passes that disagree about
    // which rows exist are two passes that disagree about widget identity.

    /**
     * @brief How tall one tree row is, for a caller sizing its own scroll region.
     *
     * The row height and the minimum hit target, whichever is larger: a row shorter than a finger
     * is a row that cannot be tapped, and the extent has to be computed from the same number the
     * layout uses or the list scrolls past its own last row.
     *
     * @param theme The theme in force.
     * @return The height of one row, in physical pixels.
     */
    [[nodiscard]] float studioTreeRowHeight(const StudioTheme& theme);

    /** @brief Which slice of a longer list a caller has built. */
    struct StudioTreeWindow
    {
        /** @brief The index of `rows.front()` in the whole list. */
        std::size_t firstRow = 0;

        /** @brief How many rows the slice holds, or should hold. */
        std::size_t rowCount = 0;

        /** @brief How long the whole list is. Zero means the rows are all of it. */
        std::size_t totalRows = 0;

        /** @brief Whether this is a slice rather than the whole list. */
        [[nodiscard]] bool isWindowed() const { return totalRows > 0; }
    };

    /**
     * @brief Which rows of a list of @p totalRows are worth building.
     *
     * @param view The open scroll region the rows will be drawn into.
     * @param totalRows How long the whole list is.
     * @param theme The theme in force, for the row height.
     * @return The window, ready to hand back to @ref studioTreeRows.
     */
    [[nodiscard]] StudioTreeWindow studioTreeWindow(const StudioScrollResult& view,
                                                    std::size_t totalRows,
                                                    const StudioTheme& theme);

    /**
     * @brief Draws tree rows into a scroll region the caller has already opened.
     *
     * Indices reported in the result — `clicked`, `dropped`, `renamed` and the rest — are into
     * @p rows, not into the whole list, so a caller that holds only the slice can index it
     * directly. Adding @ref StudioTreeWindow::firstRow gives the position in the whole.
     *
     * @param frame The frame.
     * @param view The scroll region, from @ref studioBeginScroll.
     * @param rows The rows of the window, in display order.
     * @param state Expansion and rename state.
     * @param window Where @p rows sits in the whole list. A default one means @p rows is all of it.
     * @return What the user did.
     */
    StudioTreeResult studioTreeRows(StudioFrame& frame, const StudioScrollResult& view,
                                    const std::vector<StudioTreeRow>& rows,
                                    StudioTreeState& state,
                                    const StudioTreeWindow& window = {});
}
