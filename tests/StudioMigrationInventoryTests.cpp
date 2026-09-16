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
#include "CNA/Studio/Scene/StudioCamera3D.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
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


    /** @brief The whole of a source file, or empty when it is not there. */
    std::string readSource(const std::string& relative)
    {
        std::ifstream stream{sourceRoot() / relative, std::ios::binary};
        std::stringstream buffer;
        buffer << stream.rdbuf();
        return buffer.str();
    }

    /** @brief A shell with the document commands and the panels bound, as the editor has them. */
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

// ------------------------------------------------------------------------------------------------
// The level the inventory did not reach (STUDIO-07041)
//
// The inventory accounts for panels, menu items, toolbar controls and shortcuts. It does not
// account for **controls inside a panel**, and that is not a small omission: the prototype's
// Inspector draws eight sections, and until STUDIO-07040 the native Details panel had three of
// them. One of the missing five was Add Component -- so an entity created in the native shell
// could never be given anything to do, and the inventory said the migration was complete.
//
// This is that level, as a checklist. It is a list of section names rather than a walk over the
// prototype's code, because the prototype is being deleted and a test that reads it would be
// deleted with it -- and the point of this one is to outlive the thing it was written about.
// ------------------------------------------------------------------------------------------------

namespace
{
    /** @brief One section the prototype's Inspector draws, and where the native answer is. */
    struct InspectorSection
    {
        /** @brief The prototype's member that draws it. */
        const char* prototype;
        /** @brief Where the native answer lives, or null when there is none yet. */
        const char* nativeFile;
        /** @brief A string that must appear in that file. Empty when there is no answer. */
        const char* nativeMarker;
        /** @brief The task that closes it, for a section with no answer. */
        const char* task;
    };

    /**
     * @brief Every section the prototype's Inspector has, and the native shell's answer.
     *
     * Ordered as the prototype draws them. A section with a null file is an admitted gap with a
     * task against it -- which is the honest shape for this table, because the alternative was a
     * document that said the migration was complete while Add Component did not exist.
     */
    const std::vector<InspectorSection>& inspectorSections()
    {
        static const std::vector<InspectorSection> sections = {
            {"component property grid", "src/shell-panels/StudioDetailsPanel.cpp",
             "SetPropertyCommand", nullptr},
            {"drawAddComponentControl", "src/shell-panels/StudioDetailsPanel.cpp",
             "AddComponentCommand", nullptr},
            {"component removal", "src/shell-panels/StudioDetailsPanel.cpp",
             "RemoveComponentCommand", nullptr},
            {"drawSceneEnvironment", "src/shell-panels/StudioDetailsPanel.cpp",
             "Scene Environment", nullptr},
            {"drawProjectInspector", "src/shell-panels/StudioDetailsPanel.cpp",
             "Layers", nullptr},
            {"drawPrefabSection", "src/shell-panels/StudioDetailsPanel.cpp",
             "studioPrefabSection", nullptr},
            {"drawAnimationPreview", "src/shell-panels/StudioDetailsPanel.cpp",
             "studioAnimationPreview", nullptr},
            {"drawAudioPreview", "src/shell-panels/StudioDetailsPanel.cpp",
             "studioAudioPreviewRow", nullptr},
            {"drawAssetInspector", "src/shell-panels/StudioDetailsPanel.cpp",
             "studioAssetInspector", nullptr},
            {"drawMaterialAsset", "src/shell-panels/StudioDetailsPanel.cpp",
             "studioMaterialEditor", nullptr},
        };
        return sections;
    }
}

CNA_STUDIO_TEST(EveryInspectorSectionWithANativeAnswerActuallyHasOne)
{
    // The half that can go stale silently: a section recorded as answered whose answer was removed
    // or renamed. Checked against the file rather than against a document, because a document
    // saying it is answered is exactly what was wrong before.
    for (const InspectorSection& section : inspectorSections())
    {
        if (section.nativeFile == nullptr) { continue; }

        const std::string text = readSource(section.nativeFile);
        if (text.empty())
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                std::string{"the inspector section '"} + section.prototype
                + "' is recorded as answered in " + section.nativeFile + ", which does not exist.");
            continue;
        }
        if (text.find(section.nativeMarker) == std::string::npos)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                std::string{"the inspector section '"} + section.prototype
                + "' is recorded as answered by '" + section.nativeMarker + "' in "
                + section.nativeFile + ", which no longer contains it.");
        }
    }
}

CNA_STUDIO_TEST(EveryUnansweredInspectorSectionNamesTheTaskThatClosesIt)
{
    // And the half that would otherwise become a quiet permanent gap. A row with no answer has to
    // carry a task id, and that id has to be a real row in the plan -- a gap recorded against a
    // task nobody created is a gap nobody will close.
    const std::string phase = readSource("plans/phase-07-panel-migration.md");
    CNA_STUDIO_EXPECT(!phase.empty());

    std::size_t unanswered = 0;
    for (const InspectorSection& section : inspectorSections())
    {
        if (section.nativeFile != nullptr) { continue; }
        ++unanswered;

        CNA_STUDIO_EXPECT(section.task != nullptr);
        if (section.task != nullptr && phase.find(section.task) == std::string::npos)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                std::string{"the inspector section '"} + section.prototype + "' names "
                + section.task + ", which is not a task in Phase 7.");
        }
    }

    // Stated so that closing the last one is a deliberate edit to this number rather than
    // something nobody notices. Dear ImGui cannot be deleted while this is above zero.
    //
    // Five when STUDIO-07041 took the inventory, and **zero** now: STUDIO-07045 answered the
    // asset inspector, STUDIO-07044 the audio preview, STUDIO-07043 the sprite animation preview,
    // STUDIO-07046 the material editor and STUDIO-07042 the prefab section.
    //
    // This number reaching zero is what unblocks STUDIO-07030 -- deleting the Dear ImGui panels.
    // It must not go back up: a section added to the prototype's Inspector from here would be a
    // section added to a panel that is being deleted.
    CNA_STUDIO_EXPECT_EQ(unanswered, std::size_t{0});
}
