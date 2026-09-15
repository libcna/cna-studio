// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/UiCore/StudioDockTree.hpp
 * @brief The dock node tree: how Studio's workspace is arranged, as data.
 *
 * `plan.md` STUDIO-05001, STUDIO-05002, STUDIO-05003, STUDIO-05008.
 *
 * ### What this is, and what it deliberately is not
 *
 * This is **not** a reimplementation of Dear ImGui's docking. That system infers a layout from the
 * order windows happen to be submitted in, keeps it in an opaque `.ini` blob, and has no model a
 * test can assert against — which is fine for a debug UI and wrong for a tool whose workspace is a
 * user's working environment, expected to survive a restart, an upgrade and a shared team preset.
 *
 * So the arrangement is an explicit tree and nothing else knows it:
 *
 * - A **leaf** holds a tab group: an ordered list of panel ids and which one is showing.
 * - A **split** holds exactly two children, an orientation and a fraction.
 *
 * Everything a user can do to the workspace is an operation on that tree — split a leaf, move a
 * panel, drag a splitter, close a panel — and every one of them is a function with a testable
 * postcondition rather than a side effect of drawing.
 *
 * ### Why an arena rather than pointers
 *
 * Nodes live in one vector and refer to each other by index. Three things follow, and all three
 * matter more than the pointer-chasing convenience they cost:
 *
 * 1. The tree is **trivially copyable as a value**, so "restore the layout I had before I
 *    experimented" is a copy rather than a deep clone with parent fix-ups.
 * 2. It is **serializable without inventing identity**: an index is already a name.
 * 3. A stale reference is an index that is out of range, which is *checkable*, rather than a
 *    dangling pointer, which is not.
 *
 * ### Minimum sizes are a property of the subtree, not of a widget
 *
 * A panel has a smallest useful size. A split of two panels therefore has one too, and it is the
 * sum of its children's plus the splitter between them — computed up the tree rather than guessed
 * at each level. That is what makes "drag the splitter as far as it will go" stop somewhere
 * sensible instead of producing a two-pixel outliner, and what makes a window dragged very small
 * degrade to a cramped workspace rather than to inverted rectangles.
 */

#include "CNA/Studio/Core/Json.hpp"
#include "CNA/Studio/UiCore/UiRect.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace CNA::Studio
{
    /** @brief Index of a node within a @ref StudioDockTree. */
    using StudioDockNodeId = std::uint32_t;

    /** @brief The id no node has. */
    inline constexpr StudioDockNodeId kInvalidDockNode = 0xFFFFFFFFu;

    /** @brief Which axis a split divides. */
    enum class StudioDockOrientation : std::uint8_t
    {
        /** @brief Children side by side; the splitter is vertical and drags left and right. */
        Horizontal,
        /** @brief Children stacked; the splitter is horizontal and drags up and down. */
        Vertical
    };

    /** @brief Where a new panel goes relative to an existing node. */
    enum class StudioDockSide : std::uint8_t
    {
        Left,
        Right,
        Top,
        Bottom
    };

    /** @brief Returns a stable English name for an orientation. */
    [[nodiscard]] std::string_view studioDockOrientationName(StudioDockOrientation orientation);

    /** @brief Returns a stable English name for a side. */
    [[nodiscard]] std::string_view studioDockSideName(StudioDockSide side);

    /**
     * @brief One node: either a tab group or a split of two children.
     *
     * Both shapes share a struct rather than being a variant, because the resolved geometry and the
     * parent link belong to every node and a variant would either duplicate them or wrap them.
     */
    struct StudioDockNode
    {
        /** @brief Enclosing split, or @ref kInvalidDockNode for the root. */
        StudioDockNodeId parent = kInvalidDockNode;
        /** @brief Left or top child of a split; invalid on a leaf. */
        StudioDockNodeId first = kInvalidDockNode;
        /** @brief Right or bottom child of a split; invalid on a leaf. */
        StudioDockNodeId second = kInvalidDockNode;

        /** @brief Which axis this split divides. Meaningless on a leaf. */
        StudioDockOrientation orientation = StudioDockOrientation::Horizontal;

        /**
         * @brief Share of the content extent given to @ref first, in `[0, 1]`.
         *
         * A fraction rather than a pixel width, so that moving the window to a larger display
         * rescales the arrangement instead of leaving the inspector 300px wide on a 4K screen.
         */
        float fraction = 0.5f;

        /** @brief Panel ids in tab order. Non-empty only on a leaf. */
        std::vector<std::string> panels;
        /** @brief Index into @ref panels of the tab that is showing. */
        std::size_t activePanel = 0;

        /** @brief Resolved rectangle, filled by @ref StudioDockTree::layout. */
        UiRect bounds;
        /** @brief Resolved splitter rectangle of a split node; empty on a leaf. */
        UiRect splitter;

        /** @brief Smallest width this subtree can be given, resolved by layout(). */
        float minimumWidth = 0.0f;
        /** @brief Smallest height this subtree can be given, resolved by layout(). */
        float minimumHeight = 0.0f;

        /** @brief Whether this node holds panels rather than children. */
        [[nodiscard]] bool isLeaf() const { return first == kInvalidDockNode; }
    };

    /** @brief How a leaf's geometry is divided between its tab strip and its body. */
    struct StudioDockLeafGeometry
    {
        UiRect tabStrip;
        UiRect body;
    };

    /**
     * @brief A tab group floating free of the dock tree.
     *
     * The same shape as a leaf — an ordered list of panels and which one shows — because a float
     * that could hold only one panel would make "put the inspector and the details side by side
     * over on the second monitor" impossible, and that is most of why anyone undocks anything.
     *
     * ### Why logical units rather than fractions
     *
     * The dock tree stores fractions, because a docked arrangement should rescale with the window:
     * an inspector that is a fifth of the width stays a fifth of the width on a larger display. A
     * float is the opposite. It is a palette the user placed and sized deliberately, and scaling it
     * with the window would grow a small colour picker into a quarter of a 4K screen. So its
     * geometry is what the user set, and @ref StudioDockTree::layout only *clamps* it back into
     * view — a float saved at (3000, 1800) must not be unreachable on a laptop.
     */
    struct StudioFloatingDock
    {
        /** @brief Panel ids in tab order. A float with none is removed. */
        std::vector<std::string> panels;

        /** @brief Index into @ref panels of the tab that is showing. */
        std::size_t activePanel = 0;

        /** @brief Position relative to the workspace area's top-left, in logical units. */
        float x = 0.0f;
        /** @brief Position relative to the workspace area's top-left, in logical units. */
        float y = 0.0f;
        /** @brief Width in logical units. */
        float width = 360.0f;
        /** @brief Height in logical units. */
        float height = 280.0f;

        /** @brief Resolved rectangle, filled by @ref StudioDockTree::layout. */
        UiRect bounds;
    };

    /** @brief The float no index addresses. */
    inline constexpr std::size_t kInvalidFloatingDock = static_cast<std::size_t>(-1);

    /**
     * @brief Smallest extent a floating window is ever given, in logical units.
     *
     * Smaller than a docked leaf's minimum on purpose: a float is often a narrow palette, and
     * holding it to the width an inspector needs would make half of what people float impossible.
     */
    inline constexpr float kMinimumFloatingExtent = 80.0f;

    /**
     * @brief A workspace arrangement.
     *
     * Starts as a single empty leaf. Every arrangement a user can reach is built from @ref split,
     * @ref addPanel and @ref movePanel, and every one of those keeps the tree well-formed — which
     * @ref isWellFormed checks directly rather than leaving to inspection.
     */
    class StudioDockTree
    {
    public:
        /** @brief Smallest extent a leaf is ever resolved to, in logical units. */
        static constexpr float kDefaultMinimumExtent = 120.0f;

        /** @brief Constructs a tree with one empty leaf as its root. */
        StudioDockTree();

        /** @brief The root node's id. Never invalid. */
        [[nodiscard]] StudioDockNodeId root() const { return root_; }

        /** @brief Number of nodes, live and free. */
        [[nodiscard]] std::size_t nodeCount() const { return nodes_.size(); }

        /**
         * @brief Reports whether an id addresses a live node.
         * @param id Node id.
         * @return True when the node exists and has not been recycled.
         */
        [[nodiscard]] bool isLive(StudioDockNodeId id) const;

        /**
         * @brief Returns a node.
         * @param id Node id. Must be live.
         * @return The node.
         */
        [[nodiscard]] const StudioDockNode& node(StudioDockNodeId id) const;

        /**
         * @brief Returns a node for modification.
         * @param id Node id. Must be live.
         * @return The node.
         */
        [[nodiscard]] StudioDockNode& node(StudioDockNodeId id);

        /** @brief Every live leaf, in left-to-right, top-to-bottom order. */
        [[nodiscard]] std::vector<StudioDockNodeId> leaves() const;

        /** @brief Every live split, parents before children. */
        [[nodiscard]] std::vector<StudioDockNodeId> splits() const;

        // --- Structure -------------------------------------------------------------------------

        /**
         * @brief Splits a leaf, returning the new empty leaf on @p side.
         *
         * The existing leaf keeps its panels and becomes the other child. A split of a split is
         * refused — the caller means one of its leaves and should say which.
         *
         * @param leaf Leaf to split.
         * @param side Which side the new leaf takes.
         * @param fraction Share of the split given to the **new** leaf, in `(0, 1)`.
         * @return The new leaf, or @ref kInvalidDockNode when @p leaf is not a live leaf.
         */
        StudioDockNodeId split(StudioDockNodeId leaf, StudioDockSide side, float fraction = 0.25f);

        /**
         * @brief The other child of a node's parent.
         *
         * @ref split reuses the split node's id for the split itself, so the leaf that was split
         * now lives at a new id. This is how a caller finds it: `sibling(split(leaf, side))` is
         * the content that was there before. Building the default workspace without it means
         * reaching into child links, which is the kind of code that stops compiling the first time
         * the tree gains a node kind.
         *
         * @param id Node whose sibling is wanted.
         * @return The sibling, or @ref kInvalidDockNode for the root or a dead node.
         */
        [[nodiscard]] StudioDockNodeId sibling(StudioDockNodeId id) const;

        /**
         * @brief The leaf whose resolved area contains (@p x, @p y).
         *
         * Leaves only: a split node's area is the union of its children's, so returning one would
         * be an answer no caller can use. Needs @ref layout to have run — before that every node's
         * area is empty and this correctly finds nothing.
         *
         * @param x Horizontal position in logical units.
         * @param y Vertical position.
         * @return The leaf, or @ref kInvalidDockNode.
         */
        [[nodiscard]] StudioDockNodeId leafAt(float x, float y) const;

        /**
         * @brief Adds a panel to a leaf's tab group.
         * @param leaf Leaf to add to.
         * @param panelId Panel id. Adding one that is already somewhere in the tree moves it.
         * @return True when it was added.
         */
        bool addPanel(StudioDockNodeId leaf, std::string panelId);

        /**
         * @brief Moves a panel into a leaf, from anywhere.
         *
         * Docking a floating panel is the same operation seen from the other side, so it is this
         * one rather than a second function: one drag gesture reaches both, and a caller that had
         * to ask where the panel currently is would be answering a question the tree already knows.
         *
         * @param panelId Panel to move.
         * @param destination Leaf to move it to.
         * @param index Position in the destination's tab order; past the end appends.
         * @return True when the panel was found and moved.
         */
        bool movePanel(std::string_view panelId, StudioDockNodeId destination,
                       std::size_t index = static_cast<std::size_t>(-1));

        /**
         * @brief Removes a panel, collapsing a leaf that is left empty.
         *
         * Collapsing is the part that is easy to get wrong and expensive to leave out: a leaf
         * nobody can see still takes its share of the split, so closing the last panel in a dock
         * would otherwise leave a permanent empty stripe that no gesture can remove.
         *
         * A float left holding nothing is removed outright, for the same reason: an empty window
         * with a title bar and no content is a thing a user has to close by hand for no purpose.
         *
         * @param panelId Panel to remove.
         * @return True when it was there.
         */
        bool removePanel(std::string_view panelId);

        /**
         * @brief Finds the leaf holding a panel.
         *
         * Docked leaves only. A panel that has been floated is not in the tree, so this correctly
         * finds nothing for it — ask @ref findFloatingPanel as well when the question is "is this
         * panel open anywhere".
         *
         * @param panelId Panel to find.
         * @return The leaf, or @ref kInvalidDockNode.
         */
        [[nodiscard]] StudioDockNodeId findPanel(std::string_view panelId) const;

        // --- Floating --------------------------------------------------------------------------

        /** @brief Every float, back to front: the last is the one on top. */
        [[nodiscard]] const std::vector<StudioFloatingDock>& floating() const { return floating_; }

        /**
         * @brief Returns a float for modification.
         * @param index Float index; must be in range.
         * @return The float.
         */
        [[nodiscard]] StudioFloatingDock& floatingAt(std::size_t index);

        /**
         * @brief Finds the float holding a panel.
         * @param panelId Panel to find.
         * @return Its index, or @ref kInvalidFloatingDock.
         */
        [[nodiscard]] std::size_t findFloatingPanel(std::string_view panelId) const;

        /**
         * @brief Takes a panel out of the dock tree and gives it a floating window of its own.
         *
         * Undocking is the one operation that can empty a leaf without the panel being closed, so
         * it collapses exactly as @ref removePanel does: a leaf nobody can see still takes its
         * share of the split.
         *
         * @param panelId Panel to float. May already be floating, which moves it to a new float.
         * @param x Position relative to the workspace, in logical units.
         * @param y Position relative to the workspace.
         * @param width Width in logical units; raised to the minimum when smaller.
         * @param height Height in logical units; raised to the minimum when smaller.
         * @return The new float's index, or @ref kInvalidFloatingDock when the panel was not open.
         */
        std::size_t floatPanel(std::string_view panelId, float x, float y,
                               float width = 360.0f, float height = 280.0f);

        /**
         * @brief Moves a panel into a floating window's tab group, from anywhere.
         *
         * Docked or floating, because one drag gesture reaches both and a caller that had to ask
         * where the panel currently is would be answering a question the tree already knows.
         *
         * @param panelId Panel to move.
         * @param destination Float index to move it into.
         * @param index Position in the destination's tab order; past the end appends.
         * @return True when the panel was open and was moved.
         */
        bool movePanelToFloating(std::string_view panelId, std::size_t destination,
                                 std::size_t index = static_cast<std::size_t>(-1));

        /**
         * @brief Raises a float above the others.
         *
         * Z-order is the vector order, so this is a rotation rather than a stored depth: two floats
         * can never claim the same depth, and "which one is on top" has exactly one answer.
         *
         * @param index Float to raise.
         * @return Its new index, or @ref kInvalidFloatingDock when @p index was out of range.
         */
        std::size_t raiseFloating(std::size_t index);

        /**
         * @brief The float whose resolved rectangle contains (@p x, @p y), front-most first.
         *
         * Needs @ref layout to have run. Front-most rather than any, because overlapping floats are
         * ordinary and the one the user can see is the one they mean.
         *
         * @param x Horizontal position in logical units.
         * @param y Vertical position.
         * @return The float's index, or @ref kInvalidFloatingDock.
         */
        [[nodiscard]] std::size_t floatingAt(float x, float y) const;



        /** @brief Every panel id, docked leaves in leaf order and then the floats. */
        [[nodiscard]] std::vector<std::string> panels() const;

        /**
         * @brief Makes a panel the showing tab of its leaf or float.
         *
         * Activating a floating panel also raises its float: a tab brought forward behind another
         * window has not been brought forward.
         *
         * @param panelId Panel to activate.
         * @return True when it was found.
         */
        bool activatePanel(std::string_view panelId);

        // --- Geometry --------------------------------------------------------------------------

        /**
         * @brief Resolves every node's rectangle inside @p area, and clamps every float into it.
         *
         * Minimums are computed bottom-up first, so a split knows what its children need before it
         * divides anything. When @p area cannot satisfy the tree's minimum the extents are scaled
         * down proportionally rather than clamped: a cramped workspace is usable and a workspace
         * where the first panel took everything is not.
         *
         * @param area Region the root fills.
         * @param splitterThickness Grab thickness between children, in logical units.
         * @param tabStripHeight Height of a leaf's tab strip, in logical units.
         * @param minimumExtent Smallest extent a leaf resolves to.
         */
        void layout(const UiRect& area, float splitterThickness, float tabStripHeight,
                    float minimumExtent = kDefaultMinimumExtent);

        /**
         * @brief Splits a leaf's resolved rectangle into its tab strip and its body.
         * @param leaf Leaf to divide.
         * @param tabStripHeight Height of the strip.
         * @return The two regions; both empty when @p leaf is not a live leaf.
         */
        [[nodiscard]] StudioDockLeafGeometry leafGeometry(StudioDockNodeId leaf,
                                                          float tabStripHeight) const;

        /**
         * @brief Moves a splitter by a pixel delta, honouring both children's minimums.
         *
         * Expressed in pixels rather than as a new fraction because that is what a drag produces,
         * and converting in the caller is where an off-by-one between "where the pointer is" and
         * "where the splitter went" creeps in.
         *
         * @param splitNode The split whose splitter moved.
         * @param deltaPixels Movement along the split's axis.
         * @param minimumExtent Smallest extent a leaf may be reduced to.
         * @return True when the fraction changed.
         */
        bool moveSplitter(StudioDockNodeId splitNode, float deltaPixels,
                          float minimumExtent = kDefaultMinimumExtent);

        /** @brief The total minimum width the whole tree needs. Valid after layout(). */
        [[nodiscard]] float minimumWidth() const;
        /** @brief The total minimum height the whole tree needs. Valid after layout(). */
        [[nodiscard]] float minimumHeight() const;

        // --- Integrity and persistence -----------------------------------------------------------

        /**
         * @brief Reports whether the tree is structurally sound.
         *
         * Every live node reachable from the root exactly once, every parent link consistent, every
         * split holding two live children, every leaf holding no children, and every active tab
         * index in range. Checked directly rather than assumed, because a layout read from a file
         * somebody edited by hand is not a layout this code wrote.
         *
         * @param outProblem Receives a description of the first problem found.
         * @return True when the tree is well-formed.
         */
        [[nodiscard]] bool isWellFormed(std::string* outProblem = nullptr) const;

        /** @brief The version written into serialized layouts. */
        static constexpr int kLayoutVersion = 1;

        /**
         * @brief Serializes the arrangement.
         *
         * Fractions, orientations, panel ids and active tabs — never resolved pixels, which belong
         * to the window the layout was last shown in and not to the layout.
         *
         * @return The JSON document.
         */
        [[nodiscard]] JsonValue toJson() const;

        /**
         * @brief Rebuilds an arrangement from JSON.
         *
         * Never fails into an unusable state. A document from a newer version, a malformed node, a
         * cycle or a missing child yields the default single-leaf tree and says why — a corrupt
         * layout file must never be the reason Studio will not start (STUDIO-05012).
         *
         * @param value Document to read.
         * @param outProblem Receives why the document was rejected, when it was.
         * @return The tree, or a default one.
         */
        [[nodiscard]] static StudioDockTree fromJson(const JsonValue& value,
                                                     std::string* outProblem = nullptr);

    private:
        StudioDockNodeId allocate();
        void release(StudioDockNodeId id);
        void computeMinimums(StudioDockNodeId id, float minimumExtent, float tabStripHeight);
        void resolve(StudioDockNodeId id, const UiRect& area, float splitterThickness);
        void collapse(StudioDockNodeId emptyLeaf);
        void collectLeaves(StudioDockNodeId id, std::vector<StudioDockNodeId>& out) const;
        void collectSplits(StudioDockNodeId id, std::vector<StudioDockNodeId>& out) const;

        /** @brief Removes @p index and returns whether it was there. */
        bool releaseFloating(std::size_t index);

        std::vector<StudioFloatingDock> floating_;
        std::vector<StudioDockNode> nodes_;
        std::vector<bool> live_;
        std::vector<StudioDockNodeId> free_;
        StudioDockNodeId root_ = kInvalidDockNode;
    };
} // namespace CNA::Studio
