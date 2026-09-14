// SPDX-License-Identifier: MS-PL
/**
 * @file StudioDockTree.cpp
 * @brief Tree structure, minimum-size propagation, geometry resolution and serialization.
 */

#include "CNA/Studio/UiCore/StudioDockTree.hpp"

#include <algorithm>
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
        const StudioDockNodeId source = findPanel(panelId);
        if (source == kInvalidDockNode || !isLive(destination) || !nodes_[destination].isLeaf())
        {
            return false;
        }

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
        return result;
    }

    bool StudioDockTree::activatePanel(std::string_view panelId)
    {
        const StudioDockNodeId leaf = findPanel(panelId);
        if (leaf == kInvalidDockNode) { return false; }

        const std::vector<std::string>& panels = nodes_[leaf].panels;
        const auto found = std::find(panels.begin(), panels.end(), panelId);
        nodes_[leaf].activePanel = static_cast<std::size_t>(found - panels.begin());
        return true;
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
        if (!tree.isWellFormed(&problem)) { return reject(problem); }
        return tree;
    }
} // namespace CNA::Studio
