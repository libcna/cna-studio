// SPDX-License-Identifier: MS-PL
/**
 * @file StudioDetailsPanelTests.cpp
 * @brief The Details panel: the first ported panel that writes to the document.
 *
 * `plan.md` STUDIO-07007.
 *
 * That is the whole difference between this and the outliner, and it decides what is worth testing.
 * Showing a scene wrong is a bad afternoon; editing one wrong is a lost afternoon's work. So the
 * cases here are about *when* a value reaches the document (on commit, never per keystroke), *how*
 * it gets there (through the command history, so Ctrl+Z reaches it), and what happens to input that
 * is not a value at all.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Assets/AssetCommands.hpp"
#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/Assets/AssetDependencies.hpp"
#include "CNA/Studio/Scene/BuiltinComponents.hpp"
#include "CNA/Studio/Scene/SceneCommands.hpp"
#include "CNA/Studio/Core/NumberText.hpp"
#include "CNA/Studio/Scene/SceneTransform.hpp"
#include "CNA/Studio/ShellPanels/StudioDetailsPanel.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"
#include "CNA/Studio/UiCore/UiSoftwareRasterizer.hpp"

#include <algorithm>
#include <filesystem>
#include <cstdint>
#include <set>
#include <cmath>
#include <memory>
#include <string>

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
        input.displayHeight = 720.0f;
        input.mouseX = x;
        input.mouseY = y;
        input.mouseInWindow = true;
        input.setMouseDown(UiMouseButton::Left, leftDown);
        return input;
    }

    /** @brief A context with one entity carrying a Transform, selected. */
    struct Fixture
    {
        StudioContext context;
        Uuid entity;

        Fixture()
        {
            StudioEntity subject{Uuid::generate(), "Player"};

            StudioComponent transform{"CNA.Transform"};
            transform.setProperty("position", PropertyValue{StudioVector3{1.0f, 2.0f, 3.0f}});
            subject.getComponents().push_back(std::move(transform));

            entity = subject.getId();
            context.getScene().addEntity(std::move(subject));
            context.select(entity);
        }

        [[nodiscard]] StudioVector3 position() const
        {
            const StudioEntity* found = context.getScene().findEntity(entity);
            if (found == nullptr) { return {}; }
            const StudioComponent* transform = found->findComponent("CNA.Transform");
            if (transform == nullptr) { return {}; }
            return transform->getProperty("position").get<StudioVector3>();
        }

        [[nodiscard]] std::string name() const
        {
            const StudioEntity* found = context.getScene().findEntity(entity);
            return found != nullptr ? found->getName() : std::string{};
        }
    };

    /** @brief The shell with the Details panel raised and driving @p fixture. */
    struct Harness
    {
        std::unique_ptr<StudioShell> shell = std::make_unique<StudioShell>(StudioTheme::dark());
        UiRect bounds;
        StudioDetailsResult last;

        explicit Harness(StudioContext& context)
        {
            shell->resetLayout();
            shell->renderFrame(at(-1.0f, -1.0f));
            CNA_STUDIO_EXPECT(shell->activatePanel("details"));
            CNA_STUDIO_EXPECT(shell->setPanelContent("details",
                [this, &context](StudioFrame& frame, const UiRect& area) {
                    const StudioDetailsResult result = studioDetailsPanel(frame, area, context);
                    if (frame.isInputPass()) { last = result; }
                    if (frame.isDrawPass()) { bounds = area; }
                }));
            shell->renderFrame(at(-1.0f, -1.0f));
        }

        /** @brief Clicks, starting from an un-pressed frame so the router sees a real press. */
        void click(float x, float y)
        {
            shell->renderFrame(at(x, y, false));
            shell->renderFrame(at(x, y, true));
            shell->renderFrame(at(x, y, false));
        }

        void type(const std::vector<char16_t>& characters, float x, float y)
        {
            UiInputState input = at(x, y);
            input.characters = characters;
            shell->renderFrame(input);
        }

        void press(UiKey key, float x, float y)
        {
            UiInputState input = at(x, y);
            input.setKeyDown(key, true);
            shell->renderFrame(input);
            shell->renderFrame(at(x, y));
        }

        /**
         * @brief Presses at (@p x, @p y) and drags @p dx pixels sideways, then releases.
         *
         * `STUDIO-07055`. The move is delivered in one frame rather than several on purpose: a
         * scrub is anchored to the value at the press, so a gesture that arrives in one jump and
         * one that arrives in twenty have to end at the same number. A test that only ever moved a
         * pixel at a time would pass for an implementation that accumulated frame deltas, which is
         * the implementation this widget deliberately is not.
         */
        void drag(float x, float y, float dx)
        {
            shell->renderFrame(at(x, y, false));
            shell->renderFrame(at(x, y, true));
            shell->renderFrame(at(x + dx, y, true));
            shell->renderFrame(at(x + dx, y, false));
            shell->renderFrame(at(x + dx, y, false));
        }
    };
}

CNA_STUDIO_TEST(TheDetailsPanelShowsTheSelectedEntitysComponents)
{
    Fixture fixture;
    Harness harness{fixture.context};

    CNA_STUDIO_EXPECT(!harness.bounds.isEmpty());
    CNA_STUDIO_EXPECT_EQ(harness.last.componentCount, std::size_t{1});

    // Name, Enabled, a gap, the component header and its properties.
    CNA_STUDIO_EXPECT(harness.last.rowsDrawn >= std::size_t{4});
    CNA_STUDIO_EXPECT_EQ(harness.shell->frame().phaseViolations(), std::size_t{0});
}

CNA_STUDIO_TEST(WithNothingSelectedThePanelSaysWhatToDoNext)
{
    // "Select an entity to see its details" is a next action; "nothing to show" is a dead end.
    Fixture fixture;
    fixture.context.clearSelection();
    Harness harness{fixture.context};

    CNA_STUDIO_EXPECT_EQ(harness.last.rowsDrawn, std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(harness.last.componentCount, std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(harness.shell->frame().phaseViolations(), std::size_t{0});
}

CNA_STUDIO_TEST(RenamingAnEntityGoesThroughTheHistorySoUndoReachesIt)
{
    Fixture fixture;
    Harness harness{fixture.context};

    // The Name field is the first row's control, which sits in the right-hand column.
    const float x = harness.bounds.left() + harness.bounds.width * 0.75f;
    const float y = harness.bounds.top() + 14.0f;

    harness.click(x, y);
    harness.type({u'!'}, x, y);
    // Nothing yet: a field that wrote per keystroke would put one undo entry per letter.
    CNA_STUDIO_EXPECT_EQ(fixture.name(), std::string{"Player"});

    harness.press(UiKey::Enter, x, y);
    CNA_STUDIO_EXPECT_EQ(fixture.name(), std::string{"Player!"});

    // And the history has it, which is the point of going through a command at all.
    CNA_STUDIO_EXPECT(fixture.context.getHistory().canUndo());
    CNA_STUDIO_EXPECT(fixture.context.getHistory().undo());
    CNA_STUDIO_EXPECT_EQ(fixture.name(), std::string{"Player"});
}

CNA_STUDIO_TEST(EditingOneAxisOfAVectorLeavesTheOthersAlone)
{
    // The failure this catches is the classic one: a vector row that rebuilds all three components
    // from the field being edited and writes the other two back as whatever it last formatted.
    Fixture fixture;
    Harness harness{fixture.context};

    const StudioVector3 before = fixture.position();
    CNA_STUDIO_EXPECT_EQ(before.x, 1.0f);
    CNA_STUDIO_EXPECT_EQ(before.y, 2.0f);
    CNA_STUDIO_EXPECT_EQ(before.z, 3.0f);

    // The y field of the position row: the middle of the three boxes in the right-hand column.
    // Found by sweeping the row rather than by computing a pixel, so a metric change does not turn
    // this into a test that types into nothing and passes.
    bool edited = false;
    for (float y = harness.bounds.top() + 20.0f;
         y < harness.bounds.top() + 200.0f && !edited; y += 6.0f)
    {
        const float columnLeft = harness.bounds.left() + harness.bounds.width * 0.40f;
        const float x = columnLeft + (harness.bounds.right() - columnLeft) * 0.5f;

        harness.click(x, y);
        harness.type({u'9'}, x, y);
        harness.press(UiKey::Enter, x, y);
        edited = fixture.position().y != before.y;
    }

    CNA_STUDIO_EXPECT(edited);
    CNA_STUDIO_EXPECT_EQ(fixture.position().x, 1.0f);
    CNA_STUDIO_EXPECT_EQ(fixture.position().z, 3.0f);
    CNA_STUDIO_EXPECT(fixture.context.getHistory().canUndo());
}

CNA_STUDIO_TEST(TextThatIsNotANumberIsRejectedRatherThanTurnedIntoZero)
{
    // "3abc" is not three, and "" is not zero. Accepting a prefix, or treating a failed parse as a
    // default, is how a typo silently becomes a value the user did not enter and cannot see is
    // wrong -- in a position, which puts their sprite somewhere else.
    Fixture fixture;
    Harness harness{fixture.context};

    const StudioVector3 before = fixture.position();

    // Below the Name and Enabled rows. A string field would accept "abc" perfectly correctly, so
    // sweeping across it would leave an undo entry this case would then blame on the number.
    const StudioTheme theme = StudioTheme::dark();
    const float rowPitch = static_cast<float>(std::max(theme.metric(StudioMetric::ControlHeight),
                                                       theme.metric(StudioMetric::MinimumHitTarget))
                                              + theme.metric(StudioMetric::SpacingXSmall));
    const float firstPropertyRow = harness.bounds.top() + rowPitch * 3.0f;

    for (float y = firstPropertyRow; y < firstPropertyRow + 200.0f; y += 6.0f)
    {
        const float columnLeft = harness.bounds.left() + harness.bounds.width * 0.40f;
        const float x = columnLeft + (harness.bounds.right() - columnLeft) * 0.2f;

        harness.click(x, y);
        harness.type({u'a', u'b', u'c'}, x, y);
        harness.press(UiKey::Enter, x, y);
    }

    CNA_STUDIO_EXPECT_EQ(fixture.position().x, before.x);
    CNA_STUDIO_EXPECT_EQ(fixture.position().y, before.y);
    CNA_STUDIO_EXPECT_EQ(fixture.position().z, before.z);
    CNA_STUDIO_EXPECT(!fixture.context.getHistory().canUndo());
}

CNA_STUDIO_TEST(AnEntityDeletedWhileSelectedIsSaidSoRatherThanCrashedOn)
{
    // The selection outlives the entity whenever something else removes it -- an undo, a script, a
    // second view. Dereferencing what is no longer there would take the editor down with it.
    Fixture fixture;
    Harness harness{fixture.context};
    CNA_STUDIO_EXPECT(harness.last.componentCount > 0);

    (void)fixture.context.getScene().removeEntityRecursive(fixture.entity);
    harness.shell->renderFrame(at(-1.0f, -1.0f));

    CNA_STUDIO_EXPECT_EQ(harness.last.componentCount, std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(harness.last.rowsDrawn, std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(harness.shell->frame().phaseViolations(), std::size_t{0});
}

// ---------------------------------------------------------------------------------------------
// The kinds that used to be read-only (plan.md STUDIO-07018, STUDIO-07019)
// ---------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(TurningAnEntityOffGoesThroughTheHistorySoUndoReachesIt)
{
    // It was the one edit in the panel Ctrl+Z could not reach, and it is the one somebody does by
    // accident: the flag decides whether an entity renders, ticks and answers queries at all.
    Fixture fixture;
    CNA_STUDIO_EXPECT(fixture.context.getScene().findEntity(fixture.entity)->isEnabled());

    auto command = std::make_unique<SetEntityEnabledCommand>(fixture.context.getScene(),
                                                             fixture.entity, false);
    CNA_STUDIO_EXPECT(command->isValid());
    fixture.context.execute(std::move(command));

    CNA_STUDIO_EXPECT(!fixture.context.getScene().findEntity(fixture.entity)->isEnabled());
    CNA_STUDIO_EXPECT_EQ(fixture.context.getHistory().getCount(), std::size_t{1});

    fixture.context.getHistory().undo();
    CNA_STUDIO_EXPECT(fixture.context.getScene().findEntity(fixture.entity)->isEnabled());
}

CNA_STUDIO_TEST(SettingTheEnabledFlagToWhatItAlreadyIsIsRefused)
{
    // An undo stack with no-ops in it makes Ctrl+Z appear to do nothing, which is worse than doing
    // the wrong thing: the user cannot tell how many more to press.
    Fixture fixture;
    const SetEntityEnabledCommand command{fixture.context.getScene(), fixture.entity, true};
    CNA_STUDIO_EXPECT(!command.isValid());

    const SetEntityEnabledCommand missing{fixture.context.getScene(), Uuid::generate(), false};
    CNA_STUDIO_EXPECT(!missing.isValid());
}

CNA_STUDIO_TEST(RepeatedEnabledFlipsMergeIntoOneUndoStep)
{
    Fixture fixture;
    fixture.context.execute(
        std::make_unique<SetEntityEnabledCommand>(fixture.context.getScene(), fixture.entity, false),
        MergePolicy::MergeWithPrevious);
    fixture.context.execute(
        std::make_unique<SetEntityEnabledCommand>(fixture.context.getScene(), fixture.entity, true),
        MergePolicy::MergeWithPrevious);

    CNA_STUDIO_EXPECT_EQ(fixture.context.getHistory().getCount(), std::size_t{1});

    // And undoing that one step returns to where it started, not to the intermediate state.
    fixture.context.getHistory().undo();
    CNA_STUDIO_EXPECT(fixture.context.getScene().findEntity(fixture.entity)->isEnabled());
}

CNA_STUDIO_TEST(AQuaternionIsEditedAsAnglesRatherThanAsFourRawNumbers)
{
    // Nobody knows what to type into w to turn something thirty degrees, and four independent
    // numbers is how you produce a value that is not a rotation at all. The panel therefore shows
    // Euler degrees, in the convention the runtime reads back.
    Fixture fixture;

    StudioEntity* entity = fixture.context.getScene().findEntity(fixture.entity);
    StudioComponent* transform = entity->findComponent("CNA.Transform");
    transform->setProperty("rotation",
                           PropertyValue{quaternionFromEulerDegrees(StudioVector3{0.0f, 90.0f, 0.0f})});

    Harness harness{fixture.context};
    CNA_STUDIO_EXPECT(!harness.bounds.isEmpty());

    const StudioVector3 shown = eulerDegreesOf(
        fixture.context.getScene().findEntity(fixture.entity)
            ->findComponent("CNA.Transform")->getProperty("rotation").get<StudioQuaternion>());
    CNA_STUDIO_EXPECT(std::abs(shown.y - 90.0f) < 0.01f);
}

CNA_STUDIO_TEST(EveryPropertyKindTheSchemaDeclaresGetsAControlRatherThanASummary)
{
    // The failure this catches is a property kind silently falling through to "(not editable
    // yet)": it looks deliberate, reads as a decision, and is how a kind stays unimplemented long
    // after the widget it needed arrived.
    Fixture fixture;

    StudioEntity* entity = fixture.context.getScene().findEntity(fixture.entity);
    StudioComponent extras{"Test.Kinds"};
    extras.setProperty("colour", PropertyValue{StudioColor{10, 20, 30, 40}});
    extras.setProperty("rect", PropertyValue{StudioRectangle{1, 2, 3, 4}});
    extras.setProperty("four", PropertyValue{StudioVector4{1.0f, 2.0f, 3.0f, 4.0f}});
    extras.setProperty("turn", PropertyValue{StudioQuaternion{}});
    extras.setProperty("asset", PropertyValue{PropertyValue::AssetReference{Uuid{}}});
    extras.setProperty("entity", PropertyValue{PropertyValue::EntityReference{Uuid{}}});
    entity->addComponent(std::move(extras));

    Harness harness{fixture.context};

    // Six kinds, none of them falling through to a summary. Counting the fall-throughs is what
    // makes this assertion mean something: a row count would be satisfied by six summaries.
    CNA_STUDIO_EXPECT_EQ(harness.shell->frame().phaseViolations(), std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(harness.last.componentCount, std::size_t{2});
    CNA_STUDIO_EXPECT_EQ(harness.last.readOnlyProperties, std::size_t{0});

    // And a list is no longer one of them (STUDIO-07054). It used to fall through to "1 item" and
    // be counted here; it now gets a row that expands into its elements, so the count stays at
    // zero. This assertion is kept rather than deleted because it is the one that would notice a
    // kind quietly *losing* its editor again.
    StudioComponent nested{"Test.Nested"};
    PropertyValue::ListValue list;
    list.items.push_back(PropertyValue{1});
    nested.setProperty("items", PropertyValue{std::move(list)});
    fixture.context.getScene().findEntity(fixture.entity)->addComponent(std::move(nested));

    Harness second{fixture.context};
    CNA_STUDIO_EXPECT_EQ(second.last.readOnlyProperties, std::size_t{0});
}

// ------------------------------------------------------------------------------------------------
// Adding and removing components (STUDIO-07040)
//
// The gap that stopped Dear ImGui being deleted. The prototype's Inspector has had an Add Component
// control since it existed and the native Details panel had none -- so an entity created in the
// native shell could never be given anything to do. The migration inventory did not catch it
// because it accounts for panels, menus, toolbars and shortcuts, and this is a button inside a
// panel. These tests are at the level the inventory did not reach.
//
// Both sweep for the control rather than computing a pixel, like every other test in this file: a
// test that computes a coordinate becomes, the first time a metric changes, a test that clicks
// nothing and passes.
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(AComponentCanBeAddedToTheSelectedEntityAndUndone)
{
    Fixture fixture;
    Harness harness{fixture.context};

    const auto componentCount = [&fixture] {
        const StudioEntity* found = fixture.context.getScene().findEntity(fixture.entity);
        return found != nullptr ? found->getComponents().size() : std::size_t{0};
    };

    const std::size_t before = componentCount();
    CNA_STUDIO_EXPECT_EQ(before, std::size_t{1});

    // The Add button is at the right-hand end of the last row. Swept from the bottom of the panel
    // upward, because the row it is on moves with the number of properties above it.
    bool added = false;
    for (float y = harness.bounds.bottom() - 8.0f;
         y > harness.bounds.top() && !added; y -= 5.0f)
    {
        harness.click(harness.bounds.right() - 24.0f, y);
        added = componentCount() > before;
    }

    CNA_STUDIO_EXPECT(added);
    CNA_STUDIO_EXPECT_EQ(componentCount(), before + 1);

    // Through the history like every other edit. A component added outside it is one Undo cannot
    // take back, and adding the wrong one is exactly the mistake a click makes.
    CNA_STUDIO_EXPECT(fixture.context.getHistory().canUndo());
    CNA_STUDIO_EXPECT(fixture.context.getHistory().undo());
    CNA_STUDIO_EXPECT_EQ(componentCount(), before);
}

CNA_STUDIO_TEST(AComponentIsRemovedFromItsOwnHeaderAndUndone)
{
    // A Transform alone cannot be removed -- its descriptor marks it required -- so the entity
    // gets a second component to take away. That the required one *refuses* is the other half, and
    // is asserted by the count not falling to zero.
    Fixture fixture;
    {
        StudioEntity* entity = fixture.context.getScene().findEntity(fixture.entity);
        CNA_STUDIO_EXPECT(entity != nullptr);
        entity->getComponents().push_back(StudioComponent{"CNA.SpriteRenderer"});
    }

    Harness harness{fixture.context};

    const auto componentCount = [&fixture] {
        const StudioEntity* found = fixture.context.getScene().findEntity(fixture.entity);
        return found != nullptr ? found->getComponents().size() : std::size_t{0};
    };

    const std::size_t before = componentCount();
    CNA_STUDIO_EXPECT_EQ(before, std::size_t{2});

    bool removed = false;
    for (float y = harness.bounds.top() + 8.0f;
         y < harness.bounds.bottom() && !removed; y += 5.0f)
    {
        harness.click(harness.bounds.right() - 12.0f, y);
        removed = componentCount() < before;
    }

    CNA_STUDIO_EXPECT(removed);
    CNA_STUDIO_EXPECT_EQ(componentCount(), before - 1);

    // And it is the *right* one. The sweep starts at the top, where the Transform's header is, so
    // a Remove that ignored `required` would have taken the Transform first -- and an entity
    // without one is not an entity. Asserting only the count would pass either way.
    {
        const StudioEntity* found = fixture.context.getScene().findEntity(fixture.entity);
        CNA_STUDIO_EXPECT(found->findComponent("CNA.Transform") != nullptr);
        CNA_STUDIO_EXPECT(found->findComponent("CNA.SpriteRenderer") == nullptr);
    }

    CNA_STUDIO_EXPECT(fixture.context.getHistory().canUndo());
    CNA_STUDIO_EXPECT(fixture.context.getHistory().undo());
    CNA_STUDIO_EXPECT_EQ(componentCount(), before);
}

// ------------------------------------------------------------------------------------------------
// The asset inspector (STUDIO-07045)
//
// One of the five Inspector sections `STUDIO-07041` found with no native answer, and therefore one
// of the five things that stop Dear ImGui being deleted. The prototype shows a selected asset's
// identity and its importer's settings; the native Details panel showed the scene settings, because
// it could not see that an asset had been selected at all.
// ------------------------------------------------------------------------------------------------

namespace
{
    /** @brief A context with one texture asset whose importer declares settings. */
    struct AssetFixture
    {
        StudioContext context;
        Uuid textureId;

        AssetFixture()
        {
            AssetRecord record;
            record.id = Uuid::generate();
            record.sourcePath = "Assets/Textures/hero.png";
            record.type = AssetType::Texture2D;
            record.importerId = AssetDatabase::defaultImporterFor(record.type);
            textureId = record.id;
            (void)context.getAssets().add(std::move(record));
        }

        /** @brief Runs one full frame of the asset inspector over @p area. */
        StudioDetailsResult draw(StudioShell& shell, UiInputState input)
        {
            StudioDetailsResult last;
            (void)shell.setPanelContent("details", [&](StudioFrame& frame, const UiRect& area) {
                const StudioDetailsResult pass = studioDetailsPanel(frame, area, context);
                if (frame.isDrawPass()) { last = pass; }
            });
            shell.renderFrame(input);
            return last;
        }
    };
}

CNA_STUDIO_TEST(SelectingAnAssetShowsItRatherThanTheSceneSettings)
{
    // The defect this task closes, stated directly. The native Content Browser wrote the selected
    // asset into a member of StudioShellPanels while StudioContext had a selectedAsset_ of its own
    // that only the prototype ever wrote -- so the two native panels had different ideas of what
    // was selected, and the Details panel's was always "nothing".
    AssetFixture fixture;
    StudioShell shell{StudioTheme::dark()};
    shell.resetLayout();
    CNA_STUDIO_EXPECT(shell.activatePanel("details"));

    // Nothing selected and no project: the panel says so and draws no rows at all.
    const StudioDetailsResult idle = fixture.draw(shell, at(-1.0f, -1.0f));
    CNA_STUDIO_EXPECT_EQ(idle.rowsDrawn, std::size_t{0});

    fixture.context.selectAsset(fixture.textureId);
    const StudioDetailsResult asset = fixture.draw(shell, at(-1.0f, -1.0f));

    // Identity, a gap, the importer's heading, and one row per declared setting. An asset
    // inspector that resolved to the project message would draw none of them.
    CNA_STUDIO_EXPECT(asset.rowsDrawn > 4);
    CNA_STUDIO_EXPECT_EQ(asset.componentCount, std::size_t{0});
}

CNA_STUDIO_TEST(AnAssetAndAnEntityAreNeverBothSelected)
{
    // The inspector shows one thing at a time, and which one is decided by what was clicked last.
    // Two independent selections would leave the user unable to tell which the panel is about --
    // and the answer would change as they clicked around without either selection being cleared.
    AssetFixture fixture;
    const Uuid entity = fixture.context.getScene().addEntity(
        StudioEntity{Uuid::generate(), "Player"});

    fixture.context.select(entity);
    CNA_STUDIO_EXPECT(!fixture.context.getSelection().empty());

    fixture.context.selectAsset(fixture.textureId);
    CNA_STUDIO_EXPECT(fixture.context.getSelection().empty());
    CNA_STUDIO_EXPECT(fixture.context.getSelectedAsset() == fixture.textureId);
}

CNA_STUDIO_TEST(AnAssetDeletedWhileSelectedSaysSoRatherThanFallingBack)
{
    // Falling through to the scene settings would look exactly like the click never registered,
    // which is the failure mode that costs somebody ten minutes of clicking the same row.
    AssetFixture fixture;
    StudioShell shell{StudioTheme::dark()};
    shell.resetLayout();
    CNA_STUDIO_EXPECT(shell.activatePanel("details"));

    fixture.context.selectAsset(fixture.textureId);
    const StudioDetailsResult shown = fixture.draw(shell, at(-1.0f, -1.0f));
    CNA_STUDIO_EXPECT(shown.rowsDrawn > 0);

    CNA_STUDIO_EXPECT(fixture.context.getAssets().removeRecord(fixture.textureId));
    const StudioDetailsResult gone = fixture.draw(shell, at(-1.0f, -1.0f));

    // No rows: the message is not a row, and the panel is still about the asset rather than about
    // the scene. The selection is deliberately left alone -- clearing it here would make the
    // message flash for one frame and then vanish into the scene settings.
    CNA_STUDIO_EXPECT_EQ(gone.rowsDrawn, std::size_t{0});
    CNA_STUDIO_EXPECT(fixture.context.getSelectedAsset() == fixture.textureId);
}

CNA_STUDIO_TEST(AnImporterSettingIsEditedThroughTheHistoryLikeEveryOtherProperty)
{
    // An importer setting is persisted to a `.cnaasset` sidecar, which makes it the one edit in
    // Studio that could plausibly have been written straight to disk. It goes through the command
    // history like every other edit, so Ctrl+Z reaches it.
    AssetFixture fixture;

    const AssetRecord* before = fixture.context.getAssets().find(fixture.textureId);
    CNA_STUDIO_EXPECT(before != nullptr);

    auto command = std::make_unique<SetImporterSettingCommand>(
        fixture.context.getAssets(), fixture.textureId, "generateMipmaps", PropertyValue{true});
    CNA_STUDIO_EXPECT(command->isValid());
    fixture.context.execute(std::move(command));

    const AssetRecord* after = fixture.context.getAssets().find(fixture.textureId);
    CNA_STUDIO_EXPECT(after != nullptr);
    CNA_STUDIO_EXPECT(!after->importerSettings["generateMipmaps"].isNull());

    CNA_STUDIO_EXPECT(fixture.context.getHistory().canUndo());
    fixture.context.getHistory().undo();
    CNA_STUDIO_EXPECT(fixture.context.getAssets().find(fixture.textureId)
                          ->importerSettings["generateMipmaps"].isNull());
}

CNA_STUDIO_TEST(TheAssetInspectorsHeadingIsActuallyVisibleAndNotPaintedOver)
{
    // STUDIO-35063's lesson, applied to a new panel rather than rediscovered on it. Anything
    // described before the surface it sits on is drawn before that surface and painted over by it.
    // It happened twice in one session -- the disclosure triangle, then the outliner's visibility
    // toggle -- and both worked perfectly while being invisible, which is worse than missing
    // because nothing reports it.
    //
    // Asserted by *rasterising*, not by reading the draw order. An ordering heuristic over emitted
    // quads is a test of the heuristic: the first version of this case classified quads by texture
    // coordinate and alpha, passed, and went on passing when the defect was deliberately put back.
    // Rasterising asks the only question that matters -- are those pixels there -- and cannot be
    // satisfied by geometry that is submitted and then covered.
    AssetFixture fixture;
    StudioShell shell{StudioTheme::dark()};
    shell.resetLayout();
    CNA_STUDIO_EXPECT(shell.activatePanel("details"));
    fixture.context.selectAsset(fixture.textureId);

    UiRect panel;
    (void)shell.setPanelContent("details", [&](StudioFrame& frame, const UiRect& area) {
        (void)studioDetailsPanel(frame, area, fixture.context);
        if (frame.isDrawPass()) { panel = area; }
    });

    // Two frames and a texture table kept across them: the atlas is requested on the frame it is
    // rasterised and never again, so a table built from the last frame alone would have no font and
    // every glyph would draw as a solid rectangle -- which would pass this test for the wrong
    // reason.
    UiTextureTable textures;
    shell.renderFrame(at(-1.0f, -1.0f));
    textures.apply(shell.drawData());
    shell.renderFrame(at(-1.0f, -1.0f));

    CNA_STUDIO_EXPECT(!panel.isEmpty());

    const ImageBuffer image = rasterizeUiDrawData(
        shell.drawData(), shell.theme().color(StudioColorRole::AppBackground), textures);
    CNA_STUDIO_EXPECT(!image.isEmpty());
    if (image.isEmpty()) { return; }

    // The band the identity rows occupy: the top of the panel's content, two rows deep. The file
    // name, the Path label and the Path value are all in here.
    const int left = std::max(0, static_cast<int>(panel.x));
    const int right = std::min(image.width, static_cast<int>(panel.right()));
    const int top = std::max(0, static_cast<int>(panel.y));
    const int bottom = std::min(image.height, top + 64);
    CNA_STUDIO_EXPECT(right > left && bottom > top);

    // Distinct colours in the band. A band holding text has many -- glyphs are antialiased, so a
    // single letter contributes a dozen. A band that is nothing but the panel's own fill has one,
    // which is exactly what "described, then painted over" looks like.
    std::set<std::uint32_t> colours;
    for (int y = top; y < bottom; ++y)
    {
        for (int x = left; x < right; ++x)
        {
            const std::size_t at = (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width)
                                    + static_cast<std::size_t>(x)) * 4u;
            colours.insert(static_cast<std::uint32_t>(image.pixels[at]) << 16
                           | static_cast<std::uint32_t>(image.pixels[at + 1]) << 8
                           | static_cast<std::uint32_t>(image.pixels[at + 2]));
        }
    }

    if (colours.size() < 8)
    {
        CnaStudioTest::reportFailure(__FILE__, __LINE__,
            "the asset inspector's identity rows rasterise to " + std::to_string(colours.size())
            + " distinct colours, which is a flat fill rather than text. The rows are being drawn "
              "and then painted over by the surface they sit on -- describe what has to win the "
              "click before the surface, and draw it after (plan.md STUDIO-35063).");
    }
    CNA_STUDIO_EXPECT(colours.size() >= 8);
}

CNA_STUDIO_TEST(DraggingANumericFieldScrubsTheValueWithoutTypingIntoIt)
{
    // `STUDIO-07055`. The prototype's vector fields scrub; the native field committed on Enter and
    // nothing else, so setting a position meant selecting the text and typing four characters --
    // for a value a user usually wants to *feel* their way to rather than know in advance.
    Fixture fixture;
    Harness harness{fixture.context};

    const StudioVector3 before = fixture.position();
    CNA_STUDIO_EXPECT_EQ(before.y, 2.0f);

    // The same sweep the typing case uses, for the same reason: found by walking the rows rather
    // than by computing a pixel, so a metric change does not turn this into a test that drags
    // empty space and passes.
    bool scrubbed = false;
    for (float y = harness.bounds.top() + 20.0f;
         y < harness.bounds.top() + 200.0f && !scrubbed; y += 6.0f)
    {
        const float columnLeft = harness.bounds.left() + harness.bounds.width * 0.40f;
        const float x = columnLeft + (harness.bounds.right() - columnLeft) * 0.5f;

        harness.drag(x, y, 40.0f);
        scrubbed = fixture.position().y != before.y;
    }

    CNA_STUDIO_EXPECT(scrubbed);

    // Rightwards, so upwards. The exact number depends on which row the sweep landed on and on the
    // step that row's kind uses; what this asserts is the direction and that the other two axes
    // were not touched -- the classic property-grid defect, asked of a drag rather than of typing.
    CNA_STUDIO_EXPECT(fixture.position().y > before.y);
    CNA_STUDIO_EXPECT_EQ(fixture.position().x, before.x);
    CNA_STUDIO_EXPECT_EQ(fixture.position().z, before.z);

    // And it went through the history like every other edit, so it can be taken back.
    CNA_STUDIO_EXPECT(fixture.context.getHistory().canUndo());
    CNA_STUDIO_EXPECT(fixture.context.getHistory().undo());
    CNA_STUDIO_EXPECT_EQ(fixture.position().y, before.y);
}

// ------------------------------------------------------------------------------------------------
// Lists and structures (STUDIO-07054)
//
// `studioPropertyEditor` draws *a control in a rect*, which is the right shape for every scalar
// kind and the wrong shape for these two: a list of four frames needs four rows, and a rect cannot
// grow. What that cost was concrete -- a sprite animation's frame list and a model renderer's
// per-part material overrides could not be edited in the native shell at all.
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(AddingToAListAppendsAnElementOfTheKindItAlreadyHolds)
{
    // A copy of the last element rather than a default-constructed value. A list holds one kind,
    // and an element that arrived as `monostate` would be a row with no editor in a list of rows
    // that have one -- which reads as the list having been broken by pressing Add.
    PropertyValue::ListValue list;
    list.items.push_back(PropertyValue{7});
    list.items.push_back(PropertyValue{9});

    CNA_STUDIO_EXPECT(studioApplyListEdit(list, StudioListEdit::Add, 0, list.items.back()));
    CNA_STUDIO_EXPECT_EQ(list.items.size(), std::size_t{3});
    CNA_STUDIO_EXPECT(list.items.back().getType() == PropertyType::Integer);
    CNA_STUDIO_EXPECT_EQ(list.items.back().get<std::int64_t>(), std::int64_t{9});
}

CNA_STUDIO_TEST(RemovingTakesTheNamedElementAndNotTheLastOne)
{
    // The off-by-one that is invisible in a screenshot: removing the *wrong* element still leaves a
    // list one shorter, and on a list of identical values it cannot be told from the right one.
    PropertyValue::ListValue list;
    for (std::int64_t i = 0; i < 4; ++i) { list.items.push_back(PropertyValue{i}); }

    CNA_STUDIO_EXPECT(studioApplyListEdit(list, StudioListEdit::Remove, 1, PropertyValue{}));
    CNA_STUDIO_EXPECT_EQ(list.items.size(), std::size_t{3});
    CNA_STUDIO_EXPECT_EQ(list.items[0].get<std::int64_t>(), std::int64_t{0});
    CNA_STUDIO_EXPECT_EQ(list.items[1].get<std::int64_t>(), std::int64_t{2});
    CNA_STUDIO_EXPECT_EQ(list.items[2].get<std::int64_t>(), std::int64_t{3});

    // Past the end, and on an empty list: refused rather than clamped. The caller pushes an undo
    // entry on true, and an entry that changes nothing is one a user presses Ctrl+Z on and watches
    // do nothing.
    CNA_STUDIO_EXPECT(!studioApplyListEdit(list, StudioListEdit::Remove, 3, PropertyValue{}));
    PropertyValue::ListValue empty;
    CNA_STUDIO_EXPECT(!studioApplyListEdit(empty, StudioListEdit::Remove, 0, PropertyValue{}));
}

CNA_STUDIO_TEST(MovingStopsAtEitherEndRatherThanWrapping)
{
    // Wrapping is never what somebody pressing Up at the top meant, and it is silent when it
    // happens -- the element they were looking at is suddenly at the other end of the list.
    PropertyValue::ListValue list;
    for (std::int64_t i = 0; i < 3; ++i) { list.items.push_back(PropertyValue{i}); }

    CNA_STUDIO_EXPECT(!studioApplyListEdit(list, StudioListEdit::MoveUp, 0, PropertyValue{}));
    CNA_STUDIO_EXPECT_EQ(list.items[0].get<std::int64_t>(), std::int64_t{0});

    CNA_STUDIO_EXPECT(!studioApplyListEdit(list, StudioListEdit::MoveDown, 2, PropertyValue{}));
    CNA_STUDIO_EXPECT_EQ(list.items[2].get<std::int64_t>(), std::int64_t{2});

    // And in the middle it swaps with its neighbour, both ways, returning to where it started.
    CNA_STUDIO_EXPECT(studioApplyListEdit(list, StudioListEdit::MoveUp, 1, PropertyValue{}));
    CNA_STUDIO_EXPECT_EQ(list.items[0].get<std::int64_t>(), std::int64_t{1});
    CNA_STUDIO_EXPECT_EQ(list.items[1].get<std::int64_t>(), std::int64_t{0});

    CNA_STUDIO_EXPECT(studioApplyListEdit(list, StudioListEdit::MoveDown, 0, PropertyValue{}));
    CNA_STUDIO_EXPECT_EQ(list.items[0].get<std::int64_t>(), std::int64_t{0});
    CNA_STUDIO_EXPECT_EQ(list.items[1].get<std::int64_t>(), std::int64_t{1});
}

CNA_STUDIO_TEST(AListPropertyGetsARowThatExpandsIntoItsElements)
{
    // The panel-level half. A list used to be a summary -- "4 items" -- and counting it as a
    // read-only kind was the honest way to say so. It now claims a row per element, which is the
    // only shape that can hold an editor for each of them.
    Fixture fixture;
    StudioComponent nested{"Test.Nested"};
    PropertyValue::ListValue list;
    list.items.push_back(PropertyValue{1});
    list.items.push_back(PropertyValue{2});
    nested.setProperty("items", PropertyValue{std::move(list)});
    fixture.context.getScene().findEntity(fixture.entity)->addComponent(std::move(nested));

    Harness harness{fixture.context};

    // Not counted as a kind without an editor any more, which is the assertion that would notice
    // the editor being lost again.
    CNA_STUDIO_EXPECT_EQ(harness.last.readOnlyProperties, std::size_t{0});

    const std::size_t collapsed = harness.last.rowsDrawn;

    // The disclosure is the first control in the row, at the left of the control column.
    bool expandedIt = false;
    for (float y = harness.bounds.top() + 20.0f;
         y < harness.bounds.top() + 260.0f && !expandedIt; y += 6.0f)
    {
        const float columnLeft = harness.bounds.left() + harness.bounds.width * 0.38f;
        harness.click(columnLeft + 16.0f, y);
        expandedIt = harness.last.rowsDrawn > collapsed;
    }

    CNA_STUDIO_EXPECT(expandedIt);
    // Two elements, so at least two rows more than the collapsed shape.
    CNA_STUDIO_EXPECT(harness.last.rowsDrawn >= collapsed + 2);
    CNA_STUDIO_EXPECT_EQ(harness.shell->frame().phaseViolations(), std::size_t{0});
}

CNA_STUDIO_TEST(AStructurePropertyGetsARowPerField)
{
    // A structure's fields are name/value pairs, each of which is an ordinary property -- so each
    // gets the editor its own kind already has, rather than a second set written for structures.
    Fixture fixture;
    StudioComponent nested{"Test.Struct"};
    PropertyValue::StructureValue structure;
    structure.set("width", PropertyValue{4});
    structure.set("label", PropertyValue{std::string{"left"}});
    nested.setProperty("layout", PropertyValue{std::move(structure)});
    fixture.context.getScene().findEntity(fixture.entity)->addComponent(std::move(nested));

    Harness harness{fixture.context};
    CNA_STUDIO_EXPECT_EQ(harness.last.readOnlyProperties, std::size_t{0});

    const std::size_t collapsed = harness.last.rowsDrawn;

    bool expandedIt = false;
    for (float y = harness.bounds.top() + 20.0f;
         y < harness.bounds.top() + 260.0f && !expandedIt; y += 6.0f)
    {
        const float columnLeft = harness.bounds.left() + harness.bounds.width * 0.38f;
        harness.click(columnLeft + 16.0f, y);
        expandedIt = harness.last.rowsDrawn > collapsed;
    }

    CNA_STUDIO_EXPECT(expandedIt);
    CNA_STUDIO_EXPECT(harness.last.rowsDrawn >= collapsed + 2);
    CNA_STUDIO_EXPECT_EQ(harness.shell->frame().phaseViolations(), std::size_t{0});
}

CNA_STUDIO_TEST(RemovingAListElementIsOneUndoEntryThatPutsItBack)
{
    // Each change is one undo entry, which is the last clause of the acceptance and the one a
    // user meets. A list edit rewrites the *whole* property -- the list is the value -- so the
    // entry has to carry the list as it was rather than the element that went.
    //
    // A dedicated entity with nothing but the list on it, rather than the shared `Fixture`: the
    // shared one also carries a Transform, and Transform's own three rows plus Name and Enabled
    // put enough buttons above this property that a pixel search for one of *this* row's three
    // (up, down, remove) risks landing on one of theirs on the way down. Fewer rows above means
    // fewer wrong buttons to walk past.
    StudioContext context;
    StudioEntity subject{Uuid::generate(), "Widget"};
    PropertyValue::ListValue list;
    list.items.push_back(PropertyValue{1});
    list.items.push_back(PropertyValue{2});
    StudioComponent nested{"Test.Nested"};
    nested.setProperty("items", PropertyValue{std::move(list)});
    subject.getComponents().push_back(std::move(nested));
    const Uuid entity = subject.getId();
    context.getScene().addEntity(std::move(subject));
    context.select(entity);

    const auto component = [&]() -> const StudioComponent* {
        const StudioEntity* found = context.getScene().findEntity(entity);
        return found == nullptr ? nullptr : found->findComponent("Test.Nested");
    };
    const auto itemCount = [&]() -> std::size_t {
        const StudioComponent* found = component();
        return found == nullptr
            ? std::size_t{0}
            : found->getProperty("items").get<PropertyValue::ListValue>().items.size();
    };
    CNA_STUDIO_EXPECT_EQ(itemCount(), std::size_t{2});

    Harness harness{context};
    const std::size_t collapsed = harness.last.rowsDrawn;

    // Expand, found the same way `AListPropertyGetsARowThatExpandsIntoItsElements` finds it.
    for (float y = harness.bounds.top() + 8.0f;
         y < harness.bounds.top() + 200.0f && harness.last.rowsDrawn == collapsed; y += 4.0f)
    {
        harness.click(harness.bounds.left() + harness.bounds.width * 0.38f + 16.0f, y);
    }
    CNA_STUDIO_EXPECT_EQ(harness.last.rowsDrawn, collapsed + 2);

    // Captured only now: the sweep above may have clicked other rows on its way to the
    // disclosure toggle -- an "Enabled" checkbox above it, say -- and each of those is a real
    // push this test has no interest in. The one edit under test is the one after expansion.
    const std::size_t before = context.getHistory().getCount();


    // The two new rows are the last two the panel drew: with nothing else on this entity, the
    // list's elements are the only rows an expand can add, and they are contiguous with the header
    // and summary above them. `RowHeight` plus the spacing `nextRow` adds after each row is what
    // one step down the panel costs.
    const float rowStep =
        metricOf(harness.shell->theme(), StudioMetric::RowHeight)
        + metricOf(harness.shell->theme(), StudioMetric::SpacingXSmall);
    const float removeX = harness.bounds.right()
        - metricOf(harness.shell->theme(), StudioMetric::ControlHeight) * 0.5f;
    const float element0Y =
        harness.bounds.top() + (static_cast<float>(collapsed) + 0.5f) * rowStep;

    harness.click(removeX, element0Y);

    CNA_STUDIO_EXPECT_EQ(itemCount(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(context.getHistory().getCount(), before + 1);

    CNA_STUDIO_EXPECT(context.getHistory().undo());
    CNA_STUDIO_EXPECT_EQ(itemCount(), std::size_t{2});
}

// ------------------------------------------------------------------------------------------------
// Angles surviving gimbal lock (STUDIO-07057)
//
// A rotation is stored as a quaternion and edited as Euler angles, and the conversion is not
// injective: at gimbal lock, several triples of angles produce the same rotation, so recomputing
// fresh from the quaternion every frame can show a *different* triple from the one just typed --
// the field a user is looking at changes under them while they are still looking at it. The
// prototype keeps what was typed for as long as the stored value is still exactly the one it
// produced; the native editor converted afresh every frame.
// ------------------------------------------------------------------------------------------------

namespace
{
    /** @brief Drives `studioPropertyEditor` directly, over a value held outside any document. */
    struct AngleFixture
    {
        StudioFrame frame;
        UiRect bounds{100.0f, 100.0f, 240.0f, 24.0f};
        PropertyValue value{StudioQuaternion{}};

        AngleFixture() { frame.setTheme(StudioTheme::dark()); }

        /** @brief One full frame with no input, so retained state settles between gestures. */
        void settle()
        {
            frame.beginFrame(at(-1.0f, -1.0f));
            frame.beginInput();
            (void)studioPropertyEditor(frame, bounds, value, {}, {});
            frame.beginDraw();
            (void)studioPropertyEditor(frame, bounds, value, {}, {});
            frame.endFrame();
        }

        /**
         * @brief Types @p degrees into the pitch, yaw and roll fields in turn and commits each.
         *
         * The three fields divide `bounds` into equal thirds with `SpacingSmall` between them,
         * matching `numericComponents`' own layout exactly -- computed rather than guessed, so a
         * metric change moves this test's clicks along with the fields it is clicking.
         */
        void typeDegrees(const StudioVector3& degrees)
        {
            const float spacing = static_cast<float>(frame.theme().metric(StudioMetric::SpacingSmall));
            const float fieldWidth = (bounds.width - spacing * 2.0f) / 3.0f;
            const float values[3] = {degrees.x, degrees.y, degrees.z};

            for (int i = 0; i < 3; ++i)
            {
                const float x = bounds.left() + (static_cast<float>(i) + 0.5f) * fieldWidth
                              + static_cast<float>(i) * spacing;
                const float y = bounds.centerY();

                click(x, y);
                selectAll(x, y);
                const std::string text = studioFormatFloat(values[i]);
                std::vector<char16_t> characters(text.begin(), text.end());
                typeText(characters, x, y);
                pressEnter(x, y);
            }
        }

        /**
         * @brief The text actually displayed in the field named @p axis, after a `settle()`.
         *
         * Peeks the field's own retained buffer rather than recomputing anything, because
         * recomputing is exactly the defect under test -- a helper that re-derived the angle from
         * the quaternion would report the honest reading every time, whether or not the cache was
         * consulted, and the two are indistinguishable except at gimbal lock.
         *
         * @param axis One of "pitch", "yaw", "roll", matching `numericComponents`' own names.
         */
        [[nodiscard]] std::string shownText(const char* axis)
        {
            settle();
            return frame.state().get(frame.ids().make(axis)).text;
        }

    private:
        void oneFrame(const UiInputState& input)
        {
            frame.beginFrame(input);
            frame.beginInput();
            const StudioPropertyEditResult result = studioPropertyEditor(frame, bounds, value, {}, {});
            if (result.edited.has_value()) { value = *result.edited; }
            frame.beginDraw();
            (void)studioPropertyEditor(frame, bounds, value, {}, {});
            frame.endFrame();
        }

        void click(float x, float y)
        {
            oneFrame(at(x, y, false));
            oneFrame(at(x, y, true));
            oneFrame(at(x, y, false));
        }

        void selectAll(float x, float y)
        {
            UiInputState input = at(x, y);
            input.modifiers.control = true;
            input.setKeyDown(UiKey::A, true);
            oneFrame(input);
            oneFrame(at(x, y));
        }

        void typeText(const std::vector<char16_t>& characters, float x, float y)
        {
            UiInputState input = at(x, y);
            input.characters = characters;
            oneFrame(input);
        }

        void pressEnter(float x, float y)
        {
            UiInputState input = at(x, y);
            input.setKeyDown(UiKey::Enter, true);
            oneFrame(input);
            oneFrame(at(x, y));
        }
    };
}

CNA_STUDIO_TEST(TheAnglesTheUserTypedSurviveGimbalLock)
{
    // A pitch of 90 degrees is a pole: yaw and roll are no longer separable, and reading the
    // quaternion back reports the same rotation as a *different* (yaw, roll) pair.
    AngleFixture fixture;
    const StudioVector3 typed{90.0f, 40.0f, 25.0f};

    fixture.typeDegrees(typed);

    // Recomputing from the quaternion would show a folded yaw and roll 0, so the two fields
    // beside the one being edited would jump the instant pitch reached 90. Read from each
    // field's own displayed text, not re-derived from the quaternion: a helper that recomputed
    // would report the honest reading either way, and the two are indistinguishable except at
    // gimbal lock.
    CNA_STUDIO_EXPECT_EQ(fixture.shownText("yaw"), studioFormatFloat(40.0f));
    CNA_STUDIO_EXPECT_EQ(fixture.shownText("roll"), studioFormatFloat(25.0f));

    // And the honest reading really does differ, which is what makes the cache worth having --
    // a test that never entered gimbal lock would pass whether the cache existed or not.
    CNA_STUDIO_EXPECT(eulerDegreesOf(quaternionFromEulerDegrees(typed)).z != 25.0f);
}

CNA_STUDIO_TEST(EachTypedAngleFieldCommitsThroughTheHistoryAsItsOwnEntry)
{
    // The panel-level half: through a real entity, a real `SetPropertyCommand` and a real
    // undo stack, rather than `studioPropertyEditor` called directly the way the gimbal-lock
    // case above is. What the cache itself does when something else changes the rotation is
    // covered more precisely by `TheAngleCacheIsAbandonedTheInstantSomethingElseProducesA-
    // DifferentQuaternion`, which drives that case without a row to find.
    // Through the full panel and a real entity, because the acceptance is about the *document*
    // changing the rotation out from under a cached edit -- undo, specifically -- and that is not
    // something a bare `studioPropertyEditor` call can exercise.
    StudioContext context;
    StudioEntity subject{Uuid::generate(), "Widget"};
    StudioComponent transform{"CNA.Transform"};
    transform.setProperty("rotation", PropertyValue{StudioQuaternion{}});
    subject.getComponents().push_back(std::move(transform));
    const Uuid entity = subject.getId();
    context.getScene().addEntity(std::move(subject));
    context.select(entity);

    const auto rotation = [&]() -> StudioQuaternion {
        const StudioEntity* found = context.getScene().findEntity(entity);
        const StudioComponent* component = found->findComponent("CNA.Transform");
        return component->getProperty("rotation").get<StudioQuaternion>();
    };

    Harness harness{context};
    const float spacing = metricOf(harness.shell->theme(), StudioMetric::SpacingSmall);
    const float columnLeft = harness.bounds.left() + harness.bounds.width * 0.40f;
    const float fieldWidth = (harness.bounds.right() - columnLeft - spacing * 2.0f) / 3.0f;

    const auto typeInto = [&](float y, int index, float degrees) {
        const float x = columnLeft + (static_cast<float>(index) + 0.5f) * fieldWidth
                      + static_cast<float>(index) * spacing;
        harness.click(x, y);
        UiInputState select = at(x, y);
        select.modifiers.control = true;
        select.setKeyDown(UiKey::A, true);
        harness.shell->renderFrame(select);
        harness.shell->renderFrame(at(x, y));
        const std::string text = studioFormatFloat(degrees);
        harness.type(std::vector<char16_t>(text.begin(), text.end()), x, y);
        harness.press(UiKey::Enter, x, y);
    };

    // The rotation row's y is found rather than computed: a row count says how many rows there
    // are, not which one this is, and a fixed offset guessed from that count is exactly what broke
    // this test the first two times it was written. Swept the full height of the panel, pressing
    // 90 into the first field at each candidate and keeping the one that actually turned the
    // rotation -- any candidate that pressed something else is undone before the next is tried,
    // the same discipline `RemovingAListElementIsOneUndoEntryThatPutsItBack` uses for the same
    // reason.
    float rotationRowY = -1.0f;
    for (float y = harness.bounds.top() + 8.0f; y < harness.bounds.bottom(); y += 4.0f)
    {
        const std::size_t countBefore = context.getHistory().getCount();
        typeInto(y, 0, 90.0f);
        if (rotation() != StudioQuaternion{})
        {
            rotationRowY = y;
            break;
        }
        if (context.getHistory().getCount() != countBefore)
        {
            CNA_STUDIO_EXPECT(context.getHistory().undo());
        }
    }
    CNA_STUDIO_EXPECT(rotationRowY > 0.0f);

    typeInto(rotationRowY, 1, 40.0f);
    typeInto(rotationRowY, 2, 25.0f);

    CNA_STUDIO_EXPECT(rotation() == quaternionFromEulerDegrees(StudioVector3{90.0f, 40.0f, 25.0f}));
    CNA_STUDIO_EXPECT(context.getHistory().canUndo());

    // One entry per field committed -- pitch, then yaw, then roll each replace the whole
    // quaternion in turn, and none of the three is continuous input, so nothing here merges.
    // Three undoes is the round trip back to where the entity started.
    CNA_STUDIO_EXPECT(context.getHistory().undo());
    CNA_STUDIO_EXPECT(context.getHistory().undo());
    CNA_STUDIO_EXPECT(context.getHistory().undo());
    CNA_STUDIO_EXPECT(rotation() == StudioQuaternion{});
}

CNA_STUDIO_TEST(TheAngleCacheIsAbandonedTheInstantSomethingElseProducesADifferentQuaternion)
{
    // The other half of the acceptance, and the one the panel-level test above cannot isolate
    // cleanly: an undo through the real command history reverts one *field's* edit, which is a
    // different rotation from the one three individually-typed fields produce, but it is not the
    // specific "something entirely unrelated changed it" case the cache exists to be honest
    // about. This drives that case directly, the way `TheAnglesTheUserTypedSurviveGimbalLock`
    // drives entering it: through `studioPropertyEditor` itself, with no row to find.
    AngleFixture fixture;
    const StudioVector3 typed{90.0f, 40.0f, 25.0f};
    fixture.typeDegrees(typed);

    CNA_STUDIO_EXPECT_EQ(fixture.shownText("yaw"), studioFormatFloat(40.0f));
    CNA_STUDIO_EXPECT_EQ(fixture.shownText("roll"), studioFormatFloat(25.0f));

    // Something else -- a gizmo drag, an undo, a reload -- replaces the value directly, the way
    // every one of those actually reaches this editor: as a new `PropertyValue` from outside,
    // never as a call into this cache. Chosen so that it is not the rotation the cache holds.
    const StudioQuaternion external =
        quaternionFromEulerDegrees(StudioVector3{10.0f, 0.0f, 0.0f});
    CNA_STUDIO_EXPECT(external != quaternionFromEulerDegrees(typed));
    fixture.value = PropertyValue{external};

    // The cache must stop applying at once: `quaternionFromEulerDegrees(cachedDegrees)` no
    // longer equals the stored value, so the comparison that gates the cache fails on the very
    // next frame, with nothing else having to notice or say so.
    const StudioVector3 honest = eulerDegreesOf(external);
    CNA_STUDIO_EXPECT_EQ(fixture.shownText("pitch"), studioFormatFloat(honest.x));
    CNA_STUDIO_EXPECT_EQ(fixture.shownText("yaw"), studioFormatFloat(honest.y));
    CNA_STUDIO_EXPECT_EQ(fixture.shownText("roll"), studioFormatFloat(honest.z));
}

// ------------------------------------------------------------------------------------------------
// The dependency section (STUDIO-09012)
// ------------------------------------------------------------------------------------------------

/**
 * @brief The asset inspector shows both directions, and a row is a way through the graph.
 *
 * The question a user opens an asset to answer before deleting it — and the one nothing on disk
 * records, because a scene holds a Uuid rather than a path. A section that drew its headings and
 * found no references would look exactly like an asset nothing uses, which is the opposite answer.
 */
CNA_STUDIO_TEST(TheAssetInspectorShowsWhatUsesAnAssetAndWhatItUses)
{
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "cna-studio-depsection";
    std::error_code code;
    std::filesystem::remove_all(directory, code);
    std::filesystem::create_directories(directory / "Assets", code);

    StudioContext context;
    registerBuiltinComponents(context.getComponentRegistry());
    context.getAssets().setProjectRoot(directory.generic_string());

    const auto track = [&context](const std::string& path, AssetType type) {
        AssetRecord record;
        record.id = Uuid::generate();
        record.sourcePath = path;
        record.type = type;
        const Uuid id = record.id;
        CNA_STUDIO_EXPECT(context.getAssets().add(std::move(record)));
        return id;
    };

    const Uuid textureId = track("Assets/player.png", AssetType::Texture2D);
    const Uuid sceneId = track("Assets/Level.cnascene", AssetType::Scene);

    StudioEntity hero{Uuid::generate(), "Hero"};
    StudioComponent sprite{BuiltinComponentIds::kSpriteRenderer};
    sprite.applyDefaults(*context.getComponentRegistry().find(BuiltinComponentIds::kSpriteRenderer));
    sprite.setProperty("texture", PropertyValue{PropertyValue::AssetReference{textureId}});
    hero.addComponent(std::move(sprite));

    SceneDocument scene;
    scene.addEntity(std::move(hero));

    AssetDependencyIndex index;
    (void)index.build(context.getAssets(), context.getComponentRegistry());
    index.observeScene(scene, sceneId, "Assets/Level.cnascene");

    StudioDetailsServices services;
    services.dependencies = &index;

    CNA_STUDIO_EXPECT_EQ(index.referencedBy(textureId).size(), std::size_t{1});

    context.selectAsset(textureId);

    auto shell = std::make_unique<StudioShell>(StudioTheme::dark());
    shell->resetLayout();
    shell->renderFrame(at(-1.0f, -1.0f));
    CNA_STUDIO_EXPECT(shell->activatePanel("details"));

    StudioDetailsResult last;
    UiRect bounds;
    CNA_STUDIO_EXPECT(shell->setPanelContent("details",
        [&](StudioFrame& frame, const UiRect& area) {
            const StudioDetailsResult pass = studioDetailsPanel(frame, area, context, services);
            if (frame.isInputPass()) { last = pass; }
            if (frame.isDrawPass()) { bounds = area; }
        }));
    shell->renderFrame(at(-1.0f, -1.0f));

    // The texture is used once and uses nothing.
    CNA_STUDIO_EXPECT_EQ(last.dependencyRows, std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(shell->frame().phaseViolations(), std::size_t{0});

    // A row is clickable, and clicking it goes to the file holding the reference. Swept rather
    // than assuming a row height, so a metric change cannot turn this into a test that clicks
    // empty space and passes for the wrong reason.
    bool navigated = false;
    for (float y = bounds.top() + 4.0f; y < bounds.bottom() - 4.0f && !navigated; y += 5.0f)
    {
        const float x = bounds.left() + 30.0f;
        shell->renderFrame(at(x, y, false));
        shell->renderFrame(at(x, y, true));
        shell->renderFrame(at(x, y, false));
        navigated = context.getSelectedAsset() == sceneId;
    }

    if (!navigated)
    {
        CnaStudioTest::reportFailure(__FILE__, __LINE__,
                                     "no dependency row led to the scene that references the asset.");
    }
    else
    {
        // And from the scene's own inspector the other direction is the one with something in it.
        shell->renderFrame(at(-1.0f, -1.0f));
        CNA_STUDIO_EXPECT_EQ(last.dependencyRows, std::size_t{1});
    }

    std::filesystem::remove_all(directory, code);
}

/** @brief With no index, the section says so rather than reading as "nothing references this". */
CNA_STUDIO_TEST(WithoutAnIndexTheDependencySectionSaysSoRatherThanLookingEmpty)
{
    StudioContext context;
    registerBuiltinComponents(context.getComponentRegistry());

    AssetRecord record;
    record.id = Uuid::generate();
    record.sourcePath = "Assets/player.png";
    record.type = AssetType::Texture2D;
    const Uuid id = record.id;
    CNA_STUDIO_EXPECT(context.getAssets().add(std::move(record)));
    context.selectAsset(id);

    auto shell = std::make_unique<StudioShell>(StudioTheme::dark());
    shell->resetLayout();
    shell->renderFrame(at(-1.0f, -1.0f));
    CNA_STUDIO_EXPECT(shell->activatePanel("details"));

    StudioDetailsResult last;
    CNA_STUDIO_EXPECT(shell->setPanelContent("details",
        [&](StudioFrame& frame, const UiRect& area) {
            const StudioDetailsResult pass = studioDetailsPanel(frame, area, context);
            if (frame.isInputPass()) { last = pass; }
        }));
    shell->renderFrame(at(-1.0f, -1.0f));

    // No rows, and the panel still drew: an inspector that threw away its other sections because
    // one seam was unset would be a build without a dependency index having no asset inspector.
    CNA_STUDIO_EXPECT_EQ(last.dependencyRows, std::size_t{0});
    CNA_STUDIO_EXPECT(last.rowsDrawn > 0);
    CNA_STUDIO_EXPECT_EQ(shell->frame().phaseViolations(), std::size_t{0});
}
