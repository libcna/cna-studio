// SPDX-License-Identifier: MS-PL
/**
 * @file ApplicationTests.cpp
 * @brief End-to-end tests over the whole editor, driven through the null UI and viewport.
 *
 * These run the *real* EditorApplication -- there is no separate test mode that could drift away
 * from what a user gets. That is the payoff of injecting the UI and the viewport rather than
 * having the application create them (ANALYSIS.md decision D-02).
 */

#include "TestHarness.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

#include "CNA/Editor/EditorApplication.hpp"
#include "CNA/Editor/Plugins/Plugin.hpp"
#include "CNA/Editor/Scene/BuiltinComponents.hpp"
#include "CNA/Editor/Scene/TransformGizmos3D.hpp"
#include "CNA/Editor/Assets/AssetImporters.hpp"
#include "CNA/Editor/Project/BuildRunner.hpp"
#include "CNA/Editor/Project/RecoveryStore.hpp"
#include "CNA/Editor/PrefabWorkflow.hpp"
#include "CNA/Editor/ProjectCommands.hpp"
#include "CNA/Editor/Scene/PrefabCommands.hpp"
#include "CNA/Editor/Scene/PrefabDocument.hpp"
#include "CNA/Editor/Scene/SpriteAnimation.hpp"
#include "CNA/Editor/Viewport/EditorAudio.hpp"
#include "CNA/Editor/Scene/Tilemap.hpp"
#include "CNA/Editor/Scene/MissingReferences.hpp"
#include "CNA/Editor/Scene/SceneTransform.hpp"
#include "CNA/Editor/Scene/SceneCommands.hpp"

using namespace CNA::Editor;

namespace
{
    std::filesystem::path makeScratchDirectory(const std::string& name)
    {
        const std::filesystem::path directory =
            std::filesystem::temp_directory_path() / ("cna-editor-tests-" + name + "-" + Uuid::generate().toString());
        std::filesystem::create_directories(directory);
        return directory;
    }

    void writeFile(const std::filesystem::path& path, std::string_view contents)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream{path, std::ios::binary | std::ios::trunc};
        stream << contents;
    }

    /** @brief Builds an application over the null UI and viewport. */
    EditorApplication makeApplication()
    {
        return EditorApplication{std::make_unique<NullEditorUi>(), std::make_unique<NullEditorViewport>()};
    }
}

CNA_EDITOR_TEST(OptionsParseTheDocumentedFlags)
{
    const char* argv[] = {"cna-editor", "--project=/tmp/MyGame.cnaproject", "--headless", "--frames=3"};
    const EditorOptions options = EditorOptions::parse(4, argv);

    CNA_EDITOR_EXPECT(!options.hasError);
    CNA_EDITOR_EXPECT_EQ(options.projectPath, std::string{"/tmp/MyGame.cnaproject"});
    CNA_EDITOR_EXPECT(options.headless);
    CNA_EDITOR_EXPECT_EQ(options.frameLimit, 3);
}

CNA_EDITOR_TEST(OptionsAcceptABareProjectPath)
{
    const char* argv[] = {"cna-editor", "/tmp/MyGame.cnaproject"};
    const EditorOptions options = EditorOptions::parse(2, argv);
    CNA_EDITOR_EXPECT(!options.hasError);
    CNA_EDITOR_EXPECT_EQ(options.projectPath, std::string{"/tmp/MyGame.cnaproject"});
}

CNA_EDITOR_TEST(OptionsRejectGraphicsBackendSelection)
{
    // CNA fixes its backend at compile time, so accepting --graphics here would teach users a
    // mental model the framework does not support. Rejecting it loudly is the honest behaviour.
    const char* argv[] = {"cna-editor", "--graphics=vulkan"};
    const EditorOptions options = EditorOptions::parse(2, argv);

    CNA_EDITOR_EXPECT(options.hasError);
    CNA_EDITOR_EXPECT(options.errorMessage.find("compile time") != std::string::npos);
    CNA_EDITOR_EXPECT(options.errorMessage.find("cna-player") != std::string::npos);
}

CNA_EDITOR_TEST(OptionsRejectUnknownFlags)
{
    const char* argv[] = {"cna-editor", "--nonsense"};
    const EditorOptions options = EditorOptions::parse(2, argv);
    CNA_EDITOR_EXPECT(options.hasError);
}

CNA_EDITOR_TEST(UsageTextExplainsTheBackendConstraint)
{
    const std::string usage = EditorOptions::getUsage();
    CNA_EDITOR_EXPECT(usage.find("compile") != std::string::npos);
    CNA_EDITOR_EXPECT(usage.find("cna-player") != std::string::npos);
}

CNA_EDITOR_TEST(ApplicationStartsWithAUsableEmptyScene)
{
    EditorApplication application = makeApplication();
    EditorOptions options;
    options.headless = true;

    CNA_EDITOR_EXPECT(application.initialize(options));

    // A scene with no camera renders nothing, which reads as "the editor is broken".
    const SceneDocument& scene = application.getContext().getScene();
    CNA_EDITOR_EXPECT_EQ(scene.getEntityCount(), std::size_t{1});
    CNA_EDITOR_EXPECT(scene.getEntities().front().findComponent(BuiltinComponentIds::kCamera) != nullptr);
    CNA_EDITOR_EXPECT(scene.getEntities().front().findComponent(BuiltinComponentIds::kTransform) != nullptr);
}

CNA_EDITOR_TEST(ApplicationDrawsEveryPanelEachFrame)
{
    EditorApplication application = makeApplication();
    EditorOptions options;
    options.headless = true;
    CNA_EDITOR_EXPECT(application.initialize(options));

    auto& ui = static_cast<NullEditorUi&>(application.getUi());
    CNA_EDITOR_EXPECT(ui.beginFrame());
    application.renderFrame();
    ui.endFrame();

    const std::vector<std::string>& panels = ui.getLastFramePanels();
    CNA_EDITOR_EXPECT_EQ(panels.size(), std::size_t{10});

    const auto contains = [&](const std::string& title) {
        return std::find(panels.begin(), panels.end(), title) != panels.end();
    };
    CNA_EDITOR_EXPECT(contains("Scene Hierarchy"));
    CNA_EDITOR_EXPECT(contains("Viewport"));
    CNA_EDITOR_EXPECT(contains("Inspector"));
    CNA_EDITOR_EXPECT(contains("Assets"));
    CNA_EDITOR_EXPECT(contains("Console"));
    CNA_EDITOR_EXPECT(contains("Validation"));
    CNA_EDITOR_EXPECT(contains("History"));
    CNA_EDITOR_EXPECT(contains("Diagnostics"));
    CNA_EDITOR_EXPECT(contains("Backends"));
    CNA_EDITOR_EXPECT(contains("Build"));

    // The viewport must actually have rendered, or --headless would be a no-op rather than a
    // smoke test. NullEditorViewport walks the same transform and bounds code a real one does.
    const auto& viewport = static_cast<NullEditorViewport&>(application.getViewport());
    CNA_EDITOR_EXPECT_EQ(viewport.getRenderCount(), std::uint64_t{1});
    CNA_EDITOR_EXPECT(viewport.getWidth() > 0);
}

CNA_EDITOR_TEST(ApplicationHonoursItsFrameLimit)
{
    EditorApplication application = makeApplication();
    EditorOptions options;
    options.headless = true;
    options.frameLimit = 4;
    CNA_EDITOR_EXPECT(application.initialize(options));

    CNA_EDITOR_EXPECT_EQ(application.run(), 0);
    CNA_EDITOR_EXPECT_EQ(static_cast<NullEditorUi&>(application.getUi()).getFrameCount(), std::uint64_t{4});
}

CNA_EDITOR_TEST(ApplicationWalksADeepHierarchyWithoutCrashing)
{
    EditorApplication application = makeApplication();
    EditorOptions options;
    options.headless = true;
    CNA_EDITOR_EXPECT(application.initialize(options));

    EditorContext& context = application.getContext();
    SceneDocument& scene = context.getScene();

    Uuid parent;
    for (int depth = 0; depth < 64; ++depth)
    {
        EditorEntity entity{Uuid::generate(), "Level" + std::to_string(depth)};
        entity.addComponent(EditorComponent{BuiltinComponentIds::kTransform});
        const Uuid id = scene.addEntity(std::move(entity));
        if (parent.isValid()) { scene.reparentEntity(id, parent); }
        parent = id;
    }

    context.select(parent);
    application.renderFrame();

    // NullEditorUi expands every node, so this really did walk all 64 levels.
    CNA_EDITOR_EXPECT_EQ(scene.getEntityCount(), std::size_t{65});
    CNA_EDITOR_EXPECT(context.getPrimarySelection() == parent);
}

CNA_EDITOR_TEST(SelectionIsPrunedWhenItsEntityIsDeleted)
{
    EditorApplication application = makeApplication();
    EditorOptions options;
    options.headless = true;
    CNA_EDITOR_EXPECT(application.initialize(options));

    EditorContext& context = application.getContext();
    EditorEntity entity{Uuid::generate(), "Doomed"};
    entity.addComponent(EditorComponent{BuiltinComponentIds::kTransform});
    const Uuid id = context.getScene().addEntity(std::move(entity));

    context.select(id);
    CNA_EDITOR_EXPECT(context.isSelected(id));

    context.execute(std::make_unique<DeleteEntityCommand>(context.getScene(), id));

    // Without pruning, the inspector would keep showing a deleted entity and the next command
    // would target a missing id.
    CNA_EDITOR_EXPECT(!context.isSelected(id));
    CNA_EDITOR_EXPECT(!context.getPrimarySelection().isValid());

    // ...and undo must not silently resurrect the selection either; the entity returns, the
    // selection does not, which is what every editor does.
    context.getHistory().undo();
    CNA_EDITOR_EXPECT(context.getScene().findEntity(id) != nullptr);
    CNA_EDITOR_EXPECT(!context.isSelected(id));
}

CNA_EDITOR_TEST(FullProjectRoundTripThroughTheApplication)
{
    // The Phase 1 milestone in miniature: open a project, load a scene, edit a property, undo,
    // redo, save, and reopen to confirm the edit persisted.
    const std::filesystem::path directory = makeScratchDirectory("app");

    Project project = Project::createDefault("MyGame", directory.generic_string());
    project.setStartupScene("Scenes/Level01.cnascene");
    std::string errorMessage;
    CNA_EDITOR_EXPECT(project.saveToFile({}, &errorMessage));

    writeFile(directory / "Assets" / "player.png", "pixels");

    ComponentRegistry registry;
    registerBuiltinComponents(registry);

    SceneDocument scene;
    scene.setName("Level01");
    EditorEntity player{Uuid::generate(), "Player"};
    EditorComponent transform{BuiltinComponentIds::kTransform};
    transform.applyDefaults(*registry.find(BuiltinComponentIds::kTransform));
    player.addComponent(std::move(transform));
    const Uuid playerId = scene.addEntity(std::move(player));
    CNA_EDITOR_EXPECT(scene.saveToFile((directory / "Scenes" / "Level01.cnascene").generic_string(), &errorMessage));

    EditorApplication application = makeApplication();
    EditorOptions options;
    options.headless = true;
    options.projectPath = project.getFilePath();
    CNA_EDITOR_EXPECT(application.initialize(options));

    EditorContext& context = application.getContext();
    CNA_EDITOR_EXPECT(context.hasProject());
    CNA_EDITOR_EXPECT_EQ(context.getScene().getName(), std::string{"Level01"});
    CNA_EDITOR_EXPECT(context.getScene().findEntity(playerId) != nullptr);
    CNA_EDITOR_EXPECT_EQ(context.getAssets().getCount(), std::size_t{1});

    context.select(playerId);
    context.execute(std::make_unique<SetPropertyCommand>(
        context.getScene(), playerId, BuiltinComponentIds::kTransform, "position",
        PropertyValue{EditorVector3{100.0f, 220.0f, 0.0f}}));

    CNA_EDITOR_EXPECT(context.getHistory().isDirty());
    CNA_EDITOR_EXPECT(context.getHistory().undo());
    CNA_EDITOR_EXPECT(context.getHistory().redo());
    CNA_EDITOR_EXPECT(context.saveScene());
    CNA_EDITOR_EXPECT(!context.getHistory().isDirty());

    application.renderFrame();

    SceneDocument reopened;
    const SceneLoadResult result =
        reopened.loadFromFile((directory / "Scenes" / "Level01.cnascene").generic_string(), registry);
    CNA_EDITOR_EXPECT(result.succeeded);

    const EditorComponent* savedTransform =
        reopened.findEntity(playerId)->findComponent(BuiltinComponentIds::kTransform);
    CNA_EDITOR_EXPECT_EQ(savedTransform->getProperty("position").get<EditorVector3>().x, 100.0f);
    CNA_EDITOR_EXPECT_EQ(savedTransform->getProperty("position").get<EditorVector3>().y, 220.0f);

    std::filesystem::remove_all(directory);
}

CNA_EDITOR_TEST(ApplicationReportsAMissingProjectRatherThanCrashing)
{
    EditorApplication application = makeApplication();
    EditorOptions options;
    options.headless = true;
    options.projectPath = "/definitely/not/here/Missing.cnaproject";

    CNA_EDITOR_EXPECT(!application.initialize(options));

    const auto& ui = static_cast<NullEditorUi&>(application.getUi());
    bool sawError = false;
    for (const auto& entry : ui.getLog())
    {
        if (entry.severity == LogSeverity::Error) { sawError = true; }
    }
    CNA_EDITOR_EXPECT(sawError);
}

/**
 * @brief A null UI that reports whatever viewport interaction a test scripts.
 *
 * The gizmo's arithmetic is covered in ViewportTests; what these tests cover is the *wiring* --
 * that a press on a handle starts a drag rather than a reselection, that the drag survives the
 * pointer leaving the panel, and that one drag is one undo entry. All of that lives in
 * EditorApplication and none of it is reachable without a UI that can say "the left button just
 * went down here".
 */
namespace
{
    class ScriptedUi final : public NullEditorUi
    {
    public:
        UiImageInteraction interaction;

        /** @brief Buttons to report as clicked, by label. Each entry fires once. */
        std::vector<std::string> pendingClicks;

        /** @brief Keys to report as held, for the 3D viewport's fly controls. */
        std::vector<UiKey> heldKeys;

        [[nodiscard]] bool isKeyDown(UiKey key) const override
        {
            return std::find(heldKeys.begin(), heldKeys.end(), key) != heldKeys.end();
        }

        /** @brief Enum choices to apply, as (field label, option) pairs. Each fires once. */
        std::vector<std::pair<std::string, std::string>> pendingChoices;

        /** @brief Non-enum edits to apply, as (field label, new value) pairs. Each fires once. */
        std::vector<std::pair<std::string, PropertyValue>> pendingEdits;

        /** @brief Values the inspector showed during the most recent frame, by label. */
        std::vector<std::pair<std::string, PropertyValue>> shownValues;

        /** @brief Tree nodes to report as clicked, by entity id. Each fires once. */
        std::vector<Uuid> pendingNodeClicks;

        /** @brief Tree nodes to report as double-clicked, by entity id. Each fires once. */
        std::vector<Uuid> pendingNodeDoubleClicks;

        /** @brief Drops to deliver, as (drop target entity, dragged payload). Each fires once. */
        std::vector<std::pair<Uuid, std::string>> pendingDrops;

        /** @brief Text to type into the next rename field, and whether to commit it. */
        std::optional<std::pair<std::string, bool>> pendingRename;

        /** @brief Modifiers to report as held. */
        UiKeyModifiers modifiers;

        /** @brief The most recent node drawn, so drag and drop calls can be attributed to it. */
        Uuid lastNode;

        /** @brief The most recent property field drawn, for the same reason. */
        std::string lastField;

        /** @brief Entity ids the hierarchy drew a node for during the most recent frame. */
        std::vector<Uuid> drawnNodes;

        /** @brief True when a rename field was drawn during the most recent frame. */
        bool sawRenameField = false;

        /** @brief Button labels drawn during the most recent frame. */
        std::vector<std::string> drawnButtons;

        /** @brief Enum options last offered for a field, by label. */
        std::vector<std::pair<std::string, std::vector<std::string>>> offeredOptions;

        /**
         * @brief Reports the scripted pointer as the thing being interacted with.
         *
         * The real UI answers this from ImGui, where the viewport image is an item and is active
         * for the whole of a drag. Here the scripted interaction *is* the pointer, so a held button
         * is exactly the same fact -- and without it every frame would look like the end of an
         * interaction, which would stop a drag from merging into one undo entry.
         */
        [[nodiscard]] bool isAnyItemActive() const override { return interaction.leftDown; }

        UiImageInteraction image(const std::string& id,
                                 UiTextureId texture,
                                 float width,
                                 float height,
                                 bool flipVertically) override
        {
            (void)id;
            (void)texture;
            (void)width;
            (void)height;
            (void)flipVertically;
            return interaction;
        }

        /** @brief Clears the per-frame record. renderFrame() opens the dock space first. */
        void beginDockSpace() override
        {
            drawnButtons.clear();
            offeredOptions.clear();
            shownValues.clear();
            shownChecks.clear();
            drawnText.clear();
            drawnTextFields.clear();
            drawnNodes.clear();
            drawnStringNodes.clear();
            drawnStringNodeLabels.clear();
            drawnMenuItems.clear();
            sawRenameField = false;
        }

        [[nodiscard]] UiKeyModifiers getModifiers() const override { return modifiers; }

        UiTreeNodeResult treeNode(const Uuid& id,
                                  const std::string& label,
                                  bool selected,
                                  bool leaf) override
        {
            drawnNodes.push_back(id);
            lastNode = id;

            UiTreeNodeResult result = NullEditorUi::treeNode(id, label, selected, leaf);

            for (auto entry = pendingNodeClicks.begin(); entry != pendingNodeClicks.end(); ++entry)
            {
                if (*entry != id) { continue; }
                pendingNodeClicks.erase(entry);
                result.clicked = true;
                break;
            }
            for (auto entry = pendingNodeDoubleClicks.begin();
                 entry != pendingNodeDoubleClicks.end(); ++entry)
            {
                if (*entry != id) { continue; }
                pendingNodeDoubleClicks.erase(entry);
                result.doubleClicked = true;
                break;
            }
            return result;
        }

        /** @brief Asset payloads to deliver to the next field with a matching label. */
        std::vector<std::pair<std::string, std::string>> pendingAssetDrops;

        /** @brief Folder rows the browser drew, and drops to deliver to them. */
        std::vector<std::string> drawnStringNodes;
        std::vector<std::pair<std::string, std::string>> pendingFolderDrops;
        std::string lastStringNode;

        /** @brief Labels of the string-keyed rows, in draw order, for asserting on wording. */
        std::vector<std::string> drawnStringNodeLabels;

        /** @brief String-keyed rows to report as clicked, matched on an id prefix. */
        std::vector<std::string> pendingStringNodeClicks;

        UiTreeNodeResult treeNode(const std::string& id,
                                  const std::string& label,
                                  bool selected,
                                  bool leaf) override
        {
            drawnStringNodes.push_back(id);
            drawnStringNodeLabels.push_back(label);
            lastStringNode = id;

            UiTreeNodeResult result = NullEditorUi::treeNode(id, label, selected, leaf);

            // Matched on a prefix rather than in full: a validation row carries its position in
            // the report as part of its id, which a test should not have to predict.
            for (auto entry = pendingStringNodeClicks.begin();
                 entry != pendingStringNodeClicks.end(); ++entry)
            {
                if (id.rfind(*entry, 0) != 0) { continue; }
                pendingStringNodeClicks.erase(entry);
                result.clicked = true;
                break;
            }
            return result;
        }

        /** @brief Returns true when a string-keyed node with @p id was drawn last frame. */
        [[nodiscard]] bool sawStringNode(const std::string& id) const
        {
            return std::find(drawnStringNodes.begin(), drawnStringNodes.end(), id)
                != drawnStringNodes.end();
        }

        std::optional<std::string> acceptDrop(const std::string& type) override
        {
            if (type == "asset")
            {
                for (auto entry = pendingFolderDrops.begin(); entry != pendingFolderDrops.end(); ++entry)
                {
                    if (entry->first != lastStringNode) { continue; }
                    const std::string payload = entry->second;
                    pendingFolderDrops.erase(entry);
                    return payload;
                }
            }

            if (type == "asset")
            {
                // Matched against the last field *or* the last tree node: an asset slot in the
                // inspector is a labelled field, a row in the missing-references report is a node.
                for (auto entry = pendingAssetDrops.begin(); entry != pendingAssetDrops.end(); ++entry)
                {
                    if (entry->first != lastField && entry->first != lastNode.toString()) { continue; }
                    const std::string payload = entry->second;
                    pendingAssetDrops.erase(entry);
                    return payload;
                }
                return std::nullopt;
            }

            for (auto entry = pendingDrops.begin(); entry != pendingDrops.end(); ++entry)
            {
                if (entry->first != lastNode) { continue; }
                const std::string payload = entry->second;
                pendingDrops.erase(entry);
                return payload;
            }
            return std::nullopt;
        }

        UiTextFieldResult inputText(const std::string& id, std::string& text, bool takeFocus) override
        {
            (void)takeFocus;
            drawnTextFields.push_back(id);

            // Only a rename field takes the scripted text. The asset browser's filter is an
            // inputText too, and typing a scripted name into it would silently make the panel
            // show nothing while the test believed it had renamed something.
            const bool isRename = id.rfind("##rename-", 0) == 0;
            if (isRename) { sawRenameField = true; }

            for (auto entry = pendingFieldEdits.begin(); entry != pendingFieldEdits.end(); ++entry)
            {
                if (entry->first != id) { continue; }
                text = entry->second;
                pendingFieldEdits.erase(entry);
                return UiTextFieldResult{true, true};
            }

            if (!isRename || !pendingRename) { return UiTextFieldResult{}; }

            UiTextFieldResult result;
            text = pendingRename->first;
            result.changed = true;
            result.committed = pendingRename->second;
            if (result.committed) { pendingRename.reset(); }
            return result;
        }

        /** @brief Ids of text fields drawn during the most recent frame. */
        std::vector<std::string> drawnTextFields;

        /** @brief Text to type into the next field whose id is @p id, committing it. */
        void typeInto(const std::string& id, std::string text)
        {
            pendingFieldEdits.emplace_back(id, std::move(text));
        }

        /** @brief Scripted edits for fields other than renames, by id. */
        std::vector<std::pair<std::string, std::string>> pendingFieldEdits;

        void text(const std::string& value) override { drawnText.push_back(value); }

        /** @brief Text drawn during the most recent frame. */
        std::vector<std::string> drawnText;

        /** @brief Returns true when @p value was drawn last frame. */
        [[nodiscard]] bool sawText(const std::string& value) const
        {
            return std::find(drawnText.begin(), drawnText.end(), value) != drawnText.end();
        }

        bool checkbox(const std::string& label, bool& value) override
        {
            shownChecks.emplace_back(label, value);

            for (auto entry = pendingChecks.begin(); entry != pendingChecks.end(); ++entry)
            {
                if (entry->first != label) { continue; }
                value = entry->second;
                pendingChecks.erase(entry);
                return true;
            }
            return false;
        }

        /** @brief Checkbox states to apply, as (label, new value) pairs. Each fires once. */
        std::vector<std::pair<std::string, bool>> pendingChecks;

        /** @brief Checkbox states shown during the most recent frame. */
        std::vector<std::pair<std::string, bool>> shownChecks;

        /** @brief Returns the state @p label was drawn with, defaulting to false. */
        [[nodiscard]] bool shownCheckFor(const std::string& label) const
        {
            for (const auto& [field, value] : shownChecks)
            {
                if (field == label) { return value; }
            }
            return false;
        }

        /**
         * @brief Menu-bar labels to report as open, so a test can see what is inside one.
         *
         * Opt-in rather than "every menu is always open", because what a menu *offers* is itself
         * a decision worth asserting on -- an item that appears only in the 3D view has to be
         * absent in 2D, and a UI that opened everything could not tell the two apart.
         */
        std::vector<std::string> openMenus;

        bool beginMenu(const std::string& label) override
        {
            return std::find(openMenus.begin(), openMenus.end(), label) != openMenus.end();
        }

        void endMenu() override {}

        /** @brief Context-menu ids to report as open, and menu-item labels to report as clicked. */
        std::vector<std::string> openContextMenus;
        std::vector<std::string> pendingMenuClicks;
        std::vector<std::string> drawnMenuItems;

        bool beginContextMenu(const std::string& id) override
        {
            return std::find(openContextMenus.begin(), openContextMenus.end(), id)
                != openContextMenus.end();
        }

        void endContextMenu() override {}

        bool menuItem(const std::string& label,
                      const std::string& shortcut = {},
                      bool enabled = true) override
        {
            drawnMenuItems.push_back(label);
            if (!enabled) { return false; }

            for (auto entry = pendingMenuClicks.begin(); entry != pendingMenuClicks.end(); ++entry)
            {
                if (*entry != label) { continue; }
                pendingMenuClicks.erase(entry);
                return true;
            }
            (void)shortcut;
            return false;
        }

        /** @brief Returns true when a menu item with @p label was drawn last frame. */
        [[nodiscard]] bool sawMenuItem(const std::string& label) const
        {
            return std::find(drawnMenuItems.begin(), drawnMenuItems.end(), label) != drawnMenuItems.end();
        }

        bool button(const std::string& label) override
        {
            drawnButtons.push_back(label);

            for (auto entry = pendingClicks.begin(); entry != pendingClicks.end(); ++entry)
            {
                if (*entry != label) { continue; }
                pendingClicks.erase(entry);
                return true;
            }
            return false;
        }

        bool propertyField(const std::string& label,
                           PropertyValue& value,
                           const std::vector<std::string>& enumOptions = {},
                           bool readOnly = false) override
        {
            (void)readOnly;
            if (!enumOptions.empty()) { offeredOptions.emplace_back(label, enumOptions); }
            shownValues.emplace_back(label, value);
            lastField = label;

            for (auto entry = pendingChoices.begin(); entry != pendingChoices.end(); ++entry)
            {
                if (entry->first != label) { continue; }
                value = PropertyValue{PropertyValue::EnumValue{entry->second}};
                pendingChoices.erase(entry);
                return true;
            }

            for (auto entry = pendingEdits.begin(); entry != pendingEdits.end(); ++entry)
            {
                if (entry->first != label) { continue; }
                value = entry->second;
                pendingEdits.erase(entry);
                return true;
            }
            return false;
        }

        /** @brief Returns true when a button with @p label was drawn last frame. */
        [[nodiscard]] bool sawButton(const std::string& label) const
        {
            return std::find(drawnButtons.begin(), drawnButtons.end(), label) != drawnButtons.end();
        }

        /** @brief Returns the value last shown for @p label. */
        [[nodiscard]] PropertyValue shownValueFor(const std::string& label) const
        {
            for (const auto& [field, value] : shownValues)
            {
                if (field == label) { return value; }
            }
            return PropertyValue{};
        }

        /** @brief Returns the options last offered for @p label, or an empty list. */
        [[nodiscard]] std::vector<std::string> optionsFor(const std::string& label) const
        {
            for (const auto& [field, options] : offeredOptions)
            {
                if (field == label) { return options; }
            }
            return {};
        }
    };

    /** @brief Application, its scripted UI, and the id of the one entity in its scene. */
    struct GizmoFixture
    {
        std::unique_ptr<EditorApplication> application;
        ScriptedUi* ui = nullptr;
        Uuid entityId;

        /** @brief Feeds one frame of pointer input at a viewport-local point. */
        void step(const UiImageInteraction& input)
        {
            ui->interaction = input;
            application->renderFrame();
        }

        /** @brief Returns true when any console message contains @p needle. */
        [[nodiscard]] bool logContains(std::string_view needle) const
        {
            for (const auto& entry : ui->getLog())
            {
                if (entry.message.find(needle) != std::string::npos) { return true; }
            }
            return false;
        }

        [[nodiscard]] EditorVector3 getPosition() const
        {
            return getTransformProperty("position").get<EditorVector3>();
        }

        [[nodiscard]] EditorVector3 getScale() const
        {
            return getTransformProperty("scale").get<EditorVector3>(EditorVector3{1.0f, 1.0f, 1.0f});
        }

        /** @brief Returns the entity's local rotation about Z, in radians. */
        [[nodiscard]] float getRotationZ() const
        {
            return zRotationOf(getTransformProperty("rotation").get<EditorQuaternion>());
        }

        /** @brief Sets the entity's local rotation, as the scene itself would hold it. */
        void setRotationZ(float radians)
        {
            application->getContext().getScene()
                .findEntity(entityId)->findComponent(BuiltinComponentIds::kTransform)
                ->setProperty("rotation", PropertyValue{quaternionFromZRotation(radians)});
        }

        [[nodiscard]] PropertyValue getTransformProperty(const std::string& name) const
        {
            return application->getContext().getScene()
                .findEntity(entityId)->findComponent(BuiltinComponentIds::kTransform)
                ->getProperty(name);
        }
    };

    /**
     * @brief Builds an editor holding one entity at world (100, 220), selected.
     *
     * With the null UI's 1280x720 content region and the camera's defaults -- centre (0, 0), zoom 1
     * -- world (100, 220) lands at screen (740, 580), so the gizmo's origin and the coordinates the
     * tests press at are exact rather than approximate.
     */
    GizmoFixture makeGizmoFixture()
    {
        GizmoFixture fixture;
        auto ui = std::make_unique<ScriptedUi>();
        fixture.ui = ui.get();
        fixture.application =
            std::make_unique<EditorApplication>(std::move(ui), std::make_unique<NullEditorViewport>());

        EditorOptions options;
        options.headless = true;
        fixture.application->initialize(options);

        EditorContext& context = fixture.application->getContext();
        EditorEntity entity{Uuid::generate(), "Player"};
        EditorComponent transform{BuiltinComponentIds::kTransform};
        transform.applyDefaults(*context.getComponentRegistry().find(BuiltinComponentIds::kTransform));
        transform.setProperty("position", PropertyValue{EditorVector3{100.0f, 220.0f, 0.0f}});
        entity.addComponent(std::move(transform));

        fixture.entityId = entity.getId();
        context.getScene().addEntity(std::move(entity));
        context.select(fixture.entityId);

        // One frame with no input, so the viewport has sized the camera before any press is
        // hit-tested against a gizmo whose position depends on that size.
        fixture.step(UiImageInteraction{});
        return fixture;
    }

    /** @brief A hovered frame with the left button down at a viewport-local point. */
    UiImageInteraction leftAt(float x, float y, bool pressed)
    {
        UiImageInteraction input;
        input.hovered = true;
        input.localMouseX = x;
        input.localMouseY = y;
        input.leftPressed = pressed;
        input.leftDown = true;
        return input;
    }
}

CNA_EDITOR_TEST(AGizmoDragMovesTheSelectedEntityAlongTheGrabbedAxis)
{
    GizmoFixture fixture = makeGizmoFixture();

    // (790, 580) is on the X arm: the origin is at (740, 580) and the arm runs 72 pixels right.
    fixture.step(leftAt(790.0f, 580.0f, true));
    fixture.step(leftAt(830.0f, 580.0f, false));
    fixture.step(leftAt(830.0f, 620.0f, false));

    UiImageInteraction release;
    release.hovered = true;
    release.localMouseX = 830.0f;
    release.localMouseY = 620.0f;
    release.leftReleased = true;
    fixture.step(release);

    // Moved 40 along X, and not at all along Y despite the pointer having moved 40 down.
    CNA_EDITOR_EXPECT_EQ(fixture.getPosition().x, 140.0f);
    CNA_EDITOR_EXPECT_EQ(fixture.getPosition().y, 220.0f);
}

CNA_EDITOR_TEST(AGizmoDragOnTheCentreHandleMovesOnBothAxes)
{
    GizmoFixture fixture = makeGizmoFixture();

    fixture.step(leftAt(740.0f, 580.0f, true));
    fixture.step(leftAt(765.0f, 605.0f, false));

    CNA_EDITOR_EXPECT_EQ(fixture.getPosition().x, 125.0f);
    CNA_EDITOR_EXPECT_EQ(fixture.getPosition().y, 245.0f);
}

CNA_EDITOR_TEST(AGizmoDragIsOneUndoEntryThatReturnsToWhereItStarted)
{
    GizmoFixture fixture = makeGizmoFixture();
    CommandHistory& history = fixture.application->getContext().getHistory();
    const std::size_t before = history.getCount();

    fixture.step(leftAt(790.0f, 580.0f, true));
    for (float x = 795.0f; x <= 830.0f; x += 5.0f) { fixture.step(leftAt(x, 580.0f, false)); }

    UiImageInteraction release;
    release.hovered = true;
    release.localMouseX = 830.0f;
    release.localMouseY = 580.0f;
    release.leftReleased = true;
    fixture.step(release);

    // Eight moved frames, one entry: this is what MergePolicy::MergeWithPrevious is for.
    CNA_EDITOR_EXPECT_EQ(history.getCount(), before + 1);
    CNA_EDITOR_EXPECT(history.undo());
    CNA_EDITOR_EXPECT_EQ(fixture.getPosition().x, 100.0f);
}

CNA_EDITOR_TEST(TwoGizmoDragsAreTwoUndoEntries)
{
    GizmoFixture fixture = makeGizmoFixture();
    CommandHistory& history = fixture.application->getContext().getHistory();
    const std::size_t before = history.getCount();

    fixture.step(leftAt(790.0f, 580.0f, true));
    fixture.step(leftAt(810.0f, 580.0f, false));
    fixture.step(UiImageInteraction{});

    // The second drag grabs the arm at its new place -- the gizmo moved with the entity.
    fixture.step(leftAt(810.0f, 580.0f, true));
    fixture.step(leftAt(830.0f, 580.0f, false));
    fixture.step(UiImageInteraction{});

    CNA_EDITOR_EXPECT_EQ(fixture.getPosition().x, 140.0f);

    // Two entries, not one. The merge key is entity + component + property and matches across both
    // drags, so only the interaction boundary keeps them apart -- undoing a move the user made a
    // minute ago because they later moved the same entity again is a real way to lose work.
    CNA_EDITOR_EXPECT_EQ(history.getCount(), before + 2);
    CNA_EDITOR_EXPECT(history.undo());
    CNA_EDITOR_EXPECT_EQ(fixture.getPosition().x, 120.0f);
}

CNA_EDITOR_TEST(AGizmoDragSurvivesThePointerLeavingTheViewport)
{
    GizmoFixture fixture = makeGizmoFixture();

    fixture.step(leftAt(790.0f, 580.0f, true));

    // Still held, no longer over the panel. Ending the drag here would drop the entity wherever
    // the pointer happened to cross the edge.
    UiImageInteraction outside;
    outside.hovered = false;
    outside.leftDown = true;
    outside.localMouseX = 830.0f;
    outside.localMouseY = 580.0f;
    fixture.step(outside);

    CNA_EDITOR_EXPECT_EQ(fixture.getPosition().x, 140.0f);
}

CNA_EDITOR_TEST(AGizmoPressDoesNotAlsoChangeTheSelection)
{
    GizmoFixture fixture = makeGizmoFixture();

    // The entity has no SpriteRenderer, so the picker would find nothing here and deselect it --
    // which is exactly what must not happen when the press landed on a handle.
    UiImageInteraction press = leftAt(790.0f, 580.0f, true);
    press.clicked = true;
    fixture.step(press);

    CNA_EDITOR_EXPECT_EQ(fixture.application->getContext().getPrimarySelection().toString(),
                         fixture.entityId.toString());
}

CNA_EDITOR_TEST(PressingAHandleWithoutMovingLeavesTheHistoryAlone)
{
    GizmoFixture fixture = makeGizmoFixture();
    CommandHistory& history = fixture.application->getContext().getHistory();
    const std::size_t before = history.getCount();

    fixture.step(leftAt(790.0f, 580.0f, true));
    fixture.step(leftAt(790.0f, 580.0f, false));

    UiImageInteraction release;
    release.hovered = true;
    release.localMouseX = 790.0f;
    release.localMouseY = 580.0f;
    release.leftReleased = true;
    fixture.step(release);

    // An undo entry that restores the position the entity already had costs the user an undo to
    // reach a change they can actually see.
    CNA_EDITOR_EXPECT_EQ(history.getCount(), before);
}

CNA_EDITOR_TEST(APressAwayFromEveryHandleStillSelects)
{
    GizmoFixture fixture = makeGizmoFixture();

    UiImageInteraction press = leftAt(200.0f, 200.0f, true);
    fixture.step(press);

    UiImageInteraction click;
    click.hovered = true;
    click.localMouseX = 200.0f;
    click.localMouseY = 200.0f;
    click.clicked = true;
    click.leftReleased = true;
    fixture.step(click);

    // Nothing is under (200, 200), so the click clears the selection -- proof the press was not
    // swallowed by the gizmo.
    CNA_EDITOR_EXPECT(!fixture.application->getContext().getPrimarySelection().isValid());
}

CNA_EDITOR_TEST(ShortcutsDriveUndoAndRedo)
{
    GizmoFixture fixture = makeGizmoFixture();

    fixture.step(leftAt(790.0f, 580.0f, true));
    fixture.step(leftAt(830.0f, 580.0f, false));
    fixture.step(UiImageInteraction{});
    CNA_EDITOR_EXPECT_EQ(fixture.getPosition().x, 140.0f);

    fixture.ui->pressShortcut(UiKey::Z, withControl());
    fixture.step(UiImageInteraction{});
    CNA_EDITOR_EXPECT_EQ(fixture.getPosition().x, 100.0f);

    fixture.ui->pressShortcut(UiKey::Y, withControl());
    fixture.step(UiImageInteraction{});
    CNA_EDITOR_EXPECT_EQ(fixture.getPosition().x, 140.0f);
}

CNA_EDITOR_TEST(DeleteRemovesTheSelectionAndClearsIt)
{
    GizmoFixture fixture = makeGizmoFixture();
    EditorContext& context = fixture.application->getContext();
    const std::size_t before = context.getScene().getEntityCount();

    fixture.ui->pressShortcut(UiKey::Delete);
    fixture.step(UiImageInteraction{});

    CNA_EDITOR_EXPECT_EQ(context.getScene().getEntityCount(), before - 1);

    // The selection must not be left pointing at an entity that is gone: the inspector would go on
    // showing it, and the next command would target a missing id.
    CNA_EDITOR_EXPECT(!context.getPrimarySelection().isValid());

    fixture.ui->pressShortcut(UiKey::Z, withControl());
    fixture.step(UiImageInteraction{});
    CNA_EDITOR_EXPECT_EQ(context.getScene().getEntityCount(), before);
}

CNA_EDITOR_TEST(DuplicateCopiesTheSubtreeWithFreshIdsAndSelectsTheCopy)
{
    GizmoFixture fixture = makeGizmoFixture();
    EditorContext& context = fixture.application->getContext();

    // Give the entity a child, so the duplicate has a subtree to get wrong.
    EditorEntity child{Uuid::generate(), "Weapon"};
    EditorComponent transform{BuiltinComponentIds::kTransform};
    transform.applyDefaults(*context.getComponentRegistry().find(BuiltinComponentIds::kTransform));
    child.addComponent(std::move(transform));
    child.setParentId(fixture.entityId);
    const Uuid childId = context.getScene().addEntity(std::move(child));

    const std::size_t before = context.getScene().getEntityCount();

    fixture.ui->pressShortcut(UiKey::D, withControl());
    fixture.step(UiImageInteraction{});

    CNA_EDITOR_EXPECT_EQ(context.getScene().getEntityCount(), before + 2);

    const Uuid copyId = context.getPrimarySelection();
    CNA_EDITOR_EXPECT(copyId.isValid());
    CNA_EDITOR_EXPECT(copyId != fixture.entityId);

    // The copied root is a sibling of the original, not its child.
    const EditorEntity* copy = context.getScene().findEntity(copyId);
    CNA_EDITOR_EXPECT_EQ(copy->getParentId().toString(), Uuid{}.toString());
    CNA_EDITOR_EXPECT_EQ(copy->getName(), std::string{"Player Copy"});

    // The copied child hangs off the *copy*, not off the original -- a remapping mistake here is
    // invisible until the user moves one of them and both jump.
    const std::vector<Uuid> copiedChildren = context.getScene().getChildren(copyId);
    CNA_EDITOR_EXPECT_EQ(copiedChildren.size(), std::size_t{1});
    CNA_EDITOR_EXPECT(copiedChildren.front() != childId);
    CNA_EDITOR_EXPECT_EQ(context.getScene().getChildren(fixture.entityId).size(), std::size_t{1});

    // One undo entry for the whole subtree.
    fixture.ui->pressShortcut(UiKey::Z, withControl());
    fixture.step(UiImageInteraction{});
    CNA_EDITOR_EXPECT_EQ(context.getScene().getEntityCount(), before);
}

CNA_EDITOR_TEST(FrameSelectedBringsTheSelectionIntoView)
{
    GizmoFixture fixture = makeGizmoFixture();

    // Look somewhere far away, so framing has real work to do.
    EditorCamera2D& camera = fixture.application->getViewport().getCamera();
    camera.setCenter(EditorVector2{9000.0f, 9000.0f});

    fixture.ui->pressShortcut(UiKey::F);
    fixture.step(UiImageInteraction{});

    // The entity has no sprite, so there are no bounds to fit -- framing must still centre on it
    // rather than quietly do nothing.
    CNA_EDITOR_EXPECT_EQ(camera.getCenter().x, 100.0f);
    CNA_EDITOR_EXPECT_EQ(camera.getCenter().y, 220.0f);
}

CNA_EDITOR_TEST(TheComparisonHarnessIsOffUnlessAskedFor)
{
    const EditorOptions plain = EditorOptions::parse(0, nullptr);
    CNA_EDITOR_EXPECT(!plain.compareBackends);
    CNA_EDITOR_EXPECT_EQ(plain.comparisonTolerance, kDefaultImageTolerance);

    const char* argv[] = {"cna-editor", "--compare-backends", "--tolerance=7"};
    const EditorOptions asked = EditorOptions::parse(3, argv);
    CNA_EDITOR_EXPECT(asked.compareBackends);
    CNA_EDITOR_EXPECT_EQ(asked.comparisonTolerance, 7);
    CNA_EDITOR_EXPECT(!asked.hasError);

    // A tolerance that is not a number is a mistake worth reporting: silently taking the default
    // would run the comparison at a threshold the user did not choose and never be mentioned.
    const char* bad[] = {"cna-editor", "--tolerance=loose"};
    CNA_EDITOR_EXPECT(EditorOptions::parse(2, bad).hasError);
}

CNA_EDITOR_TEST(TheComparisonHarnessReportsWhenItCannotRun)
{
    auto ui = std::make_unique<ScriptedUi>();
    ScriptedUi* rawUi = ui.get();
    EditorApplication application{std::move(ui), std::make_unique<NullEditorViewport>()};

    EditorOptions options;
    options.headless = true;
    options.compareBackends = true;
    CNA_EDITOR_EXPECT(application.initialize(options));

    application.renderFrame();

    // No project and no player builds, so the run refuses before launching anything -- and the
    // harness must not then sit waiting for a verdict that cannot arrive.
    CNA_EDITOR_EXPECT(!rawUi->isRunning());
    CNA_EDITOR_EXPECT(application.getBackendComparison().getState() == ComparisonState::Idle);
}

CNA_EDITOR_TEST(GizmoModeShortcutsSwitchTheManipulator)
{
    GizmoFixture fixture = makeGizmoFixture();

    fixture.ui->pressShortcut(UiKey::E);
    fixture.step(UiImageInteraction{});

    // Only the manipulator on screen can be grabbed. (790, 580) is on the translate gizmo's X arm
    // and inside the rotate gizmo's ring, so in rotate mode the press must do nothing to the
    // position -- a mode switch that left the old handles live would be invisible and awful.
    fixture.step(leftAt(790.0f, 580.0f, true));
    fixture.step(leftAt(830.0f, 580.0f, false));
    CNA_EDITOR_EXPECT_EQ(fixture.getPosition().x, 100.0f);

    fixture.ui->pressShortcut(UiKey::W);
    fixture.step(UiImageInteraction{});

    fixture.step(leftAt(790.0f, 580.0f, true));
    fixture.step(leftAt(830.0f, 580.0f, false));
    CNA_EDITOR_EXPECT_EQ(fixture.getPosition().x, 140.0f);
}

CNA_EDITOR_TEST(ARotateDragTurnsTheSelectedEntity)
{
    GizmoFixture fixture = makeGizmoFixture();

    fixture.ui->pressShortcut(UiKey::E);
    fixture.step(UiImageInteraction{});

    // The ring is 68 pixels out from the origin at (740, 580). Grab it at its rightmost point and
    // sweep a quarter turn to its lowest -- world Y points down, so that is a positive turn.
    fixture.step(leftAt(808.0f, 580.0f, true));
    fixture.step(leftAt(740.0f, 648.0f, false));

    CNA_EDITOR_EXPECT(std::fabs(fixture.getRotationZ() - 3.14159265f * 0.5f) < 0.01f);

    // Rotating must not shift the entity: the pivot is the entity's own position.
    CNA_EDITOR_EXPECT_EQ(fixture.getPosition().x, 100.0f);
    CNA_EDITOR_EXPECT_EQ(fixture.getPosition().y, 220.0f);
}

CNA_EDITOR_TEST(ARotateDragIsOneUndoEntryThatReturnsToWhereItStarted)
{
    GizmoFixture fixture = makeGizmoFixture();
    CommandHistory& history = fixture.application->getContext().getHistory();

    fixture.ui->pressShortcut(UiKey::E);
    fixture.step(UiImageInteraction{});
    const std::size_t before = history.getCount();

    fixture.step(leftAt(808.0f, 580.0f, true));
    for (float y = 590.0f; y <= 640.0f; y += 10.0f) { fixture.step(leftAt(800.0f, y, false)); }
    fixture.step(UiImageInteraction{});

    CNA_EDITOR_EXPECT_EQ(history.getCount(), before + 1);
    CNA_EDITOR_EXPECT(history.undo());
    CNA_EDITOR_EXPECT(std::fabs(fixture.getRotationZ()) < 0.001f);
}

CNA_EDITOR_TEST(APressInsideTheRotateRingStillReachesThePicker)
{
    GizmoFixture fixture = makeGizmoFixture();

    fixture.ui->pressShortcut(UiKey::E);
    fixture.step(UiImageInteraction{});

    // Well inside the ring, which is deliberately not a target: the entity lives there, and a
    // gizmo that swallowed presses over its own subject would make it unclickable.
    UiImageInteraction press = leftAt(750.0f, 590.0f, true);
    fixture.step(press);

    UiImageInteraction click;
    click.hovered = true;
    click.localMouseX = 750.0f;
    click.localMouseY = 590.0f;
    click.clicked = true;
    click.leftReleased = true;
    fixture.step(click);

    CNA_EDITOR_EXPECT(!fixture.application->getContext().getPrimarySelection().isValid());
}

CNA_EDITOR_TEST(AScaleDragResizesTheSelectedEntity)
{
    GizmoFixture fixture = makeGizmoFixture();

    fixture.ui->pressShortcut(UiKey::R);
    fixture.step(UiImageInteraction{});

    // The X handle sits 64 pixels right of the origin at (740, 580). Dragging it to twice that
    // distance doubles the X scale and leaves Y alone.
    fixture.step(leftAt(804.0f, 580.0f, true));
    fixture.step(leftAt(868.0f, 580.0f, false));

    CNA_EDITOR_EXPECT(std::fabs(fixture.getScale().x - 2.0f) < 0.01f);
    CNA_EDITOR_EXPECT(std::fabs(fixture.getScale().y - 1.0f) < 0.01f);
    CNA_EDITOR_EXPECT_EQ(fixture.getPosition().x, 100.0f);
}

namespace
{
    /** @brief Adds a transform-only entity, optionally parented, and returns its id. */
    Uuid addPlainEntity(EditorContext& context, const std::string& name, const Uuid& parentId)
    {
        EditorEntity entity{Uuid::generate(), name};
        EditorComponent transform{BuiltinComponentIds::kTransform};
        transform.applyDefaults(*context.getComponentRegistry().find(BuiltinComponentIds::kTransform));
        entity.addComponent(std::move(transform));

        const Uuid id = context.getScene().addEntity(std::move(entity));
        if (parentId.isValid()) { context.getScene().reparentEntity(id, parentId); }
        return id;
    }
}

CNA_EDITOR_TEST(DuplicatingASelectionIsOneUndoEntry)
{
    GizmoFixture fixture = makeGizmoFixture();
    EditorContext& context = fixture.application->getContext();
    CommandHistory& history = context.getHistory();

    const Uuid crate = addPlainEntity(context, "Crate", Uuid{});
    addPlainEntity(context, "Lid", crate);
    const std::size_t populated = context.getScene().getEntityCount();

    context.setSelection({fixture.entityId, crate});
    fixture.step(UiImageInteraction{});

    const std::size_t before = history.getCount();
    fixture.ui->pressShortcut(UiKey::D, withControl());
    fixture.step(UiImageInteraction{});

    // Two entities duplicated -- three new ones, since the crate brings its lid -- and one undo
    // entry for the lot, rather than one per entity.
    CNA_EDITOR_EXPECT_EQ(history.getCount(), before + 1);
    CNA_EDITOR_EXPECT_EQ(context.getScene().getEntityCount(), populated + 3);

    CNA_EDITOR_EXPECT(history.undo());
    CNA_EDITOR_EXPECT_EQ(context.getScene().getEntityCount(), populated);
}

CNA_EDITOR_TEST(DeletingASelectionIsOneUndoEntry)
{
    GizmoFixture fixture = makeGizmoFixture();
    EditorContext& context = fixture.application->getContext();
    CommandHistory& history = context.getHistory();

    const Uuid crate = addPlainEntity(context, "Crate", Uuid{});
    const Uuid lid = addPlainEntity(context, "Lid", crate);
    const std::size_t populated = context.getScene().getEntityCount();

    // Three selected, one of them a child of another.
    context.setSelection({fixture.entityId, crate, lid});
    fixture.step(UiImageInteraction{});

    const std::size_t before = history.getCount();
    fixture.ui->pressShortcut(UiKey::Delete);
    fixture.step(UiImageInteraction{});

    // All three gone, in one entry: deleting a child whose parent is also selected is not a second
    // command, because the parent's delete took the subtree with it.
    CNA_EDITOR_EXPECT_EQ(history.getCount(), before + 1);
    CNA_EDITOR_EXPECT_EQ(context.getScene().getEntityCount(), populated - 3);

    // And one Ctrl+Z brings all of them back, rather than one at a time through arrangements the
    // scene was never in.
    CNA_EDITOR_EXPECT(history.undo());
    CNA_EDITOR_EXPECT_EQ(context.getScene().getEntityCount(), populated);
    CNA_EDITOR_EXPECT(context.getScene().findEntity(lid) != nullptr);
}

CNA_EDITOR_TEST(AGizmoDragOnAMultiSelectionMovesEveryEntityAsOneUndoEntry)
{
    GizmoFixture fixture = makeGizmoFixture();
    EditorContext& context = fixture.application->getContext();
    CommandHistory& history = context.getHistory();

    // A second entity 200 units to the right of the fixture's own, so the shared pivot is 100
    // units right of the first -- screen 840 with the null UI's 1280-wide viewport.
    EditorEntity second{Uuid::generate(), "Crate"};
    EditorComponent transform{BuiltinComponentIds::kTransform};
    transform.applyDefaults(*context.getComponentRegistry().find(BuiltinComponentIds::kTransform));
    transform.setProperty("position", PropertyValue{EditorVector3{300.0f, 220.0f, 0.0f}});
    second.addComponent(std::move(transform));

    const Uuid secondId = context.getScene().addEntity(std::move(second));
    context.setSelection({fixture.entityId, secondId});
    fixture.step(UiImageInteraction{});

    const std::size_t before = history.getCount();

    // The gizmo sits on the pivot at world x=200, screen 840. Grab its X arm 50 pixels along and
    // drag 30 to the right.
    fixture.step(leftAt(890.0f, 580.0f, true));
    fixture.step(leftAt(920.0f, 580.0f, false));
    fixture.step(UiImageInteraction{});

    const auto positionOf = [&](const Uuid& id) {
        return context.getScene().findEntity(id)->findComponent(BuiltinComponentIds::kTransform)
            ->getProperty("position").get<EditorVector3>();
    };

    // Both moved, by the same amount: a selection keeps its shape.
    CNA_EDITOR_EXPECT_EQ(positionOf(fixture.entityId).x, 130.0f);
    CNA_EDITOR_EXPECT_EQ(positionOf(secondId).x, 330.0f);

    // One drag, one entry -- and it undoes both at once. A command per entity would take two
    // presses of Ctrl+Z and would pass through an arrangement the scene was never in.
    CNA_EDITOR_EXPECT_EQ(history.getCount(), before + 1);
    CNA_EDITOR_EXPECT(history.undo());
    CNA_EDITOR_EXPECT_EQ(positionOf(fixture.entityId).x, 100.0f);
    CNA_EDITOR_EXPECT_EQ(positionOf(secondId).x, 300.0f);
}

CNA_EDITOR_TEST(AMultiSelectionGizmoLeavesAChildOfASelectedParentAlone)
{
    GizmoFixture fixture = makeGizmoFixture();
    EditorContext& context = fixture.application->getContext();

    EditorEntity child{Uuid::generate(), "Hat"};
    EditorComponent transform{BuiltinComponentIds::kTransform};
    transform.applyDefaults(*context.getComponentRegistry().find(BuiltinComponentIds::kTransform));
    transform.setProperty("position", PropertyValue{EditorVector3{0.0f, -20.0f, 0.0f}});
    child.addComponent(std::move(transform));

    const Uuid childId = context.getScene().addEntity(std::move(child));
    context.getScene().reparentEntity(childId, fixture.entityId);
    context.setSelection({fixture.entityId, childId});
    fixture.step(UiImageInteraction{});

    // The pivot is the average of parent (100, 220) and child (100, 200) -- world (100, 210),
    // screen (740, 570). Grab the X arm and drag 30 right.
    fixture.step(leftAt(790.0f, 570.0f, true));
    fixture.step(leftAt(820.0f, 570.0f, false));
    fixture.step(UiImageInteraction{});

    const auto positionOf = [&](const Uuid& id) {
        return context.getScene().findEntity(id)->findComponent(BuiltinComponentIds::kTransform)
            ->getProperty("position").get<EditorVector3>();
    };

    // The parent moved and the child's *local* position did not: it is carried by its parent, and
    // moving it too would move it twice.
    CNA_EDITOR_EXPECT_EQ(positionOf(fixture.entityId).x, 130.0f);
    CNA_EDITOR_EXPECT_EQ(positionOf(childId).x, 0.0f);
}

CNA_EDITOR_TEST(TwoInspectorDragsOfOneFieldAreTwoUndoEntries)
{
    GizmoFixture fixture = makeGizmoFixture();
    CommandHistory& history = fixture.application->getContext().getHistory();
    const std::size_t before = history.getCount();

    // A drag of a slider: several edits with the widget still held. They fold into one entry, which
    // is what MergeWithPrevious is for -- one undo should undo the whole drag.
    UiImageInteraction held;
    held.leftDown = true;

    for (float x : {110.0f, 120.0f, 130.0f})
    {
        fixture.ui->pendingEdits.emplace_back("Position",
                                              PropertyValue{EditorVector3{x, 220.0f, 0.0f}});
        fixture.step(held);
    }
    CNA_EDITOR_EXPECT_EQ(history.getCount(), before + 1);
    CNA_EDITOR_EXPECT_EQ(fixture.getPosition().x, 130.0f);

    // Let go, then drag the same field again. The merge key is identical -- same entity, same
    // component, same property -- so only the interaction boundary keeps the two apart.
    fixture.step(UiImageInteraction{});

    fixture.ui->pendingEdits.emplace_back("Position", PropertyValue{EditorVector3{200.0f, 220.0f, 0.0f}});
    fixture.step(held);

    CNA_EDITOR_EXPECT_EQ(history.getCount(), before + 2);

    // And the first drag is still there to come back to. Undoing a change made a minute ago because
    // the same field was touched again is a real way to lose work.
    CNA_EDITOR_EXPECT(history.undo());
    CNA_EDITOR_EXPECT_EQ(fixture.getPosition().x, 130.0f);
    CNA_EDITOR_EXPECT(history.undo());
    CNA_EDITOR_EXPECT_EQ(fixture.getPosition().x, 100.0f);
}

CNA_EDITOR_TEST(HoldingTheSnapModifierRoundsAGizmoDragToTheGrid)
{
    GizmoFixture fixture = makeGizmoFixture();

    // The entity is at (100, 220) and the camera is at 1:1, so the grid the renderer draws is 50
    // world units apart -- and the snap uses that same function, which is the point of sharing it.
    UiImageInteraction press = leftAt(790.0f, 580.0f, true);
    press.control = true;
    fixture.step(press);

    UiImageInteraction drag = leftAt(853.0f, 580.0f, false);
    drag.control = true;
    fixture.step(drag);

    // Dragged 63 along X from 100, which lands on 150 rather than on 163.
    CNA_EDITOR_EXPECT_EQ(fixture.getPosition().x, 150.0f);

    // Y is untouched: the drag was constrained to the X arm, and snapping an axis the user
    // constrained out would move the entity somewhere they cannot see it going.
    CNA_EDITOR_EXPECT_EQ(fixture.getPosition().y, 220.0f);

    // Without the modifier the next drag lands exactly where the cursor put it. The gizmo has
    // moved with the entity, so its X arm now starts at screen 790.
    fixture.step(UiImageInteraction{});
    fixture.step(leftAt(840.0f, 580.0f, true));
    fixture.step(leftAt(850.0f, 580.0f, false));
    CNA_EDITOR_EXPECT_EQ(fixture.getPosition().x, 160.0f);
}

CNA_EDITOR_TEST(CtrlClickingAddsToTheSelectionAndClickingEmptySpaceWithItDoesNot)
{
    GizmoFixture fixture = makeGizmoFixture();
    EditorContext& context = fixture.application->getContext();

    // A second entity with a sprite, so the picker has something to find. The fixture's own entity
    // has no bounds at all, which is what makes "clicking empty space" easy to arrange.
    EditorEntity second{Uuid::generate(), "Prop"};
    EditorComponent transform{BuiltinComponentIds::kTransform};
    transform.applyDefaults(*context.getComponentRegistry().find(BuiltinComponentIds::kTransform));
    transform.setProperty("position", PropertyValue{EditorVector3{-300.0f, -100.0f, 0.0f}});
    second.addComponent(std::move(transform));

    EditorComponent sprite{BuiltinComponentIds::kSpriteRenderer};
    sprite.applyDefaults(*context.getComponentRegistry().find(BuiltinComponentIds::kSpriteRenderer));
    sprite.setProperty("sourceRectangle", PropertyValue{EditorRectangle{0, 0, 64, 64}});
    second.addComponent(std::move(sprite));

    const Uuid secondId = context.getScene().addEntity(std::move(second));
    fixture.step(UiImageInteraction{});

    // World (-300, -100) is screen (340, 260) at the null UI's 1280x720 with the default camera.
    UiImageInteraction click;
    click.hovered = true;
    click.localMouseX = 340.0f;
    click.localMouseY = 260.0f;
    click.clicked = true;
    click.leftReleased = true;
    click.control = true;
    fixture.step(click);

    CNA_EDITOR_EXPECT_EQ(context.getSelection().size(), std::size_t{2});
    CNA_EDITOR_EXPECT(context.isSelected(fixture.entityId));
    CNA_EDITOR_EXPECT(context.isSelected(secondId));

    // Ctrl on empty space does nothing: clearing a selection somebody is halfway through
    // assembling is the one outcome they cannot have meant.
    UiImageInteraction empty = click;
    empty.localMouseX = 1000.0f;
    empty.localMouseY = 100.0f;
    fixture.step(empty);
    CNA_EDITOR_EXPECT_EQ(context.getSelection().size(), std::size_t{2});

    // Ctrl again on the same entity removes it, which is what makes the modifier a toggle.
    fixture.step(click);
    CNA_EDITOR_EXPECT_EQ(context.getSelection().size(), std::size_t{1});

    // And a plain click still replaces the whole selection.
    UiImageInteraction plain = click;
    plain.control = false;
    fixture.step(plain);
    CNA_EDITOR_EXPECT_EQ(context.getSelection().size(), std::size_t{1});
    CNA_EDITOR_EXPECT(context.isSelected(secondId));
}

CNA_EDITOR_TEST(TheViewportToolbarShowsAndSetsTheGizmoModeAndSpace)
{
    GizmoFixture fixture = makeGizmoFixture();

    // The toolbar reports state the keys and the menu can only change. A user who cannot see which
    // manipulator is active has to press a key to find out what it was.
    fixture.ui->pendingChoices.emplace_back("##gizmo", "Scale");
    fixture.step(UiImageInteraction{});
    CNA_EDITOR_EXPECT(fixture.application->getGizmoMode() == GizmoMode::Scale);

    // The space button is labelled with the space it is *in*, so pressing "World" leaves Local.
    fixture.ui->pendingClicks.emplace_back("World##space");
    fixture.step(UiImageInteraction{});
    CNA_EDITOR_EXPECT(fixture.application->getGizmoSpace() == GizmoSpace::Local);

    fixture.ui->pendingClicks.emplace_back("Local##space");
    fixture.step(UiImageInteraction{});
    CNA_EDITOR_EXPECT(fixture.application->getGizmoSpace() == GizmoSpace::World);

    // And the keys still work, so the toolbar is a second way in rather than the only one.
    fixture.ui->pressShortcut(UiKey::W);
    fixture.step(UiImageInteraction{});
    CNA_EDITOR_EXPECT(fixture.application->getGizmoMode() == GizmoMode::Translate);
}

CNA_EDITOR_TEST(TheGizmoSpaceShortcutTogglesBothWays)
{
    GizmoFixture fixture = makeGizmoFixture();
    CNA_EDITOR_EXPECT(fixture.application->getGizmoSpace() == GizmoSpace::World);

    fixture.ui->pressShortcut(UiKey::X);
    fixture.step(UiImageInteraction{});
    CNA_EDITOR_EXPECT(fixture.application->getGizmoSpace() == GizmoSpace::Local);

    // Said out loud, because on an unrotated entity the two spaces look identical -- a user who
    // toggled and saw nothing would reasonably conclude the key was broken.
    CNA_EDITOR_EXPECT(fixture.logContains("Gizmo space: Local"));

    fixture.ui->pressShortcut(UiKey::X);
    fixture.step(UiImageInteraction{});
    CNA_EDITOR_EXPECT(fixture.application->getGizmoSpace() == GizmoSpace::World);
}

CNA_EDITOR_TEST(ALocalSpaceDragFollowsTheEntitysOwnAxis)
{
    GizmoFixture fixture = makeGizmoFixture();

    // Half a right angle, so the entity's own X arm runs diagonally and neither space can be
    // mistaken for the other.
    fixture.setRotationZ(3.14159265f * 0.25f);
    fixture.ui->pressShortcut(UiKey::X);
    fixture.step(UiImageInteraction{});

    // 50 pixels out along that diagonal arm from the origin at (740, 580).
    fixture.step(leftAt(775.36f, 615.36f, true));
    fixture.step(leftAt(815.36f, 615.36f, false));

    // A purely horizontal cursor move of 40 projects onto the arm as 28.28, which is 20 along each
    // world axis. In world space that press would have hit no handle at all.
    CNA_EDITOR_EXPECT(std::fabs(fixture.getPosition().x - 120.0f) < 0.1f);
    CNA_EDITOR_EXPECT(std::fabs(fixture.getPosition().y - 240.0f) < 0.1f);
}

CNA_EDITOR_TEST(AnArmedShortcutFiresExactlyOnce)
{
    GizmoFixture fixture = makeGizmoFixture();
    EditorContext& context = fixture.application->getContext();
    const std::size_t before = context.getScene().getEntityCount();

    fixture.ui->pressShortcut(UiKey::D, withControl());
    fixture.step(UiImageInteraction{});
    fixture.step(UiImageInteraction{});
    fixture.step(UiImageInteraction{});

    // Three frames, one duplicate. A shortcut that stayed armed would fill the scene.
    CNA_EDITOR_EXPECT_EQ(context.getScene().getEntityCount(), before + 1);
}

CNA_EDITOR_TEST(AddingAComponentThroughTheInspectorIsUndoable)
{
    GizmoFixture fixture = makeGizmoFixture();
    EditorContext& context = fixture.application->getContext();

    fixture.ui->pendingChoices.emplace_back("##addComponentType", "Rendering / Camera");
    fixture.step(UiImageInteraction{});

    fixture.ui->pendingClicks.push_back("Add Component");
    fixture.step(UiImageInteraction{});

    const EditorEntity* entity = context.getScene().findEntity(fixture.entityId);
    CNA_EDITOR_EXPECT(entity->findComponent(BuiltinComponentIds::kCamera) != nullptr);

    // Every edit goes through a command, so this one undoes like any other (D-06).
    fixture.ui->pressShortcut(UiKey::Z, withControl());
    fixture.step(UiImageInteraction{});
    CNA_EDITOR_EXPECT(context.getScene().findEntity(fixture.entityId)
                          ->findComponent(BuiltinComponentIds::kCamera) == nullptr);
}

CNA_EDITOR_TEST(TheAddPickerDropsAUniqueComponentTheEntityAlreadyHas)
{
    GizmoFixture fixture = makeGizmoFixture();

    fixture.step(UiImageInteraction{});
    const std::vector<std::string> before = fixture.ui->optionsFor("##addComponentType");

    // The transform is unique and already present, so it must never have been on offer.
    CNA_EDITOR_EXPECT(std::find(before.begin(), before.end(), "Transform") == before.end());
    CNA_EDITOR_EXPECT(std::find(before.begin(), before.end(), "Rendering / Camera") != before.end());

    fixture.ui->pendingChoices.emplace_back("##addComponentType", "Rendering / Camera");
    fixture.step(UiImageInteraction{});
    fixture.ui->pendingClicks.push_back("Add Component");
    fixture.step(UiImageInteraction{});
    fixture.step(UiImageInteraction{});

    // Offering it again would be offering an entry AddComponentCommand refuses.
    const std::vector<std::string> after = fixture.ui->optionsFor("##addComponentType");
    CNA_EDITOR_EXPECT(std::find(after.begin(), after.end(), "Rendering / Camera") == after.end());
}

CNA_EDITOR_TEST(ARequiredComponentGetsNoRemoveButton)
{
    GizmoFixture fixture = makeGizmoFixture();

    fixture.step(UiImageInteraction{});

    // The entity has only its transform, which is required: removing it would leave the entity
    // with no position at all, so the button is absent rather than present and dead.
    CNA_EDITOR_EXPECT(!fixture.ui->sawButton("Remove##0"));

    fixture.ui->pendingChoices.emplace_back("##addComponentType", "Rendering / Camera");
    fixture.step(UiImageInteraction{});
    fixture.ui->pendingClicks.push_back("Add Component");
    fixture.step(UiImageInteraction{});
    fixture.step(UiImageInteraction{});

    // The camera is not required, so it gets one -- at its own index.
    CNA_EDITOR_EXPECT(!fixture.ui->sawButton("Remove##0"));
    CNA_EDITOR_EXPECT(fixture.ui->sawButton("Remove##1"));
}

CNA_EDITOR_TEST(RemovingAComponentThroughTheInspectorIsUndoable)
{
    GizmoFixture fixture = makeGizmoFixture();
    EditorContext& context = fixture.application->getContext();

    fixture.ui->pendingChoices.emplace_back("##addComponentType", "Rendering / Camera");
    fixture.step(UiImageInteraction{});
    fixture.ui->pendingClicks.push_back("Add Component");
    fixture.step(UiImageInteraction{});

    fixture.ui->pendingClicks.push_back("Remove##1");
    fixture.step(UiImageInteraction{});

    const EditorEntity* entity = context.getScene().findEntity(fixture.entityId);
    CNA_EDITOR_EXPECT(entity->findComponent(BuiltinComponentIds::kCamera) == nullptr);

    // The transform survived: removing index 1 must not have taken index 0 with it.
    CNA_EDITOR_EXPECT(entity->findComponent(BuiltinComponentIds::kTransform) != nullptr);

    fixture.ui->pressShortcut(UiKey::Z, withControl());
    fixture.step(UiImageInteraction{});
    CNA_EDITOR_EXPECT(context.getScene().findEntity(fixture.entityId)
                          ->findComponent(BuiltinComponentIds::kCamera) != nullptr);
}

CNA_EDITOR_TEST(RotationIsEditedAsDegreesAndStoredAsAQuaternion)
{
    GizmoFixture fixture = makeGizmoFixture();
    EditorContext& context = fixture.application->getContext();

    fixture.ui->pendingEdits.emplace_back("Rotation (deg)",
                                          PropertyValue{EditorVector3{0.0f, 0.0f, 45.0f}});
    fixture.step(UiImageInteraction{});

    const EditorComponent* transform = context.getScene().findEntity(fixture.entityId)
                                           ->findComponent(BuiltinComponentIds::kTransform);
    const EditorQuaternion stored = transform->getProperty("rotation").get<EditorQuaternion>();

    // The scene keeps a quaternion; only the inspector deals in degrees. Storing angles would put
    // the convention in the file, where every reader would have to agree with it forever.
    CNA_EDITOR_EXPECT(stored == quaternionFromEulerDegrees(EditorVector3{0.0f, 0.0f, 45.0f}));

    fixture.step(UiImageInteraction{});
    const EditorVector3 shown = fixture.ui->shownValueFor("Rotation (deg)").get<EditorVector3>();
    CNA_EDITOR_EXPECT_EQ(shown.z, 45.0f);
}

CNA_EDITOR_TEST(TheInspectorKeepsTheAnglesTheUserTypedAtGimbalLock)
{
    GizmoFixture fixture = makeGizmoFixture();

    // A pitch of 90 degrees is a pole: yaw and roll are no longer separable, and reading the
    // quaternion back reports the same rotation as a *different* (yaw, roll) pair.
    const EditorVector3 typed{90.0f, 40.0f, 25.0f};
    fixture.ui->pendingEdits.emplace_back("Rotation (deg)", PropertyValue{typed});
    fixture.step(UiImageInteraction{});
    fixture.step(UiImageInteraction{});

    const EditorVector3 shown = fixture.ui->shownValueFor("Rotation (deg)").get<EditorVector3>();

    // Recomputing from the quaternion would show roll 0 and a folded yaw, so the two fields beside
    // the one being edited would jump the instant the pitch reached 90.
    CNA_EDITOR_EXPECT_EQ(shown.y, 40.0f);
    CNA_EDITOR_EXPECT_EQ(shown.z, 25.0f);

    // And the honest reading really does differ, which is what makes the cache worth having.
    CNA_EDITOR_EXPECT(eulerDegreesOf(quaternionFromEulerDegrees(typed)).z != 25.0f);
}

CNA_EDITOR_TEST(TheAngleCacheStopsApplyingOnceSomethingElseChangesTheRotation)
{
    GizmoFixture fixture = makeGizmoFixture();
    EditorContext& context = fixture.application->getContext();

    fixture.ui->pendingEdits.emplace_back("Rotation (deg)",
                                          PropertyValue{EditorVector3{90.0f, 40.0f, 25.0f}});
    fixture.step(UiImageInteraction{});
    fixture.step(UiImageInteraction{});
    CNA_EDITOR_EXPECT_EQ(fixture.ui->shownValueFor("Rotation (deg)").get<EditorVector3>().z, 25.0f);

    // Undo puts back a rotation the cache did not produce, so the cache must stop applying at once
    // -- otherwise the inspector would go on showing angles for a value the scene no longer holds.
    fixture.ui->pressShortcut(UiKey::Z, withControl());
    fixture.step(UiImageInteraction{});
    fixture.step(UiImageInteraction{});

    const EditorComponent* transform = context.getScene().findEntity(fixture.entityId)
                                           ->findComponent(BuiltinComponentIds::kTransform);
    const EditorQuaternion stored = transform->getProperty("rotation").get<EditorQuaternion>();

    const EditorVector3 shown = fixture.ui->shownValueFor("Rotation (deg)").get<EditorVector3>();
    const EditorVector3 honest = eulerDegreesOf(stored);
    CNA_EDITOR_EXPECT_EQ(shown.x, honest.x);
    CNA_EDITOR_EXPECT_EQ(shown.y, honest.y);
    CNA_EDITOR_EXPECT_EQ(shown.z, honest.z);
}

namespace
{
    /** @brief Adds a child entity named @p name under @p parentId and returns its id. */
    Uuid addChildEntity(EditorContext& context, const Uuid& parentId, const std::string& name)
    {
        EditorEntity entity{Uuid::generate(), name};
        EditorComponent transform{BuiltinComponentIds::kTransform};
        transform.applyDefaults(*context.getComponentRegistry().find(BuiltinComponentIds::kTransform));
        entity.addComponent(std::move(transform));
        entity.setParentId(parentId);
        return context.getScene().addEntity(std::move(entity));
    }
}

CNA_EDITOR_TEST(DoubleClickingAHierarchyNodeRenamesItInPlace)
{
    GizmoFixture fixture = makeGizmoFixture();
    EditorContext& context = fixture.application->getContext();

    fixture.ui->pendingNodeDoubleClicks.push_back(fixture.entityId);
    fixture.step(UiImageInteraction{});

    // The row is now a text field rather than a tree node.
    fixture.ui->pendingRename = std::make_pair(std::string{"Hero"}, true);
    fixture.step(UiImageInteraction{});
    CNA_EDITOR_EXPECT(fixture.ui->sawRenameField);

    CNA_EDITOR_EXPECT_EQ(context.getScene().findEntity(fixture.entityId)->getName(),
                         std::string{"Hero"});

    // And the field is gone once the edit commits.
    fixture.step(UiImageInteraction{});
    CNA_EDITOR_EXPECT(!fixture.ui->sawRenameField);

    fixture.ui->pressShortcut(UiKey::Z, withControl());
    fixture.step(UiImageInteraction{});
    CNA_EDITOR_EXPECT_EQ(context.getScene().findEntity(fixture.entityId)->getName(),
                         std::string{"Player"});
}

CNA_EDITOR_TEST(AnEmptyRenameIsTreatedAsASlipAndKeepsTheOldName)
{
    GizmoFixture fixture = makeGizmoFixture();
    EditorContext& context = fixture.application->getContext();
    const std::size_t before = context.getHistory().getCount();

    fixture.ui->pressShortcut(UiKey::F2);
    fixture.step(UiImageInteraction{});

    fixture.ui->pendingRename = std::make_pair(std::string{}, true);
    fixture.step(UiImageInteraction{});

    // An unnamed row in the hierarchy is unusable, and the old name is still right there to keep.
    CNA_EDITOR_EXPECT_EQ(context.getScene().findEntity(fixture.entityId)->getName(),
                         std::string{"Player"});
    CNA_EDITOR_EXPECT_EQ(context.getHistory().getCount(), before);
}

CNA_EDITOR_TEST(DraggingAnEntityOntoAnotherReparentsIt)
{
    GizmoFixture fixture = makeGizmoFixture();
    EditorContext& context = fixture.application->getContext();

    const Uuid otherId = addChildEntity(context, Uuid{}, "Crate");

    // Drop the Player onto the Crate.
    fixture.ui->pendingDrops.emplace_back(otherId, fixture.entityId.toString());
    fixture.step(UiImageInteraction{});

    CNA_EDITOR_EXPECT_EQ(context.getScene().findEntity(fixture.entityId)->getParentId().toString(),
                         otherId.toString());

    fixture.ui->pressShortcut(UiKey::Z, withControl());
    fixture.step(UiImageInteraction{});
    CNA_EDITOR_EXPECT(!context.getScene().findEntity(fixture.entityId)->getParentId().isValid());
}

CNA_EDITOR_TEST(DroppingAParentOntoItsOwnChildIsRefusedWithoutAnUndoEntry)
{
    GizmoFixture fixture = makeGizmoFixture();
    EditorContext& context = fixture.application->getContext();

    const Uuid childId = addChildEntity(context, fixture.entityId, "Weapon");
    const std::size_t before = context.getHistory().getCount();

    // Dropping the parent onto its own child would make a cycle. SceneDocument would refuse it
    // anyway, but the command would still land in the undo stack having done nothing.
    fixture.ui->pendingDrops.emplace_back(childId, fixture.entityId.toString());
    fixture.step(UiImageInteraction{});

    CNA_EDITOR_EXPECT(!context.getScene().findEntity(fixture.entityId)->getParentId().isValid());
    CNA_EDITOR_EXPECT_EQ(context.getScene().findEntity(childId)->getParentId().toString(),
                         fixture.entityId.toString());
    CNA_EDITOR_EXPECT_EQ(context.getHistory().getCount(), before);
    CNA_EDITOR_EXPECT(fixture.logContains("under one of its own children"));
}

CNA_EDITOR_TEST(DroppingAnEntityOntoItselfDoesNothing)
{
    GizmoFixture fixture = makeGizmoFixture();
    EditorContext& context = fixture.application->getContext();
    const std::size_t before = context.getHistory().getCount();

    fixture.ui->pendingDrops.emplace_back(fixture.entityId, fixture.entityId.toString());
    fixture.step(UiImageInteraction{});

    CNA_EDITOR_EXPECT_EQ(context.getHistory().getCount(), before);
}

CNA_EDITOR_TEST(CtrlClickExtendsTheHierarchySelection)
{
    GizmoFixture fixture = makeGizmoFixture();
    EditorContext& context = fixture.application->getContext();

    const Uuid otherId = addChildEntity(context, Uuid{}, "Crate");

    fixture.ui->modifiers = withControl();
    fixture.ui->pendingNodeClicks.push_back(otherId);
    fixture.step(UiImageInteraction{});

    CNA_EDITOR_EXPECT_EQ(context.getSelection().size(), std::size_t{2});
    CNA_EDITOR_EXPECT(context.isSelected(fixture.entityId));
    CNA_EDITOR_EXPECT(context.isSelected(otherId));

    // Without the modifier a click replaces the selection, which is the ordinary case.
    fixture.ui->modifiers = UiKeyModifiers{};
    fixture.ui->pendingNodeClicks.push_back(otherId);
    fixture.step(UiImageInteraction{});

    CNA_EDITOR_EXPECT_EQ(context.getSelection().size(), std::size_t{1});
    CNA_EDITOR_EXPECT(context.isSelected(otherId));
}

CNA_EDITOR_TEST(TheHierarchyKeepsDrawingWhileAReparentIsPending)
{
    GizmoFixture fixture = makeGizmoFixture();
    EditorContext& context = fixture.application->getContext();

    const Uuid otherId = addChildEntity(context, Uuid{}, "Crate");
    const Uuid childId = addChildEntity(context, fixture.entityId, "Weapon");

    fixture.ui->pendingDrops.emplace_back(otherId, fixture.entityId.toString());
    fixture.step(UiImageInteraction{});

    // The reparent runs after the tree has finished drawing, so the traversal never walked a
    // child list that was being reordered underneath it. Every entity is still reachable.
    fixture.step(UiImageInteraction{});
    CNA_EDITOR_EXPECT_EQ(fixture.ui->drawnNodes.size(), context.getScene().getEntityCount());
    CNA_EDITOR_EXPECT_EQ(context.getScene().findEntity(childId)->getParentId().toString(),
                         fixture.entityId.toString());
}

namespace
{
    /** @brief Registers an asset of @p type at @p path and returns its id. */
    Uuid addAsset(EditorContext& context, const std::string& path, AssetType type)
    {
        AssetRecord record;
        record.id = Uuid::generate();
        record.sourcePath = path;
        record.type = type;

        const Uuid id = record.id;
        context.getAssets().add(std::move(record));
        return id;
    }
}

CNA_EDITOR_TEST(DroppingATextureOntoASpriteSlotSetsIt)
{
    GizmoFixture fixture = makeGizmoFixture();
    EditorContext& context = fixture.application->getContext();

    fixture.ui->pendingChoices.emplace_back("##addComponentType", "Rendering / Sprite Renderer");
    fixture.step(UiImageInteraction{});
    fixture.ui->pendingClicks.push_back("Add Component");
    fixture.step(UiImageInteraction{});

    const Uuid textureId = addAsset(context, "Textures/Player.png", AssetType::Texture2D);

    fixture.ui->pendingAssetDrops.emplace_back("Texture", textureId.toString());
    fixture.step(UiImageInteraction{});

    const EditorComponent* sprite = context.getScene().findEntity(fixture.entityId)
                                        ->findComponent(BuiltinComponentIds::kSpriteRenderer);
    CNA_EDITOR_EXPECT_EQ(sprite->getProperty("texture").get<PropertyValue::AssetReference>().id.toString(),
                         textureId.toString());

    // Setting a reference by drop is an edit like any other.
    fixture.ui->pressShortcut(UiKey::Z, withControl());
    fixture.step(UiImageInteraction{});
    CNA_EDITOR_EXPECT(!context.getScene().findEntity(fixture.entityId)
                           ->findComponent(BuiltinComponentIds::kSpriteRenderer)
                           ->getProperty("texture").get<PropertyValue::AssetReference>().id.isValid());
}

CNA_EDITOR_TEST(ASlotRefusesAnAssetOfTheWrongKindAndSaysWhy)
{
    GizmoFixture fixture = makeGizmoFixture();
    EditorContext& context = fixture.application->getContext();

    fixture.ui->pendingChoices.emplace_back("##addComponentType", "Rendering / Sprite Renderer");
    fixture.step(UiImageInteraction{});
    fixture.ui->pendingClicks.push_back("Add Component");
    fixture.step(UiImageInteraction{});

    const Uuid soundId = addAsset(context, "Audio/Jump.wav", AssetType::SoundEffect);

    fixture.ui->pendingAssetDrops.emplace_back("Texture", soundId.toString());
    fixture.step(UiImageInteraction{});

    // Accepting it would give a scene that loads and a sprite that never appears, with nothing
    // anywhere to explain why.
    const EditorComponent* sprite = context.getScene().findEntity(fixture.entityId)
                                        ->findComponent(BuiltinComponentIds::kSpriteRenderer);
    CNA_EDITOR_EXPECT(!sprite->getProperty("texture").get<PropertyValue::AssetReference>().id.isValid());
    CNA_EDITOR_EXPECT(fixture.logContains("this field takes a Texture2D"));
}

/**
 * @brief A slot that declares no kind takes anything -- which now needs a descriptor of its own.
 *
 * This test used to drop a `RawData` asset onto `ModelRenderer`'s Material Override, because that
 * slot declared no kind: materials were not a tracked asset type. ED-403 made them one, so that
 * slot now declares "Material" and correctly refuses -- which left the *rule* untested rather than
 * the component.
 *
 * So the rule gets a component of its own. That is the better test anyway: the behaviour belongs to
 * the descriptor's contract ("empty means any", so a plugin can name a kind this build was never
 * compiled against), not to whichever built-in happened to leave the field blank.
 */
CNA_EDITOR_TEST(ASlotWithNoDeclaredKindTakesAnything)
{
    GizmoFixture fixture = makeGizmoFixture();
    EditorContext& context = fixture.application->getContext();

    ComponentDescriptor anything;
    anything.typeId = "Test.AnyAsset";
    anything.displayName = "Any Asset";
    anything.category = "Test";

    PropertyDescriptor slot;
    slot.name = "payload";
    slot.displayName = "Payload";
    slot.type = PropertyType::AssetReference;
    slot.defaultValue = PropertyValue{PropertyValue::AssetReference{}};
    // No assetType: this is the whole point of the test.
    anything.properties = {slot};

    context.getComponentRegistry().registerComponent(anything);

    fixture.ui->pendingChoices.emplace_back("##addComponentType", "Test / Any Asset");
    fixture.step(UiImageInteraction{});
    fixture.ui->pendingClicks.push_back("Add Component");
    fixture.step(UiImageInteraction{});

    const Uuid rawId = addAsset(context, "Data/Table.json", AssetType::RawData);

    fixture.ui->pendingAssetDrops.emplace_back("Payload", rawId.toString());
    fixture.step(UiImageInteraction{});

    const EditorComponent* component =
        context.getScene().findEntity(fixture.entityId)->findComponent("Test.AnyAsset");
    CNA_EDITOR_EXPECT(component != nullptr);
    CNA_EDITOR_EXPECT_EQ(
        component->getProperty("payload").get<PropertyValue::AssetReference>().id.toString(),
        rawId.toString());
}

/** @brief ED-403 tightened the Material Override slot, so it takes a material and nothing else. */
CNA_EDITOR_TEST(TheMaterialOverrideSlotTakesAMaterialAndRefusesEverythingElse)
{
    GizmoFixture fixture = makeGizmoFixture();
    EditorContext& context = fixture.application->getContext();

    fixture.ui->pendingChoices.emplace_back("##addComponentType", "Rendering (3D) / Model Renderer");
    fixture.step(UiImageInteraction{});
    fixture.ui->pendingClicks.push_back("Add Component");
    fixture.step(UiImageInteraction{});

    const Uuid rawId = addAsset(context, "Materials/Brick.json", AssetType::RawData);
    fixture.ui->pendingAssetDrops.emplace_back("Material Override", rawId.toString());
    fixture.step(UiImageInteraction{});

    const EditorComponent* model = context.getScene().findEntity(fixture.entityId)
                                       ->findComponent(BuiltinComponentIds::kModelRenderer);
    CNA_EDITOR_EXPECT(
        !model->getProperty("material").get<PropertyValue::AssetReference>().id.isValid());

    const Uuid materialId = addAsset(context, "Materials/Brick.cnamaterial", AssetType::Material);
    fixture.ui->pendingAssetDrops.emplace_back("Material Override", materialId.toString());
    fixture.step(UiImageInteraction{});

    model = context.getScene().findEntity(fixture.entityId)
                ->findComponent(BuiltinComponentIds::kModelRenderer);
    CNA_EDITOR_EXPECT_EQ(
        model->getProperty("material").get<PropertyValue::AssetReference>().id.toString(),
        materialId.toString());
}

CNA_EDITOR_TEST(TheConsoleCopiesWhatItIsShowing)
{
    GizmoFixture fixture = makeGizmoFixture();
    EditorContext& context = fixture.application->getContext();

    context.log(LogSeverity::Trace, "a trace line");
    context.log(LogSeverity::Warning, "a warning line");
    context.log(LogSeverity::Error, "an error line");

    fixture.ui->pendingClicks.push_back("Copy");
    fixture.step(UiImageInteraction{});

    // Unfiltered, Copy takes everything.
    CNA_EDITOR_EXPECT(fixture.ui->getClipboardText().find("a trace line") != std::string::npos);
    CNA_EDITOR_EXPECT(fixture.ui->getClipboardText().find("an error line") != std::string::npos);

    fixture.ui->pendingChoices.emplace_back("##consoleSeverity", "Warning");
    fixture.step(UiImageInteraction{});

    fixture.ui->pendingClicks.push_back("Copy");
    fixture.step(UiImageInteraction{});

    // Filtered, it takes what the user can see. Copying hidden messages would be a surprise.
    CNA_EDITOR_EXPECT(fixture.ui->getClipboardText().find("a trace line") == std::string::npos);
    CNA_EDITOR_EXPECT(fixture.ui->getClipboardText().find("a warning line") != std::string::npos);
    CNA_EDITOR_EXPECT(fixture.ui->getClipboardText().find("an error line") != std::string::npos);
}

CNA_EDITOR_TEST(TheConsoleClearButtonEmptiesIt)
{
    GizmoFixture fixture = makeGizmoFixture();
    EditorContext& context = fixture.application->getContext();

    context.log(LogSeverity::Info, "something happened");
    CNA_EDITOR_EXPECT(!fixture.ui->getLog().empty());

    fixture.ui->pendingClicks.push_back("Clear");
    fixture.step(UiImageInteraction{});

    CNA_EDITOR_EXPECT(fixture.ui->getLog().empty());
}

CNA_EDITOR_TEST(TheConsoleScrollLockIsRememberedAcrossFrames)
{
    GizmoFixture fixture = makeGizmoFixture();

    fixture.step(UiImageInteraction{});
    CNA_EDITOR_EXPECT(fixture.ui->shownCheckFor("Auto-scroll"));

    fixture.ui->pendingChecks.emplace_back("Auto-scroll", false);
    fixture.step(UiImageInteraction{});

    // The setting is the console's own state, so it has to survive frames that do not touch it.
    // A checkbox that reset every frame would be a scroll-lock that never locks.
    fixture.step(UiImageInteraction{});
    fixture.step(UiImageInteraction{});
    CNA_EDITOR_EXPECT(!fixture.ui->shownCheckFor("Auto-scroll"));

    fixture.ui->pendingChecks.emplace_back("Auto-scroll", true);
    fixture.step(UiImageInteraction{});
    fixture.step(UiImageInteraction{});
    CNA_EDITOR_EXPECT(fixture.ui->shownCheckFor("Auto-scroll"));
}

CNA_EDITOR_TEST(TheConsoleSeverityFilterIsRememberedAcrossFrames)
{
    GizmoFixture fixture = makeGizmoFixture();

    fixture.ui->pendingChoices.emplace_back("##consoleSeverity", "Error");
    fixture.step(UiImageInteraction{});
    fixture.step(UiImageInteraction{});
    fixture.step(UiImageInteraction{});

    CNA_EDITOR_EXPECT_EQ(fixture.ui->shownValueFor("##consoleSeverity")
                             .get<PropertyValue::EnumValue>().name,
                         std::string{"Error"});
}

CNA_EDITOR_TEST(RelinkingFromTheReportFixesEveryReferenceAtOnce)
{
    GizmoFixture fixture = makeGizmoFixture();
    EditorContext& context = fixture.application->getContext();

    const Uuid goneId = Uuid::generate();
    const Uuid replacementId = addAsset(context, "Textures/Correct.png", AssetType::Texture2D);

    // Two entities pointing at the same missing texture -- the ordinary shape of the problem.
    for (int index = 0; index < 2; ++index)
    {
        EditorEntity entity{Uuid::generate(), "Broken" + std::to_string(index)};
        EditorComponent sprite{BuiltinComponentIds::kSpriteRenderer};
        sprite.applyDefaults(*context.getComponentRegistry().find(BuiltinComponentIds::kSpriteRenderer));
        sprite.setProperty("texture", PropertyValue{PropertyValue::AssetReference{goneId}});
        entity.addComponent(std::move(sprite));
        context.getScene().addEntity(std::move(entity));
    }

    CNA_EDITOR_EXPECT_EQ(findMissingReferences(context.getScene(), context.getAssets()).size(),
                         std::size_t{2});

    fixture.ui->pendingAssetDrops.emplace_back(goneId.toString(), replacementId.toString());
    fixture.step(UiImageInteraction{});

    // Both now point at the replacement. Asserting on the stored ids rather than on the report
    // going empty: the scratch asset has no file behind it, so it would be reported as missing for
    // an entirely different reason and the test would be checking the wrong thing.
    const auto texturesInScene = [&] {
        std::vector<std::string> ids;
        for (const EditorEntity& entity : context.getScene().getEntities())
        {
            if (const EditorComponent* sprite = entity.findComponent(BuiltinComponentIds::kSpriteRenderer))
            {
                ids.push_back(sprite->getProperty("texture").get<PropertyValue::AssetReference>().id.toString());
            }
        }
        return ids;
    };

    CNA_EDITOR_EXPECT_EQ(texturesInScene().size(), std::size_t{2});
    for (const std::string& id : texturesInScene()) { CNA_EDITOR_EXPECT_EQ(id, replacementId.toString()); }

    // One undo entry for the whole relink: undoing a fix of forty sprites must not be forty
    // presses of Ctrl+Z.
    fixture.ui->pressShortcut(UiKey::Z, withControl());
    fixture.step(UiImageInteraction{});
    for (const std::string& id : texturesInScene()) { CNA_EDITOR_EXPECT_EQ(id, goneId.toString()); }
}

CNA_EDITOR_TEST(TheReportSaysSoWhenNothingIsBroken)
{
    GizmoFixture fixture = makeGizmoFixture();
    fixture.step(UiImageInteraction{});

    // An empty panel reads as "not implemented yet", which is the wrong thing for a report whose
    // good state is emptiness.
    CNA_EDITOR_EXPECT(fixture.ui->sawText("No broken asset references."));
}

CNA_EDITOR_TEST(SelectingAnAssetSwitchesTheInspectorToItsImportSettings)
{
    GizmoFixture fixture = makeGizmoFixture();
    EditorContext& context = fixture.application->getContext();

    const Uuid textureId = addAsset(context, "Textures/Hero.png", AssetType::Texture2D);
    context.getAssets().findMutable(textureId)->importerId = ImporterIds::kTexture;

    fixture.step(UiImageInteraction{});
    CNA_EDITOR_EXPECT(fixture.ui->sawText("Entity: Player"));

    fixture.ui->pendingNodeClicks.push_back(textureId);
    fixture.step(UiImageInteraction{});
    fixture.step(UiImageInteraction{});

    // One inspector showing one thing. Two independent selections would leave the user unable to
    // tell which the panel is about.
    CNA_EDITOR_EXPECT(fixture.ui->sawText("Asset: Textures/Hero.png"));
    CNA_EDITOR_EXPECT(!fixture.ui->sawText("Entity: Player"));
    CNA_EDITOR_EXPECT(context.getSelection().empty());

    // The texture importer's declared settings are what it offers.
    CNA_EDITOR_EXPECT_EQ(fixture.ui->optionsFor("Wrap Mode").size(), std::size_t{3});

    // And selecting an entity again takes the inspector back.
    fixture.ui->pendingNodeClicks.push_back(fixture.entityId);
    fixture.step(UiImageInteraction{});
    fixture.step(UiImageInteraction{});
    CNA_EDITOR_EXPECT(fixture.ui->sawText("Entity: Player"));
    CNA_EDITOR_EXPECT(!context.getSelectedAsset().isValid());
}

CNA_EDITOR_TEST(EditingAnImportSettingGoesThroughTheUndoStack)
{
    GizmoFixture fixture = makeGizmoFixture();
    EditorContext& context = fixture.application->getContext();

    const Uuid textureId = addAsset(context, "Textures/Hero.png", AssetType::Texture2D);
    context.getAssets().findMutable(textureId)->importerId = ImporterIds::kTexture;
    context.selectAsset(textureId);

    fixture.ui->pendingEdits.emplace_back("Filter Mode",
                                          PropertyValue{PropertyValue::EnumValue{"Point"}});
    fixture.step(UiImageInteraction{});

    CNA_EDITOR_EXPECT_EQ(context.getAssets().find(textureId)->importerSettings["filterMode"].asString(),
                         std::string{"Point"});

    // The asset database is a document like the scene, so its edits undo like the scene's do --
    // an editor where some edits undo and others quietly do not is worse than one where none do.
    fixture.ui->pressShortcut(UiKey::Z, withControl());
    fixture.step(UiImageInteraction{});
    CNA_EDITOR_EXPECT(context.getAssets().find(textureId)->importerSettings["filterMode"].isNull());
}

CNA_EDITOR_TEST(AnExternallyEditedAssetIsReloadedAndReported)
{
    const std::filesystem::path directory = makeScratchDirectory("watchapp");
    writeFile(directory / "HelloSprites.cnaproject",
              R"({"formatVersion":1,"name":"Watched","kind":"CnaNative","assetDirectory":"Assets"})");
    writeFile(directory / "Assets" / "Textures" / "Hero.png", "first contents");

    EditorApplication application{std::make_unique<NullEditorUi>(),
                                 std::make_unique<NullEditorViewport>()};
    EditorOptions options;
    options.headless = true;
    options.projectPath = (directory / "HelloSprites.cnaproject").generic_string();
    application.initialize(options);

    EditorContext& context = application.getContext();
    CNA_EDITOR_EXPECT_EQ(context.getAssets().getCount(), std::size_t{1});
    const std::string path = context.getAssets().getAll().front()->sourcePath;

    application.getAssetWatcher().setInterval(0.0);

    writeFile(directory / "Assets" / "Textures" / "Hero.png",
              "second contents, a different length entirely");

    application.renderFrame(0.0);

    const auto& ui = static_cast<NullEditorUi&>(application.getUi());
    bool reported = false;
    for (const auto& entry : ui.getLog())
    {
        if (entry.message.find("Reloaded '" + path + "'") != std::string::npos) { reported = true; }
    }

    // Without this the editor goes on showing the art from before the edit, and the only fix is
    // to restart it.
    CNA_EDITOR_EXPECT(reported);

    std::filesystem::remove_all(directory);
}

namespace
{
    /** @brief An editor over a real on-disk project with a few tracked assets. */
    struct BrowserFixture
    {
        std::filesystem::path directory;
        std::unique_ptr<EditorApplication> application;
        ScriptedUi* ui = nullptr;

        explicit BrowserFixture(const std::string& name)
        {
            directory = makeScratchDirectory(name);
            writeFile(directory / "Game.cnaproject",
                      R"({"formatVersion":1,"name":"Browsed","kind":"CnaNative","assetDirectory":"Assets"})");
            writeFile(directory / "Assets" / "Textures" / "hero.png", "hero");
            writeFile(directory / "Assets" / "Textures" / "enemy.png", "enemy");
            writeFile(directory / "Assets" / "Audio" / "jump.wav", "jump");

            auto scripted = std::make_unique<ScriptedUi>();
            ui = scripted.get();
            application = std::make_unique<EditorApplication>(std::move(scripted),
                                                             std::make_unique<NullEditorViewport>());

            EditorOptions options;
            options.headless = true;
            options.projectPath = (directory / "Game.cnaproject").generic_string();
            application->initialize(options);
        }

        ~BrowserFixture()
        {
            std::error_code errorCode;
            std::filesystem::remove_all(directory, errorCode);
        }

        BrowserFixture(const BrowserFixture&) = delete;
        BrowserFixture& operator=(const BrowserFixture&) = delete;

        [[nodiscard]] EditorContext& context() { return application->getContext(); }

        void step() { application->renderFrame(0.0); }

        [[nodiscard]] Uuid idOf(const std::string& path) const
        {
            const AssetRecord* record =
                const_cast<BrowserFixture*>(this)->context().getAssets().findByPath(path);
            return record != nullptr ? record->id : Uuid{};
        }
    };
}

CNA_EDITOR_TEST(TheAssetBrowserShowsAFolderTree)
{
    BrowserFixture fixture{"browsertree"};
    fixture.step();

    CNA_EDITOR_EXPECT_EQ(fixture.context().getAssets().getCount(), std::size_t{3});
    CNA_EDITOR_EXPECT(fixture.ui->sawStringNode("folder:Assets"));
    CNA_EDITOR_EXPECT(fixture.ui->sawStringNode("folder:Assets/Textures"));
    CNA_EDITOR_EXPECT(fixture.ui->sawStringNode("folder:Assets/Audio"));
    CNA_EDITOR_EXPECT(fixture.ui->sawText("3 assets"));
}

CNA_EDITOR_TEST(TheAssetBrowserFilterHidesWhatDoesNotMatch)
{
    BrowserFixture fixture{"browserfilter"};

    fixture.ui->typeInto("##assetFilter", "audio");
    fixture.step();
    fixture.step();

    // Only the matching branch survives; a filtered tree never shows an empty folder.
    CNA_EDITOR_EXPECT(fixture.ui->sawStringNode("folder:Assets/Audio"));
    CNA_EDITOR_EXPECT(!fixture.ui->sawStringNode("folder:Assets/Textures"));
    CNA_EDITOR_EXPECT(fixture.ui->sawText("1 of 3 assets"));

    // A filter matching nothing says so rather than looking like a broken panel.
    fixture.ui->typeInto("##assetFilter", "zzz");
    fixture.step();
    fixture.step();
    CNA_EDITOR_EXPECT(fixture.ui->sawText("Nothing matches 'zzz'."));
}

CNA_EDITOR_TEST(DroppingAnAssetOnAFolderMovesTheFileAndNotAnyScene)
{
    BrowserFixture fixture{"browsermove"};
    EditorContext& context = fixture.context();

    const Uuid heroId = fixture.idOf("Assets/Textures/hero.png");
    CNA_EDITOR_EXPECT(heroId.isValid());

    // Point a scene at it, then move it. The reference is a Uuid, so it must survive untouched.
    EditorEntity entity{Uuid::generate(), "Player"};
    EditorComponent sprite{BuiltinComponentIds::kSpriteRenderer};
    sprite.applyDefaults(*context.getComponentRegistry().find(BuiltinComponentIds::kSpriteRenderer));
    sprite.setProperty("texture", PropertyValue{PropertyValue::AssetReference{heroId}});
    entity.addComponent(std::move(sprite));
    context.getScene().addEntity(std::move(entity));

    fixture.step();
    fixture.ui->pendingFolderDrops.emplace_back("folder:Assets/Audio", heroId.toString());
    fixture.step();

    CNA_EDITOR_EXPECT_EQ(context.getAssets().find(heroId)->sourcePath,
                         std::string{"Assets/Audio/hero.png"});
    CNA_EDITOR_EXPECT(std::filesystem::exists(fixture.directory / "Assets" / "Audio" / "hero.png"));

    // The id never changed, so nothing referencing it broke (D-08).
    CNA_EDITOR_EXPECT(findMissingReferences(context.getScene(), context.getAssets()).empty());

    // And it undoes like any other document change.
    fixture.ui->pressShortcut(UiKey::Z, withControl());
    fixture.step();
    CNA_EDITOR_EXPECT_EQ(context.getAssets().find(heroId)->sourcePath,
                         std::string{"Assets/Textures/hero.png"});
}

CNA_EDITOR_TEST(RenamingAnAssetKeepsItInItsFolder)
{
    BrowserFixture fixture{"browserrename"};
    EditorContext& context = fixture.context();

    const Uuid heroId = fixture.idOf("Assets/Textures/hero.png");

    fixture.ui->pendingNodeDoubleClicks.push_back(heroId);
    fixture.step();

    fixture.ui->pendingRename = std::make_pair(std::string{"champion.png"}, true);
    fixture.step();

    CNA_EDITOR_EXPECT_EQ(context.getAssets().find(heroId)->sourcePath,
                         std::string{"Assets/Textures/champion.png"});
    CNA_EDITOR_EXPECT(std::filesystem::exists(fixture.directory / "Assets" / "Textures" / "champion.png"));
    CNA_EDITOR_EXPECT(std::filesystem::exists(fixture.directory / "Assets" / "Textures" / "champion.png.cnaasset"));
}

CNA_EDITOR_TEST(ARenameContainingASeparatorIsRefusedWithAReason)
{
    BrowserFixture fixture{"browserrenamepath"};
    EditorContext& context = fixture.context();

    const Uuid heroId = fixture.idOf("Assets/Textures/hero.png");

    fixture.ui->pendingNodeDoubleClicks.push_back(heroId);
    fixture.step();

    fixture.ui->pendingRename = std::make_pair(std::string{"../escaped.png"}, true);
    fixture.step();

    // A name with a separator in it is a move disguised as a rename, and the folder drop already
    // does that unambiguously.
    CNA_EDITOR_EXPECT_EQ(context.getAssets().find(heroId)->sourcePath,
                         std::string{"Assets/Textures/hero.png"});

    bool explained = false;
    for (const auto& entry : fixture.ui->getLog())
    {
        if (entry.message.find("cannot contain a path separator") != std::string::npos)
        {
            explained = true;
        }
    }
    CNA_EDITOR_EXPECT(explained);
}

CNA_EDITOR_TEST(TheValidationPanelReportsAnIssueAndSelectsItsEntity)
{
    auto scripted = std::make_unique<ScriptedUi>();
    ScriptedUi* ui = scripted.get();
    EditorApplication application{std::move(scripted), std::make_unique<NullEditorViewport>()};

    EditorOptions options;
    options.headless = true;
    CNA_EDITOR_EXPECT(application.initialize(options));

    EditorContext& context = application.getContext();

    // The starting scene already holds a primary camera, so a second one is a conflict rather
    // than a scene assembled to produce a warning.
    EditorEntity second{Uuid::generate(), "Cutscene Camera"};
    EditorComponent transform{BuiltinComponentIds::kTransform};
    transform.applyDefaults(*context.getComponentRegistry().find(BuiltinComponentIds::kTransform));
    second.addComponent(std::move(transform));
    EditorComponent camera{BuiltinComponentIds::kCamera};
    camera.applyDefaults(*context.getComponentRegistry().find(BuiltinComponentIds::kCamera));
    second.addComponent(std::move(camera));
    const Uuid secondId = context.getScene().addEntity(std::move(second));

    application.renderFrame();

    CNA_EDITOR_EXPECT(ui->sawText("2 error(s), 0 warning(s)"));

    const auto sawIssueLabel = [&](std::string_view needle) {
        for (const std::string& label : ui->drawnStringNodeLabels)
        {
            if (label.find(needle) != std::string::npos) { return true; }
        }
        return false;
    };
    CNA_EDITOR_EXPECT(sawIssueLabel("[error] Cutscene Camera: Marked primary"));

    // Clicking the row is the whole point of reporting one: finding which of two hundred entities
    // carries the fault is the hunt the panel exists to prevent.
    context.clearSelection();
    ui->pendingStringNodeClicks.push_back("issue-1-duplicate-primary-camera");
    application.renderFrame();

    CNA_EDITOR_EXPECT(context.getPrimarySelection() == secondId);

    // Removing the second camera empties the report rather than leaving a stale row behind.
    context.execute(std::make_unique<DeleteEntityCommand>(context.getScene(), secondId));
    application.renderFrame();

    CNA_EDITOR_EXPECT(ui->sawText("No scene issues."));
    CNA_EDITOR_EXPECT(ui->sawText("No broken asset references."));
}

CNA_EDITOR_TEST(TheHistoryPanelListsEveryEntryIncludingTheUndoneOnes)
{
    auto scripted = std::make_unique<ScriptedUi>();
    ScriptedUi* ui = scripted.get();
    EditorApplication application{std::move(scripted), std::make_unique<NullEditorViewport>()};

    EditorOptions options;
    options.headless = true;
    CNA_EDITOR_EXPECT(application.initialize(options));

    EditorContext& context = application.getContext();
    context.getHistory().markSaved();

    for (int index = 0; index < 3; ++index)
    {
        EditorEntity entity{Uuid::generate(), "Entity" + std::to_string(index)};
        entity.addComponent(EditorComponent{BuiltinComponentIds::kTransform});
        context.execute(std::make_unique<CreateEntityCommand>(context.getScene(), std::move(entity)));
    }

    application.renderFrame();
    CNA_EDITOR_EXPECT(ui->sawText("3 of 3 applied, keeping 512"));

    const auto rowLabel = [&](std::size_t position) {
        const std::string id = "history-" + std::to_string(position);
        for (std::size_t index = 0; index < ui->drawnStringNodes.size(); ++index)
        {
            if (ui->drawnStringNodes[index] == id) { return ui->drawnStringNodeLabels[index]; }
        }
        return std::string{};
    };

    // One row per position rather than per entry, so the state the document was opened in is
    // reachable -- it is the one a user asking to "put it back" is aiming at.
    CNA_EDITOR_EXPECT(rowLabel(0).find("Opened") != std::string::npos);
    CNA_EDITOR_EXPECT(rowLabel(0).find("(saved)") != std::string::npos);
    CNA_EDITOR_EXPECT(rowLabel(3).find("> ") == 0);

    application.undo();
    application.undo();
    application.renderFrame();

    CNA_EDITOR_EXPECT(ui->sawText("1 of 3 applied, keeping 512"));

    // Undone entries stay listed. Hiding them would hide exactly what the user is trying to
    // get back to.
    CNA_EDITOR_EXPECT(rowLabel(1).find("> ") == 0);
    CNA_EDITOR_EXPECT(rowLabel(2).find("(undone)") != std::string::npos);
    CNA_EDITOR_EXPECT(rowLabel(3).find("(undone)") != std::string::npos);
}

CNA_EDITOR_TEST(ClickingAHistoryRowMovesTheCursorToIt)
{
    auto scripted = std::make_unique<ScriptedUi>();
    ScriptedUi* ui = scripted.get();
    EditorApplication application{std::move(scripted), std::make_unique<NullEditorViewport>()};

    EditorOptions options;
    options.headless = true;
    CNA_EDITOR_EXPECT(application.initialize(options));

    EditorContext& context = application.getContext();
    const std::size_t entitiesBefore = context.getScene().getEntityCount();

    std::vector<Uuid> added;
    for (int index = 0; index < 4; ++index)
    {
        EditorEntity entity{Uuid::generate(), "Entity" + std::to_string(index)};
        entity.addComponent(EditorComponent{BuiltinComponentIds::kTransform});
        added.push_back(entity.getId());
        context.execute(std::make_unique<CreateEntityCommand>(context.getScene(), std::move(entity)));
    }
    CNA_EDITOR_EXPECT_EQ(context.getScene().getEntityCount(), entitiesBefore + 4);

    // Jumping back four positions in one click is the whole point: fifteen presses of Ctrl+Z with
    // no idea how many are left is how a person loses work they meant to keep.
    ui->pendingStringNodeClicks.push_back("history-1");
    application.renderFrame();

    CNA_EDITOR_EXPECT_EQ(context.getHistory().getCursor(), std::size_t{1});
    CNA_EDITOR_EXPECT_EQ(context.getScene().getEntityCount(), entitiesBefore + 1);
    CNA_EDITOR_EXPECT(context.getScene().findEntity(added[0]) != nullptr);
    CNA_EDITOR_EXPECT(context.getScene().findEntity(added[3]) == nullptr);

    // And forward again, through the same rows.
    ui->pendingStringNodeClicks.push_back("history-4");
    application.renderFrame();

    CNA_EDITOR_EXPECT_EQ(context.getHistory().getCursor(), std::size_t{4});
    CNA_EDITOR_EXPECT_EQ(context.getScene().getEntityCount(), entitiesBefore + 4);
    CNA_EDITOR_EXPECT(context.getScene().findEntity(added[3]) != nullptr);

    // Navigating goes through the application's own undo, so a jump prunes the selection the same
    // way Ctrl+Z does -- otherwise the inspector would keep showing an entity that no longer exists.
    context.select(added[3]);
    ui->pendingStringNodeClicks.push_back("history-0");
    application.renderFrame();

    CNA_EDITOR_EXPECT_EQ(context.getHistory().getCursor(), std::size_t{0});
    CNA_EDITOR_EXPECT(context.getSelection().empty());
}

CNA_EDITOR_TEST(AnEmptyHistorySaysSoRatherThanDrawingNothing)
{
    auto scripted = std::make_unique<ScriptedUi>();
    ScriptedUi* ui = scripted.get();
    EditorApplication application{std::move(scripted), std::make_unique<NullEditorViewport>()};

    EditorOptions options;
    options.headless = true;
    CNA_EDITOR_EXPECT(application.initialize(options));

    application.renderFrame();
    CNA_EDITOR_EXPECT(ui->sawText("Nothing to undo yet."));
}

namespace
{
    /** @brief A project on disk, an editor over it, and a scratch directory for its snapshots. */
    struct RecoveryFixture
    {
        std::filesystem::path directory;
        std::unique_ptr<EditorApplication> application;
        ScriptedUi* ui = nullptr;

        explicit RecoveryFixture(const std::string& name, double autosaveSeconds = 1.0)
        {
            directory = makeScratchDirectory(name);
            writeFile(directory / "Game.cnaproject",
                      R"({"formatVersion":1,"name":"Recovered","kind":"CnaNative",)"
                      R"("assetDirectory":"Assets","startupScene":"Scenes/Level01.cnascene"})");
            writeFile(directory / "Scenes" / "Level01.cnascene",
                      R"({"formatVersion":1,"sceneId":"c486b3f0-2a41-4d6b-9f18-7e0c5a1b4d92",)"
                      R"("name":"Level01","entities":[]})");

            auto scripted = std::make_unique<ScriptedUi>();
            ui = scripted.get();
            application = std::make_unique<EditorApplication>(std::move(scripted),
                                                             std::make_unique<NullEditorViewport>());

            EditorOptions options;
            options.headless = true;
            options.autosaveSeconds = autosaveSeconds;
            options.recoveryDirectory = (directory / "recovery").generic_string();
            options.projectPath = (directory / "Game.cnaproject").generic_string();
            application->initialize(options);
        }

        ~RecoveryFixture()
        {
            application.reset();
            std::error_code errorCode;
            std::filesystem::remove_all(directory, errorCode);
        }

        [[nodiscard]] EditorContext& context() { return application->getContext(); }
        [[nodiscard]] RecoveryStore store() const
        {
            return RecoveryStore{(directory / "recovery").generic_string()};
        }
        [[nodiscard]] std::string projectPath() const
        {
            return (directory / "Game.cnaproject").generic_string();
        }

        /** @brief Adds one entity through the undo stack, so the document becomes dirty. */
        Uuid addEntity(const std::string& name)
        {
            EditorEntity entity{Uuid::generate(), name};
            entity.addComponent(EditorComponent{BuiltinComponentIds::kTransform});
            const Uuid id = entity.getId();
            context().execute(std::make_unique<CreateEntityCommand>(context().getScene(), std::move(entity)));
            return id;
        }
    };
}

CNA_EDITOR_TEST(AnUnsavedSceneIsSnapshottedAndTheSnapshotGoesAwayOnSave)
{
    RecoveryFixture fixture{"autosave"};

    // Nothing to rescue while the document matches its file.
    fixture.application->renderFrame(5.0);
    CNA_EDITOR_EXPECT(!fixture.store().findForProject(fixture.projectPath()).has_value());

    fixture.addEntity("Player");

    // Below the interval: the clock is passed in, so this is exact rather than a race.
    fixture.application->renderFrame(0.4);
    CNA_EDITOR_EXPECT(!fixture.store().findForProject(fixture.projectPath()).has_value());

    fixture.application->renderFrame(0.7);
    const std::optional<RecoverySnapshot> snapshot = fixture.store().findForProject(fixture.projectPath());
    CNA_EDITOR_EXPECT(snapshot.has_value());
    CNA_EDITOR_EXPECT_EQ(snapshot->sceneName, std::string{"Level01"});
    CNA_EDITOR_EXPECT_EQ(snapshot->scene["entities"].getElements().size(), std::size_t{1});

    // The scene file itself is untouched until the user saves. A snapshot that wrote through to
    // the document would be an autosave, which is a different feature with different consent.
    SceneDocument onDisk;
    ComponentRegistry registry;
    registerBuiltinComponents(registry);
    CNA_EDITOR_EXPECT(onDisk.loadFromFile(
        (fixture.directory / "Scenes" / "Level01.cnascene").generic_string(), registry).succeeded);
    CNA_EDITOR_EXPECT_EQ(onDisk.getEntityCount(), std::size_t{0});

    // Saving makes the snapshot pointless, and leaving it would train users to click "discard"
    // on a message they stopped reading.
    fixture.application->saveScene();
    fixture.application->renderFrame(2.0);
    CNA_EDITOR_EXPECT(!fixture.store().findForProject(fixture.projectPath()).has_value());
}

CNA_EDITOR_TEST(RecoveredWorkIsOfferedRatherThanRestoredBehindTheUsersBack)
{
    RecoveryFixture first{"recoveroffer"};
    first.addEntity("Player");
    first.application->renderFrame(2.0);
    CNA_EDITOR_EXPECT(first.store().findForProject(first.projectPath()).has_value());

    // Open the same project again, as if the editor had been killed. The snapshot directory is
    // the fixture's, so a second application over the same files sees the first one's work.
    auto scripted = std::make_unique<ScriptedUi>();
    ScriptedUi* ui = scripted.get();
    EditorApplication reopened{std::move(scripted), std::make_unique<NullEditorViewport>()};

    EditorOptions options;
    options.headless = true;
    options.autosaveSeconds = 1.0;
    options.recoveryDirectory = (first.directory / "recovery").generic_string();
    options.projectPath = first.projectPath();
    CNA_EDITOR_EXPECT(reopened.initialize(options));

    // Offered, not applied: the document is still the file on disk.
    CNA_EDITOR_EXPECT(reopened.getRecoverableScene() != nullptr);
    CNA_EDITOR_EXPECT_EQ(reopened.getContext().getScene().getEntityCount(), std::size_t{0});

    bool warned = false;
    for (const auto& entry : ui->getLog())
    {
        if (entry.message.find("Unsaved changes to scene 'Level01'") != std::string::npos)
        {
            warned = entry.severity == LogSeverity::Warning;
        }
    }
    CNA_EDITOR_EXPECT(warned);

    // Until it is answered, autosave must not overwrite it: this session's unsaved seconds are
    // worth less than the previous session's unsaved work.
    reopened.getContext().getScene().setName("Touched");
    EditorEntity entity{Uuid::generate(), "Later"};
    entity.addComponent(EditorComponent{BuiltinComponentIds::kTransform});
    reopened.getContext().execute(
        std::make_unique<CreateEntityCommand>(reopened.getContext().getScene(), std::move(entity)));
    reopened.renderFrame(5.0);

    const RecoveryStore store{options.recoveryDirectory};
    CNA_EDITOR_EXPECT_EQ(store.findForProject(options.projectPath)->sceneName, std::string{"Level01"});

    // Accepting it brings the work back, leaves the file alone, and reports the document as
    // holding changes that were never saved.
    reopened.recoverScene();
    CNA_EDITOR_EXPECT(reopened.getRecoverableScene() == nullptr);
    CNA_EDITOR_EXPECT_EQ(reopened.getContext().getScene().getEntityCount(), std::size_t{1});
    CNA_EDITOR_EXPECT(reopened.getContext().getHistory().isDirty());
    CNA_EDITOR_EXPECT_EQ(reopened.getContext().getHistory().getCount(), std::size_t{0});
}

CNA_EDITOR_TEST(DiscardingARecoveredSceneRemovesTheSnapshotForGood)
{
    RecoveryFixture first{"recoverydiscard"};
    first.addEntity("Player");
    first.application->renderFrame(2.0);

    auto scripted = std::make_unique<ScriptedUi>();
    EditorApplication reopened{std::move(scripted), std::make_unique<NullEditorViewport>()};

    EditorOptions options;
    options.headless = true;
    options.autosaveSeconds = 1.0;
    options.recoveryDirectory = (first.directory / "recovery").generic_string();
    options.projectPath = first.projectPath();
    CNA_EDITOR_EXPECT(reopened.initialize(options));
    CNA_EDITOR_EXPECT(reopened.getRecoverableScene() != nullptr);

    reopened.discardRecoveredScene();

    CNA_EDITOR_EXPECT(reopened.getRecoverableScene() == nullptr);
    const RecoveryStore store{options.recoveryDirectory};
    CNA_EDITOR_EXPECT(!store.findForProject(options.projectPath).has_value());

    // And the editor goes back to protecting the current session.
    EditorEntity entity{Uuid::generate(), "Later"};
    entity.addComponent(EditorComponent{BuiltinComponentIds::kTransform});
    reopened.getContext().execute(
        std::make_unique<CreateEntityCommand>(reopened.getContext().getScene(), std::move(entity)));
    reopened.renderFrame(5.0);
    CNA_EDITOR_EXPECT(store.findForProject(options.projectPath).has_value());
}

CNA_EDITOR_TEST(AutosaveCanBeTurnedOffEntirely)
{
    RecoveryFixture fixture{"autosaveoff", 0.0};

    fixture.addEntity("Player");
    fixture.application->renderFrame(600.0);

    CNA_EDITOR_EXPECT(!fixture.store().findForProject(fixture.projectPath()).has_value());
}

CNA_EDITOR_TEST(OptionsParseTheAutosaveAndRecoveryFlags)
{
    const char* argv[] = {"cna-editor", "--autosave=5", "--recovery-dir=/tmp/snapshots"};
    const EditorOptions options = EditorOptions::parse(3, argv);

    CNA_EDITOR_EXPECT(!options.hasError);
    CNA_EDITOR_EXPECT(options.autosaveSeconds > 4.9 && options.autosaveSeconds < 5.1);
    CNA_EDITOR_EXPECT_EQ(options.recoveryDirectory, std::string{"/tmp/snapshots"});

    const char* bad[] = {"cna-editor", "--autosave=often"};
    CNA_EDITOR_EXPECT(EditorOptions::parse(2, bad).hasError);

    // A negative interval is clamped rather than rejected: it means the same thing as zero, and
    // failing to start over it would be a poor trade.
    const char* negative[] = {"cna-editor", "--autosave=-1"};
    const EditorOptions clamped = EditorOptions::parse(2, negative);
    CNA_EDITOR_EXPECT(!clamped.hasError);
    CNA_EDITOR_EXPECT_EQ(clamped.autosaveSeconds, 0.0);
}

namespace
{
    /** @brief Registers a component carrying one `List<String>` property. */
    void registerTaggedComponent(ComponentRegistry& registry)
    {
        ComponentDescriptor descriptor;
        descriptor.typeId = "Game.Tagged";
        descriptor.displayName = "Tagged";

        PropertyDescriptor tags;
        tags.name = "tags";
        tags.displayName = "Tags";
        tags.type = PropertyType::List;
        tags.elementType = PropertyType::String;
        tags.defaultValue = PropertyValue{PropertyValue::ListValue{}};
        descriptor.properties.push_back(std::move(tags));

        registry.registerComponent(std::move(descriptor));
    }

    /** @brief Returns the stored list as "a, b", which the assertion macro can print. */
    std::string tagsOf(const EditorContext& context, const Uuid& entityId)
    {
        const EditorEntity* entity = context.getScene().findEntity(entityId);
        if (entity == nullptr) { return {}; }

        const EditorComponent* component = entity->findComponent("Game.Tagged");
        if (component == nullptr) { return {}; }

        std::string joined;
        for (const PropertyValue& item : component->getProperty("tags").get<PropertyValue::ListValue>().items)
        {
            if (!joined.empty()) { joined += ", "; }
            joined += item.get<std::string>();
        }
        return joined;
    }

    /** @brief Returns how many elements the stored list holds. */
    std::size_t tagCountOf(const EditorContext& context, const Uuid& entityId)
    {
        const EditorEntity* entity = context.getScene().findEntity(entityId);
        if (entity == nullptr) { return 0; }

        const EditorComponent* component = entity->findComponent("Game.Tagged");
        if (component == nullptr) { return 0; }

        return component->getProperty("tags").get<PropertyValue::ListValue>().items.size();
    }
}

CNA_EDITOR_TEST(TheInspectorAddsRemovesAndReordersListElements)
{
    auto scripted = std::make_unique<ScriptedUi>();
    ScriptedUi* ui = scripted.get();
    EditorApplication application{std::move(scripted), std::make_unique<NullEditorViewport>()};

    EditorOptions options;
    options.headless = true;
    CNA_EDITOR_EXPECT(application.initialize(options));

    EditorContext& context = application.getContext();
    registerTaggedComponent(context.getComponentRegistry());

    EditorEntity entity{Uuid::generate(), "Tile"};
    entity.addComponent(EditorComponent{BuiltinComponentIds::kTransform});
    entity.addComponent(EditorComponent{"Game.Tagged"});
    const Uuid entityId = context.getScene().addEntity(std::move(entity));
    context.select(entityId);

    application.renderFrame();
    CNA_EDITOR_EXPECT(ui->sawStringNode("list-Game.Tagged-tags"));

    // Add twice. Each press is one action and must be one undo entry, or pressing Add three times
    // would undo in one.
    ui->pendingClicks.emplace_back("Add##list-Game.Tagged-tags");
    application.renderFrame();
    ui->pendingClicks.emplace_back("Add##list-Game.Tagged-tags");
    application.renderFrame();

    CNA_EDITOR_EXPECT_EQ(tagCountOf(context, entityId), std::size_t{2});
    CNA_EDITOR_EXPECT_EQ(context.getHistory().getCount(), std::size_t{2});

    // The new elements carry the *declared* element type, not the type of whatever was already
    // there -- an empty list has nothing to copy from, and that is when Add is pressed.
    ui->pendingEdits.emplace_back("0##list-Game.Tagged-tags-0", PropertyValue{std::string{"ground"}});
    application.renderFrame();
    ui->pendingEdits.emplace_back("1##list-Game.Tagged-tags-1", PropertyValue{std::string{"solid"}});
    application.renderFrame();

    CNA_EDITOR_EXPECT_EQ(tagsOf(context, entityId), std::string{"ground, solid"});

    // Moving is a swap with the neighbour, and the first row has no Up button rather than a dead
    // one -- a button that does nothing when pressed is a bug report waiting to be filed.
    CNA_EDITOR_EXPECT(!ui->sawButton("Up##list-Game.Tagged-tags-0"));
    CNA_EDITOR_EXPECT(ui->sawButton("Up##list-Game.Tagged-tags-1"));
    CNA_EDITOR_EXPECT(!ui->sawButton("Down##list-Game.Tagged-tags-1"));

    ui->pendingClicks.emplace_back("Up##list-Game.Tagged-tags-1");
    application.renderFrame();
    CNA_EDITOR_EXPECT_EQ(tagsOf(context, entityId), std::string{"solid, ground"});

    ui->pendingClicks.emplace_back("Remove##list-Game.Tagged-tags-0");
    application.renderFrame();
    CNA_EDITOR_EXPECT_EQ(tagsOf(context, entityId), std::string{"ground"});

    // And every one of those steps undoes on its own.
    application.undo();
    CNA_EDITOR_EXPECT_EQ(tagsOf(context, entityId), std::string{"solid, ground"});
    application.undo();
    CNA_EDITOR_EXPECT_EQ(tagsOf(context, entityId), std::string{"ground, solid"});
}

CNA_EDITOR_TEST(TheInspectorEditsTheProjectsLayersWhenNothingIsSelected)
{
    RecoveryFixture fixture{"layers", 0.0};
    EditorContext& context = fixture.context();
    ScriptedUi* ui = fixture.ui;

    // Nothing selected: the panel used to say so and stop. Project settings have to be editable
    // somewhere, and a panel that is blank half the time has room in it.
    context.clearSelection();
    fixture.application->renderFrame();
    CNA_EDITOR_EXPECT(ui->sawStringNode("project-layers"));
    CNA_EDITOR_EXPECT(ui->sawText("Project: Recovered"));

    ui->pendingClicks.emplace_back("Add##project-layers");
    fixture.application->renderFrame();
    CNA_EDITOR_EXPECT_EQ(context.getProject().getLayers().size(), std::size_t{2});

    ui->pendingEdits.emplace_back("1##project-layers-1", PropertyValue{std::string{"Foreground"}});
    fixture.application->renderFrame();
    CNA_EDITOR_EXPECT_EQ(context.getProject().getLayers().back(), std::string{"Foreground"});

    // The component's choices follow the project, because the descriptor is re-registered.
    const ComponentDescriptor* layer = context.getComponentRegistry().find(BuiltinComponentIds::kLayer);
    CNA_EDITOR_EXPECT(layer != nullptr);
    CNA_EDITOR_EXPECT_EQ(layer->findProperty("layer")->enumOptions.size(), std::size_t{2});

    // Written through, like an importer setting: a project change that lived only in memory would
    // be lost by a crash the recovery snapshot cannot help with, since that holds the scene.
    Project onDisk;
    CNA_EDITOR_EXPECT(onDisk.loadFromFile(fixture.projectPath()).succeeded);
    CNA_EDITOR_EXPECT_EQ(onDisk.getLayers().size(), std::size_t{2});
    CNA_EDITOR_EXPECT_EQ(onDisk.getLayers().back(), std::string{"Foreground"});

    // And it undoes, in both the project and the registry -- an editor where some edits undo and
    // others quietly do not is worse than one where nothing does.
    fixture.application->undo();
    CNA_EDITOR_EXPECT_EQ(context.getProject().getLayers().back(), std::string{"Layer 1"});
    fixture.application->undo();
    CNA_EDITOR_EXPECT_EQ(context.getProject().getLayers().size(), std::size_t{1});
    CNA_EDITOR_EXPECT_EQ(
        context.getComponentRegistry().find(BuiltinComponentIds::kLayer)->findProperty("layer")->enumOptions.size(),
        std::size_t{1});

    // The last layer has no Remove button: a project with none has nothing for an entity to be on.
    fixture.application->renderFrame();
    CNA_EDITOR_EXPECT(!ui->sawButton("Remove##project-layers-0"));
}

CNA_EDITOR_TEST(APrefabIsMadeFromASelectionAndDroppedBackIntoTheScene)
{
    RecoveryFixture fixture{"prefab", 0.0};
    EditorContext& context = fixture.context();
    ScriptedUi* ui = fixture.ui;

    // A two-entity subtree, the shape a prefab is actually useful for.
    EditorEntity root{Uuid::generate(), "Enemy"};
    EditorComponent transform{BuiltinComponentIds::kTransform};
    transform.applyDefaults(*context.getComponentRegistry().find(BuiltinComponentIds::kTransform));
    root.addComponent(std::move(transform));
    const Uuid rootId = context.getScene().addEntity(std::move(root));

    EditorEntity child{Uuid::generate(), "Weapon"};
    child.addComponent(EditorComponent{BuiltinComponentIds::kTransform});
    const Uuid childId = context.getScene().addEntity(std::move(child));
    CNA_EDITOR_EXPECT(context.getScene().reparentEntity(childId, rootId));

    context.select(rootId);
    ui->openContextMenus.push_back("entity-" + rootId.toString());
    ui->pendingMenuClicks.emplace_back("Create Prefab");
    fixture.application->renderFrame();

    // The file is on disk and tracked, and the entity it was made from is now an instance of it.
    // Leaving the original unlinked would mean the first edit afterwards silently did not reach
    // the prefab, which is how users learn not to trust the feature.
    const std::filesystem::path prefabPath = fixture.directory / "Assets" / "Prefabs" / "Enemy.cnaprefab";
    CNA_EDITOR_EXPECT(std::filesystem::exists(prefabPath));

    const AssetRecord* record = context.getAssets().findByPath("Assets/Prefabs/Enemy.cnaprefab");
    CNA_EDITOR_EXPECT(record != nullptr);
    if (record == nullptr) { return; }
    CNA_EDITOR_EXPECT(record->type == AssetType::Prefab);
    CNA_EDITOR_EXPECT(getPrefabAssetOf(context.getScene(), rootId) == record->id);
    CNA_EDITOR_EXPECT(findInstanceRoot(context.getScene(), childId) == rootId);

    // Dropping it onto another row instantiates a second copy there.
    const std::size_t before = context.getScene().getEntityCount();
    ui->openContextMenus.clear();
    ui->pendingAssetDrops.emplace_back(childId.toString(), record->id.toString());
    fixture.application->renderFrame();

    CNA_EDITOR_EXPECT_EQ(context.getScene().getEntityCount(), before + 2);

    const Uuid instanceRoot = context.getPrimarySelection();
    CNA_EDITOR_EXPECT(instanceRoot.isValid() && instanceRoot != rootId);
    CNA_EDITOR_EXPECT(context.getScene().findEntity(instanceRoot)->getParentId() == childId);

    // And it undoes, taking the file with it -- an editor whose undo stack and filesystem disagree
    // about what exists is worse than one that does not offer undo at all.
    fixture.application->undo();
    CNA_EDITOR_EXPECT_EQ(context.getScene().getEntityCount(), before);

    fixture.application->undo();
    CNA_EDITOR_EXPECT(!std::filesystem::exists(prefabPath));
    CNA_EDITOR_EXPECT(context.getAssets().findByPath("Assets/Prefabs/Enemy.cnaprefab") == nullptr);
    CNA_EDITOR_EXPECT(!getPrefabAssetOf(context.getScene(), rootId).isValid());
}

CNA_EDITOR_TEST(TheInspectorReportsOverridesAndRevertsOrAppliesThem)
{
    RecoveryFixture fixture{"prefabinspector", 0.0};
    EditorContext& context = fixture.context();
    ScriptedUi* ui = fixture.ui;

    EditorEntity root{Uuid::generate(), "Enemy"};
    EditorComponent transform{BuiltinComponentIds::kTransform};
    transform.applyDefaults(*context.getComponentRegistry().find(BuiltinComponentIds::kTransform));
    root.addComponent(std::move(transform));
    const Uuid rootId = context.getScene().addEntity(std::move(root));

    context.select(rootId);
    ui->openContextMenus.push_back("entity-" + rootId.toString());
    ui->pendingMenuClicks.emplace_back("Create Prefab");
    fixture.application->renderFrame();

    ui->openContextMenus.clear();
    fixture.application->renderFrame();
    CNA_EDITOR_EXPECT(ui->sawText("Prefab: Enemy"));
    CNA_EDITOR_EXPECT(ui->sawText("No changes from the prefab."));

    // Diverge, and the inspector says so -- computed by comparing, not by anything recorded.
    context.execute(std::make_unique<SetPropertyCommand>(
        context.getScene(), rootId, BuiltinComponentIds::kTransform, "position",
        PropertyValue{EditorVector3{40.0f, 8.0f, 0.0f}}));
    fixture.application->renderFrame();
    CNA_EDITOR_EXPECT(ui->sawText("1 change(s) from the prefab"));

    // Revert puts it back the way the prefab has it.
    ui->pendingClicks.emplace_back("Revert##prefab");
    fixture.application->renderFrame();

    CNA_EDITOR_EXPECT_EQ(context.getScene()
                             .findEntity(rootId)
                             ->findComponent(BuiltinComponentIds::kTransform)
                             ->getProperty("position")
                             .get<EditorVector3>()
                             .x,
                         0.0f);

    // Apply goes the other way: the prefab file is rewritten to match the instance.
    context.execute(std::make_unique<SetPropertyCommand>(
        context.getScene(), rootId, BuiltinComponentIds::kTransform, "position",
        PropertyValue{EditorVector3{7.0f, 0.0f, 0.0f}}));
    fixture.application->renderFrame();

    ui->pendingClicks.emplace_back("Apply##prefab");
    fixture.application->renderFrame();

    PrefabDocument onDisk;
    const std::filesystem::path prefabPath = fixture.directory / "Assets" / "Prefabs" / "Enemy.cnaprefab";
    CNA_EDITOR_EXPECT(
        onDisk.loadFromFile(prefabPath.generic_string(), context.getComponentRegistry()).succeeded);
    CNA_EDITOR_EXPECT_EQ(onDisk.getEntities()
                             .front()
                             .findComponent(BuiltinComponentIds::kTransform)
                             ->getProperty("position")
                             .get<EditorVector3>()
                             .x,
                         7.0f);

    // The instance now matches the prefab, by construction: it *is* the prefab.
    fixture.application->renderFrame();
    CNA_EDITOR_EXPECT(ui->sawText("No changes from the prefab."));

    // And applying undoes, restoring the file's previous contents.
    fixture.application->undo();
    PrefabDocument restored;
    CNA_EDITOR_EXPECT(
        restored.loadFromFile(prefabPath.generic_string(), context.getComponentRegistry()).succeeded);
    CNA_EDITOR_EXPECT_EQ(restored.getEntities()
                             .front()
                             .findComponent(BuiltinComponentIds::kTransform)
                             ->getProperty("position")
                             .get<EditorVector3>()
                             .x,
                         0.0f);
}

CNA_EDITOR_TEST(ApplyingAnInstantiatedInstanceKeepsEveryLinkIntact)
{
    // The case create-then-apply cannot catch: an instantiated instance has *fresh* entity ids, so
    // writing them into the prefab verbatim would leave every link naming an entity the file no
    // longer has -- and the next comparison would report the whole instance as added.
    RecoveryFixture fixture{"prefabapply", 0.0};
    EditorContext& context = fixture.context();
    ScriptedUi* ui = fixture.ui;

    EditorEntity root{Uuid::generate(), "Enemy"};
    EditorComponent transform{BuiltinComponentIds::kTransform};
    transform.applyDefaults(*context.getComponentRegistry().find(BuiltinComponentIds::kTransform));
    root.addComponent(std::move(transform));
    const Uuid sourceId = context.getScene().addEntity(std::move(root));

    context.select(sourceId);
    ui->openContextMenus.push_back("entity-" + sourceId.toString());
    ui->pendingMenuClicks.emplace_back("Create Prefab");
    fixture.application->renderFrame();
    ui->openContextMenus.clear();

    const AssetRecord* record = context.getAssets().findByPath("Assets/Prefabs/Enemy.cnaprefab");
    CNA_EDITOR_EXPECT(record != nullptr);
    if (record == nullptr) { return; }

    ui->pendingAssetDrops.emplace_back(sourceId.toString(), record->id.toString());
    fixture.application->renderFrame();

    const Uuid instanceRoot = context.getPrimarySelection();
    CNA_EDITOR_EXPECT(instanceRoot.isValid() && instanceRoot != sourceId);

    // Add a child to the instance, then apply. The addition has no link, so it earns a fresh
    // prefab id -- and the live entity has to be told about it.
    EditorEntity extra{Uuid::generate(), "Shield"};
    extra.addComponent(EditorComponent{BuiltinComponentIds::kTransform});
    const Uuid extraId = context.getScene().addEntity(std::move(extra));
    CNA_EDITOR_EXPECT(context.getScene().reparentEntity(extraId, instanceRoot));

    context.select(instanceRoot);
    fixture.application->renderFrame();
    CNA_EDITOR_EXPECT(ui->sawText("1 change(s) from the prefab"));

    ui->pendingClicks.emplace_back("Apply##prefab");
    fixture.application->renderFrame();

    PrefabDocument onDisk;
    const std::filesystem::path prefabPath = fixture.directory / "Assets" / "Prefabs" / "Enemy.cnaprefab";
    CNA_EDITOR_EXPECT(
        onDisk.loadFromFile(prefabPath.generic_string(), context.getComponentRegistry()).succeeded);
    CNA_EDITOR_EXPECT_EQ(onDisk.getEntities().size(), std::size_t{2});

    // The prefab carries no instance bookkeeping: leaving it in would make every future instance
    // born claiming to be an instance of something else.
    for (const EditorEntity& entity : onDisk.getEntities())
    {
        CNA_EDITOR_EXPECT(entity.getEditorState().count(PrefabKeys::kPrefabEntity) == 0);
        CNA_EDITOR_EXPECT(entity.getEditorState().count(PrefabKeys::kPrefabAsset) == 0);
    }

    // And the instance now matches it exactly, which is the whole claim Apply makes.
    fixture.application->renderFrame();
    CNA_EDITOR_EXPECT(ui->sawText("No changes from the prefab."));
    CNA_EDITOR_EXPECT(findPrefabOverrides(context.getScene(), instanceRoot, onDisk, context.getComponentRegistry()).empty());
}

namespace
{
    /**
     * @brief Builds an editor holding one selected 8x8 tilemap whose origin is at world (0, 0).
     *
     * With the null UI's 1280x720 content region and the camera's defaults -- centre (0, 0), zoom 1
     * -- world (0, 0) lands at screen (640, 360), so tile (0, 0) covers screen x 640..671 and
     * y 360..391. Every coordinate the test presses at is exact rather than approximate.
     */
    GizmoFixture makeTilemapFixture()
    {
        GizmoFixture fixture;
        auto ui = std::make_unique<ScriptedUi>();
        fixture.ui = ui.get();
        fixture.application =
            std::make_unique<EditorApplication>(std::move(ui), std::make_unique<NullEditorViewport>());

        EditorOptions options;
        options.headless = true;
        fixture.application->initialize(options);

        EditorContext& context = fixture.application->getContext();
        const ComponentRegistry& registry = context.getComponentRegistry();

        EditorEntity entity{Uuid::generate(), "Ground"};
        EditorComponent transform{BuiltinComponentIds::kTransform};
        transform.applyDefaults(*registry.find(BuiltinComponentIds::kTransform));
        entity.addComponent(std::move(transform));

        EditorComponent tilemap{BuiltinComponentIds::kTilemap};
        tilemap.applyDefaults(*registry.find(BuiltinComponentIds::kTilemap));
        tilemap.setProperty(TilemapKeys::kColumns, PropertyValue{std::int64_t{8}});
        tilemap.setProperty(TilemapKeys::kRows, PropertyValue{std::int64_t{8}});
        entity.addComponent(std::move(tilemap));

        fixture.entityId = entity.getId();
        context.getScene().addEntity(std::move(entity));
        context.select(fixture.entityId);

        fixture.step(UiImageInteraction{});
        return fixture;
    }

    /** @brief Reads the tilemap grid off the fixture's entity. */
    TilemapGrid gridOf(const GizmoFixture& fixture)
    {
        const EditorContext& context = fixture.application->getContext();
        const EditorComponent* tilemap = context.getScene()
                                             .findEntity(fixture.entityId)
                                             ->findComponent(BuiltinComponentIds::kTilemap);
        return readTilemapGrid(*tilemap, context.getComponentRegistry().find(BuiltinComponentIds::kTilemap));
    }
}

CNA_EDITOR_TEST(TheBrushPaintsAcrossADragAsOneUndoEntry)
{
    GizmoFixture fixture = makeTilemapFixture();
    EditorContext& context = fixture.application->getContext();

    fixture.application->setEditorTool(EditorTool::PaintTiles);
    fixture.application->setPaintTile(4);

    const std::size_t before = context.getHistory().getCount();

    // A drag straight across the first row: tiles are 32 wide and (0, 0) starts at screen x 640.
    fixture.step(leftAt(650.0f, 370.0f, true));
    fixture.step(leftAt(682.0f, 370.0f, false));
    fixture.step(leftAt(714.0f, 370.0f, false));

    UiImageInteraction release;
    release.hovered = true;
    release.localMouseX = 714.0f;
    release.localMouseY = 370.0f;
    release.leftReleased = true;
    fixture.step(release);

    const TilemapGrid grid = gridOf(fixture);
    CNA_EDITOR_EXPECT_EQ(grid.at(0, 0), std::int64_t{4});
    CNA_EDITOR_EXPECT_EQ(grid.at(1, 0), std::int64_t{4});
    CNA_EDITOR_EXPECT_EQ(grid.at(2, 0), std::int64_t{4});
    CNA_EDITOR_EXPECT_EQ(grid.at(3, 0), kEmptyTile);

    // One stroke, one entry. Three would make undoing a drag three presses, which is not what the
    // user did.
    CNA_EDITOR_EXPECT_EQ(context.getHistory().getCount(), before + 1);

    fixture.application->undo();
    CNA_EDITOR_EXPECT_EQ(gridOf(fixture).at(1, 0), kEmptyTile);

    // A second stroke is its own entry: the merge key carries the stroke, not just the property.
    fixture.application->redo();
    fixture.step(leftAt(650.0f, 402.0f, true));
    fixture.step(leftAt(682.0f, 402.0f, false));
    CNA_EDITOR_EXPECT_EQ(context.getHistory().getCount(), before + 2);
    CNA_EDITOR_EXPECT_EQ(gridOf(fixture).at(0, 1), std::int64_t{4});

    fixture.application->undo();
    CNA_EDITOR_EXPECT_EQ(gridOf(fixture).at(0, 1), kEmptyTile);
    CNA_EDITOR_EXPECT_EQ(gridOf(fixture).at(0, 0), std::int64_t{4});
}

CNA_EDITOR_TEST(TheEraserClearsAndTheBrushDoesNotSelectOrPickTheCamera)
{
    GizmoFixture fixture = makeTilemapFixture();
    EditorContext& context = fixture.application->getContext();

    fixture.application->setEditorTool(EditorTool::PaintTiles);
    fixture.application->setPaintTile(2);
    fixture.step(leftAt(650.0f, 370.0f, true));
    CNA_EDITOR_EXPECT_EQ(gridOf(fixture).at(0, 0), std::int64_t{2});

    // While a brush is active a press paints and nothing else. Clearing the selection here would
    // take away the very tilemap being painted into.
    UiImageInteraction click;
    click.hovered = true;
    click.localMouseX = 1200.0f;
    click.localMouseY = 700.0f;
    click.clicked = true;
    fixture.step(click);
    CNA_EDITOR_EXPECT(context.getPrimarySelection() == fixture.entityId);

    fixture.application->setEditorTool(EditorTool::EraseTiles);
    fixture.step(leftAt(650.0f, 370.0f, true));
    CNA_EDITOR_EXPECT_EQ(gridOf(fixture).at(0, 0), kEmptyTile);

    // Painting outside the map does nothing rather than growing it: a map's size is a property the
    // user set, not something a stray drag should change.
    const std::size_t entries = context.getHistory().getCount();
    fixture.application->setEditorTool(EditorTool::PaintTiles);
    fixture.step(leftAt(300.0f, 370.0f, true));
    CNA_EDITOR_EXPECT_EQ(context.getHistory().getCount(), entries);
}

CNA_EDITOR_TEST(TheBrushSaysSoWhenTheSelectionHasNoTilemap)
{
    GizmoFixture fixture = makeGizmoFixture();

    fixture.application->setEditorTool(EditorTool::PaintTiles);
    fixture.step(leftAt(700.0f, 400.0f, true));

    // Once per stroke, not once per frame: a brush over a sprite is a near miss, and sixty lines a
    // second about it is how a console stops being read.
    CNA_EDITOR_EXPECT(fixture.logContains("Select an entity with a Tilemap component"));

    std::size_t mentions = 0;
    for (const auto& entry : fixture.ui->getLog())
    {
        if (entry.message.find("Select an entity with a Tilemap") != std::string::npos) { ++mentions; }
    }
    fixture.step(leftAt(710.0f, 400.0f, false));
    fixture.step(leftAt(720.0f, 400.0f, false));

    std::size_t after = 0;
    for (const auto& entry : fixture.ui->getLog())
    {
        if (entry.message.find("Select an entity with a Tilemap") != std::string::npos) { ++after; }
    }
    CNA_EDITOR_EXPECT_EQ(after, mentions);
}

CNA_EDITOR_TEST(TheInspectorPreviewsAnAnimationWithoutPuttingItInTheDocument)
{
    auto scripted = std::make_unique<ScriptedUi>();
    ScriptedUi* ui = scripted.get();
    EditorApplication application{std::move(scripted), std::make_unique<NullEditorViewport>()};

    EditorOptions options;
    options.headless = true;
    CNA_EDITOR_EXPECT(application.initialize(options));

    EditorContext& context = application.getContext();
    const ComponentRegistry& registry = context.getComponentRegistry();

    EditorEntity entity{Uuid::generate(), "Hero"};
    EditorComponent transform{BuiltinComponentIds::kTransform};
    transform.applyDefaults(*registry.find(BuiltinComponentIds::kTransform));
    entity.addComponent(std::move(transform));

    EditorComponent animation{BuiltinComponentIds::kSpriteAnimation};
    animation.applyDefaults(*registry.find(BuiltinComponentIds::kSpriteAnimation));
    PropertyValue::ListValue frames;
    for (std::int64_t index = 0; index < 4; ++index) { frames.items.emplace_back(index); }
    animation.setProperty(SpriteAnimationKeys::kFrames, PropertyValue{frames});
    animation.setProperty(SpriteAnimationKeys::kFramesPerSecond, PropertyValue{10.0f});
    entity.addComponent(std::move(animation));

    const Uuid entityId = entity.getId();
    context.getScene().addEntity(std::move(entity));
    context.select(entityId);

    application.renderFrame();
    CNA_EDITOR_EXPECT(ui->sawText("Frame 1 of 4  (400 ms)"));
    CNA_EDITOR_EXPECT(ui->sawButton("Play##anim"));

    // Stepping is a deliberate look at one frame.
    ui->pendingClicks.emplace_back(">##anim");
    application.renderFrame();

    // The counter is drawn above the buttons, so the new frame shows on the next pass. That is
    // ordinary immediate-mode ordering, not a bug worth reordering the panel for.
    application.renderFrame();
    CNA_EDITOR_EXPECT(ui->sawText("Frame 2 of 4  (400 ms)"));

    // Playing advances on the frame clock the application is given, so this is exact rather than
    // a race with wall time.
    ui->pendingClicks.emplace_back("Play##anim");
    application.renderFrame();
    application.renderFrame(0.25);
    CNA_EDITOR_EXPECT(ui->sawText("Frame 4 of 4  (400 ms)"));

    // And none of it reached the document. A scene that recorded the frame an artist happened to
    // be paused on would carry it into every save and every diff.
    const EditorComponent* stored =
        context.getScene().findEntity(entityId)->findComponent(BuiltinComponentIds::kSpriteAnimation);
    CNA_EDITOR_EXPECT(!stored->hasProperty("position"));
    CNA_EDITOR_EXPECT(!stored->hasProperty("playing"));
    CNA_EDITOR_EXPECT(!context.getHistory().isDirty());

    // Selecting something else stops the preview rather than leaving it running against a clip
    // nobody is looking at.
    context.clearSelection();
    application.renderFrame(1.0);
    context.select(entityId);
    application.renderFrame();
    CNA_EDITOR_EXPECT(ui->sawText("Frame 1 of 4  (400 ms)"));
}

CNA_EDITOR_TEST(TheEyedropperTakesATileAndGoesBackToPainting)
{
    GizmoFixture fixture = makeTilemapFixture();
    fixture.application->setEditorTool(EditorTool::PaintTiles);
    fixture.application->setPaintTile(3);
    fixture.step(leftAt(650.0f, 370.0f, true));

    UiImageInteraction release;
    release.hovered = true;
    release.localMouseX = 650.0f;
    release.localMouseY = 370.0f;
    release.leftReleased = true;
    fixture.step(release);

    fixture.application->setPaintTile(9);
    fixture.application->setEditorTool(EditorTool::PickTile);
    fixture.step(leftAt(650.0f, 370.0f, true));

    // Picking a tile is never the goal; painting with it is. So the eyedropper hands the brush
    // back to the paint tool, which is what every editor does.
    CNA_EDITOR_EXPECT_EQ(fixture.application->getPaintTile(), std::int64_t{3});
    CNA_EDITOR_EXPECT(fixture.application->getEditorTool() == EditorTool::PaintTiles);

    // An empty cell is not a tile. Taking -1 as the brush would silently turn the eyedropper into
    // an eraser, which is a different tool the user did not choose.
    fixture.application->setEditorTool(EditorTool::PickTile);
    fixture.step(leftAt(714.0f, 370.0f, true));
    CNA_EDITOR_EXPECT_EQ(fixture.application->getPaintTile(), std::int64_t{3});
    CNA_EDITOR_EXPECT(fixture.application->getEditorTool() == EditorTool::PickTile);
    CNA_EDITOR_EXPECT(fixture.logContains("That cell is empty"));

    // And painting is unaffected by the excursion.
    CNA_EDITOR_EXPECT_EQ(gridOf(fixture).at(0, 0), std::int64_t{3});
}

CNA_EDITOR_TEST(AFillCoversTheDraggedRectangleAsOneUndoEntry)
{
    GizmoFixture fixture = makeTilemapFixture();
    EditorContext& context = fixture.application->getContext();

    fixture.application->setEditorTool(EditorTool::FillTiles);
    fixture.application->setPaintTile(6);

    const std::size_t before = context.getHistory().getCount();

    // Press on tile (0, 0) and release on (2, 1): a three-by-two rectangle.
    fixture.step(leftAt(650.0f, 370.0f, true));

    // Nothing is applied while the drag is in progress. A fill that painted as it went would be a
    // brush with extra steps, and could not be adjusted before release.
    CNA_EDITOR_EXPECT_EQ(gridOf(fixture).at(0, 0), kEmptyTile);

    fixture.step(leftAt(714.0f, 402.0f, false));

    UiImageInteraction release;
    release.hovered = true;
    release.localMouseX = 714.0f;
    release.localMouseY = 402.0f;
    release.leftReleased = true;
    fixture.step(release);

    const TilemapGrid grid = gridOf(fixture);
    CNA_EDITOR_EXPECT_EQ(grid.at(0, 0), std::int64_t{6});
    CNA_EDITOR_EXPECT_EQ(grid.at(2, 1), std::int64_t{6});
    CNA_EDITOR_EXPECT_EQ(grid.at(3, 0), kEmptyTile);
    CNA_EDITOR_EXPECT_EQ(grid.at(0, 2), kEmptyTile);

    // One entry however large the rectangle, and one press of Ctrl+Z takes all six back.
    CNA_EDITOR_EXPECT_EQ(context.getHistory().getCount(), before + 1);
    CNA_EDITOR_EXPECT(fixture.logContains("Filled 6 tile(s)."));

    fixture.application->undo();
    CNA_EDITOR_EXPECT_EQ(gridOf(fixture).at(0, 0), kEmptyTile);
    CNA_EDITOR_EXPECT_EQ(gridOf(fixture).at(2, 1), kEmptyTile);
}

CNA_EDITOR_TEST(AFillDraggedBackwardsAndPastTheEdgeStillFillsWhatExists)
{
    GizmoFixture fixture = makeTilemapFixture();

    fixture.application->setEditorTool(EditorTool::FillTiles);
    fixture.application->setPaintTile(2);

    // Started at (2, 1) and released at (0, 0): a rectangle is a rectangle whichever corner it was
    // dragged from.
    fixture.step(leftAt(714.0f, 402.0f, true));

    UiImageInteraction release;
    release.hovered = true;
    release.localMouseX = 650.0f;
    release.localMouseY = 370.0f;
    release.leftReleased = true;
    fixture.step(release);

    CNA_EDITOR_EXPECT_EQ(gridOf(fixture).at(0, 0), std::int64_t{2});
    CNA_EDITOR_EXPECT_EQ(gridOf(fixture).at(2, 1), std::int64_t{2});

    // Dragging past the edge fills what exists rather than nothing: the cells outside are refused
    // by the command, not by the tool.
    fixture.application->setPaintTile(5);
    fixture.step(leftAt(650.0f, 370.0f, true));

    UiImageInteraction outside;
    outside.hovered = true;
    outside.localMouseX = 1270.0f;
    outside.localMouseY = 370.0f;
    outside.leftReleased = true;
    fixture.step(outside);

    CNA_EDITOR_EXPECT_EQ(gridOf(fixture).at(0, 0), std::int64_t{5});
    CNA_EDITOR_EXPECT_EQ(gridOf(fixture).at(7, 0), std::int64_t{5});
}

CNA_EDITOR_TEST(TheDiagnosticsPanelReportsWhatThisBuildIsAndCanDo)
{
    auto scripted = std::make_unique<ScriptedUi>();
    ScriptedUi* ui = scripted.get();
    EditorApplication application{std::move(scripted), std::make_unique<NullEditorViewport>()};

    EditorOptions options;
    options.headless = true;
    CNA_EDITOR_EXPECT(application.initialize(options));

    std::vector<PlayerBuild> builds;
    builds.push_back(PlayerBuild{"software", "/opt/cna/cna-player-software"});
    application.setPlayerBuilds(std::move(builds));

    application.renderFrame();

    CNA_EDITOR_EXPECT(ui->sawText("Editor UI: null"));
    CNA_EDITOR_EXPECT(ui->sawText("Viewport: null"));

    // Because CNA fixes its backend at compile time, which player binaries exist is a real
    // question with a real answer, and this is where the user sees it.
    CNA_EDITOR_EXPECT(ui->sawText("Player builds found: 1"));
    CNA_EDITOR_EXPECT(ui->sawText("    software  /opt/cna/cna-player-software"));

    // The whole backend table, not only the one this binary was built against: the editor has to
    // be able to talk about a backend it cannot itself run.
    CNA_EDITOR_EXPECT(ui->sawText("Backends this editor knows about"));

    // A headless run has no device to ask, and says so rather than showing an empty list that
    // reads as "not implemented".
    CNA_EDITOR_EXPECT(ui->sawText("No graphics device, so no capabilities to report."));
}

CNA_EDITOR_TEST(TheInspectorPreviewsAClipWithTheSettingsTheComponentDeclares)
{
    RecoveryFixture fixture{"audio", 0.0};
    EditorContext& context = fixture.context();
    ScriptedUi* ui = fixture.ui;

    // The null audio is what --headless uses too, so the panel code that offers a preview runs
    // identically with and without a device.
    auto audio = std::make_unique<NullEditorAudio>();
    NullEditorAudio* recorded = audio.get();
    fixture.application->setAudio(std::move(audio));

    writeFile(fixture.directory / "Assets" / "jump.wav", "not really a wav");
    CNA_EDITOR_EXPECT(context.getAssets().scan("Assets").succeeded);

    const AssetRecord* clip = context.getAssets().findByPath("Assets/jump.wav");
    CNA_EDITOR_EXPECT(clip != nullptr);
    if (clip == nullptr) { return; }

    EditorEntity entity{Uuid::generate(), "Footstep"};
    EditorComponent transform{BuiltinComponentIds::kTransform};
    transform.applyDefaults(*context.getComponentRegistry().find(BuiltinComponentIds::kTransform));
    entity.addComponent(std::move(transform));

    EditorComponent source{BuiltinComponentIds::kAudioSource};
    source.applyDefaults(*context.getComponentRegistry().find(BuiltinComponentIds::kAudioSource));
    source.setProperty("clip", PropertyValue{PropertyValue::AssetReference{clip->id}});
    source.setProperty("volume", PropertyValue{0.25f});
    source.setProperty("pan", PropertyValue{-1.0f});
    entity.addComponent(std::move(source));

    const Uuid entityId = entity.getId();
    context.getScene().addEntity(std::move(entity));
    context.select(entityId);

    fixture.application->renderFrame();
    ui->pendingClicks.emplace_back("Play##audio");
    fixture.application->renderFrame();

    CNA_EDITOR_EXPECT_EQ(recorded->getRequests().size(), std::size_t{1});
    if (recorded->getRequests().empty()) { return; }

    // The component's own settings, not neutral ones. A preview at some other level is a preview
    // of a different sound, and "why is this quiet in game" is what a preview exists to answer.
    const NullEditorAudio::Request& request = recorded->getRequests().front();
    CNA_EDITOR_EXPECT(request.assetId == clip->id);
    CNA_EDITOR_EXPECT_EQ(request.volume, 0.25f);
    CNA_EDITOR_EXPECT_EQ(request.pan, -1.0f);
    CNA_EDITOR_EXPECT(recorded->isPlaying());

    ui->pendingClicks.emplace_back("Stop##audio");
    fixture.application->renderFrame();
    CNA_EDITOR_EXPECT(!recorded->isPlaying());

    // Selecting the asset itself offers the same preview, with nothing an entity chose applied:
    // hearing a clip is most often wanted right after importing it, when no entity uses it yet.
    context.selectAsset(clip->id);
    fixture.application->renderFrame();
    ui->pendingClicks.emplace_back("Play##assetAudio");
    fixture.application->renderFrame();

    CNA_EDITOR_EXPECT_EQ(recorded->getRequests().size(), std::size_t{2});
    CNA_EDITOR_EXPECT_EQ(recorded->getRequests().back().volume, 1.0f);

    // A clip that will not load is reported, because it and a clip of silence sound identical and
    // only one of them is the user's problem to fix.
    CNA_EDITOR_EXPECT(fixture.ui->getLog().size() > 0);
}

CNA_EDITOR_TEST(AnEntityWithNoClipIsToldRatherThanOfferedADeadButton)
{
    RecoveryFixture fixture{"audioempty", 0.0};
    EditorContext& context = fixture.context();

    EditorEntity entity{Uuid::generate(), "Silent"};
    EditorComponent transform{BuiltinComponentIds::kTransform};
    transform.applyDefaults(*context.getComponentRegistry().find(BuiltinComponentIds::kTransform));
    entity.addComponent(std::move(transform));

    EditorComponent source{BuiltinComponentIds::kAudioSource};
    source.applyDefaults(*context.getComponentRegistry().find(BuiltinComponentIds::kAudioSource));
    entity.addComponent(std::move(source));

    const Uuid entityId = entity.getId();
    context.getScene().addEntity(std::move(entity));
    context.select(entityId);

    fixture.application->renderFrame();
    CNA_EDITOR_EXPECT(fixture.ui->sawText("Assign a clip to hear it."));
    CNA_EDITOR_EXPECT(!fixture.ui->sawButton("Play##audio"));
}

CNA_EDITOR_TEST(TheBuildPanelExplainsItselfBeforeOfferingToBuild)
{
    RecoveryFixture fixture{"buildpanel", 0.0};
    ScriptedUi* ui = fixture.ui;

    fixture.application->renderFrame();

    // The example-shaped project has no CMakeLists, and that is said plainly instead of the button
    // being offered and CMake producing a wall of text about a missing file.
    CNA_EDITOR_EXPECT(ui->sawText("Cannot build: the project has no CMakeLists.txt, so there is "
                                 "nothing for the editor to build. A CNA game's build is the "
                                 "game's own -- the editor only runs it."));
    CNA_EDITOR_EXPECT(!ui->sawButton("Build##build"));

    // Give it one, and the panel shows the exact commands. A real build has options the editor
    // does not model, and someone who needs one has to be able to take the command away.
    writeFile(fixture.directory / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\n");
    fixture.application->renderFrame();

    if (findCMake().empty())
    {
        // No toolchain on this machine: the panel says so rather than offering a dead button.
        CNA_EDITOR_EXPECT(!ui->sawButton("Build##build"));
        return;
    }

    CNA_EDITOR_EXPECT(ui->sawButton("Build##build"));

    bool sawCommand = false;
    bool sawOutput = false;
    for (const std::string& line : ui->drawnText)
    {
        if (line.find("-DCMAKE_BUILD_TYPE=Release") != std::string::npos) { sawCommand = true; }
        if (line.find("Output: ") != std::string::npos
            && line.find("/build/linux-x64") != std::string::npos)
        {
            sawOutput = true;
        }
    }
    CNA_EDITOR_EXPECT(sawCommand);
    CNA_EDITOR_EXPECT(sawOutput);
}

namespace
{
    /** @brief A hovered frame with a middle or right drag of the given screen delta. */
    UiImageInteraction dragBy(float dx, float dy, bool rightButton = false)
    {
        UiImageInteraction input;
        input.hovered = true;
        input.localMouseX = 640.0f;
        input.localMouseY = 360.0f;
        input.dragging = true;
        input.dragDeltaX = dx;
        input.dragDeltaY = dy;
        input.rightDown = rightButton;
        return input;
    }
}

CNA_EDITOR_TEST(TheThreeDimensionalViewIsAToggleThatLeavesTheTwoDimensionalCameraAlone)
{
    GizmoFixture fixture = makeGizmoFixture();

    CNA_EDITOR_EXPECT(!fixture.application->isThreeDimensionalView());

    // The 2D framing has to survive the round trip. A user who glances at a scene in 3D and comes
    // back must find their view exactly as they left it, which is why both cameras are alive at
    // once rather than one being converted into the other.
    EditorCamera2D& camera2D = fixture.application->getViewport().getCamera();
    camera2D.setCenter(EditorVector2{321.0f, -654.0f});
    camera2D.setZoom(3.0f);

    fixture.application->setThreeDimensionalView(true);
    fixture.step(UiImageInteraction{});

    CNA_EDITOR_EXPECT(fixture.application->isThreeDimensionalView());

    // The wireframe reached the viewport: the ground grid alone is many segments, and a headless
    // run that built one and dropped it silently would look just like one that built nothing.
    const auto& viewport = static_cast<NullEditorViewport&>(fixture.application->getViewport());
    CNA_EDITOR_EXPECT(viewport.getLastWireframeSegments() > 0);

    fixture.application->setThreeDimensionalView(false);
    fixture.step(UiImageInteraction{});

    CNA_EDITOR_EXPECT_EQ(camera2D.getCenter().x, 321.0f);
    CNA_EDITOR_EXPECT_EQ(camera2D.getZoom(), 3.0f);
}

CNA_EDITOR_TEST(TheGridPlaneIsOfferedOnlyWhereItChangesSomething)
{
    GizmoFixture fixture = makeGizmoFixture();
    fixture.ui->openMenus.emplace_back("View");

    // The scene's own plane by default: everything this editor can place today lives in XY.
    CNA_EDITOR_EXPECT(fixture.application->getGridPlane() == GridPlane::SceneXY);

    fixture.step(UiImageInteraction{});
    CNA_EDITOR_EXPECT(!fixture.ui->sawMenuItem("Grid on Ground Plane"));

    fixture.application->setThreeDimensionalView(true);
    fixture.step(UiImageInteraction{});
    CNA_EDITOR_EXPECT(fixture.ui->sawMenuItem("Grid on Ground Plane"));

    fixture.ui->pendingMenuClicks.emplace_back("Grid on Ground Plane");
    fixture.step(UiImageInteraction{});
    CNA_EDITOR_EXPECT(fixture.application->getGridPlane() == GridPlane::Ground);

    // Named for what pressing it does, so once the grid is a floor the item offers the way back.
    fixture.step(UiImageInteraction{});
    CNA_EDITOR_EXPECT(fixture.ui->sawMenuItem("Grid on Scene Plane"));

    // Back in 2D the choice is gone rather than merely ignored: one plane, no decision to make,
    // and a menu item that changes nothing visible is a bug report waiting to be filed.
    fixture.application->setThreeDimensionalView(false);
    fixture.step(UiImageInteraction{});
    CNA_EDITOR_EXPECT(!fixture.ui->sawMenuItem("Grid on Scene Plane"));
}

CNA_EDITOR_TEST(ThreeDimensionalNavigationOrbitsFliesAndPans)
{
    GizmoFixture fixture = makeGizmoFixture();
    fixture.application->setThreeDimensionalView(true);
    fixture.step(UiImageInteraction{});

    EditorCamera3D& camera = fixture.application->getViewport().getCamera3D();
    camera.setPivot(EditorVector3{});
    camera.setDistance(50.0f);
    camera.setYaw(0.0f);
    camera.setPitch(0.0f);

    // Middle-drag orbits: the pivot stays, the eye swings round it.
    fixture.step(dragBy(100.0f, 0.0f));
    CNA_EDITOR_EXPECT(std::abs(camera.getYaw()) > 0.1f);
    CNA_EDITOR_EXPECT(std::abs(camera.getDistance() - 50.0f) < 0.001f);
    CNA_EDITOR_EXPECT(camera.getPivot() == EditorVector3{});

    // Shift with the same drag pans instead, and must not turn the camera at all.
    const float yawAfterOrbit = camera.getYaw();
    UiImageInteraction pan = dragBy(0.0f, 60.0f);
    pan.shift = true;
    fixture.step(pan);
    CNA_EDITOR_EXPECT_EQ(camera.getYaw(), yawAfterOrbit);
    CNA_EDITOR_EXPECT(camera.getPivot() != EditorVector3{});

    // Right-drag turns in place: the eye is what stays put.
    camera.setPivot(EditorVector3{});
    const EditorVector3 eyeBefore = camera.getEye();
    fixture.step(dragBy(80.0f, 0.0f, true));
    CNA_EDITOR_EXPECT(std::abs(length(subtract(camera.getEye(), eyeBefore))) < 0.01f);

    // W with the right button held flies forward. Without the button it must not, or the gizmo
    // shortcut and the fly control would fire on the same press.
    const EditorVector3 pivotBeforeFly = camera.getPivot();
    fixture.ui->heldKeys = {UiKey::W};

    UiImageInteraction hoverOnly;
    hoverOnly.hovered = true;
    fixture.step(hoverOnly);
    CNA_EDITOR_EXPECT(camera.getPivot() == pivotBeforeFly);

    UiImageInteraction flying;
    flying.hovered = true;
    flying.rightDown = true;
    fixture.step(flying);
    CNA_EDITOR_EXPECT(length(subtract(camera.getPivot(), pivotBeforeFly)) > 0.1f);

    // The wheel dollies, and scrolling up moves the eye closer.
    fixture.ui->heldKeys.clear();
    const float distanceBefore = camera.getDistance();
    UiImageInteraction wheel;
    wheel.hovered = true;
    wheel.wheel = 1.0f;
    fixture.step(wheel);
    CNA_EDITOR_EXPECT(camera.getDistance() < distanceBefore);
}

CNA_EDITOR_TEST(TheGizmoKeysFlyRatherThanSwitchingManipulatorInTheThreeDimensionalView)
{
    GizmoFixture fixture = makeGizmoFixture();
    fixture.application->setGizmoMode(GizmoMode::Scale);
    fixture.application->setThreeDimensionalView(true);

    // Pressing W while flying must not quietly leave the 2D view on a different manipulator than
    // the user left it on -- a hidden state change is exactly what makes an editor feel haunted.
    fixture.ui->pressShortcut(UiKey::W);
    fixture.step(UiImageInteraction{});
    CNA_EDITOR_EXPECT(fixture.application->getGizmoMode() == GizmoMode::Scale);

    // Back in 2D the key means what it always did.
    fixture.application->setThreeDimensionalView(false);
    fixture.ui->pressShortcut(UiKey::W);
    fixture.step(UiImageInteraction{});
    CNA_EDITOR_EXPECT(fixture.application->getGizmoMode() == GizmoMode::Translate);
}

CNA_EDITOR_TEST(TheDigitsSelectTheViewAndKeepWorkingWhileFlying)
{
    GizmoFixture fixture = makeGizmoFixture();
    CNA_EDITOR_EXPECT(!fixture.application->isThreeDimensionalView());

    fixture.ui->pressShortcut(UiKey::Digit3);
    fixture.step(UiImageInteraction{});
    CNA_EDITOR_EXPECT(fixture.application->isThreeDimensionalView());

    // Pressed again it is not a toggle: a key named for the view it selects has to be safe to
    // press when the user is already there.
    fixture.ui->pressShortcut(UiKey::Digit3);
    fixture.step(UiImageInteraction{});
    CNA_EDITOR_EXPECT(fixture.application->isThreeDimensionalView());

    // And unlike W, E and R, it still works while the 3D view has the keyboard for flying.
    fixture.application->setGizmoMode(GizmoMode::Scale);
    fixture.ui->pressShortcut(UiKey::Digit2);
    fixture.step(UiImageInteraction{});
    CNA_EDITOR_EXPECT(!fixture.application->isThreeDimensionalView());
    CNA_EDITOR_EXPECT(fixture.application->getGizmoMode() == GizmoMode::Scale);
}

CNA_EDITOR_TEST(FramingAndPickingFollowWhicheverCameraIsOnScreen)
{
    GizmoFixture fixture = makeGizmoFixture();
    fixture.application->setThreeDimensionalView(true);
    fixture.step(UiImageInteraction{});

    EditorCamera3D& camera = fixture.application->getViewport().getCamera3D();
    camera.setPivot(EditorVector3{9999.0f, 9999.0f, 9999.0f});
    camera.setDistance(10000.0f);

    // Frame Selected has to move the camera the user is looking through. Framing the 2D one while
    // the 3D view is on screen would look exactly like a key that does nothing.
    fixture.application->frameSelection();
    CNA_EDITOR_EXPECT(length(subtract(camera.getPivot(), EditorVector3{9999.0f, 9999.0f, 9999.0f}))
                      > 1.0f);
    CNA_EDITOR_EXPECT(camera.getDistance() < 10000.0f);

    // And a click picks through the 3D projection: after framing, the entity is at the centre of
    // the panel, so a click there selects it and a click in the corner clears the selection.
    fixture.application->getContext().select(Uuid{});
    fixture.step(UiImageInteraction{});

    UiImageInteraction click;
    click.hovered = true;
    click.clicked = true;
    click.localMouseX = 640.0f;
    click.localMouseY = 360.0f;
    fixture.step(click);
    CNA_EDITOR_EXPECT(fixture.application->getContext().getSelection().size() == 1);

    click.localMouseX = 4.0f;
    click.localMouseY = 4.0f;
    fixture.step(click);
    CNA_EDITOR_EXPECT(fixture.application->getContext().getSelection().empty());
}

CNA_EDITOR_TEST(AProjectsOwnSnapStepWinsOverTheVisibleGrid)
{
    GizmoFixture fixture = makeGizmoFixture();

    // The fixture has no project, so the snap is the drawn grid's 50 units -- the case the test
    // above covers. A project that lays out on a 16-unit tile grid says so once instead.
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("cna-snap-" + Uuid::generate().toString());
    std::filesystem::create_directories(root);

    EditorContext& context = fixture.application->getContext();
    context.getProject() = Project::createDefault("Tiles", root.generic_string());
    CNA_EDITOR_EXPECT(context.getProject().saveToFile((root / "Tiles.cnaproject").generic_string()));
    context.getProject().setGridSnap(16.0f);

    UiImageInteraction press = leftAt(790.0f, 580.0f, true);
    press.control = true;
    fixture.step(press);

    UiImageInteraction drag = leftAt(853.0f, 580.0f, false);
    drag.control = true;
    fixture.step(drag);

    // Dragged 63 along X from 100: 163 rounds to 160 on a 16-unit step, where the drawn grid
    // would have put it on 150.
    CNA_EDITOR_EXPECT_EQ(fixture.getPosition().x, 160.0f);

    // Zero is not "no snapping" -- Ctrl is what turns snapping on -- it is "use the visible grid",
    // which is what the editor did before the setting existed and what an older project means.
    context.getProject().setGridSnap(0.0f);
    fixture.step(UiImageInteraction{});

    UiImageInteraction pressAgain = leftAt(850.0f, 580.0f, true);
    pressAgain.control = true;
    fixture.step(pressAgain);

    UiImageInteraction dragAgain = leftAt(893.0f, 580.0f, false);
    dragAgain.control = true;
    fixture.step(dragAgain);
    CNA_EDITOR_EXPECT_EQ(fixture.getPosition().x, 200.0f);

    std::error_code cleanup;
    std::filesystem::remove_all(root, cleanup);
}

CNA_EDITOR_TEST(AThreeDimensionalGizmoDragMovesTheEntityAndUndoesAsOneEntry)
{
    GizmoFixture fixture = makeGizmoFixture();
    fixture.application->setThreeDimensionalView(true);
    fixture.step(UiImageInteraction{});

    // Looking down -Z with no pitch, so world +X is screen right and the X arm is horizontal.
    EditorCamera3D& camera = fixture.application->getViewport().getCamera3D();
    camera.setYaw(0.0f);
    camera.setPitch(0.0f);
    camera.setPivot(EditorVector3{100.0f, 220.0f, 0.0f});
    camera.setDistance(400.0f);
    fixture.step(UiImageInteraction{});

    const std::optional<TranslateGizmo3DLayout> layout = computeTranslateGizmo3DLayout(
        fixture.application->getContext().getScene(), camera, fixture.entityId);
    CNA_EDITOR_EXPECT(layout.has_value());
    if (!layout) { return; }

    const float startX = fixture.getPosition().x;
    const EditorVector2 grab{(layout->screenOrigin.x + layout->screenTips[0].x) * 0.5f,
                             layout->screenOrigin.y};

    fixture.step(leftAt(grab.x, grab.y, true));
    fixture.step(leftAt(grab.x + 40.0f, grab.y, false));

    CNA_EDITOR_EXPECT(fixture.getPosition().x > startX + 1.0f);

    // A press on an arm is a manipulation and must not also reselect: the gizmo sits on its own
    // entity's box, so a press that fell through would pick that box and look like nothing.
    CNA_EDITOR_EXPECT_EQ(fixture.application->getContext().getSelection().size(), std::size_t{1});

    // One drag is one undo entry, exactly as in 2D. Releasing and dragging again must not merge
    // into it -- the merge key cannot tell two gestures apart, so the boundary has to.
    fixture.step(UiImageInteraction{});
    fixture.application->undo();
    CNA_EDITOR_EXPECT_EQ(fixture.getPosition().x, startX);

    // And a click on empty space still selects nothing, so the gizmo has not swallowed the picker.
    UiImageInteraction click;
    click.hovered = true;
    click.clicked = true;
    click.localMouseX = 4.0f;
    click.localMouseY = 4.0f;
    fixture.step(click);
    CNA_EDITOR_EXPECT(fixture.application->getContext().getSelection().empty());
}

CNA_EDITOR_TEST(AThreeDimensionalTurnCarriesAWholeSelectionAboutItsPivot)
{
    GizmoFixture fixture = makeGizmoFixture();
    EditorContext& context = fixture.application->getContext();

    EditorEntity second{Uuid::generate(), "Crate"};
    EditorComponent transform{BuiltinComponentIds::kTransform};
    transform.applyDefaults(*context.getComponentRegistry().find(BuiltinComponentIds::kTransform));
    transform.setProperty("position", PropertyValue{EditorVector3{-160.0f, 220.0f, 0.0f}});
    second.addComponent(std::move(transform));
    const Uuid secondId = second.getId();
    context.getScene().addEntity(std::move(second));

    context.select(fixture.entityId);
    context.toggleSelection(secondId);

    fixture.application->setThreeDimensionalView(true);
    fixture.application->setGizmoMode(GizmoMode::Rotate);

    const std::optional<EditorVector3> pivot =
        computeSelectionPivot3D(context.getScene(), context.getSelection());
    CNA_EDITOR_EXPECT(pivot.has_value());
    if (!pivot) { return; }

    // Looking straight at the XY plane, so the Z ring faces the camera and is the one a click on
    // the ring lands on.
    EditorCamera3D& camera = fixture.application->getViewport().getCamera3D();
    camera.setYaw(0.0f);
    camera.setPitch(0.0f);
    camera.setPivot(*pivot);
    camera.setDistance(500.0f);
    fixture.step(UiImageInteraction{});

    const auto positionOf = [&context](const Uuid& id) {
        return context.getScene().findEntity(id)
            ->findComponent(BuiltinComponentIds::kTransform)
            ->getProperty("position")
            .get<EditorVector3>();
    };
    const auto distanceFromPivot = [&pivot](const EditorVector3& position) {
        return length(subtract(position, *pivot));
    };

    const std::optional<RotateGizmo3DLayout> layout = computeRotateGizmo3DLayout(
        context.getScene(), camera, fixture.entityId, GizmoSpace::World, pivot);
    CNA_EDITOR_EXPECT(layout.has_value());
    if (!layout) { return; }

    const std::vector<EditorVector2>& ring = layout->rings[2];
    CNA_EDITOR_EXPECT(ring.size() > 8);
    if (ring.size() <= 8) { return; }

    const EditorVector3 firstBefore = positionOf(fixture.entityId);
    const EditorVector3 secondBefore = positionOf(secondId);

    fixture.step(leftAt(ring.front().x, ring.front().y, true));
    fixture.step(leftAt(ring[ring.size() / 8].x, ring[ring.size() / 8].y, false));

    const EditorVector3 firstAfter = positionOf(fixture.entityId);
    const EditorVector3 secondAfter = positionOf(secondId);

    // Both were carried around the pivot rather than left where they were and merely turned...
    CNA_EDITOR_EXPECT(length(subtract(firstAfter, firstBefore)) > 1.0f);
    CNA_EDITOR_EXPECT(length(subtract(secondAfter, secondBefore)) > 1.0f);

    // ...and the arrangement kept its shape: turning a group must not scatter it.
    CNA_EDITOR_EXPECT(std::abs(distanceFromPivot(firstAfter) - distanceFromPivot(firstBefore)) < 0.5f);
    CNA_EDITOR_EXPECT(std::abs(distanceFromPivot(secondAfter) - distanceFromPivot(secondBefore)) < 0.5f);

    CNA_EDITOR_EXPECT(std::abs(fixture.getRotationZ()) > 0.01f);

    // And one Ctrl+Z puts the whole gesture back.
    fixture.step(UiImageInteraction{});
    fixture.application->undo();
    CNA_EDITOR_EXPECT(length(subtract(positionOf(fixture.entityId), firstBefore)) < 0.01f);
    CNA_EDITOR_EXPECT(length(subtract(positionOf(secondId), secondBefore)) < 0.01f);
    CNA_EDITOR_EXPECT(std::abs(fixture.getRotationZ()) < 0.001f);
}

CNA_EDITOR_TEST(AThreeDimensionalScaleDragResizesTheEntityAndUndoesAsOneEntry)
{
    GizmoFixture fixture = makeGizmoFixture();
    fixture.application->setThreeDimensionalView(true);
    fixture.application->setGizmoMode(GizmoMode::Scale);
    fixture.step(UiImageInteraction{});

    // Orbited off the axes, so all three arms have room on screen. Straight down -Z the Z arm
    // would be the degenerate one, which is a property the scene tests pin rather than this one.
    EditorCamera3D& camera = fixture.application->getViewport().getCamera3D();
    camera.setYaw(0.6f);
    camera.setPitch(0.4f);
    camera.setPivot(EditorVector3{100.0f, 220.0f, 0.0f});
    camera.setDistance(400.0f);
    fixture.step(UiImageInteraction{});

    const std::optional<ScaleGizmo3DLayout> layout = computeScaleGizmo3DLayout(
        fixture.application->getContext().getScene(), camera, fixture.entityId);
    CNA_EDITOR_EXPECT(layout.has_value());
    if (!layout) { return; }

    const EditorVector2 handle = layout->screenHandles[0];
    const EditorVector2 arm{handle.x - layout->screenOrigin.x, handle.y - layout->screenOrigin.y};

    CNA_EDITOR_EXPECT(std::abs(fixture.getScale().x - 1.0f) < 0.001f);

    fixture.step(leftAt(handle.x, handle.y, true));
    fixture.step(leftAt(layout->screenOrigin.x + arm.x * 1.5f, layout->screenOrigin.y + arm.y * 1.5f,
                        false));

    // Half again as far out is half again the size, and only on the grabbed axis.
    CNA_EDITOR_EXPECT(std::abs(fixture.getScale().x - 1.5f) < 0.05f);
    CNA_EDITOR_EXPECT(std::abs(fixture.getScale().y - 1.0f) < 0.001f);

    // A press on a handle is a manipulation and must not also reselect what it is drawn over.
    CNA_EDITOR_EXPECT_EQ(fixture.application->getContext().getSelection().size(), std::size_t{1});

    // One drag is one undo entry, exactly as translate and rotate are.
    fixture.step(UiImageInteraction{});
    fixture.application->undo();
    CNA_EDITOR_EXPECT(std::abs(fixture.getScale().x - 1.0f) < 0.001f);
}

CNA_EDITOR_TEST(AThreeDimensionalDragMovesAWholeSelectionAsOneUndoEntry)
{
    GizmoFixture fixture = makeGizmoFixture();
    EditorContext& context = fixture.application->getContext();

    // A second entity, well away from the first, and both selected.
    EditorEntity second{Uuid::generate(), "Crate"};
    EditorComponent transform{BuiltinComponentIds::kTransform};
    transform.applyDefaults(*context.getComponentRegistry().find(BuiltinComponentIds::kTransform));
    transform.setProperty("position", PropertyValue{EditorVector3{-160.0f, 220.0f, 0.0f}});
    second.addComponent(std::move(transform));
    const Uuid secondId = second.getId();
    context.getScene().addEntity(std::move(second));

    context.select(fixture.entityId);
    context.toggleSelection(secondId);
    CNA_EDITOR_EXPECT_EQ(context.getSelection().size(), std::size_t{2});

    fixture.application->setThreeDimensionalView(true);

    EditorCamera3D& camera = fixture.application->getViewport().getCamera3D();
    camera.setYaw(0.0f);
    camera.setPitch(0.0f);
    camera.setPivot(EditorVector3{100.0f, 220.0f, 0.0f});
    camera.setDistance(500.0f);
    fixture.step(UiImageInteraction{});

    const auto positionOf = [&context](const Uuid& id) {
        return context.getScene().findEntity(id)
            ->findComponent(BuiltinComponentIds::kTransform)
            ->getProperty("position")
            .get<EditorVector3>();
    };

    // Drawn on the selection's shared pivot, so that is where it has to be grabbed -- and where a
    // rotate or scale of the group turns and grows about.
    const std::optional<EditorVector3> pivot =
        computeSelectionPivot3D(context.getScene(), context.getSelection());
    CNA_EDITOR_EXPECT(pivot.has_value());
    if (!pivot) { return; }

    CNA_EDITOR_EXPECT(std::abs(pivot->x - (-30.0f)) < 0.001f);

    const std::optional<TranslateGizmo3DLayout> layout =
        computeTranslateGizmo3DLayout(context.getScene(), camera, fixture.entityId, GizmoSpace::World,
                                      pivot);
    CNA_EDITOR_EXPECT(layout.has_value());
    if (!layout) { return; }

    const EditorVector3 firstBefore = positionOf(fixture.entityId);
    const EditorVector3 secondBefore = positionOf(secondId);

    const EditorVector2 grab{(layout->screenOrigin.x + layout->screenTips[0].x) * 0.5f,
                             layout->screenOrigin.y};
    fixture.step(leftAt(grab.x, grab.y, true));
    fixture.step(leftAt(grab.x + 50.0f, grab.y, false));

    // Both moved, and by the same world delta: one gesture, many entities, so twenty of them
    // cannot disagree about how far the cursor went.
    const float firstDelta = positionOf(fixture.entityId).x - firstBefore.x;
    const float secondDelta = positionOf(secondId).x - secondBefore.x;
    CNA_EDITOR_EXPECT(firstDelta > 1.0f);
    CNA_EDITOR_EXPECT(std::abs(firstDelta - secondDelta) < 0.01f);

    // And one Ctrl+Z puts both back. A command per entity would undo them one at a time, through
    // arrangements the scene was never in.
    fixture.step(UiImageInteraction{});
    fixture.application->undo();
    CNA_EDITOR_EXPECT(std::abs(positionOf(fixture.entityId).x - firstBefore.x) < 0.001f);
    CNA_EDITOR_EXPECT(std::abs(positionOf(secondId).x - secondBefore.x) < 0.001f);
}

/**
 * @brief ED-311/ED-410: a structure element is edited field by field, and each edit is its own undo.
 *
 * The inspector half of `NestedStructure`. It matters that the *descriptor* drives the rows rather
 * than the stored value: a structure that has never been written has no fields to draw, and one
 * whose fields followed its contents could not be edited into having any.
 */
CNA_EDITOR_TEST(APerPartMaterialRowIsEditedFieldByField)
{
    GizmoFixture fixture = makeGizmoFixture();
    EditorContext& context = fixture.application->getContext();

    fixture.ui->pendingChoices.emplace_back("##addComponentType", "Rendering (3D) / Model Renderer");
    fixture.step(UiImageInteraction{});
    fixture.ui->pendingClicks.push_back("Add Component");
    fixture.step(UiImageInteraction{});

    // Adding a row gives a structure with its declared fields at their declared defaults, not an
    // empty one -- an element with no fields could never be edited into having any. The null UI
    // expands every tree, so the list's rows are drawn without asking.
    fixture.ui->pendingClicks.push_back("Add##list-CNA.ModelRenderer-materials");
    fixture.step(UiImageInteraction{});

    const auto readList = [&]
    {
        const EditorComponent* model = context.getScene().findEntity(fixture.entityId)
                                           ->findComponent(BuiltinComponentIds::kModelRenderer);
        return model->getProperty("materials").get<PropertyValue::ListValue>();
    };

    const PropertyValue::ListValue afterAdd = readList();
    CNA_EDITOR_EXPECT_EQ(afterAdd.items.size(), std::size_t{1});
    CNA_EDITOR_EXPECT(afterAdd.items[0].getType() == PropertyType::Structure);

    const PropertyValue::StructureValue fresh =
        afterAdd.items[0].get<PropertyValue::StructureValue>();
    CNA_EDITOR_EXPECT(fresh.find("part") != nullptr);
    CNA_EDITOR_EXPECT(fresh.find("material") != nullptr);

    // Adding a row is its own undo entry: pressing Add three times must not undo in one.
    CNA_EDITOR_EXPECT(context.getHistory().undo());
    CNA_EDITOR_EXPECT(readList().items.empty());
    CNA_EDITOR_EXPECT(context.getHistory().redo());
    CNA_EDITOR_EXPECT_EQ(readList().items.size(), std::size_t{1});
}

#ifdef CNA_EDITOR_TEST_PLUGIN_DIR
/**
 * @brief ED-411: a real shared library is opened, initialised, and closed in the right order.
 *
 * Against a *real* `.so` rather than a double, because nothing that actually breaks in a plugin
 * system -- `dlopen`, symbol resolution, allocating in one runtime and freeing in another, unload
 * order -- can be checked against a fake. A double would exercise the host's bookkeeping and none
 * of it.
 *
 * The observable proof is a component descriptor: present in the registry only while the plugin is
 * active, which makes "initialize ran" and "shutdown cleaned up" the same assertion read twice.
 */
CNA_EDITOR_TEST(APluginIsLoadedFromARealLibraryAndUnloadsCleanly)
{
    EditorContext context;
    PluginHost host;

    const std::vector<LoadedPlugin> found = host.discover(CNA_EDITOR_TEST_PLUGIN_DIR);
    CNA_EDITOR_EXPECT_EQ(found.size(), std::size_t{1});
    CNA_EDITOR_EXPECT_EQ(found[0].manifest.id, std::string{"org.openeggbert.testplugin"});
    CNA_EDITOR_EXPECT(found[0].loaded);

    // Discovered is not active: the manifest passed its checks, and nothing has been opened.
    CNA_EDITOR_EXPECT(!found[0].active);
    CNA_EDITOR_EXPECT(context.getComponentRegistry().find("Test.PluginComponent") == nullptr);

    CNA_EDITOR_EXPECT_EQ(host.loadAll(context), std::size_t{1});
    CNA_EDITOR_EXPECT_EQ(host.getActiveCount(), std::size_t{1});
    CNA_EDITOR_EXPECT(host.getPlugins()[0].active);
    CNA_EDITOR_EXPECT(host.getPlugins()[0].error.empty());

    // The plugin's own component is in the editor's registry -- and it is the *editor's*, which is
    // the thing a plugin linking the editor statically would silently fail at.
    const ComponentDescriptor* descriptor =
        context.getComponentRegistry().find("Test.PluginComponent");
    CNA_EDITOR_EXPECT(descriptor != nullptr);
    CNA_EDITOR_EXPECT_EQ(descriptor->displayName, std::string{"Plugin Component"});

    // ED-412's two extension points that needed a registry: a panel and a menu command.
    CNA_EDITOR_EXPECT_EQ(context.getPluginExtensions().getPanels().size(), std::size_t{1});
    CNA_EDITOR_EXPECT_EQ(context.getPluginExtensions().getPanels()[0].title,
                         std::string{"Test Plugin Panel"});
    CNA_EDITOR_EXPECT_EQ(context.getPluginExtensions().getMenuCommands().size(), std::size_t{1});
    CNA_EDITOR_EXPECT_EQ(context.getPluginExtensions().getMenuNames().size(), std::size_t{1});

    host.unloadAll(context);
    CNA_EDITOR_EXPECT_EQ(host.getActiveCount(), std::size_t{0});

    // Gone. A descriptor left registered past dlclose points into unmapped memory, which is why
    // shutdown() removing everything is a requirement rather than a courtesy.
    CNA_EDITOR_EXPECT(context.getComponentRegistry().find("Test.PluginComponent") == nullptr);

    // And so are the panel and the command -- a `std::function` outliving its library is the same
    // failure wearing different clothes, and it would not show until the next frame drew it.
    CNA_EDITOR_EXPECT(context.getPluginExtensions().getPanels().empty());
    CNA_EDITOR_EXPECT(context.getPluginExtensions().getMenuCommands().empty());
}

/**
 * @brief The host removes a plugin's registrations even when the plugin forgets to (ED-412).
 *
 * A backstop rather than the contract: `shutdown` is required to clean up, and a plugin author who
 * relies on this would leak everywhere the backstop does not reach. It exists because the failure
 * it prevents is not an error message -- it is a `std::function` pointing into unmapped code,
 * which fails on the next frame that draws it rather than at the moment of the mistake.
 */
CNA_EDITOR_TEST(TheHostRemovesRegistrationsAPluginForgotToRemove)
{
    EditorContext context;
    PluginHost host;

    host.discover(CNA_EDITOR_TEST_PLUGIN_DIR);
    CNA_EDITOR_EXPECT_EQ(host.loadAll(context), std::size_t{1});

    // Something the plugin never registered and will never clean up, under its id.
    PluginPanel stray;
    stray.ownerId = "org.openeggbert.testplugin";
    stray.title = "Forgotten Panel";
    stray.draw = [](EditorUi&, EditorContext&) {};
    context.getPluginExtensions().addPanel(std::move(stray));
    CNA_EDITOR_EXPECT_EQ(context.getPluginExtensions().getPanels().size(), std::size_t{2});

    host.unloadAll(context);
    CNA_EDITOR_EXPECT(context.getPluginExtensions().getPanels().empty());
}

/**
 * @brief Hot-reload is an unload and a load, and says so: the plugin comes back registered afresh.
 *
 * What the test pins is that reload leaves the editor in the state a fresh load would -- one copy
 * of the component, not two, and not none. A reload that re-registered without unregistering, or
 * unregistered without coming back, both look fine for one cycle.
 */
CNA_EDITOR_TEST(ReloadingAPluginLeavesItRegisteredExactlyOnce)
{
    EditorContext context;
    PluginHost host;

    host.discover(CNA_EDITOR_TEST_PLUGIN_DIR);
    CNA_EDITOR_EXPECT_EQ(host.loadAll(context), std::size_t{1});

    for (int cycle = 0; cycle < 3; ++cycle)
    {
        CNA_EDITOR_EXPECT(host.reload(context, "org.openeggbert.testplugin"));
        CNA_EDITOR_EXPECT_EQ(host.getActiveCount(), std::size_t{1});
        CNA_EDITOR_EXPECT(context.getComponentRegistry().find("Test.PluginComponent") != nullptr);
    }

    // An id nobody has is a failure rather than a silent no-op: a user who typed it wrong would
    // otherwise be told nothing and believe the reload happened.
    CNA_EDITOR_EXPECT(!host.reload(context, "org.openeggbert.nosuchplugin"));

    host.unloadAll(context);
    CNA_EDITOR_EXPECT(context.getComponentRegistry().find("Test.PluginComponent") == nullptr);
}
#endif
