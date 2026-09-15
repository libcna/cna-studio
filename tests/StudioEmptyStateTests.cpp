// SPDX-License-Identifier: MS-PL
/**
 * @file StudioEmptyStateTests.cpp
 * @brief A panel with nothing in it explains itself (plan.md STUDIO-06013).
 *
 * An empty panel is the state a user meets *first* — a fresh Studio is nothing but empty panels —
 * and a blank rectangle is indistinguishable from one whose content failed to draw. So every panel
 * has to say what it is for and what to do next, and this is the check: with no project, no scene
 * and no selection, a panel's content must put *words* on the screen.
 *
 * Words specifically. Text and fills batch into the same draw command against the same atlas
 * texture, which is what makes them fast and what makes "did this panel say anything, or is it a
 * coloured rectangle?" unanswerable from the draw data — hence `StudioDrawList::glyphCount`.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/ShellPanels/StudioShellActions.hpp"
#include "CNA/Studio/ShellPanels/StudioShellPanels.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/Scene/StudioCamera3D.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    constexpr float kWidth = 900.0f;
    constexpr float kHeight = 500.0f;

    UiInputState offScreen()
    {
        UiInputState input;
        input.displayWidth = kWidth;
        input.displayHeight = kHeight;
        input.mouseX = -1.0f;
        input.mouseY = -1.0f;
        return input;
    }

    struct Fixture
    {
        StudioContext context;
        StudioLog log;
        StudioShell shell;
        StudioCamera2D camera;
        StudioCamera3D camera3D;
        StudioShellPanels panels{shell, context, log};

        Fixture()
        {
            shell.resetLayout();
            (void)bindStudioShellActions(shell, context, log);
            panels.setViewportServices(camera, camera3D, {});
        }

        /** @brief Glyphs the panel's content drew on its own, with nothing open. */
        [[nodiscard]] std::size_t glyphsFor(const std::string& id)
        {
            StudioFrame frame{StudioTheme::dark()};
            frame.setFontAtlas(shell.frame().fontAtlas());

            std::size_t drawn = 0;
            runStudioFrame(frame, offScreen(), [&](StudioFrame& pass) {
                (void)shell.describePanelContent(id, pass, UiRect{0.0f, 0.0f, kWidth, kHeight});
                if (pass.isDrawPass()) { drawn = pass.drawList().glyphCount(); }
            });
            return drawn;
        }
    };
}

CNA_STUDIO_TEST(EveryPanelSaysSomethingWhenThereIsNothingToShow)
{
    // The list is the same discipline as the unimplemented-command and empty-panel guards: a panel
    // that says nothing is named here with the reason, or the build fails. It fails in both
    // directions, so giving one an empty state and forgetting this list also fails.
    //
    // A panel with no content at all is not listed here: that is the empty-panel guard's business
    // (`EveryPanelWithoutContentIsNamedRatherThanBeingAnEmptyRectangle`), and naming it in both
    // would make one of the two lists wrong the moment the other was updated.
    const std::vector<std::pair<std::string, std::string>> pending = {};

    Fixture fixture;
    std::vector<std::string> silent;

    for (const StudioPanelDescriptor& descriptor : fixture.shell.registeredPanels())
    {
        if (!fixture.shell.hasPanelContent(descriptor.id)) { continue; }
        if (fixture.glyphsFor(descriptor.id) == 0) { silent.push_back(descriptor.id); }
    }

    for (const std::string& id : silent)
    {
        const bool named = std::any_of(pending.begin(), pending.end(),
                                       [&](const auto& entry) { return entry.first == id; });
        if (!named)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "the '" + id + "' panel draws no text with nothing open, so a user meets a blank "
                "rectangle they cannot tell from a panel that failed. Give it an empty state "
                "saying what it is for, or add it here with the reason it has none.");
        }
    }

    for (const auto& [id, reason] : pending)
    {
        const bool still = std::find(silent.begin(), silent.end(), id) != silent.end();
        if (!still)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "the '" + id + "' panel now says something with nothing open, so remove it from "
                "the pending list (it was there because " + reason + ").");
        }
    }
}

CNA_STUDIO_TEST(AnEmptyStateSurvivesAPanelTooSmallToShowIt)
{
    // A panel dragged very narrow is ordinary, and an empty state that produced invalid geometry
    // or a phase violation at a small size would fail in the one arrangement nobody photographs.
    Fixture fixture;

    for (const StudioPanelDescriptor& descriptor : fixture.shell.registeredPanels())
    {
        if (!fixture.shell.hasPanelContent(descriptor.id)) { continue; }

        for (const UiRect& bounds : {UiRect{0.0f, 0.0f, 0.0f, 0.0f},
                                     UiRect{0.0f, 0.0f, 12.0f, 8.0f},
                                     UiRect{0.0f, 0.0f, 40.0f, 400.0f}})
        {
            StudioFrame frame{StudioTheme::dark()};
            frame.setFontAtlas(fixture.shell.frame().fontAtlas());

            runStudioFrame(frame, offScreen(), [&](StudioFrame& pass) {
                (void)fixture.shell.describePanelContent(descriptor.id, pass, bounds);
            });

            if (frame.phaseViolations() != 0)
            {
                CnaStudioTest::reportFailure(__FILE__, __LINE__,
                    "the '" + descriptor.id + "' panel raised a phase violation at "
                    + std::to_string(static_cast<int>(bounds.width)) + "x"
                    + std::to_string(static_cast<int>(bounds.height)) + ".");
            }

            const UiDrawDataValidation checked = validate(frame.drawData());
            if (!checked.valid)
            {
                CnaStudioTest::reportFailure(__FILE__, __LINE__,
                    "the '" + descriptor.id + "' panel produced invalid draw data at "
                    + std::to_string(static_cast<int>(bounds.width)) + "x"
                    + std::to_string(static_cast<int>(bounds.height)) + ".");
            }
        }
    }
}
