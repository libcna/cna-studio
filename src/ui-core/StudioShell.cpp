// SPDX-License-Identifier: MS-PL
/**
 * @file StudioShell.cpp
 * @brief Menu, toolbar, dock, status-bar and shortcut behaviour for the application frame.
 */

#include "CNA/Studio/UiCore/StudioShell.hpp"

#include "CNA/Studio/UiCore/StudioWidgets.hpp"

#include <algorithm>

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
        registerCoreStudioActions(actions_);
        menus_ = defaultMenus();
        toolbar_ = defaultToolbar();

        registerPanel({"viewport", "Viewport", false, /*closable=*/false, /*isViewport=*/true});
        registerPanel({"outliner", "World Outliner"});
        registerPanel({"layers", "Layers"});
        registerPanel({"details", "Details"});
        registerPanel({"material", "Material"});
        registerPanel({"content", "Content Browser"});
        registerPanel({"output", "Output Log"});
        registerPanel({"build", "Build"});
        registerPanel({"problems", "Problems"});
        resetLayout();

        // The one core action the shell itself owns, attached here rather than left for a service
        // that will never exist: the workspace arrangement is the shell's own state, and a Window
        // menu whose only entry reports "not implemented" is worse than one with no entry.
        if (const StudioAction* found = actions_.find("studio.window.resetLayout"))
        {
            StudioAction reset = *found;
            reset.run = [this]() { resetLayout(); };
            actions_.add(std::move(reset));
        }

        statusLeft_ = "No project open";
        statusRight_ = "Renderer: unknown";
    }

    std::vector<StudioMenuDefinition> StudioShell::defaultMenus()
    {
        const std::string sep{kStudioMenuSeparatorId};
        return {
            {"File", {"studio.file.newProject", "studio.file.openProject", sep,
                      "studio.file.save", "studio.file.saveAll", sep, "studio.file.quit"}},
            {"Edit", {"studio.edit.undo", "studio.edit.redo", sep,
                      "studio.edit.duplicate", "studio.edit.delete"}},
            {"View", {"studio.view.focusSelected", "studio.view.toggleGrid", sep,
                      "studio.view.translate", "studio.view.rotate", "studio.view.scale"}},
            {"Project", {}},
            {"Build", {"studio.build.build", "studio.build.package"}},
            {"Play", {"studio.play.play", "studio.play.stop"}},
            {"Tools", {}},
            {"Window", {"studio.window.resetLayout"}},
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
        const auto existing = std::find_if(panels_.begin(), panels_.end(),
            [&](const StudioPanelDescriptor& p) { return p.id == descriptor.id; });
        if (existing != panels_.end()) { *existing = std::move(descriptor); return; }
        panels_.push_back(std::move(descriptor));
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
        dock_.addPanel(bottom, "content");
        dock_.addPanel(bottom, "output");
        dock_.addPanel(bottom, "build");
        dock_.addPanel(bottom, "problems");
        dock_.addPanel(centre, "viewport");

        for (const StudioDockNodeId leaf : dock_.leaves())
        {
            if (!dock_.node(leaf).panels.empty()) { dock_.node(leaf).activePanel = 0; }
        }
    }

    bool StudioShell::isPanelOpen(std::string_view id) const
    {
        return dock_.findPanel(id) != kInvalidDockNode;
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
        openMenu_ = -1;
        highlightedEntry_ = -1;
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
            highlightedEntry_ = -1;
            return;
        }
        openMenuAt(index);
    }

    void StudioShell::openMenuAt(int index)
    {
        openMenu_ = index;
        // No row is highlighted until the keyboard asks for one. Pre-selecting the first item
        // would make Enter -- pressed to dismiss something else entirely -- run a command the user
        // never looked at.
        highlightedEntry_ = -1;
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

    UiRect StudioShell::menuTitleBounds(std::size_t index) const
    {
        if (index >= menuTitles_.size()) { return UiRect{}; }
        return menuTitles_[index].bounds;
    }

    UiRect StudioShell::menuRowBounds(std::size_t index) const
    {
        if (index >= menuRows_.size()) { return UiRect{}; }
        return menuRows_[index].bounds;
    }

    std::string_view StudioShell::menuRowActionId(std::size_t index) const
    {
        if (index >= menuRows_.size()) { return {}; }
        return menuRows_[index].id;
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
        keyboardConsumed_ = false;

        // The blocking layer is decided before anything is described, from state that already
        // exists. Inferring it from description order would leak one frame of input to the panels
        // underneath an open menu.
        frame_.beginFrame(input, isMenuOpen() ? kMenuLayer : 0);
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
            const float width_ = frame_.measureText(StudioFontRole::Body, menu.title).width
                               + titlePadding * 2.0f;
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
                const float wanted = std::max(buttonHeight,
                                              studioLabelWidth(frame_, label, StudioFontRole::BodySmall));
                geometry.bounds = toolbarCursor.splitLeft(std::min(wanted, toolbarCursor.width));
                toolbarCursor.splitLeft(std::min(spacing, toolbarCursor.width));
            }
            toolbarEntries_.push_back(geometry);
        }

        // --- The open menu's popup ------------------------------------------------------------------
        menuRows_.clear();
        menuPopup_ = UiRect{};
        if (!isMenuOpen() || static_cast<std::size_t>(openMenu_) >= menuTitles_.size()) { return; }

        const StudioMenuDefinition& menu = menus_[static_cast<std::size_t>(openMenu_)];
        if (menu.entries.empty()) { return; }

        const float rowHeight = studioMenuItemHeight(theme);
        const float separatorHeight = studioMenuSeparatorHeight(theme);
        const float padding = metricOf(theme, StudioMetric::SpacingMedium);
        const float checkColumn = metricOf(theme, StudioMetric::IconSize);

        float widest = 0.0f;
        float totalHeight = metricOf(theme, StudioMetric::SpacingSmall) * 2.0f;
        for (const std::string& entry : menu.entries)
        {
            if (entry == kStudioMenuSeparatorId) { totalHeight += separatorHeight; continue; }
            totalHeight += rowHeight;

            const StudioAction* action = actions_.find(entry);
            const std::string_view label = action != nullptr ? std::string_view{action->label}
                                                             : std::string_view{entry};
            float rowWidth = padding * 2.0f + checkColumn
                           + metricOf(theme, StudioMetric::SpacingSmall)
                           + frame_.measureText(StudioFontRole::Body, label).width;

            if (action != nullptr && action->shortcut.isBound())
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
        float popupWidth = std::max(widest, minimumWidth);
        popupWidth = std::min(popupWidth, layout_.window.width);

        float popupX = menuTitles_[static_cast<std::size_t>(openMenu_)].bounds.left();
        // Kept on screen. A menu opened from the rightmost title must not run off the edge, and
        // shifting it left is what every desktop menu does rather than clipping it.
        popupX = std::min(popupX, layout_.window.right() - popupWidth);
        popupX = std::max(popupX, layout_.window.left());

        const float popupY = layout_.menuBar.bottom();
        const float available = std::max(0.0f, layout_.window.bottom() - popupY);
        menuPopup_ = UiRect{popupX, popupY, popupWidth, std::min(totalHeight, available)};

        UiRect rowCursor = menuPopup_.inset(
            UiEdges{0.0f, metricOf(theme, StudioMetric::SpacingSmall)});
        for (const std::string& entry : menu.entries)
        {
            MenuRowGeometry row;
            row.id = entry;
            row.separator = entry == kStudioMenuSeparatorId;

            const StudioAction* action = row.separator ? nullptr : actions_.find(entry);
            row.enabled = !row.separator && action != nullptr && actions_.isEnabled(entry);
            row.bounds = rowCursor.splitTop(row.separator ? separatorHeight : rowHeight);
            menuRows_.push_back(std::move(row));
        }
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
        describeStatusBar();

        // Last, so it draws over everything and so its widgets win hover against anything they
        // overlap. Being in a raised input layer is what stops the panels beneath from responding;
        // being described last is what stops the popup from being drawn underneath them.
        describeMenuPopup();
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
        if (!isMenuOpen() || menuRows_.empty() || menuPopup_.isEmpty()) { return; }

        const StudioTheme& theme = frame_.theme();
        frame_.pushLayer(kMenuLayer);
        frame_.pushClip(menuPopup_);

        if (frame_.isDrawPass())
        {
            frame_.drawList().fillRect(menuPopup_, theme.color(StudioColorRole::PopupBackground));
            frame_.drawList().strokeRect(menuPopup_, theme.color(StudioColorRole::BorderStrong),
                                         metricOf(theme, StudioMetric::BorderWidth));
        }

        frame_.ids().push("menu");
        frame_.ids().pushIndex(openMenu_);

        int hoveredRow = -1;
        bool activatedThisPass = false;

        for (std::size_t i = 0; i < menuRows_.size(); ++i)
        {
            const MenuRowGeometry& row = menuRows_[i];
            if (row.separator)
            {
                studioMenuSeparator(frame_, row.bounds);
                continue;
            }

            const StudioAction* action = actions_.find(row.id);
            const std::string shortcut = action != nullptr
                ? describeStudioShortcut(action->shortcut) : std::string{};

            StudioMenuItemOptions options;
            options.enabled = row.enabled;
            options.checkable = action != nullptr && action->checkable;
            options.checked = options.checkable && actions_.isChecked(row.id);
            options.highlighted = highlightedEntry_ == static_cast<int>(i);
            options.shortcut = shortcut;

            const std::string_view label = action != nullptr ? std::string_view{action->label}
                                                             : std::string_view{row.id};

            const StudioWidgetResult result =
                studioMenuItem(frame_, frame_.ids().make(row.id), row.bounds, label, options);

            if (!frame_.isInputPass()) { continue; }
            if (result.interaction.hovered) { hoveredRow = static_cast<int>(i); }
            if (result.activated && !activatedThisPass)
            {
                activatedThisPass = true;
                invoke(row.id);
            }
        }

        frame_.ids().pop();
        frame_.ids().pop();
        frame_.popClip();
        frame_.popLayer();

        if (!frame_.isInputPass()) { return; }

        // The pointer moves the highlight too, so that arrowing down and then reaching for the
        // mouse does not leave two rows looking chosen.
        if (hoveredRow >= 0) { highlightedEntry_ = hoveredRow; }

        if (activatedThisPass)
        {
            setOpenMenu(-1);
            return;
        }

        // A press that landed on neither the bar nor the list dismisses the menu without
        // activating anything -- the other half of "click elsewhere to cancel".
        if (frame_.router().mousePressed(UiMouseButton::Left))
        {
            const float x = frame_.router().mouseX();
            const float y = frame_.router().mouseY();
            if (!menuPopup_.contains(x, y) && !layout_.menuBar.contains(x, y))
            {
                setOpenMenu(-1);
            }
        }
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

            const std::string_view label = action != nullptr ? std::string_view{action->label}
                                                             : std::string_view{entry.id};
            const StudioWidgetResult result =
                studioButton(frame_, frame_.ids().make(entry.id), entry.bounds, label, options);

            if (result.activated) { invoke(entry.id); }
        }
        frame_.ids().pop();
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
            frame_.pushClip(geometry.tabStrip);

            UiRect cursor = geometry.tabStrip;
            for (std::size_t i = 0; i < node.panels.size(); ++i)
            {
                if (cursor.width <= 0.0f) { break; }

                const std::string& panelId = node.panels[i];
                const StudioPanelDescriptor* descriptor = panel(panelId);
                const std::string_view title = descriptor != nullptr
                    ? std::string_view{descriptor->title} : std::string_view{panelId};

                const float width = std::min(
                    studioLabelWidth(frame_, title) + metricOf(theme, StudioMetric::SpacingMedium),
                    cursor.width);

                StudioTabOptions options;
                options.active = node.activePanel == i;
                options.modified = descriptor != nullptr && descriptor->modified;

                const UiRect tab = cursor.splitLeft(width);
                const StudioWidgetResult result =
                    studioTab(frame_, frame_.ids().make(panelId), tab, title, options);

                if (frame_.isInputPass()) { tabBounds_.emplace_back(panelId, tab); }
                if (result.activated) { node.activePanel = i; }
            }

            frame_.popClip();
            frame_.ids().pop();

            if (!frame_.isDrawPass()) { continue; }

            frame_.drawList().drawHorizontalSeparator(
                UiRect{geometry.tabStrip.left(), geometry.tabStrip.bottom(),
                       geometry.tabStrip.width, 0.0f},
                theme.color(StudioColorRole::Separator),
                metricOf(theme, StudioMetric::SeparatorThickness));

            const std::string& active = node.panels[std::min(node.activePanel,
                                                             node.panels.size() - 1)];
            const StudioPanelDescriptor* descriptor = panel(active);
            if (descriptor != nullptr && descriptor->isViewport)
            {
                describeViewportBody(geometry.body);
            }
            else
            {
                // A panel surface awaiting the panel that will be ported into it (Phase 7).
                frame_.drawList().fillRect(geometry.body,
                                           theme.color(StudioColorRole::PanelBackground));
            }
        }
        frame_.ids().pop();
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

    void StudioShell::describeStatusBar()
    {
        if (!frame_.isDrawPass() || layout_.statusBar.isEmpty()) { return; }

        const StudioTheme& theme = frame_.theme();
        frame_.drawList().fillRect(layout_.statusBar, theme.color(StudioColorRole::PanelHeader));
        frame_.drawList().drawHorizontalSeparator(
            UiRect{layout_.statusBar.left(), layout_.statusBar.top(), layout_.statusBar.width, 0.0f},
            theme.color(StudioColorRole::Separator),
            metricOf(theme, StudioMetric::SeparatorThickness));

        const UiRect inner =
            layout_.statusBar.inset(UiEdges{metricOf(theme, StudioMetric::SpacingMedium), 0.0f});
        if (inner.isEmpty()) { return; }

        studioDrawText(frame_, inner, statusLeft_, StudioFontRole::BodySmall,
                       theme.color(StudioColorRole::TextSecondary), StudioTextAlign::Left);
        studioDrawText(frame_, inner, statusRight_, StudioFontRole::BodySmall,
                       theme.color(StudioColorRole::TextSecondary), StudioTextAlign::Right);
    }

    void StudioShell::moveHighlight(int delta)
    {
        if (menuRows_.empty()) { return; }

        const auto count = static_cast<int>(menuRows_.size());
        int index = highlightedEntry_;

        // Wrapping and skipping in one loop, bounded by the row count so that a menu of nothing
        // but separators terminates rather than spinning.
        for (int step = 0; step < count; ++step)
        {
            index = index < 0 ? (delta > 0 ? 0 : count - 1) : (index + delta + count) % count;
            const MenuRowGeometry& row = menuRows_[static_cast<std::size_t>(index)];
            if (!row.separator && row.enabled)
            {
                highlightedEntry_ = index;
                return;
            }
        }
    }

    void StudioShell::handleMenuKeyboard()
    {
        if (!isMenuOpen()) { return; }

        StudioInputRouter& router = frame_.router();

        if (router.keyPressed(UiKey::Escape))
        {
            setOpenMenu(-1);
            keyboardConsumed_ = true;
            return;
        }
        if (router.keyPressed(UiKey::DownArrow)) { moveHighlight(1); keyboardConsumed_ = true; }
        if (router.keyPressed(UiKey::UpArrow)) { moveHighlight(-1); keyboardConsumed_ = true; }
        if (router.keyPressed(UiKey::Home))
        {
            highlightedEntry_ = -1;
            moveHighlight(1);
            keyboardConsumed_ = true;
        }
        if (router.keyPressed(UiKey::End))
        {
            highlightedEntry_ = -1;
            moveHighlight(-1);
            keyboardConsumed_ = true;
        }

        if (!menus_.empty())
        {
            const auto count = static_cast<int>(menus_.size());
            if (router.keyPressed(UiKey::LeftArrow))
            {
                openMenuAt((openMenu_ - 1 + count) % count);
                keyboardConsumed_ = true;
            }
            if (router.keyPressed(UiKey::RightArrow))
            {
                openMenuAt((openMenu_ + 1) % count);
                keyboardConsumed_ = true;
            }
        }

        if (router.keyPressed(UiKey::Enter))
        {
            keyboardConsumed_ = true;
            if (highlightedEntry_ >= 0
                && static_cast<std::size_t>(highlightedEntry_) < menuRows_.size())
            {
                const MenuRowGeometry& row = menuRows_[static_cast<std::size_t>(highlightedEntry_)];
                if (!row.separator && row.enabled) { invoke(row.id); }
                setOpenMenu(-1);
            }
        }
    }

    void StudioShell::dispatchShortcuts()
    {
        // While a menu is open the menu owns the keyboard: Enter chooses a row, Escape closes, and
        // neither should also fire whatever global chord happens to use the same key. The same
        // holds for the frame a menu closes *on*, which is what keyboardConsumed_ carries.
        if (isMenuOpen() || keyboardConsumed_) { return; }

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
