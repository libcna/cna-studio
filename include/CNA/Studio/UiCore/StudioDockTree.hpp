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
         * @brief Adds a panel to a leaf's tab group.
         * @param leaf Leaf to add to.
         * @param panelId Panel id. Adding one that is already somewhere in the tree moves it.
         * @return True when it was added.
         */
        bool addPanel(StudioDockNodeId leaf, std::string panelId);

        /**
         * @brief Moves a panel into another leaf.
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
         * @param panelId Panel to remove.
         * @return True when it was there.
         */
        bool removePanel(std::string_view panelId);

        /**
         * @brief Finds the leaf holding a panel.
         * @param panelId Panel to find.
         * @return The leaf, or @ref kInvalidDockNode.
         */
        [[nodiscard]] StudioDockNodeId findPanel(std::string_view panelId) const;

        /** @brief Every panel id in the tree, in leaf order. */
        [[nodiscard]] std::vector<std::string> panels() const;

        /**
         * @brief Makes a panel the showing tab of its leaf.
         * @param panelId Panel to activate.
         * @return True when it was found.
         */
        bool activatePanel(std::string_view panelId);

        // --- Geometry --------------------------------------------------------------------------

        /**
         * @brief Resolves every node's rectangle inside @p area.
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

        std::vector<StudioDockNode> nodes_;
        std::vector<bool> live_;
        std::vector<StudioDockNodeId> free_;
        StudioDockNodeId root_ = kInvalidDockNode;
    };
} // namespace CNA::Studio
