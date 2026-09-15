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

#include "CNA/Studio/ShellPanels/StudioDetailsPanel.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"

#include <algorithm>
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
