// SPDX-License-Identifier: MS-PL
/**
 * @file StudioDockTree.cpp
 * @brief Tree structure, minimum-size propagation, geometry resolution and serialization.
 */

#include "CNA/Studio/UiCore/StudioDockTree.hpp"

#include <algorithm>
#include <cmath>
#include <set>

namespace CNA::Studio
{
    namespace
    {
        /** @brief Whether a side splits along x. */
        bool isHorizontal(StudioDockSide side)
        {
            return side == StudioDockSide::Left || side == StudioDockSide::Right;
        }

        /** @brief Whether a side puts the new leaf first in the split. */
        bool isFirst(StudioDockSide side)
        {
            return side == StudioDockSide::Left || side == StudioDockSide::Top;
        }
    } // namespace

    std::string_view studioDockOrientationName(StudioDockOrientation orientation)
    {
        return orientation == StudioDockOrientation::Horizontal ? "horizontal" : "vertical";
    }

    std::string_view studioDockSideName(StudioDockSide side)
    {
        switch (side)
        {
            case StudioDockSide::Left:   return "left";
            case StudioDockSide::Right:  return "right";
            case StudioDockSide::Top:    return "top";
            case StudioDockSide::Bottom: return "bottom";
        }
        return "";
    }

    StudioDockTree::StudioDockTree() { root_ = allocate(); }

    StudioDockNodeId StudioDockTree::allocate()
    {
        if (!free_.empty())
        {
            const StudioDockNodeId id = free_.back();
            free_.pop_back();
            nodes_[id] = StudioDockNode{};
            live_[id] = true;
            return id;
        }
        nodes_.emplace_back();
        live_.push_back(true);
        return static_cast<StudioDockNodeId>(nodes_.size() - 1);
    }

    void StudioDockTree::release(StudioDockNodeId id)
    {
        if (!isLive(id)) { return; }
        nodes_[id] = StudioDockNode{};
        live_[id] = false;
        free_.push_back(id);
    }

    bool StudioDockTree::isLive(StudioDockNodeId id) const
    {
        return id < live_.size() && live_[id];
    }

    const StudioDockNode& StudioDockTree::node(StudioDockNodeId id) const { return nodes_[id]; }

    StudioDockNode& StudioDockTree::node(StudioDockNodeId id) { return nodes_[id]; }

    void StudioDockTree::collectLeaves(StudioDockNodeId id, std::vector<StudioDockNodeId>& out) const
    {
        if (!isLive(id)) { return; }
        const StudioDockNode& n = nodes_[id];
        if (n.isLeaf()) { out.push_back(id); return; }
        collectLeaves(n.first, out);
        collectLeaves(n.second, out);
    }

    void StudioDockTree::collectSplits(StudioDockNodeId id, std::vector<StudioDockNodeId>& out) const
    {
        if (!isLive(id)) { return; }
        const StudioDockNode& n = nodes_[id];
        if (n.isLeaf()) { return; }
        out.push_back(id);
        collectSplits(n.first, out);
        collectSplits(n.second, out);
    }

    std::vector<StudioDockNodeId> StudioDockTree::leaves() const
    {
        std::vector<StudioDockNodeId> result;
        collectLeaves(root_, result);
        return result;
    }

    std::vector<StudioDockNodeId> StudioDockTree::splits() const
    {
        std::vector<StudioDockNodeId> result;
        collectSplits(root_, result);
        return result;
    }

    StudioDockNodeId StudioDockTree::split(StudioDockNodeId leaf, StudioDockSide side,
                                           float fraction)
    {
        // Splitting a split is refused rather than guessed at: the caller means one of its leaves,
        // and picking one for them would put the panel somewhere they did not ask for.
        if (!isLive(leaf) || !nodes_[leaf].isLeaf()) { return kInvalidDockNode; }

        const StudioDockNodeId existing = allocate();
        const StudioDockNodeId created = allocate();

        // The node being split *becomes* the split, so its id -- which the caller and any saved
        // layout already refer to -- keeps addressing the same region of the workspace.
        StudioDockNode& parent = nodes_[leaf];
        StudioDockNode moved = parent;

        nodes_[existing] = moved;
        nodes_[existing].parent = leaf;
        nodes_[existing].first = kInvalidDockNode;
        nodes_[existing].second = kInvalidDockNode;

        nodes_[created] = StudioDockNode{};
        nodes_[created].parent = leaf;

        parent.panels.clear();
        parent.activePanel = 0;
        parent.orientation = isHorizontal(side) ? StudioDockOrientation::Horizontal
                                                : StudioDockOrientation::Vertical;

        const float clamped = std::clamp(fraction, 0.05f, 0.95f);
        if (isFirst(side))
        {
            parent.first = created;
            parent.second = existing;
            parent.fraction = clamped;
        }
        else
        {
            parent.first = existing;
            parent.second = created;
            parent.fraction = 1.0f - clamped;
        }
        return created;
    }

    StudioDockNodeId StudioDockTree::sibling(StudioDockNodeId id) const
    {
        if (!isLive(id)) { return kInvalidDockNode; }
        const StudioDockNodeId parent = nodes_[id].parent;
        if (!isLive(parent)) { return kInvalidDockNode; }
        return nodes_[parent].first == id ? nodes_[parent].second : nodes_[parent].first;
    }

    StudioDockNodeId StudioDockTree::leafAt(float x, float y) const
    {
        for (const StudioDockNodeId leaf : leaves())
        {
            if (node(leaf).bounds.contains(x, y)) { return leaf; }
        }
        return kInvalidDockNode;
    }

    bool StudioDockTree::addPanel(StudioDockNodeId leaf, std::string panelId)
    {
        if (!isLive(leaf) || !nodes_[leaf].isLeaf() || panelId.empty()) { return false; }

        // A panel lives in exactly one place. Adding one that is already docked moves it, rather
        // than producing two tabs that both claim to be the Details panel.
        removePanel(panelId);
        if (!isLive(leaf) || !nodes_[leaf].isLeaf()) { return false; }

        nodes_[leaf].panels.push_back(std::move(panelId));
        nodes_[leaf].activePanel = nodes_[leaf].panels.size() - 1;
        return true;
    }

    bool StudioDockTree::movePanel(std::string_view panelId, StudioDockNodeId destination,
                                   std::size_t index)
    {
        const bool open = findPanel(panelId) != kInvalidDockNode
                       || findFloatingPanel(panelId) != kInvalidFloatingDock;
        if (!open || !isLive(destination) || !nodes_[destination].isLeaf()) { return false; }

        std::string moved{panelId};
        removePanel(panelId);
        if (!isLive(destination) || !nodes_[destination].isLeaf()) { return false; }

        std::vector<std::string>& panels = nodes_[destination].panels;
        const std::size_t position = std::min(index, panels.size());
        panels.insert(panels.begin() + static_cast<std::ptrdiff_t>(position), std::move(moved));
        nodes_[destination].activePanel = position;
        return true;
    }

    bool StudioDockTree::removePanel(std::string_view panelId)
    {
        const std::size_t floatIndex = findFloatingPanel(panelId);
        if (floatIndex != kInvalidFloatingDock)
        {
            StudioFloatingDock& window = floating_[floatIndex];
            const auto found = std::find(window.panels.begin(), window.panels.end(), panelId);
            const auto position = static_cast<std::size_t>(found - window.panels.begin());
            window.panels.erase(found);

            // A float left holding nothing goes, rather than staying as a title bar over an empty
            // rectangle that a user then has to close by hand for no purpose.
            if (window.panels.empty()) { return releaseFloating(floatIndex); }

            if (window.activePanel >= window.panels.size())
            {
                window.activePanel = window.panels.size() - 1;
            }
            else if (window.activePanel > position)
            {
                --window.activePanel;
            }
            return true;
        }

        const StudioDockNodeId leaf = findPanel(panelId);
        if (leaf == kInvalidDockNode) { return false; }

        std::vector<std::string>& panels = nodes_[leaf].panels;
        const auto found = std::find(panels.begin(), panels.end(), panelId);
        const auto position = static_cast<std::size_t>(found - panels.begin());
        panels.erase(found);

        if (panels.empty()) { collapse(leaf); return true; }
        if (nodes_[leaf].activePanel >= panels.size())
        {
            nodes_[leaf].activePanel = panels.size() - 1;
        }
        else if (nodes_[leaf].activePanel > position)
        {
            // The showing tab moved left by one. Leaving the index alone would silently switch the
            // user to the panel that took its place.
            --nodes_[leaf].activePanel;
        }
        return true;
    }

    void StudioDockTree::collapse(StudioDockNodeId emptyLeaf)
    {
        if (!isLive(emptyLeaf) || !nodes_[emptyLeaf].isLeaf()) { return; }

        const StudioDockNodeId parent = nodes_[emptyLeaf].parent;
        if (parent == kInvalidDockNode)
        {
            // The root, and now empty. Kept rather than removed: a tree with no root has nothing
            // for the next panel to dock into.
            return;
        }

        const StudioDockNode& split = nodes_[parent];
        const StudioDockNodeId sibling = split.first == emptyLeaf ? split.second : split.first;
        if (!isLive(sibling)) { return; }

        // The sibling takes the split's place. Its own id disappears and the split's id survives,
        // which keeps the parent's child link valid without a second fix-up pass.
        StudioDockNode promoted = nodes_[sibling];
        const StudioDockNodeId keptParent = split.parent;

        nodes_[parent] = promoted;
        nodes_[parent].parent = keptParent;
        if (!nodes_[parent].isLeaf())
        {
            nodes_[nodes_[parent].first].parent = parent;
            nodes_[nodes_[parent].second].parent = parent;
        }

        release(emptyLeaf);
        release(sibling);
    }

    StudioDockNodeId StudioDockTree::findPanel(std::string_view panelId) const
    {
        for (const StudioDockNodeId leaf : leaves())
        {
            const std::vector<std::string>& panels = nodes_[leaf].panels;
            if (std::find(panels.begin(), panels.end(), panelId) != panels.end()) { return leaf; }
        }
        return kInvalidDockNode;
    }

    std::vector<std::string> StudioDockTree::panels() const
    {
        std::vector<std::string> result;
        for (const StudioDockNodeId leaf : leaves())
        {
            for (const std::string& panel : nodes_[leaf].panels) { result.push_back(panel); }
        }
        // After the docked ones, because a caller enumerating panels is normally walking the
        // workspace left to right and a float has no place in that order.
        for (const StudioFloatingDock& window : floating_)
        {
            for (const std::string& panel : window.panels) { result.push_back(panel); }
        }
        return result;
    }

    bool StudioDockTree::activatePanel(std::string_view panelId)
    {
        const std::size_t floatIndex = findFloatingPanel(panelId);
        if (floatIndex != kInvalidFloatingDock)
        {
            StudioFloatingDock& window = floating_[floatIndex];
            const auto found = std::find(window.panels.begin(), window.panels.end(), panelId);
            window.activePanel = static_cast<std::size_t>(found - window.panels.begin());

            // And raised, because a tab brought to the front of a window that is itself behind
            // another window has not been brought to the front of anything.
            (void)raiseFloating(floatIndex);
            return true;
        }

        const StudioDockNodeId leaf = findPanel(panelId);
        if (leaf == kInvalidDockNode) { return false; }

        const std::vector<std::string>& panels = nodes_[leaf].panels;
        const auto found = std::find(panels.begin(), panels.end(), panelId);
        nodes_[leaf].activePanel = static_cast<std::size_t>(found - panels.begin());
        return true;
    }

    // --- Floating -----------------------------------------------------------------------------

    StudioFloatingDock& StudioDockTree::floatingAt(std::size_t index) { return floating_[index]; }

    std::size_t StudioDockTree::findFloatingPanel(std::string_view panelId) const
    {
        for (std::size_t i = 0; i < floating_.size(); ++i)
        {
            const std::vector<std::string>& panels = floating_[i].panels;
            if (std::find(panels.begin(), panels.end(), panelId) != panels.end()) { return i; }
        }
        return kInvalidFloatingDock;
    }

    bool StudioDockTree::releaseFloating(std::size_t index)
    {
        if (index >= floating_.size()) { return false; }
        floating_.erase(floating_.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }

    std::size_t StudioDockTree::floatPanel(std::string_view panelId, float x, float y,
                                           float width, float height)
    {
        const bool open = findPanel(panelId) != kInvalidDockNode
                       || findFloatingPanel(panelId) != kInvalidFloatingDock;
        if (!open || panelId.empty()) { return kInvalidFloatingDock; }

        std::string moved{panelId};
        removePanel(panelId);

        StudioFloatingDock window;
        window.panels.push_back(std::move(moved));
        window.x = x;
        window.y = y;
        // Raised rather than refused: a float dragged out with a two-pixel gesture should be a
        // small window, not a failed undock the user has to work out how to repeat.
        window.width = std::max(width, kMinimumFloatingExtent);
        window.height = std::max(height, kMinimumFloatingExtent);

        floating_.push_back(std::move(window));
        return floating_.size() - 1;
    }

    bool StudioDockTree::movePanelToFloating(std::string_view panelId, std::size_t destination,
                                             std::size_t index)
    {
        if (destination >= floating_.size()) { return false; }

        const std::size_t source = findFloatingPanel(panelId);
        const bool wasDocked = source == kInvalidFloatingDock;
        if (wasDocked && findPanel(panelId) == kInvalidDockNode) { return false; }

        // Whether the removal will take the source float with it, worked out *before* it happens.
        // Indices go stale across the erase, so the only thing that survives is an answer computed
        // while the vector still holds what it is about to lose.
        const bool sourceVanishes = !wasDocked && floating_[source].panels.size() == 1;
        if (source == destination && sourceVanishes) { return true; }

        std::string moved{panelId};
        removePanel(panelId);

        const std::size_t target = (sourceVanishes && source < destination) ? destination - 1
                                                                            : destination;
        if (target >= floating_.size()) { return false; }

        StudioFloatingDock& window = floating_[target];
        const std::size_t position = std::min(index, window.panels.size());
        window.panels.insert(window.panels.begin() + static_cast<std::ptrdiff_t>(position),
                             std::move(moved));
        window.activePanel = position;
        return true;
    }

    std::size_t StudioDockTree::raiseFloating(std::size_t index)
    {
        if (index >= floating_.size()) { return kInvalidFloatingDock; }
        if (index + 1 == floating_.size()) { return index; }

        StudioFloatingDock raised = std::move(floating_[index]);
        floating_.erase(floating_.begin() + static_cast<std::ptrdiff_t>(index));
        floating_.push_back(std::move(raised));
        return floating_.size() - 1;
    }

    std::size_t StudioDockTree::floatingAt(float x, float y) const
    {
        // Back to front, so the one the user can see is the one they get.
        for (std::size_t i = floating_.size(); i-- > 0;)
        {
            if (floating_[i].bounds.contains(x, y)) { return i; }
        }
        return kInvalidFloatingDock;
    }

    void StudioDockTree::computeMinimums(StudioDockNodeId id, float minimumExtent,
                                         float tabStripHeight)
    {
        if (!isLive(id)) { return; }
        StudioDockNode& n = nodes_[id];

        if (n.isLeaf())
        {
            n.minimumWidth = minimumExtent;
            // A leaf must fit its tab strip plus something to show beneath it. Without the strip in
            // the minimum, dragging a horizontal splitter to its limit leaves a dock that is all
            // tabs and no panel.
            n.minimumHeight = tabStripHeight + minimumExtent;
            return;
        }

        computeMinimums(n.first, minimumExtent, tabStripHeight);
        computeMinimums(n.second, minimumExtent, tabStripHeight);

        const StudioDockNode& a = nodes_[n.first];
        const StudioDockNode& b = nodes_[n.second];

        if (n.orientation == StudioDockOrientation::Horizontal)
        {
            n.minimumWidth = a.minimumWidth + b.minimumWidth;
            n.minimumHeight = std::max(a.minimumHeight, b.minimumHeight);
        }
        else
        {
            n.minimumWidth = std::max(a.minimumWidth, b.minimumWidth);
            n.minimumHeight = a.minimumHeight + b.minimumHeight;
        }
    }

    void StudioDockTree::resolve(StudioDockNodeId id, const UiRect& area, float splitterThickness)
    {
        if (!isLive(id)) { return; }
        StudioDockNode& n = nodes_[id];
        n.bounds = area;
        n.splitter = UiRect{};

        if (n.isLeaf()) { return; }

        const bool horizontal = n.orientation == StudioDockOrientation::Horizontal;
        const float total = horizontal ? area.width : area.height;
        const float content = std::max(0.0f, total - splitterThickness);

        const float firstMinimum = horizontal ? nodes_[n.first].minimumWidth
                                              : nodes_[n.first].minimumHeight;
        const float secondMinimum = horizontal ? nodes_[n.second].minimumWidth
                                               : nodes_[n.second].minimumHeight;

        float firstExtent = content * std::clamp(n.fraction, 0.0f, 1.0f);

        if (firstMinimum + secondMinimum <= content)
        {
            firstExtent = std::clamp(firstExtent, firstMinimum, content - secondMinimum);
        }
        else if (firstMinimum + secondMinimum > 0.0f)
        {
            // Not enough room for both. Scaling both down in proportion to what they asked for
            // keeps a cramped workspace usable; clamping the first to its minimum would give the
            // second nothing at all, which reads as a panel that has vanished.
            firstExtent = content * (firstMinimum / (firstMinimum + secondMinimum));
        }
        firstExtent = std::clamp(firstExtent, 0.0f, content);

        // Snapped to a whole pixel. A fraction of the dock area is almost never an integer, and a
        // panel edge at x = 123.4 puts a partially covered column of pixels between two panels --
        // a seam that is faint at 100% and obvious at 150%, where it lands differently on every
        // splitter. Snapping the split, rather than each panel afterwards, is what keeps the two
        // children exactly adjacent: rounding them independently can leave a one-pixel gap or
        // overlap between them.
        firstExtent = std::round(firstExtent);

        UiRect remaining = area;
        if (horizontal)
        {
            const UiRect firstArea = remaining.splitLeft(firstExtent);
            n.splitter = remaining.splitLeft(splitterThickness);
            resolve(n.first, firstArea, splitterThickness);
            resolve(n.second, remaining, splitterThickness);
        }
        else
        {
            const UiRect firstArea = remaining.splitTop(firstExtent);
            n.splitter = remaining.splitTop(splitterThickness);
            resolve(n.first, firstArea, splitterThickness);
            resolve(n.second, remaining, splitterThickness);
        }
    }

    void StudioDockTree::layout(const UiRect& area, float splitterThickness, float tabStripHeight,
                                float minimumExtent)
    {
        computeMinimums(root_, std::max(0.0f, minimumExtent), std::max(0.0f, tabStripHeight));
        resolve(root_, area, std::max(0.0f, splitterThickness));

        // Floats are placed by the user and kept that way -- but *clamped* into the workspace, so
        // a window saved at (3000, 1800) on a large display is still reachable on a laptop. The
        // clamp keeps the whole window in view where it fits, and where it does not it keeps the
        // top-left corner in: a title bar that cannot be grabbed is a window that cannot be moved.
        //
        // Clamped into `bounds` and *not* back into the window's own fields, which is the whole
        // difference between a resize and an edit. Writing the clamp back -- which this did -- made
        // a window briefly dragged small carry every float into the corner and leave them there
        // when it grew again. The user did not move those palettes.
        const float strip = std::max(0.0f, tabStripHeight);
        for (StudioFloatingDock& window : floating_)
        {
            const float width = std::clamp(window.width, kMinimumFloatingExtent,
                                           std::max(kMinimumFloatingExtent, area.width));
            const float height = std::clamp(window.height, strip + kMinimumFloatingExtent,
                                            std::max(strip + kMinimumFloatingExtent, area.height));

            const float x = std::clamp(window.x, 0.0f, std::max(0.0f, area.width - width));
            const float y = std::clamp(window.y, 0.0f, std::max(0.0f, area.height - height));

            window.bounds = UiRect{std::round(area.left() + x), std::round(area.top() + y),
                                   std::round(width), std::round(height)};
        }
    }

    StudioDockLeafGeometry StudioDockTree::leafGeometry(StudioDockNodeId leaf,
                                                        float tabStripHeight) const
    {
        StudioDockLeafGeometry geometry;
        if (!isLive(leaf) || !nodes_[leaf].isLeaf()) { return geometry; }

        UiRect remaining = nodes_[leaf].bounds;
        geometry.tabStrip = remaining.splitTop(tabStripHeight);
        geometry.body = remaining;
        return geometry;
    }

    bool StudioDockTree::moveSplitter(StudioDockNodeId splitNode, float deltaPixels,
                                      float minimumExtent)
    {
        if (!isLive(splitNode) || nodes_[splitNode].isLeaf() || deltaPixels == 0.0f)
        {
            return false;
        }

        StudioDockNode& n = nodes_[splitNode];
        const bool horizontal = n.orientation == StudioDockOrientation::Horizontal;
        const float total = horizontal ? n.bounds.width : n.bounds.height;
        const float splitterThickness = horizontal ? n.splitter.width : n.splitter.height;
        const float content = total - splitterThickness;
        if (content <= 0.0f) { return false; }

        const float firstMinimum = horizontal ? nodes_[n.first].minimumWidth
                                              : nodes_[n.first].minimumHeight;
        const float secondMinimum = horizontal ? nodes_[n.second].minimumWidth
                                               : nodes_[n.second].minimumHeight;
        if (firstMinimum + secondMinimum > content) { return false; }

        const float current = content * std::clamp(n.fraction, 0.0f, 1.0f);
        const float wanted = std::clamp(current + deltaPixels, std::max(firstMinimum, minimumExtent),
                                        content - std::max(secondMinimum, minimumExtent));
        const float updated = wanted / content;
        if (updated == n.fraction) { return false; }

        n.fraction = updated;
        return true;
    }

    float StudioDockTree::minimumWidth() const
    {
        return isLive(root_) ? nodes_[root_].minimumWidth : 0.0f;
    }

    float StudioDockTree::minimumHeight() const
    {
        return isLive(root_) ? nodes_[root_].minimumHeight : 0.0f;
    }

    bool StudioDockTree::isWellFormed(std::string* outProblem) const
    {
        const auto fail = [outProblem](std::string message) {
            if (outProblem != nullptr) { *outProblem = std::move(message); }
            return false;
        };

        if (!isLive(root_)) { return fail("the root is not a live node"); }
        if (nodes_[root_].parent != kInvalidDockNode) { return fail("the root has a parent"); }

        std::set<StudioDockNodeId> seen;
        std::vector<StudioDockNodeId> stack{root_};
        std::set<std::string> panelIds;

        while (!stack.empty())
        {
            const StudioDockNodeId id = stack.back();
            stack.pop_back();

            if (!isLive(id)) { return fail("a child link addresses a dead node"); }
            if (!seen.insert(id).second) { return fail("a node is reachable more than once"); }

            const StudioDockNode& n = nodes_[id];
            if (n.isLeaf())
            {
                if (n.second != kInvalidDockNode) { return fail("a leaf has a second child"); }
                if (!n.panels.empty() && n.activePanel >= n.panels.size())
                {
                    return fail("a leaf's active tab is out of range");
                }
                for (const std::string& panel : n.panels)
                {
                    if (!panelIds.insert(panel).second)
                    {
                        return fail("panel '" + panel + "' is docked in two places");
                    }
                }
                continue;
            }

            if (!isLive(n.first) || !isLive(n.second))
            {
                return fail("a split is missing a child");
            }
            if (nodes_[n.first].parent != id || nodes_[n.second].parent != id)
            {
                return fail("a child does not point back at its split");
            }
            stack.push_back(n.first);
            stack.push_back(n.second);
        }

        // Floats are part of the workspace, so the same rule holds across both: a panel docked in
        // one place and floating in another is two tabs that both claim to be the Details panel,
        // and a layout file somebody edited by hand is exactly where that arrives from.
        for (const StudioFloatingDock& window : floating_)
        {
            if (window.panels.empty()) { return fail("a floating window holds no panel"); }
            if (window.activePanel >= window.panels.size())
            {
                return fail("a floating window's active tab is out of range");
            }
            for (const std::string& panel : window.panels)
            {
                if (!panelIds.insert(panel).second)
                {
                    return fail("panel '" + panel + "' is open in two places");
                }
            }
        }
        return true;
    }

    JsonValue StudioDockTree::toJson() const
    {
        // Written as a nested document rather than as a flat node table with indices. Indices are
        // the right representation in memory and the wrong one on disk: a hand-edited file with a
        // mistyped index is a cycle, while a hand-edited nested document with a mistake is a
        // subtree in the wrong place -- recoverable, reviewable, and obvious in a diff.
        const auto writeNode = [this](const auto& self, StudioDockNodeId id) -> JsonValue {
            JsonValue value = JsonValue::makeObject();
            if (!isLive(id)) { return value; }
            const StudioDockNode& n = nodes_[id];

            if (n.isLeaf())
            {
                value.set("kind", std::string{"leaf"});
                JsonValue panels = JsonValue::makeArray();
                for (const std::string& panel : n.panels) { panels.append(JsonValue{panel}); }
                value.set("panels", std::move(panels));
                value.set("active", static_cast<int>(n.activePanel));
                return value;
            }

            value.set("kind", std::string{"split"});
            value.set("orientation", std::string{studioDockOrientationName(n.orientation)});
            value.set("fraction", static_cast<double>(n.fraction));
            value.set("first", self(self, n.first));
            value.set("second", self(self, n.second));
            return value;
        };

        JsonValue document = JsonValue::makeObject();
        document.set("version", kLayoutVersion);
        document.set("root", writeNode(writeNode, root_));

        // Written only when there are any, so an ordinary layout file looks exactly as it did
        // before floats existed -- which is what makes a diff of one legible.
        if (!floating_.empty())
        {
            JsonValue windows = JsonValue::makeArray();
            for (const StudioFloatingDock& window : floating_)
            {
                JsonValue entry = JsonValue::makeObject();
                JsonValue panels = JsonValue::makeArray();
                for (const std::string& panel : window.panels) { panels.append(JsonValue{panel}); }
                entry.set("panels", std::move(panels));
                entry.set("active", static_cast<int>(window.activePanel));
                // Geometry, unlike a split's fraction, *is* the user's answer rather than a
                // proportion of a window they no longer have -- so it is written as they set it.
                entry.set("x", static_cast<double>(window.x));
                entry.set("y", static_cast<double>(window.y));
                entry.set("width", static_cast<double>(window.width));
                entry.set("height", static_cast<double>(window.height));
                windows.append(std::move(entry));
            }
            document.set("floating", std::move(windows));
        }
        return document;
    }

    StudioDockTree StudioDockTree::fromJson(const JsonValue& value, std::string* outProblem)
    {
        const auto reject = [outProblem](std::string message) {
            if (outProblem != nullptr) { *outProblem = std::move(message); }
            return StudioDockTree{};
        };

        if (!value.isObject()) { return reject("the layout document is not an object"); }

        const int version = value["version"].asInt(0);
        if (version <= 0) { return reject("the layout document has no version"); }
        if (version > kLayoutVersion)
        {
            // Refused rather than guessed at. A newer Studio may have written node kinds this one
            // cannot represent, and silently dropping them would lose a workspace the user built.
            return reject("the layout was written by a newer CNA Studio (version "
                          + std::to_string(version) + ")");
        }

        StudioDockTree tree;
        std::string problem;

        const auto readNode = [&tree, &problem](const auto& self, const JsonValue& source,
                                                StudioDockNodeId into, int depth) -> bool {
            // Bounded rather than trusted. A deliberately deep document is a stack overflow, and a
            // workspace nobody could build by hand is not a workspace worth restoring.
            if (depth > 64) { problem = "the layout nests too deeply"; return false; }
            if (!source.isObject()) { problem = "a layout node is not an object"; return false; }

            const std::string kind = source["kind"].asString();
            if (kind == "leaf")
            {
                std::vector<std::string> panels;
                for (const JsonValue& panel : source["panels"].getElements())
                {
                    const std::string id = panel.asString();
                    if (!id.empty()) { panels.push_back(id); }
                }
                tree.node(into).panels = std::move(panels);
                const int active = source["active"].asInt(0);
                tree.node(into).activePanel =
                    tree.node(into).panels.empty()
                        ? 0
                        : std::min(static_cast<std::size_t>(std::max(0, active)),
                                   tree.node(into).panels.size() - 1);
                return true;
            }
            if (kind != "split") { problem = "a layout node has an unknown kind"; return false; }

            const StudioDockNodeId first = tree.allocate();
            const StudioDockNodeId second = tree.allocate();
            StudioDockNode& node = tree.node(into);
            node.first = first;
            node.second = second;
            node.orientation = source["orientation"].asString() == "vertical"
                ? StudioDockOrientation::Vertical
                : StudioDockOrientation::Horizontal;
            node.fraction = std::clamp(source["fraction"].asFloat(0.5f), 0.0f, 1.0f);
            tree.node(first).parent = into;
            tree.node(second).parent = into;

            return self(self, source["first"], first, depth + 1)
                && self(self, source["second"], second, depth + 1);
        };

        if (!readNode(readNode, value["root"], tree.root(), 0)) { return reject(problem); }

        for (const JsonValue& entry : value["floating"].getElements())
        {
            StudioFloatingDock window;
            for (const JsonValue& panel : entry["panels"].getElements())
            {
                const std::string id = panel.asString();
                if (!id.empty()) { window.panels.push_back(id); }
            }
            // A float holding nothing is dropped rather than restored: it would be a title bar
            // over an empty rectangle that the user never asked for and has to close by hand.
            if (window.panels.empty()) { continue; }

            window.activePanel = std::min(
                static_cast<std::size_t>(std::max(0, entry["active"].asInt(0))),
                window.panels.size() - 1);
            window.x = entry["x"].asFloat(0.0f);
            window.y = entry["y"].asFloat(0.0f);
            window.width = std::max(entry["width"].asFloat(360.0f), kMinimumFloatingExtent);
            window.height = std::max(entry["height"].asFloat(280.0f), kMinimumFloatingExtent);
            tree.floating_.push_back(std::move(window));
        }

        if (!tree.isWellFormed(&problem)) { return reject(problem); }
        return tree;
    }
} // namespace CNA::Studio
