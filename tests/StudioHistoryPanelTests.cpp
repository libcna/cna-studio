// SPDX-License-Identifier: MS-PL
/**
 * @file StudioHistoryPanelTests.cpp
 * @brief The History panel: positions, not entries (plan.md STUDIO-07013).
 *
 * The property worth testing hardest is the one that is easy to get subtly wrong: a list of *n*
 * commands has *n + 1* positions, and the extra one — the document as opened — is the row a user
 * reaching for "put it back how it was" is actually aiming at. A panel that lists entries can take
 * them everywhere except there.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Scene/BuiltinComponents.hpp"
#include "CNA/Studio/Scene/SceneCommands.hpp"
#include "CNA/Studio/ShellPanels/StudioHistoryPanel.hpp"
#include "CNA/Studio/StudioContext.hpp"

#include <memory>
#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    constexpr float kWidth = 320.0f;
    constexpr float kHeight = 400.0f;

    UiInputState at(float x, float y, bool leftDown = false)
    {
        UiInputState input;
        input.displayWidth = kWidth;
        input.displayHeight = kHeight;
        input.mouseX = x;
        input.mouseY = y;
        input.mouseInWindow = true;
        input.deltaSeconds = 1.0f / 60.0f;
        input.setMouseDown(UiMouseButton::Left, leftDown);
        return input;
    }

    /** @brief A context with a few undoable renames on it. */
    struct Fixture
    {
        StudioContext context;
        StudioTreeState state;
        StudioFrame frame{StudioTheme::dark()};
        UiRect body{0.0f, 0.0f, kWidth, kHeight};
        StudioHistoryResult last;
        Uuid entity;

        Fixture()
        {
            StudioEntity subject{Uuid::generate(), "Player"};
            StudioComponent transform{BuiltinComponentIds::kTransform};
            subject.addComponent(std::move(transform));
            entity = subject.getId();
            context.getScene().addEntity(std::move(subject));
        }

        /** @brief Runs one undoable rename. */
        void rename(const std::string& to)
        {
            context.execute(std::make_unique<RenameEntityCommand>(context.getScene(), entity, to));
        }

        [[nodiscard]] std::string name() const
        {
            const StudioEntity* found = context.getScene().findEntity(entity);
            return found != nullptr ? found->getName() : std::string{};
        }

        void run(const UiInputState& input)
        {
            runStudioFrame(frame, input, [&](StudioFrame& f) {
                const StudioHistoryResult drawn = studioHistoryPanel(f, body, context, state);
                if (f.isInputPass()) { last = drawn; }
            });
        }

        void settle() { run(at(kWidth - 5.0f, kHeight - 5.0f)); }

        void click(float x, float y)
        {
            run(at(x, y));
            run(at(x, y, /*leftDown=*/true));
            run(at(x, y));
        }

        [[nodiscard]] float rowCentre(std::size_t position) const
        {
            const auto rowHeight = static_cast<float>(frame.theme().metric(StudioMetric::RowHeight));
            return (static_cast<float>(position) + 0.5f) * rowHeight;
        }
    };
}

CNA_STUDIO_TEST(AnEmptyHistoryStillHasThePositionItStartedFrom)
{
    Fixture fixture;
    fixture.settle();

    const std::vector<StudioTreeRow> rows = studioHistoryRows(fixture.context);
    CNA_STUDIO_EXPECT_EQ(rows.size(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(rows.front().label, std::string{"Opened"});
    CNA_STUDIO_EXPECT(rows.front().selected);
    CNA_STUDIO_EXPECT_EQ(fixture.last.positions, std::size_t{1});
}

CNA_STUDIO_TEST(ThereIsOneMoreRowThanThereAreCommands)
{
    Fixture fixture;
    fixture.rename("A");
    fixture.rename("B");
    fixture.rename("C");

    const std::vector<StudioTreeRow> rows = studioHistoryRows(fixture.context);
    CNA_STUDIO_EXPECT_EQ(rows.size(), std::size_t{4});
    CNA_STUDIO_EXPECT_EQ(fixture.context.getHistory().getCount(), std::size_t{3});
    CNA_STUDIO_EXPECT(rows.back().selected);
}

CNA_STUDIO_TEST(UndoneEntriesAreMarkedRatherThanHidden)
{
    // They are precisely what a user is trying to get back to; a list that hides them has no
    // forward direction at all.
    Fixture fixture;
    fixture.rename("A");
    fixture.rename("B");
    fixture.context.getHistory().undo();

    const std::vector<StudioTreeRow> rows = studioHistoryRows(fixture.context);
    CNA_STUDIO_EXPECT_EQ(rows.size(), std::size_t{3});
    CNA_STUDIO_EXPECT(rows[1].selected);
    CNA_STUDIO_EXPECT_EQ(rows[2].detail, std::string{"undone"});
    CNA_STUDIO_EXPECT(rows[2].muted);
}

CNA_STUDIO_TEST(TheSavedPositionIsMarkedSoItCanBeReturnedTo)
{
    // "Where was this when I last saved it" is the question behind most uses of an undo list.
    Fixture fixture;
    fixture.rename("A");
    fixture.context.getHistory().markSaved();
    fixture.rename("B");

    const std::vector<StudioTreeRow> rows = studioHistoryRows(fixture.context);
    CNA_STUDIO_EXPECT_EQ(rows[1].detail, std::string{"saved"});
    CNA_STUDIO_EXPECT(rows[1].detailRole == StudioColorRole::Success);
    CNA_STUDIO_EXPECT(rows[2].detail.empty());
}

CNA_STUDIO_TEST(NavigatingBackwardsUndoesOneCommandAtATime)
{
    // Through the same history Ctrl+Z uses, rather than by setting the cursor: a panel that moved
    // the cursor directly would leave the document and the history describing different things.
    Fixture fixture;
    fixture.rename("A");
    fixture.rename("B");
    fixture.rename("C");
    CNA_STUDIO_EXPECT_EQ(fixture.name(), std::string{"C"});

    const std::size_t ran = studioNavigateHistory(fixture.context, 1);

    CNA_STUDIO_EXPECT_EQ(ran, std::size_t{2});
    CNA_STUDIO_EXPECT_EQ(fixture.context.getHistory().getCursor(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(fixture.name(), std::string{"A"});
}

CNA_STUDIO_TEST(NavigatingForwardsRedoesOneCommandAtATime)
{
    Fixture fixture;
    fixture.rename("A");
    fixture.rename("B");
    studioNavigateHistory(fixture.context, 0);
    CNA_STUDIO_EXPECT_EQ(fixture.name(), std::string{"Player"});

    const std::size_t ran = studioNavigateHistory(fixture.context, 2);

    CNA_STUDIO_EXPECT_EQ(ran, std::size_t{2});
    CNA_STUDIO_EXPECT_EQ(fixture.name(), std::string{"B"});
}

CNA_STUDIO_TEST(NavigatingToWhereYouAlreadyAreRunsNothing)
{
    Fixture fixture;
    fixture.rename("A");

    CNA_STUDIO_EXPECT_EQ(studioNavigateHistory(fixture.context, 1), std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(fixture.name(), std::string{"A"});
}

CNA_STUDIO_TEST(ClickingARowAsksToNavigateRatherThanNavigatingMidList)
{
    // Navigating runs commands, which changes the very list being drawn -- half the rows would
    // describe one history and half another.
    Fixture fixture;
    fixture.rename("A");
    fixture.rename("B");
    fixture.settle();

    fixture.click(kWidth * 0.5f, fixture.rowCentre(0));

    CNA_STUDIO_EXPECT(fixture.last.navigateTo.has_value());
    CNA_STUDIO_EXPECT_EQ(*fixture.last.navigateTo, std::size_t{0});
    // And nothing moved: the panel reported, the caller decides.
    CNA_STUDIO_EXPECT_EQ(fixture.context.getHistory().getCursor(), std::size_t{2});
}

CNA_STUDIO_TEST(ClickingTheCurrentPositionAsksForNothing)
{
    Fixture fixture;
    fixture.rename("A");
    fixture.settle();

    fixture.click(kWidth * 0.5f, fixture.rowCentre(1));

    CNA_STUDIO_EXPECT(!fixture.last.navigateTo.has_value());
}
