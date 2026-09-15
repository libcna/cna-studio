// SPDX-License-Identifier: MS-PL
/**
 * @file StudioMigrationInventoryTests.cpp
 * @brief The prototype inventory as a checklist rather than a document (plan.md STUDIO-00014).
 *
 * `docs/MIGRATION-INVENTORY.md` lists every panel, menu item and shortcut the Dear ImGui prototype
 * offers, with what the native shell does about each. A document saying so would be a record of
 * intentions; this makes it a *check*: an item marked answered must resolve to a registered panel
 * with content or to a command that exists on the same chord, so a native counterpart that is
 * quietly dropped fails the build rather than being noticed a year later.
 *
 * The one thing it cannot check is whether the two *behave* the same, which is what
 * `STUDIO-07020`–`07023` are for. It can check that the counterpart exists, which is where the
 * three disagreeing shortcuts were found.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/ShellPanels/StudioShellActions.hpp"
#include "CNA/Studio/ShellPanels/StudioShellPanels.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    /** @brief One row of a table in the inventory. */
    struct InventoryRow
    {
        std::vector<std::string> cells;
        std::size_t line = 0;
    };

    std::filesystem::path sourceRoot() { return std::filesystem::path{CNA_STUDIO_SOURCE_ROOT}; }

    std::string trimmed(std::string text)
    {
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) { text.erase(0, 1); }
        while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
        {
            text.pop_back();
        }
        return text;
    }

    /** @brief Strips the backticks a markdown table uses for code spans. */
    std::string unquoted(std::string text)
    {
        std::string out;
        for (const char character : text)
        {
            if (character != '`') { out += character; }
        }
        return trimmed(std::move(out));
    }

    /** @brief Every pipe-delimited row of the inventory, header and rule rows dropped. */
    std::vector<InventoryRow> inventoryRows()
    {
        std::vector<InventoryRow> rows;

        std::ifstream stream{sourceRoot() / "docs" / "MIGRATION-INVENTORY.md", std::ios::binary};
        std::string line;
        std::size_t number = 0;

        while (std::getline(stream, line))
        {
            ++number;
            if (line.empty() || line.front() != '|') { continue; }
            if (line.find("---") != std::string::npos) { continue; }

            InventoryRow row;
            row.line = number;

            std::stringstream cells{line};
            std::string cell;
            while (std::getline(cells, cell, '|')) { row.cells.push_back(unquoted(cell)); }

            // A leading and trailing empty cell from the outer pipes.
            if (!row.cells.empty() && row.cells.front().empty()) { row.cells.erase(row.cells.begin()); }
            while (!row.cells.empty() && row.cells.back().empty()) { row.cells.pop_back(); }

            if (!row.cells.empty()) { rows.push_back(std::move(row)); }
        }
        return rows;
    }

    /** @brief A shell with the document commands and the panels bound, as the editor has them. */
    struct Fixture
    {
        StudioContext context;
        StudioLog log;
        StudioShell shell;
        StudioCamera2D camera;
        StudioShellPanels panels{shell, context, log};

        Fixture()
        {
            shell.resetLayout();
            (void)bindStudioShellActions(shell, context, log);
            panels.setViewportServices(camera, {});
        }
    };
}

CNA_STUDIO_TEST(TheInventoryExistsAndListsEnoughToBeAChecklist)
{
    // A file that had been emptied, renamed or never written would pass every check below by
    // having nothing in it to check.
    const std::vector<InventoryRow> rows = inventoryRows();
    CNA_STUDIO_EXPECT(rows.size() >= 30);

    std::size_t answered = 0;
    for (const InventoryRow& row : rows)
    {
        if (!row.cells.empty() && row.cells.back() == "✅") { ++answered; }
    }
    CNA_STUDIO_EXPECT(answered >= 20);
}

CNA_STUDIO_TEST(EveryPanelTheInventoryCallsAnsweredIsRegisteredAndDraws)
{
    Fixture fixture;

    for (const InventoryRow& row : inventoryRows())
    {
        // The panel table: prototype panel, source, native id, status.
        if (row.cells.size() != 4) { continue; }
        if (row.cells[1].find("src/panels/") == std::string::npos) { continue; }
        if (row.cells[3] != "✅" && row.cells[3] != "🔄") { continue; }

        const std::string& id = row.cells[2];
        if (id == "—") { continue; }

        if (fixture.shell.panel(id) == nullptr)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "MIGRATION-INVENTORY.md line " + std::to_string(row.line) + " says '"
                + row.cells[0] + "' is answered by the '" + id
                + "' panel, but no such panel is registered.");
            continue;
        }

        // Registered is not enough: an empty rectangle with a tab on it is not an answer.
        if (!fixture.shell.hasPanelContent(id))
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "MIGRATION-INVENTORY.md line " + std::to_string(row.line) + " says '"
                + row.cells[0] + "' is answered by the '" + id + "' panel, but that panel draws "
                  "nothing.");
        }
    }
}

CNA_STUDIO_TEST(EveryShortcutTheInventoryCallsAnsweredIsBoundToTheSameChord)
{
    // The check that found three disagreements. A shortcut that moved is a shortcut every existing
    // user has to relearn, and it *works* afterwards -- which is why nothing reports it.
    Fixture fixture;
    std::size_t checked = 0;

    for (const InventoryRow& row : inventoryRows())
    {
        // The shortcut table: chord, prototype action, native id, status.
        if (row.cells.size() != 4 || row.cells[3] != "✅") { continue; }

        StudioShortcut wanted;
        if (!parseStudioShortcut(row.cells[0], wanted)) { continue; }

        const std::string& id = row.cells[2];
        const StudioAction* action = fixture.shell.actions().find(id);
        if (action == nullptr)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "MIGRATION-INVENTORY.md line " + std::to_string(row.line) + " says " + row.cells[0]
                + " is answered by '" + id + "', which no command registers.");
            continue;
        }

        ++checked;
        if (!(action->shortcut == wanted))
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "MIGRATION-INVENTORY.md line " + std::to_string(row.line) + " says " + row.cells[0]
                + " invokes '" + id + "', but that command is bound to '"
                + describeStudioShortcut(action->shortcut) + "'.");
        }
    }

    // The shortcut table is the half most worth having, so an inventory whose chords all stopped
    // parsing would otherwise pass by checking nothing.
    CNA_STUDIO_EXPECT(checked >= 10);
}

CNA_STUDIO_TEST(EveryCommandTheInventoryCallsAnsweredExistsAndCanRun)
{
    // Existing is not the same as working: a menu item answered by a command with no handler is a
    // greyed-out row, which is what the prototype's item is not.
    Fixture fixture;

    for (const InventoryRow& row : inventoryRows())
    {
        // The menu table: menu, item, shortcut, native id, status.
        if (row.cells.size() != 5 || row.cells[4] != "✅") { continue; }

        const std::string& id = row.cells[3];
        if (id == "—") { continue; }

        const StudioAction* action = fixture.shell.actions().find(id);
        if (action == nullptr)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "MIGRATION-INVENTORY.md line " + std::to_string(row.line) + " says '"
                + row.cells[1] + "' is answered by '" + id + "', which no command registers.");
            continue;
        }

        if (!action->run)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "MIGRATION-INVENTORY.md line " + std::to_string(row.line) + " says '"
                + row.cells[1] + "' is answered by '" + id + "', but that command has no handler "
                  "and is drawn greyed out for ever.");
        }
    }
}
