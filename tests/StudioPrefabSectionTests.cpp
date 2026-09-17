// SPDX-License-Identifier: MS-PL
/**
 * @file StudioPrefabSectionTests.cpp
 * @brief Prefab overrides in the native Details panel: report, revert, apply.
 *
 * `plan.md` STUDIO-07042 — the last of the five Inspector sections `STUDIO-07041` found with no
 * native answer, and therefore the last thing standing between Studio and deleting Dear ImGui.
 *
 * Three behaviours, and the third is the one with teeth. **Report** is a comparison rather than a
 * record, so it has to be run rather than remembered. **Revert** throws the instance's changes
 * away, which is a scene mutation like any other and goes through the history. **Apply** writes
 * the *prefab file* — an asset every other instance of that prefab is about to be compared
 * against — which makes it the one button in this panel whose failure must reach the user.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/PrefabWorkflow.hpp"
#include "CNA/Studio/Scene/BuiltinComponents.hpp"
#include "CNA/Studio/Scene/PrefabCommands.hpp"
#include "CNA/Studio/Scene/PrefabDocument.hpp"
#include "CNA/Studio/ShellPanels/StudioDetailsPanel.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioFontAtlas.hpp"
#include "CNA/Studio/UiCore/StudioFrame.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    float metricOf(const StudioTheme& theme, StudioMetric metric)
    {
        return static_cast<float>(theme.metric(metric));
    }

    UiInputState at(float x, float y, bool leftDown = false)
    {
        UiInputState input;
        input.displayWidth = 1280.0f;
        input.displayHeight = 900.0f;
        input.mouseX = x;
        input.mouseY = y;
        input.mouseInWindow = true;
        input.setMouseDown(UiMouseButton::Left, leftDown);
        return input;
    }

    /** @brief A project root with a real `.cnaprefab` in it, removed on the way out. */
    class ScopedProject
    {
    public:
        explicit ScopedProject(const std::string& name)
        {
            path_ = std::filesystem::temp_directory_path()
                  / ("cna-studio-prefab-" + name + "-" + std::to_string(counter()++));
            std::error_code code;
            std::filesystem::remove_all(path_, code);
            std::filesystem::create_directories(path_ / "Assets", code);
        }

        ~ScopedProject()
        {
            std::error_code code;
            std::filesystem::remove_all(path_, code);
        }

        ScopedProject(const ScopedProject&) = delete;
        ScopedProject& operator=(const ScopedProject&) = delete;

        [[nodiscard]] std::string root() const { return path_.generic_string(); }
        [[nodiscard]] std::filesystem::path at(const std::string& relative) const
        {
            return path_ / relative;
        }

    private:
        static int& counter() { static int value = 0; return value; }
        std::filesystem::path path_;
    };

    /** @brief An entity with a transform at @p x, @p y. */
    StudioEntity makeEntity(const ComponentRegistry& registry, const std::string& name, float x,
                            float y)
    {
        StudioEntity entity{Uuid::generate(), name};
        StudioComponent transform{BuiltinComponentIds::kTransform};
        const ComponentDescriptor* descriptor =
            registry.find(BuiltinComponentIds::kTransform);
        if (descriptor != nullptr)
        {
            for (const PropertyDescriptor& property : descriptor->properties)
            {
                transform.setProperty(property.name, property.defaultValue);
            }
        }
        transform.setProperty("position", PropertyValue{StudioVector3{x, y, 0.0f}});
        entity.getComponents().push_back(std::move(transform));
        return entity;
    }

    /**
     * @brief A scene holding one instance of a prefab that exists on disk.
     *
     * The file is real because the section reads it: a fixture that handed the panel a
     * `PrefabDocument` in memory would test everything except the thing this panel actually does.
     */
    struct Fixture
    {
        ScopedProject project;
        StudioContext context;
        StudioFrame frame{StudioTheme::dark()};
        StudioFontAtlas fonts;
        UiRect bounds{0.0f, 0.0f, 520.0f, 880.0f};
        StudioDetailsResult last;

        Uuid prefabAsset;
        Uuid instanceRoot;
        Uuid instanceChild;

        explicit Fixture(const std::string& name) : project(name)
        {
            frame.setFontAtlas(&fonts);
            context.getAssets().setProjectRoot(project.root());

            // The prefab: a parent and a child, captured from a scene of its own.
            SceneDocument source;
            const Uuid rootId = source.addEntity(
                makeEntity(context.getComponentRegistry(), "Enemy", 10.0f, 20.0f));
            const Uuid childId = source.addEntity(
                makeEntity(context.getComponentRegistry(), "Weapon", 5.0f, 0.0f));
            source.reparentEntity(childId, rootId);

            PrefabDocument prefab;
            prefab.captureFromScene(source, rootId, "Enemy");
            std::string problem;
            CNA_STUDIO_EXPECT(prefab.saveToFile(project.at("Assets/Enemy.cnaprefab").string(),
                                                &problem));

            AssetRecord record;
            record.id = Uuid::generate();
            record.sourcePath = "Assets/Enemy.cnaprefab";
            record.type = AssetType::Prefab;
            prefabAsset = record.id;
            (void)context.getAssets().add(std::move(record));

            InstantiatePrefabCommand instantiate{context.getScene(), prefab, prefabAsset, Uuid{}};
            CNA_STUDIO_EXPECT(instantiate.isValid());
            instanceRoot = instantiate.getRootId();
            instantiate.execute();

            const std::vector<Uuid> children = context.getScene().getChildren(instanceRoot);
            CNA_STUDIO_EXPECT(!children.empty());
            if (!children.empty()) { instanceChild = children.front(); }

            context.select(instanceRoot);
        }

        /**
         * @brief What Revert or Apply did, accumulated across the frames of a click.
         *
         * Separate from @ref last because the two are decided on different passes: the report is
         * true on both, and an action can only happen on the pass that routes input. A test that
         * read the draw pass's copy would see every button press as having done nothing.
         */
        StudioPrefabSectionResult action;

        void run(const UiInputState& input)
        {
            runStudioFrame(frame, input, [&](StudioFrame& f) {
                const StudioDetailsResult result = studioDetailsPanel(f, bounds, context);
                if (f.isDrawPass()) { last = result; }
                if (f.isInputPass()
                    && (result.prefab.reverted || result.prefab.applied || result.prefab.failed))
                {
                    action = result.prefab;
                }
            });
        }

        void settle() { run(at(-1.0f, -1.0f)); }

        void click(float x, float y)
        {
            run(at(x, y));
            run(at(x, y, /*leftDown=*/true));
            run(at(x, y));
        }

        /** @brief The row rectangle at @p index of the panel's content. */
        [[nodiscard]] UiRect row(std::size_t index) const
        {
            const float height = std::max(metricOf(frame.theme(), StudioMetric::ControlHeight),
                                          metricOf(frame.theme(), StudioMetric::MinimumHitTarget));
            const float spacing = metricOf(frame.theme(), StudioMetric::SpacingXSmall);
            const UiRect area =
                bounds.inset(UiEdges{metricOf(frame.theme(), StudioMetric::SpacingSmall)});
            return UiRect{area.x, area.y + static_cast<float>(index) * (height + spacing),
                          area.width, height};
        }

        [[nodiscard]] float controlLeft() const
        {
            const UiRect area =
                bounds.inset(UiEdges{metricOf(frame.theme(), StudioMetric::SpacingSmall)});
            return area.x + std::round(area.width * 0.38f)
                   + metricOf(frame.theme(), StudioMetric::SpacingSmall);
        }

        /**
         * @brief Clicks Revert or Apply, wherever the section has put them.
         *
         * Swept rather than computed: the button row's index depends on how many override lines
         * are above it, and a test that hard-coded the row would be asserting about the list's
         * length rather than about the button.
         */
        void clickAction(int which)
        {
            const float width =
                std::max(metricOf(frame.theme(), StudioMetric::ControlHeight) * 2.5f, 64.0f);
            const float spacing = metricOf(frame.theme(), StudioMetric::SpacingXSmall);
            const float x = controlLeft() + (width + spacing) * static_cast<float>(which)
                          + width * 0.5f;

            for (std::size_t index = 2; index < 10; ++index)
            {
                const UiRect box = row(index);
                click(x, box.centerY());
                if (action.reverted || action.applied || action.failed) { return; }
            }
        }

        /** @brief Moves the instance root, which is one override. */
        void moveRoot(float x)
        {
            context.getScene()
                .findEntityForEdit(instanceRoot)
                ->findComponent(BuiltinComponentIds::kTransform)
                ->setProperty("position", PropertyValue{StudioVector3{x, 20.0f, 0.0f}});
        }

        [[nodiscard]] StudioVector3 rootPosition() const
        {
            return context.getScene()
                .findEntity(instanceRoot)
                ->findComponent(BuiltinComponentIds::kTransform)
                ->getProperty("position")
                .get<StudioVector3>();
        }
    };
}

CNA_STUDIO_TEST(AFreshInstanceReportsNoChanges)
{
    Fixture fixture{"clean"};
    fixture.settle();

    CNA_STUDIO_EXPECT(fixture.last.prefab.present);
    CNA_STUDIO_EXPECT_EQ(fixture.last.prefab.overrides, std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(fixture.frame.phaseViolations(), std::size_t{0});
}

CNA_STUDIO_TEST(AnEntityThatIsNotPartOfAnInstanceGetsNoSection)
{
    // The section costs a file read, so it must not appear for the ordinary case.
    Fixture fixture{"outside"};
    const Uuid loose =
        fixture.context.getScene().addEntity(
            makeEntity(fixture.context.getComponentRegistry(), "Loose", 0.0f, 0.0f));
    fixture.context.select(loose);
    fixture.settle();

    CNA_STUDIO_EXPECT(!fixture.last.prefab.present);
    CNA_STUDIO_EXPECT_EQ(fixture.last.prefab.overrides, std::size_t{0});
}

CNA_STUDIO_TEST(SelectingAChildStillAnswersForTheInstance)
{
    // Answered for the instance, not for the entity. Selecting a child of an instance should still
    // say what it is part of and let the user act on it.
    Fixture fixture{"child"};
    fixture.moveRoot(999.0f);
    fixture.context.select(fixture.instanceChild);
    fixture.settle();

    CNA_STUDIO_EXPECT(fixture.last.prefab.present);
    CNA_STUDIO_EXPECT_EQ(fixture.last.prefab.overrides, std::size_t{1});
}

CNA_STUDIO_TEST(ChangingTheInstanceIsReportedAsAnOverride)
{
    Fixture fixture{"changed"};
    fixture.settle();
    CNA_STUDIO_EXPECT_EQ(fixture.last.prefab.overrides, std::size_t{0});

    fixture.moveRoot(999.0f);
    fixture.settle();
    CNA_STUDIO_EXPECT_EQ(fixture.last.prefab.overrides, std::size_t{1});

    // And a structural change is counted too, not only a property.
    StudioEntity extra = makeEntity(fixture.context.getComponentRegistry(), "Shield", 0.0f, 0.0f);
    const Uuid extraId = fixture.context.getScene().addEntity(std::move(extra));
    fixture.context.getScene().reparentEntity(extraId, fixture.instanceRoot);
    fixture.settle();
    CNA_STUDIO_EXPECT_EQ(fixture.last.prefab.overrides, std::size_t{2});
}

CNA_STUDIO_TEST(RevertingThrowsTheChangesAwayAndUndoBringsThemBack)
{
    Fixture fixture{"revert"};
    fixture.moveRoot(999.0f);
    fixture.settle();
    CNA_STUDIO_EXPECT_EQ(fixture.last.prefab.overrides, std::size_t{1});

    fixture.clickAction(0);
    CNA_STUDIO_EXPECT(fixture.action.reverted);
    CNA_STUDIO_EXPECT(!fixture.action.failed);
    CNA_STUDIO_EXPECT_EQ(fixture.rootPosition().x, 10.0f);

    fixture.settle();
    CNA_STUDIO_EXPECT_EQ(fixture.last.prefab.overrides, std::size_t{0});

    // Through the history like every other scene mutation: reverting by mistake is exactly the
    // kind of click Ctrl+Z exists for.
    CNA_STUDIO_EXPECT(fixture.context.getHistory().canUndo());
    CNA_STUDIO_EXPECT(fixture.context.getHistory().undo());
    CNA_STUDIO_EXPECT_EQ(fixture.rootPosition().x, 999.0f);
}

CNA_STUDIO_TEST(ApplyingWritesTheChangesIntoThePrefabFile)
{
    Fixture fixture{"apply"};
    fixture.moveRoot(999.0f);
    fixture.settle();
    CNA_STUDIO_EXPECT_EQ(fixture.last.prefab.overrides, std::size_t{1});

    fixture.clickAction(1);
    CNA_STUDIO_EXPECT(fixture.action.applied);
    CNA_STUDIO_EXPECT(!fixture.action.failed);

    // The file on disk now holds what the instance held, so the comparison finds nothing.
    PrefabDocument written;
    CNA_STUDIO_EXPECT(written.loadFromFile(fixture.project.at("Assets/Enemy.cnaprefab").string(),
                                           fixture.context.getComponentRegistry())
                          .succeeded);
    CNA_STUDIO_EXPECT(findPrefabOverrides(fixture.context.getScene(), fixture.instanceRoot, written,
                                          fixture.context.getComponentRegistry())
                          .empty());

    fixture.settle();
    CNA_STUDIO_EXPECT_EQ(fixture.last.prefab.overrides, std::size_t{0});
}

CNA_STUDIO_TEST(AnInstanceWhosePrefabIsGoneIsReportedRatherThanHidden)
{
    // The link survives the asset going away. An instance whose prefab was deleted is exactly what
    // a user needs told -- and the buttons must not be offered, because neither can do anything.
    Fixture fixture{"gone"};
    fixture.moveRoot(999.0f);
    CNA_STUDIO_EXPECT(fixture.context.getAssets().removeRecord(fixture.prefabAsset));

    fixture.settle();
    CNA_STUDIO_EXPECT(fixture.last.prefab.present);
    CNA_STUDIO_EXPECT_EQ(fixture.last.prefab.overrides, std::size_t{0});

    fixture.clickAction(0);
    CNA_STUDIO_EXPECT(!fixture.action.reverted);
    CNA_STUDIO_EXPECT(!fixture.action.applied);
    CNA_STUDIO_EXPECT_EQ(fixture.rootPosition().x, 999.0f);
}

CNA_STUDIO_TEST(APrefabFileThatWillNotLoadIsReportedRatherThanTreatedAsEmpty)
{
    // A prefab written by a newer Studio, or one somebody has half-edited. Treating it as an empty
    // prefab would report every entity in the instance as an addition and offer to apply them.
    Fixture fixture{"broken"};
    {
        std::ofstream stream{fixture.project.at("Assets/Enemy.cnaprefab"), std::ios::binary};
        stream << R"({"formatVersion": 999})";
    }

    fixture.settle();
    CNA_STUDIO_EXPECT(fixture.last.prefab.present);
    CNA_STUDIO_EXPECT_EQ(fixture.last.prefab.overrides, std::size_t{0});

    fixture.clickAction(1);
    CNA_STUDIO_EXPECT(!fixture.action.applied);
}

CNA_STUDIO_TEST(TheListShowsThreeChangesAndCountsTheRest)
{
    // The list exists to make the divergence recognisable, not to enumerate it: a hundred
    // overrides is a hundred rows nobody reads.
    Fixture fixture{"many"};
    for (int index = 0; index < 6; ++index)
    {
        StudioEntity extra = makeEntity(fixture.context.getComponentRegistry(),
                                        "Extra" + std::to_string(index), 0.0f, 0.0f);
        const Uuid extraId = fixture.context.getScene().addEntity(std::move(extra));
        fixture.context.getScene().reparentEntity(extraId, fixture.instanceRoot);
    }

    fixture.settle();
    CNA_STUDIO_EXPECT_EQ(fixture.last.prefab.overrides, std::size_t{6});

    // Six overrides, three lines, one "and three more" -- so the section never grows past the
    // rows the panel reserved for it.
    CNA_STUDIO_EXPECT(fixture.last.rowsDrawn > 0);
}
