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
#include "CNA/Studio/Scene/SceneCommands.hpp"
#include "CNA/Studio/Scene/SceneTransform.hpp"
#include "CNA/Studio/ShellPanels/StudioDetailsPanel.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"
#include "CNA/Studio/UiCore/UiSoftwareRasterizer.hpp"

#include <algorithm>
#include <cstdint>
#include <set>
#include <cmath>
#include <memory>
#include <string>

using namespace CNA::Studio;

namespace
{
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

    // And the two kinds that genuinely have no editor yet still say what they hold, rather than
    // being left out of the panel entirely.
    StudioComponent nested{"Test.Nested"};
    PropertyValue::ListValue list;
    list.items.push_back(PropertyValue{1});
    nested.setProperty("items", PropertyValue{std::move(list)});
    fixture.context.getScene().findEntity(fixture.entity)->addComponent(std::move(nested));

    Harness second{fixture.context};
    CNA_STUDIO_EXPECT_EQ(second.last.readOnlyProperties, std::size_t{1});
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
