// SPDX-License-Identifier: MS-PL
/**
 * @file StudioShell.cpp
 * @brief Menu, toolbar, dock, status-bar and shortcut behaviour for the application frame.
 */

#include "CNA/Studio/UiCore/StudioShell.hpp"

#include "CNA/Studio/UiCore/StudioWidgets.hpp"

#include <algorithm>
#include <cmath>

namespace CNA::Studio
{
    namespace
    {
        /** @brief Returns a metric already scaled to physical pixels. */
        float metricOf(const StudioTheme& theme, StudioMetric metric)
        {
            return static_cast<float>(theme.metric(metric));
        }

        /** @brief Names an action-dispatch outcome, for the refusal log. */
        std::string_view resultName(StudioActionResult result)
        {
            switch (result)
            {
                case StudioActionResult::Invoked:        return "invoked";
                case StudioActionResult::NotFound:       return "not found";
                case StudioActionResult::Disabled:       return "disabled";
                case StudioActionResult::NotImplemented: return "not implemented";
            }
            return "";
        }

        /** @brief Every key that can begin a chord, in enumeration order. */
        constexpr int kFirstDispatchableKey = static_cast<int>(UiKey::Tab);
    } // namespace

    StudioShell::StudioShell() : StudioShell(StudioTheme::dark()) {}

    StudioShell::StudioShell(StudioTheme theme) : frame_(std::move(theme))
    {
        frame_.setFontAtlas(&fonts_);
        registerCoreStudioActions(actions_);
        menus_ = defaultMenus();
        toolbar_ = defaultToolbar();

        registerPanel({"viewport", "Viewport", false, /*closable=*/false, /*isViewport=*/true});
        registerPanel({"outliner", "World Outliner"});
        registerPanel({"layers", "Layers"});
        registerPanel({"details", "Details"});
        registerPanel({"material", "Material"});
        registerPanel({"history", "History"});
        registerPanel({"content", "Content Browser"});
        registerPanel({"output", "Output Log"});
        registerPanel({"build", "Build"});
        registerPanel({"problems", "Problems"});
        registerPanel({"comparison", "Backends"});
        registerPanel({"preferences", "Preferences"});
        registerPanel({"diagnostics", "Diagnostics"});
        resetLayout();

        // The Layouts submenu starts with Save Layout As in it, even with nothing saved. Filling
        // it only when a layout exists would leave a fresh Studio with the submenu greyed out and
        // no way to reach the command that creates the first one.
        rebuildLayoutMenu();

        // The one core action the shell itself owns, attached here rather than left for a service
        // that will never exist: the workspace arrangement is the shell's own state, and a Window
        // menu whose only entry reports "not implemented" is worse than one with no entry.
        if (const StudioAction* found = actions_.find("studio.window.resetLayout"))
        {
            StudioAction reset = *found;
            reset.run = [this]() { resetLayout(); };
            actions_.add(std::move(reset));
        }
        if (const StudioAction* found = actions_.find(std::string{kStudioSaveLayoutAsActionId}))
        {
            StudioAction saveAs = *found;
            saveAs.run = [this]() {
                StudioDialogRequest request;
                request.title = "Save Layout As";
                request.lines = {"Name this arrangement so you can come back to it."};
                request.hasTextField = true;
                request.placeholder = "Layout name";
                request.requireText = true;
                request.buttons = {"Cancel", "Save"};
                request.cancelButton = 0;
                openDialog(std::move(request));
                pending_ = PendingDialog::SaveLayoutAs;
            };
            actions_.add(std::move(saveAs));
        }
        if (const StudioAction* found = actions_.find("studio.help.about"))
        {
            StudioAction about = *found;
            about.run = [this]() {
                StudioDialogRequest request;
                request.title = "About CNA Studio";
                request.lines = aboutLines_;
                request.buttons = {"Close"};
                openDialog(std::move(request));
            };
            actions_.add(std::move(about));
        }
        if (const StudioAction* found = actions_.find(std::string{kStudioDockAllActionId}))
        {
            StudioAction dockAll = *found;
            // Greyed out when there is nothing to recover, rather than drawn available and doing
            // nothing: the workspace is the shell's own state, so it can answer for itself.
            dockAll.isEnabled = [this]() { return !dock_.floating().empty(); };
            dockAll.run = [this]() { (void)dockAllFloating(); };
            actions_.add(std::move(dockAll));
        }

        status_.message = "No project open";
        status_.renderer = "unknown";

        // What the UI core can say for itself. A host that knows its version and its renderer
        // replaces this with the truth; a preview that does not still shows something honest
        // rather than an empty box.
        aboutLines_ = {"CNA Studio", "An editor for CNA games.",
                       "Native Studio UI, no Dear ImGui."};
    }

    std::vector<StudioMenuDefinition> StudioShell::defaultMenus()
    {
        const std::string sep{kStudioMenuSeparatorId};
        return {
            // New Scene first, where the prototype has it: it is the frequent one, and a menu that
            // put the project commands above it would have people reaching past them all day.
            {"File", {"studio.file.newScene", sep,
                      "studio.file.newProject", "studio.file.openProject", sep,
                      "studio.file.save", "studio.file.saveAll", sep,
                      // Where the prototype puts them, which is where a user who has been told
                      // "File > Recover Unsaved Scene" by the log will look.
                      "studio.file.recoverScene", "studio.file.discardRecovered", sep,
                      "studio.file.quit"}},
            {"Edit", {"studio.edit.undo", "studio.edit.redo", sep,
                      "studio.edit.rename", "studio.edit.duplicate", "studio.edit.delete"}},
            {"View", {"studio.view.focusSelected", "studio.view.toggleGrid", sep,
                      "studio.view.translate", "studio.view.rotate", "studio.view.scale",
                      "studio.view.toggleGizmoSpace"}},
            {"Project", {}},
            {"Build", {"studio.build.build", "studio.build.cancel", sep,
                       "studio.build.package"}},
            {"Play", {"studio.play.play", "studio.play.stop"}},
            {"Tools", {}},
            // The panel list is filled in by registerPanel(), because which panels exist is decided
            // at run time by whoever assembles the shell. It ships empty rather than absent so the
            // row is in the same place in a shell with no panels as in one with ten.
            {"Window", {StudioMenuEntry::submenu(std::string{kStudioPanelMenuLabel}, {}),
                        StudioMenuEntry::submenu(std::string{kStudioLayoutMenuLabel}, {}),
                        std::string{kStudioMenuSeparatorId},
                        // Before Reset Layout, and greyed out until there is something to recover.
                        // The two are the same kind of answer at different costs, and a user whose
                        // floating window has got away from them should find the cheap one first.
                        std::string{kStudioDockAllActionId}, "studio.window.resetLayout"}},
            {"Help", {"studio.help.about"}},
        };
    }

    std::vector<std::string> StudioShell::defaultToolbar()
    {
        const std::string sep{kStudioMenuSeparatorId};
        return {"studio.file.save", sep,
                "studio.edit.undo", "studio.edit.redo", sep,
                "studio.view.translate", "studio.view.rotate", "studio.view.scale", sep,
                "studio.view.toggleGrid", sep,
                "studio.play.play", "studio.play.stop", sep,
                "studio.build.build"};
    }

    void StudioShell::registerPanel(StudioPanelDescriptor descriptor)
    {
        const std::string id = descriptor.id;

        const auto existing = std::find_if(panels_.begin(), panels_.end(),
            [&](const StudioPanelDescriptor& p) { return p.id == descriptor.id; });
        if (existing != panels_.end()) { *existing = std::move(descriptor); }
        else { panels_.push_back(std::move(descriptor)); }

        registerPanelAction(id);
        rebuildPanelMenu();
    }

    std::string StudioShell::panelActionId(std::string_view panelId)
    {
        return std::string{kStudioPanelActionPrefix} + std::string{panelId};
    }

    std::string StudioShell::showPanelActionId(std::string_view panelId)
    {
        return std::string{kStudioShowPanelActionPrefix} + std::string{panelId};
    }

    std::string StudioShell::closePanelActionId(std::string_view panelId)
    {
        return std::string{kStudioClosePanelActionPrefix} + std::string{panelId};
    }

    std::string StudioShell::floatPanelActionId(std::string_view panelId)
    {
        return std::string{kStudioFloatPanelActionPrefix} + std::string{panelId};
    }

    bool StudioShell::floatPanel(std::string_view id)
    {
        if (!isPanelOpen(id)) { return false; }

        // Offset from the corner by a little more each time, so undocking three panels from a menu
        // does not stack three windows exactly on top of each other with only the last one visible.
        const float step = static_cast<float>(dock_.floating().size() % 6)
                         * metricOf(frame_.theme(), StudioMetric::PanelHeaderHeight) * 2.0f;
        const float inset = metricOf(frame_.theme(), StudioMetric::SpacingLarge) * 2.0f;

        return dock_.floatPanel(id, inset + step, inset + step,
                                kStudioFloatDropWidth * frame_.theme().scale(),
                                kStudioFloatDropHeight * frame_.theme().scale())
            != kInvalidFloatingDock;
    }

    std::size_t StudioShell::dockAllFloating()
    {
        std::size_t docked = 0;
        while (!dock_.floating().empty())
        {
            // The largest leaf, the same place openPanel puts a panel that has nowhere else to go:
            // a window recovered into a two-tab corner is a window the user still has to find.
            StudioDockNodeId largest = kInvalidDockNode;
            float largestArea = -1.0f;
            for (const StudioDockNodeId leaf : dock_.leaves())
            {
                const UiRect& bounds = dock_.node(leaf).bounds;
                const float area = bounds.width * bounds.height;
                if (area > largestArea) { largestArea = area; largest = leaf; }
            }
            if (largest == kInvalidDockNode) { largest = dock_.root(); }

            const std::string panelId = dock_.floating().front().panels.front();
            if (!dock_.movePanel(panelId, largest)) { break; }
            ++docked;
        }
        return docked;
    }

    std::vector<StudioMenuEntry> StudioShell::tabContextMenu(std::string_view panelId) const
    {
        std::vector<StudioMenuEntry> rows;
        rows.emplace_back(closePanelActionId(panelId));
        // Named as well as draggable. The drag is the gesture people use once they know it; the
        // menu is how they find out it exists, and a feature reachable only by discovering a
        // gesture is a feature most users never have.
        rows.emplace_back(floatPanelActionId(panelId));
        rows.emplace_back(std::string{kStudioDockAllActionId});
        rows.emplace_back(std::string{kStudioMenuSeparatorId});

        // The whole panel list, not just this one. A user who has just closed a panel is exactly
        // the user who needs to find it again, and making them go back to the menu bar for it is
        // the kind of small friction that is never worth the space it saves.
        std::vector<StudioMenuEntry> panelRows;
        panelRows.reserve(panels_.size());
        for (const StudioPanelDescriptor& descriptor : panels_)
        {
            panelRows.emplace_back(panelActionId(descriptor.id));
        }
        rows.push_back(StudioMenuEntry::submenu(std::string{kStudioPanelMenuLabel},
                                                std::move(panelRows)));
        return rows;
    }

    void StudioShell::registerPanelAction(const std::string& panelId)
    {
        const StudioPanelDescriptor* descriptor = panel(panelId);
        if (descriptor == nullptr) { return; }

        StudioAction close;
        close.id = closePanelActionId(panelId);
        close.label = "Close";
        close.description = "Close the " + descriptor->title + " panel.";
        close.category = StudioActionCategory::Window;
        close.isEnabled = [this, panelId] {
            const StudioPanelDescriptor* current = panel(panelId);
            return current != nullptr && current->closable && isPanelOpen(panelId);
        };
        close.run = [this, panelId] { closePanel(panelId); };
        actions_.add(std::move(close));

        // Not on any menu: the Window menu has the toggle, and a second row meaning nearly the same
        // thing would be a menu that has to be read twice. This one exists to be *invoked* -- by a
        // notification saying where to look, and by whatever Go To command comes later.
        StudioAction show;
        show.id = showPanelActionId(panelId);
        show.label = "Show " + descriptor->title;
        show.description = "Show the " + descriptor->title + " panel and bring it to the front.";
        show.category = StudioActionCategory::Window;
        show.run = [this, panelId] {
            if (!isPanelOpen(panelId)) { (void)openPanel(panelId); }
            (void)activatePanel(panelId);
        };
        actions_.add(std::move(show));

        StudioAction undock;
        undock.id = floatPanelActionId(panelId);
        undock.label = "Float";
        undock.description = "Undock the " + descriptor->title + " panel into its own window.";
        undock.category = StudioActionCategory::Window;
        undock.isEnabled = [this, panelId] {
            // Not while it already is one: a command that would move a window somewhere the user
            // did not ask for reads as broken rather than as a no-op.
            return dock_.findPanel(panelId) != kInvalidDockNode;
        };
        undock.run = [this, panelId] { (void)floatPanel(panelId); };
        actions_.add(std::move(undock));

        StudioAction action;
        action.id = panelActionId(panelId);
        action.label = descriptor->title;
        action.description = "Show or hide the " + descriptor->title + " panel.";
        action.category = StudioActionCategory::Window;
        action.checkable = true;
        // Pulled rather than stored, like every other enablement in the registry: a panel closed
        // by dragging its tab away must show as unchecked without anybody remembering to tell the
        // menu about it.
        action.isChecked = [this, panelId] { return isPanelOpen(panelId); };
        action.isEnabled = [this, panelId] {
            const StudioPanelDescriptor* current = panel(panelId);
            if (current == nullptr) { return false; }
            // A panel the user must not be able to close is offered only as a way to bring it
            // back. Drawn enabled and then refusing would be the worse answer.
            return current->closable || !isPanelOpen(panelId);
        };
        action.run = [this, panelId] {
            if (isPanelOpen(panelId)) { closePanel(panelId); }
            else { openPanel(panelId); }
        };
        actions_.add(std::move(action));
    }

    void StudioShell::rebuildPanelMenu()
    {
        for (StudioMenuDefinition& menu : menus_)
        {
            for (StudioMenuEntry& entry : menu.entries)
            {
                if (!entry.isSubmenu() || entry.label != kStudioPanelMenuLabel) { continue; }

                entry.rows.clear();
                entry.rows.reserve(panels_.size());
                for (const StudioPanelDescriptor& descriptor : panels_)
                {
                    entry.rows.emplace_back(panelActionId(descriptor.id));
                }
            }
        }
    }

    const StudioPanelDescriptor* StudioShell::panel(std::string_view id) const
    {
        const auto found = std::find_if(panels_.begin(), panels_.end(),
            [&](const StudioPanelDescriptor& p) { return p.id == id; });
        return found == panels_.end() ? nullptr : &*found;
    }

    bool StudioShell::setPanelModified(std::string_view id, bool modified)
    {
        const auto found = std::find_if(panels_.begin(), panels_.end(),
            [&](const StudioPanelDescriptor& p) { return p.id == id; });
        if (found == panels_.end()) { return false; }
        found->modified = modified;
        return true;
    }

    void StudioShell::resetLayout()
    {
        // Studio's default workspace, built from the same operations a user's gestures will use.
        // Constructing it any other way would let the default reach an arrangement no gesture can
        // produce -- and therefore one the user could never get back to after changing it.
        dock_ = StudioDockTree{};

        // Each split turns the node it was given *into* the split and moves its content to a new
        // leaf, so the centre has to be re-found after every one. sibling() is what says that
        // plainly; chasing child links here would be the first thing to break when the tree gains
        // a node kind.
        // The bottom group is taken off the whole dock area first, so the Content Browser and the
        // Output Log span the full width beneath the outliner and the inspector. A log is read
        // across, and a wide one costs the side panels nothing.
        StudioDockNodeId centre = dock_.root();
        const StudioDockNodeId bottom = dock_.split(centre, StudioDockSide::Bottom, 0.28f);
        centre = dock_.sibling(bottom);
        const StudioDockNodeId left = dock_.split(centre, StudioDockSide::Left, 0.18f);
        centre = dock_.sibling(left);
        const StudioDockNodeId right = dock_.split(centre, StudioDockSide::Right, 0.24f);
        centre = dock_.sibling(right);

        dock_.addPanel(left, "outliner");
        dock_.addPanel(left, "layers");
        dock_.addPanel(right, "details");
        dock_.addPanel(right, "material");
        dock_.addPanel(right, "history");
        dock_.addPanel(bottom, "content");
        dock_.addPanel(bottom, "output");
        dock_.addPanel(bottom, "build");
        dock_.addPanel(bottom, "problems");
        dock_.addPanel(bottom, "comparison");
        dock_.addPanel(bottom, "preferences");
        dock_.addPanel(bottom, "diagnostics");
        dock_.addPanel(centre, "viewport");

        for (const StudioDockNodeId leaf : dock_.leaves())
        {
            if (!dock_.node(leaf).panels.empty()) { dock_.node(leaf).activePanel = 0; }
        }
    }

    bool StudioShell::isPanelOpen(std::string_view id) const
    {
        // Floating counts as open, and has to: the Window menu's check marks read this, and a
        // panel the user has just dragged out into its own window showing as closed would be the
        // menu contradicting what is on the screen in front of them.
        return dock_.findPanel(id) != kInvalidDockNode
            || dock_.findFloatingPanel(id) != kInvalidFloatingDock;
    }

    bool StudioShell::openPanel(std::string_view id)
    {
        if (panel(id) == nullptr) { return false; }
        if (isPanelOpen(id)) { return true; }

        StudioDockNodeId largest = kInvalidDockNode;
        float largestArea = -1.0f;
        for (const StudioDockNodeId leaf : dock_.leaves())
        {
            const UiRect& bounds = dock_.node(leaf).bounds;
            const float area = bounds.width * bounds.height;
            if (area > largestArea) { largestArea = area; largest = leaf; }
        }
        if (largest == kInvalidDockNode) { largest = dock_.root(); }
        return dock_.addPanel(largest, std::string{id});
    }

    bool StudioShell::closePanel(std::string_view id)
    {
        const StudioPanelDescriptor* descriptor = panel(id);
        if (descriptor != nullptr && !descriptor->closable) { return false; }
        return dock_.removePanel(id);
    }

    JsonValue StudioShell::saveLayout() const { return dock_.toJson(); }

    bool StudioShell::loadLayout(const JsonValue& value, std::string* outProblem)
    {
        std::string problem;
        StudioDockTree restored = StudioDockTree::fromJson(value, &problem);
        if (!problem.empty())
        {
            if (outProblem != nullptr) { *outProblem = problem; }
            resetLayout();
            return false;
        }

        // Panels this build has never heard of are dropped and named, rather than carried through
        // as tabs that show nothing. An upgrade that retired a panel must cost the user that panel
        // and not the rest of their arrangement.
        std::vector<std::string> unknown;
        for (const std::string& id : restored.panels())
        {
            if (panel(id) == nullptr) { unknown.push_back(id); }
        }
        for (const std::string& id : unknown) { restored.removePanel(id); }

        dock_ = std::move(restored);

        // A panel the user cannot close must be present however the document arrived. A workspace
        // with no viewport is not a smaller workspace.
        std::vector<std::string> missing;
        for (const StudioPanelDescriptor& descriptor : panels_)
        {
            if (!descriptor.closable && !isPanelOpen(descriptor.id))
            {
                openPanel(descriptor.id);
                missing.push_back(descriptor.id);
            }
        }

        if (!dock_.isWellFormed(&problem))
        {
            if (outProblem != nullptr) { *outProblem = problem; }
            resetLayout();
            return false;
        }

        if (unknown.empty() && missing.empty()) { return true; }

        if (outProblem != nullptr)
        {
            std::string message;
            for (const std::string& id : unknown)
            {
                message += (message.empty() ? "dropped unknown panel '" : ", '") + id + "'";
            }
            for (const std::string& id : missing)
            {
                message += (message.empty() ? "restored required panel '" : ", '") + id + "'";
            }
            *outProblem = message;
        }
        return false;
    }

    bool StudioShell::activatePanel(std::string_view id)
    {
        const StudioDockNodeId leaf = dock_.findPanel(id);
        if (leaf == kInvalidDockNode) { return false; }

        StudioDockNode& node = dock_.node(leaf);
        for (std::size_t i = 0; i < node.panels.size(); ++i)
        {
            if (node.panels[i] == id) { node.activePanel = i; return true; }
        }
        return false;
    }

    bool StudioShell::isPanelActive(std::string_view id) const
    {
        const StudioDockNodeId leaf = dock_.findPanel(id);
        if (leaf != kInvalidDockNode)
        {
            const StudioDockNode& node = dock_.node(leaf);
            return node.activePanel < node.panels.size() && node.panels[node.activePanel] == id;
        }

        // A floated panel is in front of its own window's tab strip, which is a different group.
        const std::size_t window = dock_.findFloatingPanel(id);
        if (window == kInvalidFloatingDock) { return false; }

        const StudioFloatingDock& floating = dock_.floating()[window];
        return floating.activePanel < floating.panels.size()
            && floating.panels[floating.activePanel] == id;
    }

    bool StudioShell::setPanelContent(std::string_view id, StudioPanelContent content)
    {
        if (panel(id) == nullptr) { return false; }

        const auto found = std::find_if(panelContent_.begin(), panelContent_.end(),
            [&](const auto& entry) { return entry.first == id; });
        if (found != panelContent_.end())
        {
            found->second = std::move(content);
            return true;
        }

        panelContent_.emplace_back(std::string{id}, std::move(content));
        return true;
    }

    bool StudioShell::hasPanelContent(std::string_view id) const
    {
        const auto found = std::find_if(panelContent_.begin(), panelContent_.end(),
            [&](const auto& entry) { return entry.first == id; });
        return found != panelContent_.end() && static_cast<bool>(found->second);
    }

    UiRect StudioShell::panelBounds(std::string_view id) const
    {
        const StudioDockNodeId leaf = dock_.findPanel(id);
        if (leaf == kInvalidDockNode) { return UiRect{}; }

        const StudioDockNode& node = dock_.node(leaf);
        if (node.activePanel >= node.panels.size() || node.panels[node.activePanel] != id)
        {
            return UiRect{};
        }
        return dock_.leafGeometry(leaf, tabStripHeight()).body;
    }

    UiRect StudioShell::panelTabBounds(std::string_view id) const
    {
        const auto found = std::find_if(tabBounds_.begin(), tabBounds_.end(),
            [&](const auto& entry) { return entry.first == id; });
        return found == tabBounds_.end() ? UiRect{} : found->second;
    }

    float StudioShell::tabStripHeight() const
    {
        return metricOf(frame_.theme(), StudioMetric::TabHeight);
    }

    void StudioShell::setMenus(std::vector<StudioMenuDefinition> menus)
    {
        menus_ = std::move(menus);
        setOpenMenu(-1);

        // The two submenus the shell fills in are refilled, or replacing the menus would empty
        // them -- and an empty submenu draws greyed out, so a host that customised its File menu
        // would silently lose its panel list.
        rebuildPanelMenu();
        rebuildLayoutMenu();
    }

    void StudioShell::setToolbar(std::vector<std::string> entries)
    {
        toolbar_ = std::move(entries);
    }

    void StudioShell::setOpenMenu(int index)
    {
        if (index < 0 || static_cast<std::size_t>(index) >= menus_.size())
        {
            openMenu_ = -1;
            submenuPath_.clear();
            highlight_.clear();
            submenuPendingRow_ = -1;
            submenuPendingSeconds_ = 0.0f;
            return;
        }
        openMenuAt(index);
    }

    void StudioShell::openMenuAt(int index)
    {
        openMenu_ = index;
        // One chain at a time: opening a menu-bar menu while a context menu is up replaces it.
        contextOpen_ = false;
        contextRows_.clear();
        // Every submenu closes with it: moving along the bar with the arrow keys must not leave
        // the previous menu's submenu hanging over the new one.
        submenuPath_.clear();
        submenuPendingRow_ = -1;
        submenuPendingSeconds_ = 0.0f;
        // No row is highlighted until the keyboard asks for one. Pre-selecting the first item
        // would make Enter -- pressed to dismiss something else entirely -- run a command the user
        // never looked at.
        highlight_.assign(1, -1);
    }

    void StudioShell::openContextMenu(std::vector<StudioMenuEntry> rows, float x, float y)
    {
        // A menu-bar menu and a context menu are one chain, never two at once: right-clicking with
        // the File menu down must replace it rather than leave two popups fighting for the
        // keyboard.
        setOpenMenu(-1);

        contextRows_ = std::move(rows);
        contextOpen_ = !contextRows_.empty();
        contextAnchor_ = UiRect{x, y, 0.0f, 0.0f};
        if (contextOpen_) { highlight_.assign(1, -1); }
    }

    void StudioShell::closePopup()
    {
        setOpenMenu(-1);
        contextOpen_ = false;
        contextRows_.clear();
    }

    std::string StudioShell::applyLayoutActionId(std::string_view name)
    {
        return std::string{kStudioApplyLayoutActionPrefix} + std::string{name};
    }

    std::string StudioShell::deleteLayoutActionId(std::string_view name)
    {
        return std::string{kStudioDeleteLayoutActionPrefix} + std::string{name};
    }

    void StudioShell::setSavedLayouts(std::vector<StudioNamedLayout> layouts)
    {
        savedLayouts_ = std::move(layouts);
        std::sort(savedLayouts_.begin(), savedLayouts_.end(),
                  [](const StudioNamedLayout& lhs, const StudioNamedLayout& rhs) {
                      return lhs.name < rhs.name;
                  });
        registerLayoutActions();
        rebuildLayoutMenu();
    }

    void StudioShell::registerLayoutActions()
    {
        for (const StudioNamedLayout& saved : savedLayouts_)
        {
            const std::string name = saved.name;

            StudioAction apply;
            apply.id = applyLayoutActionId(name);
            apply.label = name;
            apply.description = "Arrange the workspace as '" + name + "'.";
            apply.category = StudioActionCategory::Window;
            apply.run = [this, name] { (void)applySavedLayout(name); };
            actions_.add(std::move(apply));

            StudioAction remove;
            remove.id = deleteLayoutActionId(name);
            remove.label = name;
            remove.description = "Delete the saved layout '" + name + "'.";
            remove.category = StudioActionCategory::Window;
            remove.run = [this, name] {
                // Asked first. Deleting an arrangement somebody spent ten minutes on, because a
                // menu row was one place lower than they expected, is not undoable.
                StudioDialogRequest request;
                request.title = "Delete layout";
                request.lines = {"Delete the saved layout '" + name + "'?",
                                 "This cannot be undone."};
                request.buttons = {"Cancel", "Delete"};
                request.cancelButton = 0;
                openDialog(std::move(request));
                pending_ = PendingDialog::DeleteLayout;
                pendingArgument_ = name;
            };
            actions_.add(std::move(remove));
        }
    }

    void StudioShell::rebuildLayoutMenu()
    {
        std::vector<StudioMenuEntry> rows;
        rows.emplace_back(std::string{kStudioSaveLayoutAsActionId});

        if (!savedLayouts_.empty())
        {
            rows.emplace_back(std::string{kStudioMenuSeparatorId});
            for (const StudioNamedLayout& saved : savedLayouts_)
            {
                rows.emplace_back(applyLayoutActionId(saved.name));
            }

            std::vector<StudioMenuEntry> removals;
            removals.reserve(savedLayouts_.size());
            for (const StudioNamedLayout& saved : savedLayouts_)
            {
                removals.emplace_back(deleteLayoutActionId(saved.name));
            }
            rows.emplace_back(std::string{kStudioMenuSeparatorId});
            rows.push_back(StudioMenuEntry::submenu("Delete", std::move(removals)));
        }

        for (StudioMenuDefinition& menu : menus_)
        {
            for (StudioMenuEntry& entry : menu.entries)
            {
                if (entry.isSubmenu() && entry.label == kStudioLayoutMenuLabel)
                {
                    entry.rows = rows;
                }
            }
        }
    }

    bool StudioShell::applySavedLayout(std::string_view name)
    {
        const auto found = std::find_if(savedLayouts_.begin(), savedLayouts_.end(),
            [&](const StudioNamedLayout& saved) { return saved.name == name; });
        if (found == savedLayouts_.end()) { return false; }

        std::string problem;
        if (!loadLayout(found->layout, &problem))
        {
            refused_.push_back("layout '" + std::string{name} + "': " + problem);
            return false;
        }
        return true;
    }

    bool StudioShell::saveLayoutAs(std::string_view name)
    {
        const std::string clean = StudioWorkspaceStore::sanitizeName(name);
        if (clean.empty())
        {
            refused_.push_back("a saved layout needs a name");
            return false;
        }

        const JsonValue layout = saveLayout();

        // Persisted first. A menu that listed a layout the file never received would offer it
        // again after a restart and find nothing there.
        if (workspace_.saveNamed)
        {
            std::string problem;
            if (!workspace_.saveNamed(clean, layout, &problem))
            {
                refused_.push_back("could not save layout '" + clean + "': " + problem);
                return false;
            }
        }

        const auto found = std::find_if(savedLayouts_.begin(), savedLayouts_.end(),
            [&](const StudioNamedLayout& saved) { return saved.name == clean; });
        if (found != savedLayouts_.end()) { found->layout = layout; }
        else { savedLayouts_.push_back(StudioNamedLayout{clean, layout}); }

        setSavedLayouts(std::move(savedLayouts_));
        return true;
    }

    bool StudioShell::deleteSavedLayout(std::string_view name)
    {
        const auto found = std::find_if(savedLayouts_.begin(), savedLayouts_.end(),
            [&](const StudioNamedLayout& saved) { return saved.name == name; });
        if (found == savedLayouts_.end()) { return false; }

        if (workspace_.removeNamed)
        {
            std::string problem;
            if (!workspace_.removeNamed(std::string{name}, &problem))
            {
                refused_.push_back("could not delete layout '" + std::string{name} + "': "
                                   + problem);
                return false;
            }
        }

        // The commands go with it, or the Window menu would keep a row that names nothing and the
        // shortcut table a chord that arranges the workspace as a layout that no longer exists.
        actions_.remove(applyLayoutActionId(name));
        actions_.remove(deleteLayoutActionId(name));

        savedLayouts_.erase(found);
        setSavedLayouts(std::move(savedLayouts_));
        return true;
    }

    void StudioShell::handleDialogAnswer()
    {
        if (pending_ == PendingDialog::None || !dialogResult_.answered()) { return; }

        const PendingDialog which = pending_;
        const std::string argument = pendingArgument_;
        pending_ = PendingDialog::None;
        pendingArgument_.clear();

        // Only the affirmative answer acts. Escape and Cancel are the same event as far as a
        // destructive command is concerned, and treating a dismissal as a yes is the one mistake a
        // confirmation exists to prevent.
        if (dialogResult_.chosen != 1) { return; }

        switch (which)
        {
            case PendingDialog::SaveLayoutAs: (void)saveLayoutAs(dialogResult_.text); break;
            case PendingDialog::DeleteLayout: (void)deleteSavedLayout(argument); break;
            case PendingDialog::None: break;
        }
    }

    void StudioShell::openDialog(StudioDialogRequest request)
    {
        // The menu that opened it goes with it. A dialog raised from a menu item while the menu
        // stayed up would leave two things claiming the keyboard, and Escape would answer the
        // wrong one.
        closePopup();
        frame_.openModal(frame_.ids().make("studio.dialog"));

        dialog_ = std::move(request);
        dialogState_ = StudioDialogState{};
        dialogResult_ = StudioDialogResult{};
        dialogOpen_ = true;

        // Cleared here rather than by each caller: a dialog opened while another of the shell's
        // own was pending would otherwise answer the wrong command.
        pending_ = PendingDialog::None;
        pendingArgument_.clear();
    }

    void StudioShell::closeDialog()
    {
        frame_.closeModal();
        dialogOpen_ = false;
        dialogState_ = StudioDialogState{};
    }

    UiRect StudioShell::dialogBounds() const
    {
        if (!dialogOpen_) { return UiRect{}; }
        return studioDialogBounds(frame_, layout_.window, dialog_);
    }

    void StudioShell::describeDialog()
    {
        if (!dialogOpen_) { return; }

        frame_.deferModal([this](StudioFrame& frame) {
            const StudioDialogResult answered =
                studioDialog(frame, layout_.window, dialog_, dialogState_);
            if (!frame.isInputPass()) { return; }

            dialogResult_ = answered;
            // Closed here rather than left to the caller, because the caller reads the answer
            // *after* the frame: a dialog that stayed open until somebody remembered to close it
            // would take a second click to dismiss and would answer twice in between.
            if (answered.answered()) { closeDialog(); }
        });
    }

    void StudioShell::invoke(std::string_view id)
    {
        const StudioActionResult result = actions_.invoke(id);
        if (result == StudioActionResult::Invoked)
        {
            invoked_.emplace_back(id);
            return;
        }
        // Recorded rather than dropped. A menu entry naming an action nobody registered, or one
        // whose handler has not landed yet, must be discoverable without a debugger -- silence
        // here is exactly how a menu ends up with three rows that quietly do nothing.
        refused_.emplace_back(std::string{id} + ": " + std::string{resultName(result)});
    }

    int StudioShell::highlightedMenuEntry() const
    {
        return highlight_.empty() ? -1 : highlight_.back();
    }

    int StudioShell::highlightedMenuEntry(std::size_t level) const
    {
        return level < highlight_.size() ? highlight_[level] : -1;
    }

    int StudioShell::openSubmenuRow(std::size_t level) const
    {
        return level < submenuPath_.size() ? submenuPath_[level] : -1;
    }

    bool StudioShell::openSubmenu(std::size_t level, int row)
    {
        if (!isPopupOpen() || level >= highlight_.size()) { return false; }

        if (row < 0)
        {
            if (level >= submenuPath_.size()) { return false; }
            submenuPath_.resize(level);
            highlight_.resize(level + 1);
            return true;
        }

        if (level < submenuPath_.size() && submenuPath_[level] == row) { return false; }

        submenuPath_.resize(level + 1);
        submenuPath_[level] = row;
        // A freshly opened submenu has no highlighted row, for the same reason a freshly opened
        // menu does not: Enter pressed to dismiss something else must not run a command nobody
        // looked at.
        highlight_.resize(level + 2);
        highlight_[level + 1] = -1;
        return true;
    }

    UiRect StudioShell::menuPopupBounds(std::size_t level) const
    {
        return level < menuLevels_.size() ? menuLevels_[level].popup : UiRect{};
    }

    std::size_t StudioShell::menuRowCount(std::size_t level) const
    {
        return level < menuLevels_.size() ? menuLevels_[level].rows.size() : 0;
    }

    UiRect StudioShell::menuTitleBounds(std::size_t index) const
    {
        if (index >= menuTitles_.size()) { return UiRect{}; }
        return menuTitles_[index].bounds;
    }

    UiRect StudioShell::menuRowBounds(std::size_t level, std::size_t index) const
    {
        if (level >= menuLevels_.size() || index >= menuLevels_[level].rows.size())
        {
            return UiRect{};
        }
        return menuLevels_[level].rows[index].bounds;
    }

    std::string_view StudioShell::menuRowActionId(std::size_t level, std::size_t index) const
    {
        if (level >= menuLevels_.size() || index >= menuLevels_[level].rows.size()) { return {}; }
        const MenuRowGeometry& row = menuLevels_[level].rows[index];
        return row.submenu ? std::string_view{row.label} : std::string_view{row.id};
    }

    const std::vector<StudioMenuEntry>* StudioShell::entriesForLevel(std::size_t level) const
    {
        const std::vector<StudioMenuEntry>* entries = nullptr;
        if (contextOpen_) { entries = &contextRows_; }
        else if (isMenuOpen() && static_cast<std::size_t>(openMenu_) < menus_.size())
        {
            entries = &menus_[static_cast<std::size_t>(openMenu_)].entries;
        }
        if (entries == nullptr) { return nullptr; }

        for (std::size_t depth = 0; depth < level; ++depth)
        {
            if (depth >= submenuPath_.size()) { return nullptr; }
            const int row = submenuPath_[depth];
            if (row < 0 || static_cast<std::size_t>(row) >= entries->size()) { return nullptr; }

            const StudioMenuEntry& entry = (*entries)[static_cast<std::size_t>(row)];
            if (!entry.isSubmenu()) { return nullptr; }
            entries = &entry.rows;
        }
        return entries;
    }

    UiRect StudioShell::toolbarEntryBounds(std::size_t index) const
    {
        if (index >= toolbarEntries_.size()) { return UiRect{}; }
        return toolbarEntries_[index].bounds;
    }

    std::string_view StudioShell::toolbarEntryActionId(std::size_t index) const
    {
        if (index >= toolbarEntries_.size()) { return {}; }
        return toolbarEntries_[index].id;
    }

    void StudioShell::renderFrame(const UiInputState& input)
    {
        invoked_.clear();
        refused_.clear();
        dialogResult_ = StudioDialogResult{};
        keyboardConsumed_ = false;

        // The blocking layer is decided before anything is described, from state that already
        // exists. Inferring it from description order would leak one frame of input to the panels
        // underneath an open menu.
        // The floating layer is opened only while the pointer is over a float, or while a gesture
        // that began on one is still running. Read from the *previous* frame's resolved geometry,
        // which is what an immediate-mode frame has before it has laid itself out -- and which is
        // right, because a float that moved this frame is one the pointer is already holding.
        const bool overFloat =
            dock_.floatingAt(input.mouseX, input.mouseY) != kInvalidFloatingDock;
        if (!input.isMouseDown(UiMouseButton::Left)) { floatGesture_ = false; }
        else if (overFloat) { floatGesture_ = true; }

        // The stack as it was laid out last frame, for the same reason the floats are: the choice
        // is made before this frame has any geometry, and a toast that moved is one the pointer is
        // already over.
        const bool overToast = !toastBounds_.isEmpty() && input.mouseInWindow
                            && toastBounds_.contains(input.mouseX, input.mouseY);

        const int blocking = isPopupOpen() ? kMenuLayer
                           : overToast ? kToastLayer
                           : (overFloat || floatGesture_) ? kFloatingLayer
                                                           : 0;

        // Zero while the pointer is over the stack. A toast that vanished while it was being read,
        // or while the pointer was travelling to its button, is worse than one that never appeared
        // -- and the button is the whole reason a failure is announced rather than logged.
        notifications_.tick(overToast ? 0.0 : static_cast<double>(input.deltaSeconds));

        frame_.beginFrame(input, blocking);
        buildContent();

        frame_.beginLayout();
        computeLayout(input.displayWidth, input.displayHeight);

        frame_.beginInput();
        frame_.router().setWantsTextInput(textInputActive_);
        handleMenuKeyboard();
        describe();

        // After the description, not before. Whether a text field is taking input is something
        // the widgets declare as they are described, so a dispatch that ran first would always see
        // "no text field" and would fire F as Focus Selected while the user was typing a name.
        dispatchShortcuts();

        // The input pass may have opened, switched or closed a menu. Re-resolving the popup here
        // is what lets it appear on the same frame the user clicked rather than the one after --
        // and it is layout, not drawing, so it belongs before the draw pass rather than inside it.
        computeLayout(input.displayWidth, input.displayHeight);

        frame_.beginDraw();
        describe();
        frame_.endFrame();

        // After the frame, because acting on an answer can rearrange the whole workspace -- and
        // doing that halfway through describing it would leave the draw pass describing a dock
        // tree the input pass never saw.
        handleDialogAnswer();
    }

    void StudioShell::buildContent()
    {
        // Content descriptors are already the shell's own state; the phase exists so that an
        // application putting *its* state into them has a named place to do it. What is worth
        // doing here is refusing to draw a workspace that is not sound: a layout read from a file
        // somebody edited by hand is not a layout this code wrote.
        std::string problem;
        if (!dock_.isWellFormed(&problem))
        {
            refused_.push_back("workspace layout: " + problem + " -- reset to the default");
            resetLayout();
        }
        tabBounds_.clear();
    }

    void StudioShell::computeLayout(float width, float height)
    {
        const StudioTheme& theme = frame_.theme();
        layout_ = computeStudioShellLayout(width, height, theme);
        dock_.layout(layout_.dockArea, metricOf(theme, StudioMetric::SplitterThickness),
                     tabStripHeight(), kStudioMinimumDockExtent * theme.scale());

        // --- Menu bar titles ----------------------------------------------------------------------
        menuTitles_.clear();
        const float titlePadding = metricOf(theme, StudioMetric::SpacingMedium);
        UiRect cursor = layout_.menuBar.inset(
            UiEdges{metricOf(theme, StudioMetric::SpacingSmall), 0.0f});
        for (const StudioMenuDefinition& menu : menus_)
        {
            // Whole pixels, for the same reason the dock splits are snapped: a title starting at
            // a fraction blurs its own left edge and shifts every title after it by a different
            // sub-pixel amount.
            //
            // **Ceil, not round.** A box sized to fit its text and then rounded *down* is a box
            // its text no longer fits, and the widget dutifully truncates it -- which showed up as
            // "File" rendering as "F..." while "Project" was fine, depending on nothing but where
            // each measured width fell relative to a half pixel.
            const float width_ = std::ceil(
                frame_.measureText(StudioFontRole::Body, menu.title).width + titlePadding * 2.0f);
            MenuTitleGeometry geometry;
            geometry.bounds = cursor.splitLeft(std::min(width_, cursor.width));
            menuTitles_.push_back(geometry);
        }

        // --- Toolbar entries ----------------------------------------------------------------------
        toolbarEntries_.clear();
        const float buttonHeight = metricOf(theme, StudioMetric::ControlHeight);
        const float spacing = metricOf(theme, StudioMetric::SpacingSmall);
        UiRect toolbarCursor = layout_.toolbar.inset(
            UiEdges{metricOf(theme, StudioMetric::SpacingMedium),
                    std::max(0.0f, (layout_.toolbar.height - buttonHeight) * 0.5f)});

        for (const std::string& entry : toolbar_)
        {
            if (toolbarCursor.width <= 0.0f) { break; }

            ToolbarEntryGeometry geometry;
            geometry.id = entry;
            if (entry == kStudioMenuSeparatorId)
            {
                geometry.separator = true;
                geometry.bounds = toolbarCursor.splitLeft(
                    std::min(metricOf(theme, StudioMetric::SpacingLarge), toolbarCursor.width));
            }
            else
            {
                const StudioAction* action = actions_.find(entry);
                const std::string_view label = action != nullptr ? std::string_view{action->label}
                                                                 : std::string_view{entry};
                // An icon-only button is square; one showing a word is as wide as the word. Asking
                // the same question the draw pass will ask, rather than sizing for a label the
                // button turns out not to draw -- which would leave a toolbar of square icons in
                // rectangles wide enough for "Translate".
                //
                // Ceil for the same reason as the menu titles: rounding a content-derived width
                // down produces a control its own label does not fit in.
                const float wanted = studioIconForAction(entry) != StudioIcon::None
                    ? buttonHeight
                    : std::ceil(std::max(
                          buttonHeight, studioLabelWidth(frame_, label, StudioFontRole::BodySmall)));
                geometry.bounds = toolbarCursor.splitLeft(std::min(wanted, toolbarCursor.width));
                toolbarCursor.splitLeft(std::min(spacing, toolbarCursor.width));
            }
            toolbarEntries_.push_back(geometry);
        }

        // --- The open menu, and every submenu open beneath it -----------------------------------
        menuLevels_.clear();
        if (!isPopupOpen()) { return; }
        if (isMenuOpen() && static_cast<std::size_t>(openMenu_) >= menuTitles_.size()) { return; }

        // Level by level, each opening beside the row that opened it. The path is walked rather
        // than trusted: a menu can be replaced while it is open, and a path that no longer names a
        // submenu simply stops the walk instead of laying out a level that is not there.
        UiRect anchor = contextOpen_ ? contextAnchor_
                                     : menuTitles_[static_cast<std::size_t>(openMenu_)].bounds;
        const MenuPlacement rootPlacement =
            contextOpen_ ? MenuPlacement::AtPoint : MenuPlacement::Below;
        for (std::size_t level = 0;; ++level)
        {
            const std::vector<StudioMenuEntry>* entries = entriesForLevel(level);
            if (entries == nullptr || entries->empty()) { break; }

            MenuLevel laidOut = layOutMenuLevel(
                *entries, anchor, level > 0 ? MenuPlacement::Beside : rootPlacement);
            if (laidOut.rows.empty()) { break; }
            menuLevels_.push_back(std::move(laidOut));

            if (level >= submenuPath_.size()) { break; }
            const int row = submenuPath_[level];
            if (row < 0 || static_cast<std::size_t>(row) >= menuLevels_[level].rows.size()) { break; }
            anchor = menuLevels_[level].rows[static_cast<std::size_t>(row)].bounds;
        }

        // The path can outlive the levels that laid out -- a submenu whose rows all vanished, say.
        // Trimming it here keeps "how many popups are open" and "how deep is the path" the same
        // number, which every keyboard rule below relies on.
        if (submenuPath_.size() + 1 > menuLevels_.size())
        {
            submenuPath_.resize(menuLevels_.empty() ? 0 : menuLevels_.size() - 1);
            highlight_.resize(submenuPath_.size() + 1, -1);
        }
    }

    StudioShell::MenuLevel StudioShell::layOutMenuLevel(const std::vector<StudioMenuEntry>& entries,
                                                        const UiRect& anchor,
                                                        MenuPlacement placement) const
    {
        MenuLevel level;
        if (entries.empty()) { return level; }

        const StudioTheme& theme = frame_.theme();
        const float rowHeight = studioMenuItemHeight(theme);
        const float separatorHeight = studioMenuSeparatorHeight(theme);
        const float padding = metricOf(theme, StudioMetric::SpacingMedium);
        const float checkColumn = metricOf(theme, StudioMetric::IconSize);

        float widest = 0.0f;
        float totalHeight = metricOf(theme, StudioMetric::SpacingSmall) * 2.0f;
        for (const StudioMenuEntry& entry : entries)
        {
            if (entry.isSeparator()) { totalHeight += separatorHeight; continue; }
            totalHeight += rowHeight;

            const StudioAction* action = entry.isSubmenu() ? nullptr : actions_.find(entry.id);
            const std::string_view label = entry.isSubmenu()
                ? std::string_view{entry.label}
                : (action != nullptr ? std::string_view{action->label} : std::string_view{entry.id});

            float rowWidth = padding * 2.0f + checkColumn
                           + metricOf(theme, StudioMetric::SpacingSmall)
                           + frame_.measureText(StudioFontRole::Body, label).width;

            if (entry.isSubmenu())
            {
                // The arrow column widens the menu for the same reason the shortcut column does:
                // an arrow overlapping the last letter of "Recent Projects" is the detail that
                // makes a menu look unfinished.
                rowWidth += metricOf(theme, StudioMetric::SpacingXLarge) + checkColumn;
            }
            else if (action != nullptr && action->shortcut.isBound())
            {
                // The shortcut column widens the menu rather than being clipped: a hint the user
                // cannot read is worse than no hint, because it looks like the binding is wrong.
                rowWidth += metricOf(theme, StudioMetric::SpacingXLarge)
                          + frame_.measureText(StudioFontRole::BodySmall,
                                               describeStudioShortcut(action->shortcut)).width;
            }
            widest = std::max(widest, rowWidth);
        }

        const float minimumWidth = metricOf(theme, StudioMetric::PanelHeaderHeight) * 4.0f;
        float popupWidth = std::ceil(std::max(widest, minimumWidth));
        popupWidth = std::min(popupWidth, layout_.window.width);

        float popupX = 0.0f;
        float popupY = 0.0f;
        switch (placement)
        {
            case MenuPlacement::Beside:
                // Beside its row, and flipped to the other side when it would run off: a submenu
                // that opened half off-screen would have the user chasing it with the pointer.
                popupX = anchor.right();
                if (popupX + popupWidth > layout_.window.right())
                {
                    popupX = anchor.left() - popupWidth;
                }
                popupY = anchor.top() - metricOf(theme, StudioMetric::SpacingSmall);
                break;

            case MenuPlacement::AtPoint:
                // Down and to the right of the pointer, flipping to the other side of it rather
                // than sliding along the edge: a context menu that slid would end up under the
                // pointer, and the first thing the user did would be to choose a row by accident.
                popupX = anchor.left();
                popupY = anchor.top();
                if (popupX + popupWidth > layout_.window.right())
                {
                    popupX = anchor.left() - popupWidth;
                }
                if (popupY + totalHeight > layout_.window.bottom())
                {
                    popupY = anchor.top() - totalHeight;
                }
                break;

            case MenuPlacement::Below:
                popupX = anchor.left();
                popupY = layout_.menuBar.bottom();
                break;
        }

        popupX = std::min(popupX, layout_.window.right() - popupWidth);
        popupX = std::max(popupX, layout_.window.left());

        // Lifted rather than clipped when it would run off the bottom, which is what a long
        // submenu opened from a row near the status bar does on every desktop.
        float popupHeight = std::min(totalHeight, std::max(0.0f, layout_.window.height));
        if (popupY + popupHeight > layout_.window.bottom())
        {
            popupY = layout_.window.bottom() - popupHeight;
        }
        popupY = std::max(popupY, layout_.window.top());
        popupHeight = std::min(popupHeight, std::max(0.0f, layout_.window.bottom() - popupY));

        level.popup = UiRect{std::round(popupX), std::round(popupY), popupWidth,
                             std::round(popupHeight)};

        UiRect rowCursor = level.popup.inset(
            UiEdges{0.0f, metricOf(theme, StudioMetric::SpacingSmall)});
        for (const StudioMenuEntry& entry : entries)
        {
            MenuRowGeometry row;
            row.id = entry.id;
            row.label = entry.label;
            row.separator = entry.isSeparator();
            row.submenu = entry.isSubmenu();

            const StudioAction* action =
                (row.separator || row.submenu) ? nullptr : actions_.find(entry.id);
            // A submenu is enabled when it has something in it. One that opens on nothing is a
            // dead end the user has to discover by trying it.
            row.enabled = row.submenu ? !entry.rows.empty()
                                      : (!row.separator && action != nullptr
                                         && actions_.isEnabled(entry.id));
            row.bounds = rowCursor.splitTop(row.separator ? separatorHeight : rowHeight);
            level.rows.push_back(std::move(row));
        }
        return level;
    }

    void StudioShell::describe()
    {
        const StudioTheme& theme = frame_.theme();
        if (frame_.isDrawPass())
        {
            frame_.drawList().fillRect(layout_.window, theme.color(StudioColorRole::AppBackground));
        }

        describeMenuBar();
        describeToolbar();
        describeDocks();
        describeSplitters();

        // After the docked workspace, so a float draws over it and its widgets win hover against
        // anything they overlap -- the router's hover is last-writer-wins, so description order is
        // what puts the window in front rather than a depth the panels beneath would have to know.
        describeFloating();

        describeStatusBar();

        // After the floats, so a toast draws over one parked in the corner and its Dismiss wins
        // hover against the window underneath; before the menus, which cover everything.
        describeToasts();

        // After the docks, because it needs the tab strips to have declared themselves, and before
        // the popup, because a menu open over a drag should still draw on top.
        describeDockDrag();

        // Last, so it draws over everything and so its widgets win hover against anything they
        // overlap. Being in a raised input layer is what stops the panels beneath from responding;
        // being described last is what stops the popup from being drawn underneath them.
        describeMenuPopup();

        // Queued before the popups are flushed, so the dialog's own body is in this pass's modal
        // list rather than the next one's.
        describeDialog();

        // Deferred popups -- a drop-down's list opened inside a panel -- come after the panels
        // that queued them and before the tooltip, for the same reason menus do.
        frame_.flushPopups();

        // And a modal after those, over its scrim: a dialog is the one thing that covers an open
        // menu, and a drop-down opened *on* a dialog is queued from inside this call and runs on
        // the next frame's flush above -- which is why the two are not one function.
        frame_.flushModals();

        // After even that: a tooltip is the only thing that may cover an open menu, because it
        // describes whatever the pointer is resting on and the pointer may be resting on the menu.
        describeTooltip();

        // And last of all, the thing the pointer is carrying. It follows the pointer across every
        // panel and must be readable over all of them, including an open menu.
        studioDrawDragPreview(frame_);
    }

    void StudioShell::describeTooltip()
    {
        if (!frame_.isDrawPass()) { return; }

        const StudioFrame::StudioTooltipRequest& request = frame_.tooltip();
        if (!request.visible()) { return; }

        const StudioTheme& theme = frame_.theme();
        const float padding = metricOf(theme, StudioMetric::SpacingSmall);

        // Wrapped at the newline the caller put in, not at a width: a tooltip is one line of label
        // and one of help, and reflowing it to a measured width would put a sentence's break
        // somewhere different every time the pointer moved to a wider control.
        std::vector<std::string_view> lines;
        std::size_t from = 0;
        while (from <= request.text.size())
        {
            const std::size_t newline = request.text.find('\n', from);
            const std::size_t end = newline == std::string::npos ? request.text.size() : newline;
            lines.push_back(std::string_view{request.text}.substr(from, end - from));
            if (newline == std::string::npos) { break; }
            from = newline + 1;
        }

        float widest = 0.0f;
        float height = 0.0f;
        const float lineHeight = frame_.measureText(StudioFontRole::BodySmall, "Ag").lineHeight;
        for (const std::string_view& line : lines)
        {
            widest = std::max(widest, frame_.measureText(StudioFontRole::BodySmall, line).width);
            height += lineHeight;
        }

        const float width = std::ceil(widest + padding * 2.0f);
        const float total = std::ceil(height + padding * 2.0f);

        // Below the widget it describes, not under the pointer. A tooltip that followed the
        // pointer covers whatever is beneath it and moves while being read; anchored to the
        // control, it sits still and never hides the thing it is explaining.
        float x = std::round(request.anchor.left());
        float y = std::round(request.anchor.bottom() + metricOf(theme, StudioMetric::SpacingXSmall));

        // Clamped into the window, and flipped above the widget rather than clipped when there is
        // no room below -- a tooltip cut in half by the window edge says nothing.
        x = std::min(x, std::max(0.0f, layout_.window.right() - width));
        x = std::max(x, layout_.window.left());
        if (y + total > layout_.window.bottom())
        {
            y = std::round(request.anchor.top() - total
                           - metricOf(theme, StudioMetric::SpacingXSmall));
        }
        y = std::max(y, layout_.window.top());

        const UiRect box{x, y, width, total};

        frame_.pushLayer(kTooltipLayer);
        frame_.drawList().fillRoundedRect(box, theme.color(StudioColorRole::TooltipBackground),
                                          metricOf(theme, StudioMetric::CornerRadius));
        frame_.drawList().strokeRect(box, theme.color(StudioColorRole::Border),
                                     metricOf(theme, StudioMetric::BorderWidth));

        UiRect cursor = box.inset(UiEdges{padding});
        for (std::size_t i = 0; i < lines.size(); ++i)
        {
            const UiRect row = cursor.splitTop(lineHeight);
            studioDrawText(frame_, row, lines[i], StudioFontRole::BodySmall,
                           theme.color(i == 0 ? StudioColorRole::TextPrimary
                                              : StudioColorRole::TextSecondary));
        }
        frame_.popLayer();
    }

    void StudioShell::describeMenuBar()
    {
        if (layout_.menuBar.isEmpty() || menuTitles_.empty()) { return; }

        const StudioTheme& theme = frame_.theme();
        const bool raised = isMenuOpen();
        if (raised) { frame_.pushLayer(kMenuLayer); }

        if (frame_.isDrawPass())
        {
            frame_.drawList().fillRect(layout_.menuBar, theme.color(StudioColorRole::PanelHeader));
            frame_.drawList().drawHorizontalSeparator(
                UiRect{layout_.menuBar.left(), layout_.menuBar.bottom(), layout_.menuBar.width, 0.0f},
                theme.color(StudioColorRole::Separator),
                metricOf(theme, StudioMetric::SeparatorThickness));
        }

        frame_.ids().push("menubar");
        for (std::size_t i = 0; i < menuTitles_.size() && i < menus_.size(); ++i)
        {
            const StudioMenuDefinition& menu = menus_[i];
            const WidgetId id = frame_.ids().make(menu.title);
            const auto index = static_cast<int>(i);

            // A menu with nothing in it is disabled rather than opening an empty box. It is a
            // placeholder for work not done, and saying so is more useful than pretending.
            const StudioWidgetResult result = studioMenuBarItem(
                frame_, id, menuTitles_[i].bounds, menu.title, openMenu_ == index,
                !menu.entries.empty());

            if (!frame_.isInputPass() || menu.entries.empty()) { continue; }

            if (result.interaction.pressed)
            {
                if (openMenu_ == index) { setOpenMenu(-1); }
                else
                {
                    openMenuAt(index);
                    // The title took the mouse on its press. Handing it straight back is what
                    // makes press-drag-release reach the items: while one widget holds capture no
                    // other can be hovered, so a held title would swallow the whole gesture.
                    frame_.router().releaseCapture();
                }
            }
            else if (isMenuOpen() && openMenu_ != index && result.interaction.hovered)
            {
                // Switching menus by moving across the bar, with no second click. Every desktop
                // menu bar does this and its absence is noticed within seconds.
                openMenuAt(index);
            }
            else if (!isMenuOpen() && result.activated && !result.interaction.clicked
                     && !keyboardConsumed_)
            {
                // Keyboard only: Space or Enter on the focused title opens it. The two guards are
                // both load-bearing. Without `clicked`, the release that completes the click
                // *closing* a menu reads as an activation and reopens it on the next frame, so a
                // menu could never be dismissed by clicking its own title. Without
                // `keyboardConsumed_`, the Enter that chose a row reopens the menu the row just
                // closed -- the title still holds focus, because focus returning there is what
                // makes Escape leave the keyboard somewhere sensible.
                openMenuAt(index);
            }
        }
        frame_.ids().pop();

        if (raised) { frame_.popLayer(); }
    }

    void StudioShell::describeMenuPopup()
    {
        if (!isPopupOpen() || menuLevels_.empty()) { return; }

        const StudioTheme& theme = frame_.theme();
        StudioInputRouter& router = frame_.router();
        const float pointerX = router.mouseX();
        const float pointerY = router.mouseY();

        // Which popup the pointer is actually in. Popups overlap -- a submenu flipped to the left
        // sits on top of its parent -- and without this every row under the pointer would light up
        // and a click would reach the one underneath. The deepest wins, because it is the one
        // drawn last and therefore the one the user sees.
        int deepestUnderPointer = -1;
        for (std::size_t level = 0; level < menuLevels_.size(); ++level)
        {
            if (menuLevels_[level].popup.contains(pointerX, pointerY))
            {
                deepestUnderPointer = static_cast<int>(level);
            }
        }

        bool activatedThisPass = false;
        int hoveredLevel = -1;
        int hoveredRow = -1;

        frame_.pushLayer(kMenuLayer);
        frame_.ids().push(contextOpen_ ? "context" : "menu");
        frame_.ids().pushIndex(openMenu_);

        for (std::size_t level = 0; level < menuLevels_.size(); ++level)
        {
            const MenuLevel& popup = menuLevels_[level];
            if (popup.popup.isEmpty()) { continue; }

            frame_.pushClip(popup.popup);
            if (frame_.isDrawPass())
            {
                frame_.drawList().fillRect(popup.popup,
                                           theme.color(StudioColorRole::PopupBackground));
                frame_.drawList().strokeRect(popup.popup,
                                             theme.color(StudioColorRole::BorderStrong),
                                             metricOf(theme, StudioMetric::BorderWidth));
            }

            frame_.ids().pushIndex(static_cast<int>(level));
            for (std::size_t i = 0; i < popup.rows.size(); ++i)
            {
                const MenuRowGeometry& row = popup.rows[i];
                if (row.separator)
                {
                    studioMenuSeparator(frame_, row.bounds);
                    continue;
                }

                const StudioAction* action = row.submenu ? nullptr : actions_.find(row.id);
                const std::string shortcut = action != nullptr
                    ? describeStudioShortcut(action->shortcut) : std::string{};

                StudioMenuItemOptions options;
                options.enabled = row.enabled;
                options.checkable = action != nullptr && action->checkable;
                options.checked = options.checkable && actions_.isChecked(row.id);
                options.hasSubmenu = row.submenu;
                options.shortcut = shortcut;

                // A row whose submenu is open stays highlighted even when the pointer has moved
                // off it and into that submenu -- otherwise the trail back up the chain goes dark
                // and the user cannot see which rows they came through.
                const bool opensTheNextLevel = level < submenuPath_.size()
                    && submenuPath_[level] == static_cast<int>(i);
                options.highlighted = opensTheNextLevel
                    || (level < highlight_.size() && highlight_[level] == static_cast<int>(i));

                const std::string_view label = row.submenu
                    ? std::string_view{row.label}
                    : (action != nullptr ? std::string_view{action->label}
                                         : std::string_view{row.id});

                const StudioWidgetResult result =
                    studioMenuItem(frame_, frame_.ids().make(row.submenu ? row.label : row.id),
                                   row.bounds, label, options);

                if (!frame_.isInputPass()) { continue; }
                if (deepestUnderPointer != static_cast<int>(level)) { continue; }

                if (result.interaction.hovered)
                {
                    hoveredLevel = static_cast<int>(level);
                    hoveredRow = static_cast<int>(i);
                }
                if (result.activated && !activatedThisPass && !row.submenu)
                {
                    activatedThisPass = true;
                    invoke(row.id);
                }
                // Clicking a submenu row opens it rather than doing nothing, which is what a user
                // who has not learned that hovering is enough will try first.
                if (result.activated && row.submenu) { openSubmenu(level, static_cast<int>(i)); }
            }
            frame_.ids().pop();

            frame_.popClip();
        }

        frame_.ids().pop();
        frame_.ids().pop();
        frame_.popLayer();

        if (!frame_.isInputPass()) { return; }

        if (hoveredLevel >= 0)
        {
            // The pointer moves the highlight too, so that arrowing down and then reaching for the
            // mouse does not leave two rows looking chosen. Levels below the pointer keep theirs,
            // because they are the trail back up.
            const auto level = static_cast<std::size_t>(hoveredLevel);
            if (level < highlight_.size()) { highlight_[level] = hoveredRow; }
            updateHoverSubmenu(level, hoveredRow);
        }
        else if (deepestUnderPointer < 0)
        {
            submenuPendingRow_ = -1;
            submenuPendingSeconds_ = 0.0f;
        }

        if (activatedThisPass)
        {
            closePopup();
            return;
        }

        // A press that landed on neither the bar nor any open popup dismisses the menu without
        // activating anything -- the other half of "click elsewhere to cancel". A context menu has
        // no bar to spare, and either button dismisses it, because a right-click elsewhere is a
        // request for a *different* context menu.
        const bool pressedAway = router.mousePressed(UiMouseButton::Left)
                              || (contextOpen_ && router.mousePressed(UiMouseButton::Right));
        if (pressedAway && deepestUnderPointer < 0
            && (contextOpen_ || !layout_.menuBar.contains(pointerX, pointerY)))
        {
            closePopup();
        }
    }

    void StudioShell::updateHoverSubmenu(std::size_t level, int row)
    {
        if (row < 0 || level >= menuLevels_.size()) { return; }

        const MenuRowGeometry& hovered = menuLevels_[level].rows[static_cast<std::size_t>(row)];
        const int alreadyOpen = openSubmenuRow(level);

        // Nothing open at this level: a submenu opens the moment the pointer reaches its row, and
        // a row that is not one closes nothing, because there is nothing to close.
        if (alreadyOpen < 0)
        {
            submenuPendingRow_ = -1;
            submenuPendingSeconds_ = 0.0f;
            if (hovered.submenu && hovered.enabled) { openSubmenu(level, row); }
            return;
        }

        if (alreadyOpen == row)
        {
            submenuPendingRow_ = -1;
            submenuPendingSeconds_ = 0.0f;
            return;
        }

        // Something else is open at this level and the pointer has moved to a sibling row. It does
        // *not* switch immediately: the natural way to reach a submenu is to cut the corner
        // diagonally, which drags the pointer across one or two of the rows in between. Switching
        // on the first frame of that would slam the submenu shut halfway to it, and the user would
        // learn to travel in an L rather than trust the menu. So a sibling row has to hold the
        // pointer for a moment before it wins.
        if (submenuPendingRow_ != row || submenuPendingLevel_ != level)
        {
            submenuPendingRow_ = row;
            submenuPendingLevel_ = level;
            submenuPendingSeconds_ = 0.0f;
            return;
        }

        const float delta = frame_.input().deltaSeconds > 0.0f ? frame_.input().deltaSeconds
                                                               : 1.0f / 60.0f;
        submenuPendingSeconds_ += delta;
        if (submenuPendingSeconds_ < submenuSwitchDelay_) { return; }

        submenuPendingRow_ = -1;
        submenuPendingSeconds_ = 0.0f;
        openSubmenu(level, hovered.submenu && hovered.enabled ? row : -1);
    }

    void StudioShell::describeToolbar()
    {
        if (layout_.toolbar.isEmpty()) { return; }

        const StudioTheme& theme = frame_.theme();
        if (frame_.isDrawPass())
        {
            frame_.drawList().fillRect(layout_.toolbar,
                                       theme.color(StudioColorRole::PanelBackground));
            frame_.drawList().drawHorizontalSeparator(
                UiRect{layout_.toolbar.left(), layout_.toolbar.bottom(), layout_.toolbar.width, 0.0f},
                theme.color(StudioColorRole::Separator),
                metricOf(theme, StudioMetric::SeparatorThickness));
        }

        frame_.ids().push("toolbar");
        for (const ToolbarEntryGeometry& entry : toolbarEntries_)
        {
            if (entry.separator)
            {
                if (frame_.isDrawPass())
                {
                    const float inset = metricOf(theme, StudioMetric::SpacingSmall);
                    frame_.drawList().fillRect(
                        UiRect{entry.bounds.centerX(), entry.bounds.top() + inset,
                               metricOf(theme, StudioMetric::SeparatorThickness),
                               std::max(0.0f, entry.bounds.height - inset * 2.0f)},
                        theme.color(StudioColorRole::Separator));
                }
                continue;
            }

            const StudioAction* action = actions_.find(entry.id);

            StudioButtonOptions options;
            options.kind = StudioButtonKind::Toolbar;
            options.font = StudioFontRole::BodySmall;
            options.enabled = action != nullptr && actions_.isEnabled(entry.id);
            options.selected = action != nullptr && action->checkable && actions_.isChecked(entry.id);
            options.icon = studioIconForAction(entry.id);

            // Icon alone where there is one. The label still decides the button's identity and is
            // still what a tooltip and a screen reader read; a toolbar that spelled every command
            // out would be twice as wide and no clearer, and one that dropped the label from the
            // model to save the space would have nothing left to say about itself.
            options.iconOnly = options.icon != StudioIcon::None;

            const std::string_view label = action != nullptr ? std::string_view{action->label}
                                                             : std::string_view{entry.id};

            // The label, its shortcut, and a sentence of help. An icon-only toolbar is only
            // discoverable through this -- and a tooltip that merely repeated the word the button
            // would have shown would make the icons no more discoverable than before.
            std::string tooltip{label};
            if (action != nullptr)
            {
                const std::string chord = describeStudioShortcut(action->shortcut);
                if (!chord.empty()) { tooltip += "  (" + chord + ")"; }
                if (!action->description.empty()) { tooltip += "\n" + action->description; }
            }
            options.tooltip = tooltip;

            const StudioWidgetResult result =
                studioButton(frame_, frame_.ids().make(entry.id), entry.bounds, label, options);

            if (result.activated) { invoke(entry.id); }
        }
        frame_.ids().pop();
    }

    bool StudioShell::describePanelContent(std::string_view id, StudioFrame& frame,
                                           const UiRect& bounds)
    {
        const auto content = std::find_if(panelContent_.begin(), panelContent_.end(),
            [&](const auto& entry) { return entry.first == id; });
        if (content == panelContent_.end() || !content->second) { return false; }

        // The same scope and clip the dock gives it, so a panel described on its own is the same
        // panel: two widgets called "clear" in two panels must not share retained state here any
        // more than they do there.
        frame.ids().push(id);
        frame.pushClip(bounds);
        content->second(frame, bounds);
        frame.popClip();
        frame.ids().pop();
        return true;
    }

    void StudioShell::describeDocks()
    {
        const StudioTheme& theme = frame_.theme();
        const float tabHeight = tabStripHeight();

        frame_.ids().push("docks");
        for (const StudioDockNodeId leaf : dock_.leaves())
        {
            StudioDockNode& node = dock_.node(leaf);
            if (node.bounds.isEmpty()) { continue; }

            const StudioDockLeafGeometry geometry = dock_.leafGeometry(leaf, tabHeight);

            if (frame_.isDrawPass())
            {
                frame_.drawList().fillRect(geometry.tabStrip,
                                           theme.color(StudioColorRole::PanelHeader));
            }

            if (node.panels.empty())
            {
                // An empty leaf is not normally reachable -- removePanel collapses one -- but a
                // layout read from disk can contain one, and drawing nothing at all would leave a
                // hole in the window with no explanation.
                if (frame_.isDrawPass())
                {
                    frame_.drawList().fillRect(geometry.body,
                                               theme.color(StudioColorRole::PanelBackground));
                }
                continue;
            }

            frame_.ids().pushIndex(static_cast<std::int64_t>(leaf));
            describePanelGroup(node.panels, node.activePanel, geometry, /*inFloat=*/false);
            frame_.ids().pop();
        }
        frame_.ids().pop();
    }

    void StudioShell::describePanelGroup(std::vector<std::string>& panels,
                                         std::size_t& activePanel,
                                         const StudioDockLeafGeometry& geometry, bool inFloat)
    {
        const StudioTheme& theme = frame_.theme();

        frame_.pushClip(geometry.tabStrip);

        UiRect cursor = geometry.tabStrip;
        for (std::size_t i = 0; i < panels.size(); ++i)
        {
            if (cursor.width <= 0.0f) { break; }

            const std::string& panelId = panels[i];
            const StudioPanelDescriptor* descriptor = panel(panelId);
            const std::string_view title = descriptor != nullptr
                ? std::string_view{descriptor->title} : std::string_view{panelId};

            const float width = std::min(
                std::ceil(studioLabelWidth(frame_, title)
                          + metricOf(theme, StudioMetric::SpacingMedium)),
                cursor.width);

            StudioTabOptions options;
            options.active = activePanel == i;
            options.modified = descriptor != nullptr && descriptor->modified;

            const UiRect tab = cursor.splitLeft(width);
            const StudioWidgetResult result =
                studioTab(frame_, frame_.ids().make(panelId), tab, title, options);

            if (frame_.isInputPass()) { tabBounds_.emplace_back(panelId, tab); }
            if (result.activated) { activePanel = i; }

            // Right-click opens the tab's context menu. Routed from the tab's own rectangle
            // rather than from a hit test over the strip, so a right-click in the empty space
            // beside the last tab does nothing rather than acting on whichever panel happened
            // to be nearest.
            if (frame_.isInputPass()
                && frame_.router().mousePressed(UiMouseButton::Right)
                && tab.contains(frame_.input().mouseX, frame_.input().mouseY))
            {
                // Selected first: a context menu that acted on a tab the user could not see
                // was chosen would be acting behind their back.
                activePanel = i;
                openContextMenu(tabContextMenu(panelId), frame_.input().mouseX,
                                frame_.input().mouseY);
            }

            // A tab held and dragged beyond a threshold starts a dock drag. The threshold is
            // what keeps a click that wobbled by a pixel from becoming a rearrangement --
            // without it, selecting a tab on a trackpad would move panels around.
            if (frame_.isInputPass() && result.interaction.held && !drag_.active())
            {
                const float dx = frame_.input().mouseX - tab.centerX();
                const float dy = frame_.input().mouseY - tab.centerY();
                const float threshold = metricOf(theme, StudioMetric::SpacingLarge);
                if (dx * dx + dy * dy > threshold * threshold) { drag_.panelId = panelId; }
            }
        }

        frame_.popClip();

        if (frame_.isDrawPass())
        {
            frame_.drawList().drawHorizontalSeparator(
                UiRect{geometry.tabStrip.left(), geometry.tabStrip.bottom(),
                       geometry.tabStrip.width, 0.0f},
                theme.color(StudioColorRole::Separator),
                metricOf(theme, StudioMetric::SeparatorThickness));
        }

        if (panels.empty()) { return; }

        const std::string& active = panels[std::min(activePanel, panels.size() - 1)];
        const StudioPanelDescriptor* descriptor = panel(active);
        // The viewport's surface is drawn by the shell rather than by its content, because
        // the scene is a texture somebody else rendered and the placeholder is the shell's
        // own. Its content still runs, underneath nothing and over that surface: navigating
        // and picking are ordinary panel behaviour and belong with the other panels' content
        // rather than in a second seam of their own.
        //
        // Not in a float, though: the viewport surface is composited from a texture the host
        // rendered at the docked viewport's size, and stretching it into a floating window would
        // photograph the scene at the wrong aspect. A floated viewport draws as an ordinary panel
        // until the host can be asked for a second surface.
        const bool isViewport = !inFloat && descriptor != nullptr && descriptor->isViewport;
        if (isViewport)
        {
            if (frame_.isDrawPass()) { describeViewportBody(geometry.body); }
        }
        else if (frame_.isDrawPass())
        {
            frame_.drawList().fillRect(geometry.body,
                                       theme.color(StudioColorRole::PanelBackground));
        }

        // The strangler seam. A panel with content described here is ported; one without is
        // the empty surface every panel starts as, and the ImGui implementation keeps drawing
        // it until it is deleted (Phase 7).
        const auto content = std::find_if(panelContent_.begin(), panelContent_.end(),
            [&](const auto& entry) { return entry.first == active; });
        if (content == panelContent_.end() || !content->second) { return; }

        // The panel's own id scope, so two panels can each have a widget called "clear"
        // without the two sharing retained state, focus or capture.
        frame_.ids().push(active);
        frame_.pushClip(geometry.body);
        content->second(frame_, geometry.body);
        frame_.popClip();
        frame_.ids().pop();
    }

    void StudioShell::describeFloating()
    {
        if (dock_.floating().empty()) { return; }

        const StudioTheme& theme = frame_.theme();
        const float tabHeight = tabStripHeight();
        const float border = metricOf(theme, StudioMetric::SeparatorThickness);

        // Raised so floats draw over the docked workspace. The *input* side of this layer is
        // opened by renderFrame only while the pointer is over a float, because the router's
        // layers are modal rather than a z-order -- see kFloatingLayer.
        frame_.pushLayer(kFloatingLayer);
        frame_.ids().push("floating");

        // Back to front: the last float is the one on top, so it is described last and wins hover
        // against the ones it overlaps.
        for (std::size_t i = 0; i < dock_.floating().size(); ++i)
        {
            StudioFloatingDock& window = dock_.floatingAt(i);
            if (window.bounds.isEmpty() || window.panels.empty()) { continue; }

            frame_.ids().pushIndex(static_cast<std::int64_t>(i));

            if (frame_.isDrawPass())
            {
                // An outline, because a float over a panel of the same colour is otherwise
                // indistinguishable from a rectangle of that panel -- the border is the only thing
                // that says "this is a window".
                frame_.drawList().fillRect(window.bounds,
                                           theme.color(StudioColorRole::PanelBackground));
                // The front-most window is outlined in the accent colour and the rest in the
                // ordinary border. Which window a keystroke or a close command would reach is not
                // something a user should have to remember having clicked.
                const bool front = i + 1 == dock_.floating().size();
                frame_.drawList().strokeRect(
                    window.bounds,
                    theme.color(front ? StudioColorRole::Accent : StudioColorRole::Separator),
                    border * (front ? 2.0f : 1.0f));
            }

            UiRect inner = window.bounds.inset(UiEdges{border, border});
            StudioDockLeafGeometry geometry;
            geometry.tabStrip = inner.splitTop(std::min(tabHeight, inner.height));
            geometry.body = inner;

            if (frame_.isDrawPass())
            {
                frame_.drawList().fillRect(geometry.tabStrip,
                                           theme.color(StudioColorRole::PanelHeader));
            }

            // The close button first, so the strip the window is dragged by excludes it: a title
            // bar that also closes the window when grabbed at one end is a trap.
            UiRect strip = geometry.tabStrip;
            const UiRect closeBox =
                strip.splitRight(std::min(strip.width, tabHeight))
                     .inset(UiEdges{metricOf(theme, StudioMetric::SpacingXSmall),
                                    metricOf(theme, StudioMetric::SpacingXSmall)});
            {
                StudioButtonOptions options;
                options.icon = StudioIcon::Close;
                options.iconOnly = true;
                options.tooltip = "Close this window";
                if (studioButton(frame_, frame_.ids().make("close"), closeBox, "Close", options)
                        .activated)
                {
                    // Every panel in the group, not only the showing tab: the button closes the
                    // window, and leaving its other tabs open somewhere invisible would be a
                    // workspace the user cannot account for.
                    const std::vector<std::string> closing = window.panels;
                    for (const std::string& panelId : closing) { (void)closePanel(panelId); }
                    frame_.ids().pop();
                    continue;
                }
            }

            describePanelGroup(window.panels, window.activePanel,
                               StudioDockLeafGeometry{strip, geometry.body}, /*inFloat=*/true);

            // With this window rather than after every window: the handles are part of its chrome,
            // and describing all of them at the end would draw a window behind's resize grip on
            // top of the window in front of it.
            describeFloatingHandles(i);

            frame_.ids().pop();
        }

        applyFloatingGesture();

        frame_.ids().pop();
        frame_.popLayer();
    }

    void StudioShell::describeFloatingHandles(std::size_t index)
    {
        const StudioTheme& theme = frame_.theme();
        const float tabHeight = tabStripHeight();
        const float grip = metricOf(theme, StudioMetric::SpacingLarge);

        StudioFloatingDock& window = dock_.floatingAt(index);
        if (window.bounds.isEmpty()) { return; }

        // What is left of the strip once the tabs and the close button have taken their share is
        // the title bar. Dragging a *tab* moves the panel; dragging the space beside them moves the
        // window -- which is the distinction every editor with floating panels makes, and the
        // reason a float does not need a second bar above its tabs.
        UiRect strip = window.bounds;
        strip.height = std::min(tabHeight, window.bounds.height);
        float tabsEnd = strip.left();
        for (const std::string& panelId : window.panels)
        {
            const UiRect tab = panelTabBounds(panelId);
            if (!tab.isEmpty()) { tabsEnd = std::max(tabsEnd, tab.right()); }
        }
        const UiRect title{tabsEnd, strip.top(),
                           std::max(0.0f, strip.right() - tabHeight - tabsEnd), strip.height};

        const StudioInteraction moved =
            frame_.router().interact(frame_.ids().make("title"), title, /*enabled=*/true);
        if (frame_.isInputPass() && moved.held && movingFloat_ == kInvalidFloatingDock)
        {
            // The window's own origin at the press, so every later frame measures from the press
            // point rather than accumulating a per-frame delta that rounds away.
            movingFloatX_ = window.x;
            movingFloatY_ = window.y;
            movingFloat_ = dock_.raiseFloating(index);
        }
        if (moved.hovered || movingFloat_ == index)
        {
            (void)frame_.requestCursor(frame_.ids().make("title"), StudioCursor::Move);
        }

        const UiRect gripBox{window.bounds.right() - grip, window.bounds.bottom() - grip,
                             grip, grip};
        const StudioInteraction resized =
            frame_.router().interact(frame_.ids().make("grip"), gripBox, /*enabled=*/true);
        if (frame_.isInputPass() && resized.held && resizingFloat_ == kInvalidFloatingDock)
        {
            resizingFloat_ = index;
            resizingFloatWidth_ = window.width;
            resizingFloatHeight_ = window.height;
        }
        if (resized.hovered || resizingFloat_ == index)
        {
            (void)frame_.requestCursor(frame_.ids().make("grip"), StudioCursor::ResizeNwSe);
        }
        if (frame_.isDrawPass())
        {
            // Two short rules stepping in from the corner, which is what a resize grip looks like
            // everywhere -- a box around the corner reads as a button that does nothing.
            const float thickness = metricOf(theme, StudioMetric::SeparatorThickness);
            const StudioColor line = theme.color(resized.hovered ? StudioColorRole::Accent
                                                                 : StudioColorRole::TextSecondary);

            // Inside the window's own border, not under it. The front-most float draws its outline
            // at double thickness, which is exactly enough to swallow a grip flush with the edge --
            // and a grip nobody can see is a window nobody can resize.
            const float edge = thickness * 3.0f;
            const float right = gripBox.right() - edge;
            const float bottom = gripBox.bottom() - edge;

            // Two corners nested inside each other rather than two rules along the same edge:
            // collinear ones merge into a single thicker line, which reads as a border that has
            // gone wrong rather than as something to pull.
            for (int step = 0; step < 2; ++step)
            {
                const float offset = static_cast<float>(step) * thickness * 3.0f;
                const float length = grip * 0.6f - offset;
                if (length <= thickness) { continue; }
                frame_.drawList().fillRect(
                    UiRect{right - length, bottom - thickness - offset, length, thickness}, line);
                frame_.drawList().fillRect(
                    UiRect{right - thickness - offset, bottom - length, thickness, length}, line);
            }
        }
    }

    void StudioShell::applyFloatingGesture()
    {
        if (!frame_.isInputPass()) { return; }

        const bool down = frame_.input().isMouseDown(UiMouseButton::Left);
        const float dx = frame_.input().mouseX - frame_.router().pressX();
        const float dy = frame_.input().mouseY - frame_.router().pressY();

        if (movingFloat_ != kInvalidFloatingDock)
        {
            if (!down || movingFloat_ >= dock_.floating().size())
            {
                movingFloat_ = kInvalidFloatingDock;
            }
            else
            {
                StudioFloatingDock& window = dock_.floatingAt(movingFloat_);
                window.x = movingFloatX_ + dx;
                window.y = movingFloatY_ + dy;
            }
        }

        if (resizingFloat_ != kInvalidFloatingDock)
        {
            if (!down || resizingFloat_ >= dock_.floating().size())
            {
                resizingFloat_ = kInvalidFloatingDock;
            }
            else
            {
                StudioFloatingDock& window = dock_.floatingAt(resizingFloat_);
                window.width = std::max(kMinimumFloatingExtent, resizingFloatWidth_ + dx);
                window.height = std::max(kMinimumFloatingExtent, resizingFloatHeight_ + dy);
            }
        }
    }

    void StudioShell::describeDockDrag()
    {
        if (!drag_.active()) { return; }

        const StudioTheme& theme = frame_.theme();

        // Resolved in the input pass and replayed in the draw pass, like every interaction here:
        // the preview a user sees must be the outcome a release would produce, and recomputing it
        // from a pointer that has since moved is how the two come apart.
        if (frame_.isInputPass())
        {
            resolveDropTarget();

            if (!frame_.input().isMouseDown(UiMouseButton::Left))
            {
                applyDrop();
                drag_ = StudioDockDrag{};
                return;
            }
        }

        if (!frame_.isDrawPass() || drag_.zone == StudioDropZone::None) { return; }

        // Drawn in a raised layer so it is over the panels it describes. Translucent rather than
        // an outline: an outline on a busy panel is hard to see, and the point of the preview is
        // that the answer to "where will this land" is obvious without reading anything.
        frame_.pushLayer(kDockPreviewLayer);
        StudioColor fill = theme.color(StudioColorRole::Accent);
        fill.a = 90;
        frame_.drawList().fillRect(drag_.preview, fill);
        frame_.drawList().strokeRect(drag_.preview, theme.color(StudioColorRole::Accent),
                                     metricOf(theme, StudioMetric::FocusRingWidth));
        frame_.popLayer();
    }

    void StudioShell::resolveDropTarget()
    {
        drag_.target = kInvalidDockNode;
        drag_.targetFloat = kInvalidFloatingDock;
        drag_.zone = StudioDropZone::None;
        drag_.tabIndex = 0;
        drag_.preview = UiRect{};

        const float x = frame_.input().mouseX;
        const float y = frame_.input().mouseY;

        // Floats first, because they are over the workspace: a leaf under a floating window is not
        // the thing the user is aiming at, however much the hit test on the tree agrees it is there.
        const std::size_t window = dock_.floatingAt(x, y);
        if (window != kInvalidFloatingDock)
        {
            const StudioFloatingDock& target = dock_.floating()[window];
            drag_.targetFloat = window;
            drag_.zone = StudioDropZone::Tabs;
            drag_.preview = UiRect{target.bounds.left(), target.bounds.top(), target.bounds.width,
                                   std::min(tabStripHeight(), target.bounds.height)};

            drag_.tabIndex = target.panels.size();
            for (std::size_t i = 0; i < target.panels.size(); ++i)
            {
                const UiRect tab = panelTabBounds(target.panels[i]);
                if (tab.isEmpty()) { continue; }
                if (x < tab.centerX()) { drag_.tabIndex = i; break; }
            }
            return;
        }

        const StudioDockNodeId leaf = dock_.leafAt(x, y);
        if (leaf == kInvalidDockNode)
        {
            // Nowhere to dock: the menu bar, the status bar, the toolbar, or off the window
            // entirely. That is the undock gesture, and it is the whole reason it needs no command
            // of its own -- a user who has dragged a tab across the workspace already knows it.
            drag_.zone = StudioDropZone::Float;
            const float width = std::min(kStudioFloatDropWidth * frame_.theme().scale(),
                                         layout_.dockArea.width);
            const float height = std::min(kStudioFloatDropHeight * frame_.theme().scale(),
                                          layout_.dockArea.height);

            // Clamped into the workspace here rather than only when the window is placed, because
            // the preview is a promise: releasing over the menu bar puts the window at the top of
            // the workspace, and a preview drawn over the menu bar would have promised otherwise.
            const float left = std::clamp(x - width * 0.5f, layout_.dockArea.left(),
                                          std::max(layout_.dockArea.left(),
                                                   layout_.dockArea.right() - width));
            const float top = std::clamp(y - tabStripHeight() * 0.5f, layout_.dockArea.top(),
                                         std::max(layout_.dockArea.top(),
                                                  layout_.dockArea.bottom() - height));
            drag_.preview = UiRect{std::round(left), std::round(top), width, height};
            return;
        }

        drag_.target = leaf;

        const StudioDockLeafGeometry geometry = dock_.leafGeometry(leaf, tabStripHeight());

        // Over the tab strip means "put it in this group, here" -- which is also what makes
        // reordering within one group fall out of the same gesture rather than needing its own.
        if (geometry.tabStrip.contains(x, y))
        {
            drag_.zone = StudioDropZone::Tabs;
            drag_.preview = geometry.tabStrip;

            const StudioDockNode& node = dock_.node(leaf);
            drag_.tabIndex = node.panels.size();
            for (std::size_t i = 0; i < node.panels.size(); ++i)
            {
                const UiRect tab = panelTabBounds(node.panels[i]);
                if (tab.isEmpty()) { continue; }
                if (x < tab.centerX()) { drag_.tabIndex = i; break; }
            }
            return;
        }

        const UiRect& body = geometry.body;
        if (body.isEmpty()) { return; }

        // A quarter of the body on each edge, the middle for tabs. Proportional rather than a
        // fixed band, so the zones stay reachable in a narrow panel -- a fixed 64 pixels would
        // leave a 100-pixel-wide dock with no middle at all.
        const float edgeX = body.width * 0.25f;
        const float edgeY = body.height * 0.25f;

        if (x < body.left() + edgeX)
        {
            drag_.zone = StudioDropZone::Left;
            drag_.preview = UiRect{body.left(), body.top(), body.width * 0.5f, body.height};
        }
        else if (x > body.right() - edgeX)
        {
            drag_.zone = StudioDropZone::Right;
            drag_.preview = UiRect{body.centerX(), body.top(), body.width * 0.5f, body.height};
        }
        else if (y < body.top() + edgeY)
        {
            drag_.zone = StudioDropZone::Top;
            drag_.preview = UiRect{body.left(), body.top(), body.width, body.height * 0.5f};
        }
        else if (y > body.bottom() - edgeY)
        {
            drag_.zone = StudioDropZone::Bottom;
            drag_.preview = UiRect{body.left(), body.centerY(), body.width, body.height * 0.5f};
        }
        else
        {
            drag_.zone = StudioDropZone::Tabs;
            drag_.tabIndex = dock_.node(leaf).panels.size();
            drag_.preview = geometry.tabStrip;
        }
    }

    void StudioShell::applyDrop()
    {
        if (drag_.zone == StudioDropZone::None) { return; }

        if (drag_.zone == StudioDropZone::Float)
        {
            // Relative to the workspace, because that is what a float's geometry is measured
            // against -- the preview is already in window coordinates, so the difference is the
            // workspace's own origin and forgetting it would drop the window under the menu bar.
            (void)dock_.floatPanel(drag_.panelId, drag_.preview.left() - layout_.dockArea.left(),
                                   drag_.preview.top() - layout_.dockArea.top(),
                                   drag_.preview.width, drag_.preview.height);
            (void)activatePanel(drag_.panelId);
            return;
        }

        if (drag_.targetFloat != kInvalidFloatingDock)
        {
            (void)dock_.movePanelToFloating(drag_.panelId, drag_.targetFloat, drag_.tabIndex);
            (void)activatePanel(drag_.panelId);
            return;
        }

        if (drag_.target == kInvalidDockNode) { return; }

        if (drag_.zone == StudioDropZone::Tabs)
        {
            (void)dock_.movePanel(drag_.panelId, drag_.target, drag_.tabIndex);
            (void)activatePanel(drag_.panelId);
            return;
        }

        // Splitting the leaf the panel is being dropped on, then moving the panel into the half
        // the user aimed at. split() reuses the split node's id, so the content that was there
        // ends up at a new id -- sibling() is how the new half is found, and getting that backwards
        // would drop the panel where the existing content went.
        const StudioDockSide side = drag_.zone == StudioDropZone::Left  ? StudioDockSide::Left
                                  : drag_.zone == StudioDropZone::Right ? StudioDockSide::Right
                                  : drag_.zone == StudioDropZone::Top   ? StudioDockSide::Top
                                                                        : StudioDockSide::Bottom;

        const StudioDockNodeId created = dock_.split(drag_.target, side, 0.4f);
        if (created == kInvalidDockNode) { return; }

        (void)dock_.movePanel(drag_.panelId, created);
        (void)activatePanel(drag_.panelId);
    }

    void StudioShell::describeSplitters()
    {
        const StudioTheme& theme = frame_.theme();
        const float minimum = kStudioMinimumDockExtent * theme.scale();

        frame_.ids().push("splitters");
        for (const StudioDockNodeId id : dock_.splits())
        {
            const StudioDockNode& node = dock_.node(id);
            if (node.splitter.isEmpty()) { continue; }

            const StudioSplitterAxis axis = node.orientation == StudioDockOrientation::Horizontal
                ? StudioSplitterAxis::Horizontal
                : StudioSplitterAxis::Vertical;

            const StudioSplitterResult result = studioSplitter(
                frame_, frame_.ids().makeIndex(static_cast<std::int64_t>(id)), node.splitter, axis);

            if (result.delta != 0.0f) { dock_.moveSplitter(id, result.delta, minimum); }
        }
        frame_.ids().pop();
    }

    void StudioShell::describeViewportBody(const UiRect& body)
    {
        if (!frame_.isDrawPass() || body.isEmpty()) { return; }

        const StudioTheme& theme = frame_.theme();
        StudioDrawList& list = frame_.drawList();

        // The scene, when somebody has rendered one. Whoever owns the graphics device draws it
        // into a target and hands the texture over; the shell composites it and knows nothing else
        // about it -- not the renderer, not the camera, not what is in it.
        if (viewportImage_ != kUiTextureNone)
        {
            list.drawImage(body, viewportImage_, viewportImageFlipped_);
            return;
        }

        // Darker than the panels, so the viewport reads as a window into the scene rather than as
        // another panel. The grid is shell furniture; the real scene arrives with STUDIO-07009.
        list.fillRect(body, theme.color(StudioColorRole::ViewportBackground));

        const float spacing = 32.0f * theme.scale();
        const StudioColor minor = theme.color(StudioColorRole::ViewportGrid);
        const StudioColor major = theme.color(StudioColorRole::ViewportGridMajor);

        frame_.pushClip(body);
        int line = 0;
        for (float x = body.left(); x < body.right(); x += spacing, ++line)
        {
            list.drawLine(x, body.top(), x, body.bottom(), (line % 4 == 0) ? major : minor, 1.0f);
        }
        line = 0;
        for (float y = body.top(); y < body.bottom(); y += spacing, ++line)
        {
            list.drawLine(body.left(), y, body.right(), y, (line % 4 == 0) ? major : minor, 1.0f);
        }
        frame_.popClip();
    }

    namespace
    {
        /** @brief The theme token a notification's stripe and its severity are drawn in. */
        StudioColorRole severityColor(StudioNotificationSeverity severity)
        {
            switch (severity)
            {
                case StudioNotificationSeverity::Info: return StudioColorRole::Info;
                case StudioNotificationSeverity::Success: return StudioColorRole::Success;
                case StudioNotificationSeverity::Warning: return StudioColorRole::Warning;
                case StudioNotificationSeverity::Error: return StudioColorRole::Error;
            }
            return StudioColorRole::Info;
        }

        /** @brief The smallest rectangle containing both. */
        UiRect unite(const UiRect& a, const UiRect& b)
        {
            const float left = std::min(a.x, b.x);
            const float top = std::min(a.y, b.y);
            return UiRect{left, top, std::max(a.right(), b.right()) - left,
                          std::max(a.bottom(), b.bottom()) - top};
        }
    }

    void StudioShell::describeToasts()
    {
        const std::vector<StudioNotification> showing = notifications_.showing();
        if (showing.empty())
        {
            toastBounds_ = UiRect{};
            return;
        }

        const StudioTheme& theme = frame_.theme();
        const float pad = metricOf(theme, StudioMetric::SpacingMedium);
        const float gap = metricOf(theme, StudioMetric::SpacingSmall);
        const float border = metricOf(theme, StudioMetric::SeparatorThickness);
        const float lineHeight = metricOf(theme, StudioMetric::ControlHeight);
        const float buttonHeight = std::max(0.0f, lineHeight - gap);

        // Wide enough for a sentence and narrow enough to leave the editor visible behind it. A
        // toast that took a third of the window would be a dialog that nobody agreed to open.
        const float width = std::min(360.0f * theme.scale(),
                                     std::max(0.0f, layout_.dockArea.width - pad * 2.0f));
        if (width <= 0.0f)
        {
            toastBounds_ = UiRect{};
            return;
        }

        // Bottom right, above the status bar. The corner furthest from what the user is working on
        // in a left-to-right editor, and the one a docked panel is least likely to need.
        float bottom = layout_.dockArea.bottom() - pad;
        const float right = layout_.dockArea.right() - pad;

        frame_.pushLayer(kToastLayer);
        frame_.ids().push("toasts");

        UiRect stack{};

        // Newest nearest the corner, so the one that just arrived is where the eye already is and
        // the older ones rise away from it.
        for (std::size_t index = showing.size(); index-- > 0;)
        {
            const StudioNotification& notification = showing[index];

            const bool hasDetail = !notification.detail.empty();
            const bool hasAction = !notification.actionId.empty()
                                && actions_.find(notification.actionId) != nullptr;

            float height = pad * 2.0f + lineHeight + (hasDetail ? lineHeight : 0.0f);
            if (hasAction) { height += buttonHeight + gap; }

            if (bottom - height < layout_.dockArea.y) { break; }

            const UiRect bounds{right - width, bottom - height, width, height};
            bottom = bounds.y - gap;
            stack = stack.isEmpty() ? bounds : unite(stack, bounds);

            frame_.ids().pushIndex(static_cast<std::int64_t>(index));

            const StudioColor accent = theme.color(severityColor(notification.severity));

            if (frame_.isDrawPass())
            {
                frame_.drawList().fillRect(bounds, theme.color(StudioColorRole::PopupBackground));
                frame_.drawList().strokeRect(bounds, theme.color(StudioColorRole::BorderStrong),
                                             border);

                // A stripe down the leading edge rather than a coloured surface. Severity has to
                // be readable at a glance and the text has to stay legible, and a red panel makes
                // one of those two impossible.
                frame_.drawList().fillRect(
                    UiRect{bounds.x, bounds.y, border * 3.0f, bounds.height}, accent);
            }

            UiRect body = bounds.inset(UiEdges{pad + border * 3.0f, pad, pad, pad});

            // The dismiss sits in the title's line, at its end: a toast the user cannot get rid of
            // is a toast that has taken a corner of the editor away from them.
            UiRect titleRow = body.splitTop(lineHeight);
            const UiRect dismiss = titleRow.splitRight(std::min(titleRow.width, lineHeight));

            if (frame_.isDrawPass())
            {
                studioDrawText(frame_, titleRow,
                               studioTruncateText(frame_, theme.font(StudioFontRole::Subheading),
                                                  notification.title, titleRow.width),
                               StudioFontRole::Subheading,
                               theme.color(StudioColorRole::TextPrimary));
            }

            StudioButtonOptions dismissOptions;
            dismissOptions.kind = StudioButtonKind::Toolbar;
            dismissOptions.icon = StudioIcon::Close;
            dismissOptions.tooltip = "Dismiss";
            if (studioButton(frame_, frame_.ids().make("dismiss"), dismiss, {}, dismissOptions)
                    .activated)
            {
                notifications_.dismissAt(index);
            }

            if (hasDetail && frame_.isDrawPass())
            {
                const UiRect detailRow = body.splitTop(lineHeight);
                studioDrawText(frame_, detailRow,
                               studioTruncateText(frame_, theme.font(StudioFontRole::Body),
                                                  notification.detail, detailRow.width),
                               StudioFontRole::Body, theme.color(StudioColorRole::TextSecondary));
            }
            else if (hasDetail) { body.splitTop(lineHeight); }

            if (hasAction)
            {
                body.splitTop(gap);

                const StudioAction* action = actions_.find(notification.actionId);
                const std::string label = !notification.actionLabel.empty()
                    ? notification.actionLabel
                    : (action != nullptr ? action->label : std::string{});

                const UiRect button = body.splitTop(buttonHeight).splitRight(
                    std::min(body.width, std::ceil(studioLabelWidth(frame_, label)) + pad * 2.0f));

                StudioButtonOptions options;
                options.kind = StudioButtonKind::Accent;
                if (studioButton(frame_, frame_.ids().make("action"), button, label, options)
                        .activated)
                {
                    // Dismissed by taking it. The user has answered, and a toast still offering an
                    // answer that has been given reads as though nothing happened.
                    invoke(notification.actionId);
                    notifications_.dismissAt(index);
                }
            }

            frame_.ids().pop();
        }

        // And what did not fit, said rather than silently dropped: a stack that quietly stopped
        // growing would hide the newest failure behind the four before it.
        const std::size_t hidden = notifications_.hiddenCount();
        if (hidden > 0 && frame_.isDrawPass() && !stack.isEmpty())
        {
            const UiRect line{stack.x, stack.y - lineHeight, stack.width, lineHeight};
            if (line.y >= layout_.dockArea.y)
            {
                studioDrawText(frame_, line,
                               "and " + std::to_string(hidden) + " more in the Output Log",
                               StudioFontRole::Body, theme.color(StudioColorRole::TextSecondary),
                               StudioTextAlign::Right);
                stack = unite(stack, line);
            }
        }

        frame_.ids().pop();
        frame_.popLayer();

        toastBounds_ = stack;
    }

    void StudioShell::describeStatusBar()
    {
        if (layout_.statusBar.isEmpty()) { return; }

        const StudioTheme& theme = frame_.theme();
        const float spacing = metricOf(theme, StudioMetric::SpacingMedium);
        const float gap = metricOf(theme, StudioMetric::SpacingSmall);

        if (frame_.isDrawPass())
        {
            frame_.drawList().fillRect(layout_.statusBar,
                                       theme.color(StudioColorRole::PanelHeader));
            frame_.drawList().drawHorizontalSeparator(
                UiRect{layout_.statusBar.left(), layout_.statusBar.top(),
                       layout_.statusBar.width, 0.0f},
                theme.color(StudioColorRole::Separator),
                metricOf(theme, StudioMetric::SeparatorThickness));
        }

        UiRect inner = layout_.statusBar.inset(UiEdges{spacing, 0.0f});
        if (inner.isEmpty()) { return; }

        frame_.ids().push("status");

        // Right to left, because the right-hand facts are fixed-width answers and the left-hand
        // message is whatever the project is called: laid out the other way, a long project name
        // would push the renderer off the end of the bar.
        const auto sayRight = [&](const std::string& text, StudioColorRole role) {
            if (text.empty()) { return; }
            const float width = std::min(inner.width, std::ceil(studioLabelWidth(
                frame_, text, StudioFontRole::BodySmall)));
            const UiRect box = inner.splitRight(width);
            if (frame_.isDrawPass())
            {
                studioDrawText(frame_, box, text, StudioFontRole::BodySmall, theme.color(role),
                               StudioTextAlign::Right);
            }
            inner.splitRight(spacing);
        };

        sayRight(status_.renderer.empty() ? std::string{} : "Renderer: " + status_.renderer,
                 StudioColorRole::TextSecondary);
        // The target this project *ships* on, which is not the renderer Studio is drawing with --
        // conflating the two is exactly the mistake that makes somebody test on the wrong backend.
        sayRight(status_.target.empty() ? std::string{} : "Target: " + status_.target,
                 StudioColorRole::TextSecondary);

        // The running job, between the two, with room to grow into whatever the message leaves.
        if (!status_.jobs.empty() && inner.width > 0.0f)
        {
            const StudioStatusJob& job = status_.jobs.front();

            if (!job.stopActionId.empty() && actions_.find(job.stopActionId) != nullptr)
            {
                const float width = std::min(inner.width, std::ceil(studioLabelWidth(
                    frame_, "Stop", StudioFontRole::BodySmall)) + spacing * 2.0f);
                const UiRect box = inner.splitRight(width).inset(UiEdges{0.0f, gap * 0.5f});

                StudioButtonOptions options;
                options.enabled = actions_.isEnabled(job.stopActionId);
                options.tooltip = "Stop this job";
                if (studioButton(frame_, frame_.ids().make("stop"), box, "Stop", options).activated)
                {
                    invoke(job.stopActionId);
                }
                inner.splitRight(gap);
            }

            if (job.progress >= 0.0f)
            {
                const float width = std::min(inner.width * 0.4f,
                                             metricOf(theme, StudioMetric::ControlHeight) * 5.0f);
                const UiRect track = inner.splitRight(width)
                                          .inset(UiEdges{0.0f, metricOf(theme,
                                                 StudioMetric::SpacingSmall)});
                if (frame_.isDrawPass() && !track.isEmpty())
                {
                    frame_.drawList().fillRect(track,
                                               theme.color(StudioColorRole::ControlBackground));
                    UiRect filled = track;
                    filled.width = std::round(track.width * std::clamp(job.progress, 0.0f, 1.0f));
                    if (filled.width > 0.0f)
                    {
                        frame_.drawList().fillRect(filled, theme.color(StudioColorRole::Accent));
                    }
                    frame_.drawList().strokeRect(track, theme.color(StudioColorRole::Separator),
                                                 metricOf(theme,
                                                          StudioMetric::SeparatorThickness));
                }
                inner.splitRight(gap);
            }

            if (frame_.isDrawPass() && inner.width > 0.0f)
            {
                studioDrawText(frame_, inner, job.label, StudioFontRole::BodySmall,
                               theme.color(StudioColorRole::TextPrimary), StudioTextAlign::Right);
            }
        }

        if (frame_.isDrawPass() && inner.width > 0.0f)
        {
            // The dot is the whole unsaved-changes affordance, and it goes with the name rather
            // than somewhere else on the bar: a mark whose subject the user has to work out is a
            // mark they stop reading.
            const bool wrong = !status_.problem.empty();
            const std::string message =
                wrong ? status_.problem
                      : (status_.modified ? "\u2022 " + status_.message : status_.message);
            const StudioColorRole role = wrong ? StudioColorRole::Error
                                       : (status_.modified ? StudioColorRole::TextPrimary
                                                            : StudioColorRole::TextSecondary);
            studioDrawText(frame_, inner, message, StudioFontRole::BodySmall, theme.color(role),
                           StudioTextAlign::Left);
        }

        frame_.ids().pop();
    }

    void StudioShell::moveHighlight(int delta)
    {
        if (menuLevels_.empty() || highlight_.empty()) { return; }

        // The deepest popup, because that is the one the user is looking at. Arrowing down in a
        // menu whose submenu is open must move inside the submenu, not behind it.
        const std::size_t level = std::min(highlight_.size(), menuLevels_.size()) - 1;
        const std::vector<MenuRowGeometry>& rows = menuLevels_[level].rows;
        if (rows.empty()) { return; }

        const auto count = static_cast<int>(rows.size());
        int index = highlight_[level];

        // Wrapping and skipping in one loop, bounded by the row count so that a menu of nothing
        // but separators terminates rather than spinning.
        for (int step = 0; step < count; ++step)
        {
            index = index < 0 ? (delta > 0 ? 0 : count - 1) : (index + delta + count) % count;
            const MenuRowGeometry& row = rows[static_cast<std::size_t>(index)];
            if (!row.separator && row.enabled)
            {
                highlight_[level] = index;
                return;
            }
        }
    }

    bool StudioShell::openHighlightedSubmenu()
    {
        if (menuLevels_.empty() || highlight_.empty()) { return false; }

        const std::size_t level = std::min(highlight_.size(), menuLevels_.size()) - 1;
        const int row = highlight_[level];
        if (row < 0 || static_cast<std::size_t>(row) >= menuLevels_[level].rows.size())
        {
            return false;
        }

        const MenuRowGeometry& entry = menuLevels_[level].rows[static_cast<std::size_t>(row)];
        if (!entry.submenu || !entry.enabled) { return false; }

        openSubmenu(level, row);
        // Opened from the keyboard, so it gets a highlighted row straight away: a submenu reached
        // with Right arrow and then arrowed into would otherwise need a second Down just to reach
        // the row that should already have been chosen.
        highlight_.back() = -1;
        moveHighlightInto(level + 1, 1);
        return true;
    }

    void StudioShell::moveHighlightInto(std::size_t level, int delta)
    {
        // The level may not have laid out yet -- it is created by the next frame's layout pass --
        // so the highlight is seeded now and the first real row is found once the rows exist.
        if (level >= highlight_.size()) { return; }

        const std::vector<StudioMenuEntry>* entries = entriesForLevel(level);
        if (entries == nullptr || entries->empty()) { return; }

        const auto count = static_cast<int>(entries->size());
        int index = delta > 0 ? -1 : count;
        for (int step = 0; step < count; ++step)
        {
            index = (index + delta + count) % count;
            const StudioMenuEntry& entry = (*entries)[static_cast<std::size_t>(index)];
            if (entry.isSeparator()) { continue; }
            if (entry.isSubmenu() ? !entry.rows.empty() : actions_.isEnabled(entry.id))
            {
                highlight_[level] = index;
                return;
            }
        }
    }

    bool StudioShell::closeDeepestSubmenu()
    {
        if (submenuPath_.empty()) { return false; }

        // The highlight returns to the row that opened it, so Left then Right retraces the step
        // rather than dropping the user back at the top of the parent menu.
        const std::size_t level = submenuPath_.size() - 1;
        const int parentRow = submenuPath_[level];
        openSubmenu(level, -1);
        if (level < highlight_.size()) { highlight_[level] = parentRow; }
        return true;
    }

    void StudioShell::chooseHighlightedRow()
    {
        if (openHighlightedSubmenu()) { return; }

        if (menuLevels_.empty() || highlight_.empty()) { return; }
        const std::size_t level = std::min(highlight_.size(), menuLevels_.size()) - 1;
        const int row = highlight_[level];
        if (row < 0 || static_cast<std::size_t>(row) >= menuLevels_[level].rows.size()) { return; }

        const MenuRowGeometry& entry = menuLevels_[level].rows[static_cast<std::size_t>(row)];
        if (!entry.separator && !entry.submenu && entry.enabled) { invoke(entry.id); }
        closePopup();
    }

    void StudioShell::handleMenuKeyboard()
    {
        if (!isPopupOpen()) { return; }

        StudioInputRouter& router = frame_.router();

        if (router.keyPressed(UiKey::Escape))
        {
            // One level at a time. Escape inside a submenu backs out of it; only Escape with
            // nothing nested closes the whole menu, which is what lets a user who opened a submenu
            // by accident get back without losing their place.
            if (!closeDeepestSubmenu()) { closePopup(); }
            keyboardConsumed_ = true;
            return;
        }
        if (router.keyPressed(UiKey::DownArrow)) { moveHighlight(1); keyboardConsumed_ = true; }
        if (router.keyPressed(UiKey::UpArrow)) { moveHighlight(-1); keyboardConsumed_ = true; }
        if (router.keyPressed(UiKey::Home))
        {
            if (!highlight_.empty()) { highlight_.back() = -1; }
            moveHighlight(1);
            keyboardConsumed_ = true;
        }
        if (router.keyPressed(UiKey::End))
        {
            if (!highlight_.empty()) { highlight_.back() = -1; }
            moveHighlight(-1);
            keyboardConsumed_ = true;
        }

        if (router.keyPressed(UiKey::RightArrow))
        {
            keyboardConsumed_ = true;
            // Into the submenu when the highlighted row has one, and on to the next menu in the
            // bar when it does not. Both are what a desktop menu does, and which one applies is
            // decided by the row rather than by a mode.
            // A context menu has no bar to walk along, so Right on a plain row does nothing
            // rather than opening a menu the user cannot see the title of.
            if (!openHighlightedSubmenu() && isMenuOpen() && !menus_.empty())
            {
                const auto count = static_cast<int>(menus_.size());
                openMenuAt((openMenu_ + 1) % count);
            }
        }
        if (router.keyPressed(UiKey::LeftArrow))
        {
            keyboardConsumed_ = true;
            if (!closeDeepestSubmenu() && isMenuOpen() && !menus_.empty())
            {
                const auto count = static_cast<int>(menus_.size());
                openMenuAt((openMenu_ - 1 + count) % count);
            }
        }

        if (router.keyPressed(UiKey::Enter))
        {
            keyboardConsumed_ = true;
            chooseHighlightedRow();
        }
    }

    void StudioShell::dispatchShortcuts()
    {
        // While a menu is open the menu owns the keyboard: Enter chooses a row, Escape closes, and
        // neither should also fire whatever global chord happens to use the same key. The same
        // holds for the frame a menu closes *on*, which is what keyboardConsumed_ carries.
        if (isPopupOpen() || keyboardConsumed_) { return; }

        StudioInputRouter& router = frame_.router();
        const UiKeyModifiers modifiers = router.modifiers();
        const bool modified = modifiers.control || modifiers.alt || modifiers.super;

        // STUDIO-06008: an unmodified chord inside a text field is a character, not a command.
        // `F` frames the selection in a viewport and types an `f` in a name field, and neither the
        // viewport nor the field should have to know about the other.
        if (router.wantsTextInput() && !modified) { return; }

        for (int key = kFirstDispatchableKey; key < static_cast<int>(UiKey::Count); ++key)
        {
            const auto candidate = static_cast<UiKey>(key);
            if (!router.keyPressed(candidate)) { continue; }

            StudioShortcut shortcut;
            shortcut.key = candidate;
            shortcut.modifiers = modifiers;

            const StudioAction* action = actions_.findByShortcut(shortcut);
            if (action != nullptr) { invoke(action->id); }

            // One chord per frame. Two keys going down on the same frame is a keyboard artefact,
            // not a request for two commands.
            return;
        }
    }
} // namespace CNA::Studio
