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


    /** @brief Splits a cell listing alternatives, e.g. `2D View / 3D View`, into each of them. */
    std::vector<std::string> splitAlternatives(const std::string& cell)
    {
        std::vector<std::string> parts;
        std::size_t start = 0;
        for (std::size_t cut = cell.find(" / "); ; cut = cell.find(" / ", start))
        {
            if (cut == std::string::npos)
            {
                parts.push_back(trimmed(cell.substr(start)));
                break;
            }
            parts.push_back(trimmed(cell.substr(start, cut - start)));
            start = cut + 3;
        }
        return parts;
    }

    /** @brief The whole of a source file, or empty when it is not there. */
    std::string readSource(const std::string& relative)
    {
        std::ifstream stream{sourceRoot() / relative, std::ios::binary};
        std::stringstream buffer;
        buffer << stream.rdbuf();
        return buffer.str();
    }

    /**
     * @brief The body of the function whose signature begins at @p signature, braces matched.
     *
     * Brace-matched rather than read to the next blank line, because the prototype's toolbars
     * contain blocks and a reader that stopped early would silently check half of one -- which
     * would look exactly like a toolbar with fewer controls than it has.
     *
     * @param text Source file.
     * @param signature Text the function's definition begins with.
     * @return The body between the outermost braces, or empty when the signature is not there.
     */
    std::string functionBody(const std::string& text, const std::string& signature)
    {
        const std::size_t start = text.find(signature);
        if (start == std::string::npos) { return {}; }

        const std::size_t open = text.find('{', start);
        if (open == std::string::npos) { return {}; }

        int depth = 0;
        for (std::size_t index = open; index < text.size(); ++index)
        {
            if (text[index] == '{') { ++depth; }
            else if (text[index] == '}')
            {
                --depth;
                if (depth == 0) { return text.substr(open + 1, index - open - 1); }
            }
        }
        return {};
    }

    /** @brief Every double-quoted literal in @p text, in order, unescaped only of quotes. */
    std::vector<std::string> stringLiterals(const std::string& text)
    {
        std::vector<std::string> found;
        for (std::size_t index = 0; index < text.size(); ++index)
        {
            if (text[index] != '"') { continue; }

            std::string literal;
            ++index;
            while (index < text.size() && text[index] != '"')
            {
                if (text[index] == '\\' && index + 1 < text.size()) { ++index; }
                literal += text[index];
                ++index;
            }
            found.push_back(std::move(literal));
        }
        return found;
    }

    /**
     * @brief The literals passed to every @p call in @p body.
     *
     * The argument list is brace- and paren-matched so a nested call does not end it early.
     *
     * @param body Source text to scan.
     * @param call Call prefix, e.g. `"ui_.button("`.
     * @return Every string literal appearing in those calls' arguments.
     */
    std::vector<std::string> literalsPassedTo(const std::string& body, const std::string& call)
    {
        std::vector<std::string> found;
        for (std::size_t at = body.find(call); at != std::string::npos;
             at = body.find(call, at + call.size()))
        {
            std::size_t index = at + call.size();
            int depth = 1;
            const std::size_t start = index;
            for (; index < body.size() && depth > 0; ++index)
            {
                if (body[index] == '(') { ++depth; }
                else if (body[index] == ')') { --depth; }
            }

            for (std::string& literal : stringLiterals(body.substr(start, index - start - 1)))
            {
                found.push_back(std::move(literal));
            }
        }
        return found;
    }

    /** @brief Trims a menu label down to what the inventory can name it by. */
    std::string menuLabel(std::string text)
    {
        // `menuItem("Undo " + history.getUndoDescription())` and
        // `menuItem("Recover Unsaved Scene (" + when + ")")` both end mid-sentence, so the literal
        // carries a trailing space or bracket that no readable table would reproduce.
        while (!text.empty() && (text.back() == ' ' || text.back() == '(')) { text.pop_back(); }
        return text;
    }

    /** @brief The inventory's cells in column @p column, for rows the predicate accepts. */
    template <typename Predicate>
    std::set<std::string> inventoryColumn(std::size_t column, Predicate accepts)
    {
        std::set<std::string> values;
        for (const InventoryRow& row : inventoryRows())
        {
            if (row.cells.size() <= column || !accepts(row)) { continue; }
            for (const std::string& value : splitAlternatives(row.cells[column]))
            {
                values.insert(value);
            }
        }
        return values;
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

// --- The other direction: does the inventory cover the prototype? ----------------------------
//
// Everything above checks list → implementation: an item the inventory calls answered must resolve
// to something that exists. That direction cannot catch the failure this task is about. A panel,
// a menu item or a toolbar control the prototype has and the list never mentioned is a piece of
// the editor that is simply *forgotten*, and every check above passes while it is missing —
// because the list is what they check against.
//
// So these extract the prototype's real surface from its own source and require the list to cover
// it, item by item. The prototype is deleted by `STUDIO-07030`, at which point these become
// vacuous and are removed with it (`plan.md` STUDIO-07020).

CNA_STUDIO_TEST(EveryPrototypePanelIsInTheInventory)
{
    const std::set<std::string> listed = inventoryColumn(1, [](const InventoryRow& row) {
        return row.cells.size() == 4 && row.cells[1].find("src/panels/") != std::string::npos;
    });

    std::size_t seen = 0;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator{sourceRoot() / "src" / "panels"})
    {
        const std::string name = entry.path().filename().generic_string();
        if (entry.path().extension() != ".cpp") { continue; }

        // The menu bar is not a panel; the menu table is where its items are accounted for.
        if (name == "MainMenuBar.cpp") { continue; }

        ++seen;
        const std::string relative = "src/panels/" + name;
        if (listed.count(relative) == 0)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                relative + " is a prototype panel that MIGRATION-INVENTORY.md does not list, so "
                "nothing has decided whether the native shell answers it.");
        }
    }

    CNA_STUDIO_EXPECT(seen >= 9);
}

CNA_STUDIO_TEST(EveryPrototypeMenuItemIsInTheInventory)
{
    const std::string source = readSource("src/panels/MainMenuBar.cpp");
    CNA_STUDIO_EXPECT(!source.empty());

    const std::set<std::string> listed = inventoryColumn(1, [](const InventoryRow& row) {
        return row.cells.size() == 5 && row.cells[0] != "Toolbar"
            && row.cells[0] != "Play" && row.cells[0] != "Tools";
    });

    std::size_t seen = 0;
    for (const std::string& literal : literalsPassedTo(source, "ui_.menuItem("))
    {
        const std::string label = menuLabel(literal);

        // The shortcut column of a `menuItem` call, not a label.
        if (label.empty() || label.size() <= 3) { continue; }
        if (label.rfind("Ctrl+", 0) == 0 || label.rfind("Alt+", 0) == 0) { continue; }

        ++seen;
        if (listed.count(label) == 0)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "MainMenuBar draws a '" + label + "' item that MIGRATION-INVENTORY.md does not "
                "list, so nothing has decided whether the native menus answer it.");
        }
    }

    CNA_STUDIO_EXPECT(seen >= 12);
}

CNA_STUDIO_TEST(EveryPrototypeToolbarControlIsInTheInventory)
{
    // Both directions, because this table is new and a table listing controls the prototype does
    // not have would be a parity claim against an editor nobody is shipping.
    const std::string source = readSource("src/panels/ViewportPanel.cpp");
    CNA_STUDIO_EXPECT(!source.empty());

    std::vector<std::string> widgets;
    for (const char* signature : {"void ViewportPanel::drawPlayToolbar()",
                                  "void ViewportPanel::drawToolbar()"})
    {
        const std::string body = functionBody(source, signature);
        if (body.empty())
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                std::string{signature} + " is not in ViewportPanel.cpp, so this test is reading "
                "nothing. If the prototype's toolbars were renamed, rename them here too.");
            continue;
        }

        for (const char* call : {"ui_.button(", "ui_.propertyField("})
        {
            for (std::string& literal : literalsPassedTo(body, call))
            {
                widgets.push_back(std::move(literal));
            }
        }
    }

    const std::set<std::string> listed = inventoryColumn(2, [](const InventoryRow& row) {
        return row.cells.size() == 5 && (row.cells[0] == "Play" || row.cells[0] == "Tools");
    });

    for (const std::string& widget : widgets)
    {
        if (listed.count(widget) == 0)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "ViewportPanel's toolbars draw a '" + widget + "' control that "
                "MIGRATION-INVENTORY.md does not list, so nothing has decided whether the native "
                "shell answers it.");
        }
    }

    for (const std::string& widget : listed)
    {
        if (std::find(widgets.begin(), widgets.end(), widget) == widgets.end())
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "MIGRATION-INVENTORY.md lists a toolbar control '" + widget + "' that "
                "ViewportPanel's toolbars do not draw, so the parity claim is against an editor "
                "that does not exist.");
        }
    }

    CNA_STUDIO_EXPECT(widgets.size() >= 10);
}

CNA_STUDIO_TEST(EveryShortcutTheProtoypeDispatchesIsInTheInventory)
{
    // The chord table is the half that already found three disagreements. This is the half that
    // would find a fourth: a chord the prototype dispatches that nobody wrote down at all.
    const std::string source = readSource("src/app/StudioApplication.cpp");
    CNA_STUDIO_EXPECT(!source.empty());

    const std::string body = functionBody(source, "void StudioApplication::handleShortcuts()");
    if (body.empty())
    {
        CnaStudioTest::reportFailure(__FILE__, __LINE__,
            "StudioApplication::handleShortcuts is not where this test looks for it, so it is "
            "reading nothing.");
        return;
    }

    const std::set<std::string> listed = inventoryColumn(0, [](const InventoryRow& row) {
        StudioShortcut parsed;
        return row.cells.size() == 4 && parseStudioShortcut(row.cells[0], parsed);
    });

    std::size_t seen = 0;
    for (std::size_t at = body.find("isShortcutPressed(UiKey::"); at != std::string::npos;
         at = body.find("isShortcutPressed(UiKey::", at + 1))
    {
        const std::size_t open = body.find('(', at);
        const std::size_t close = body.find(')', open);
        if (close == std::string::npos) { break; }

        // `UiKey::S, withControl()` -- the inner parens of the modifier call land inside, so the
        // key name is read up to the first comma or the closing paren, whichever comes first.
        const std::string arguments = body.substr(open + 1, close - open - 1);
        const std::size_t comma = arguments.find(',');
        std::string key = arguments.substr(0, comma);
        key = trimmed(key.substr(key.find("::") + 2));

        // The inventory writes chords the way `describeStudioShortcut` does, which is how a user
        // reads them: `2` rather than `Digit2`.
        if (key.rfind("Digit", 0) == 0) { key = key.substr(5); }

        const bool control = comma != std::string::npos
                          && arguments.find("withControl", comma) != std::string::npos;
        const std::string chord = control ? "Ctrl+" + key : key;

        ++seen;
        if (listed.count(chord) == 0)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "handleShortcuts dispatches " + chord + ", which MIGRATION-INVENTORY.md does not "
                "list -- so nothing has decided whether the native shell answers it, and an "
                "existing user's key would quietly stop working.");
        }
    }

    CNA_STUDIO_EXPECT(seen >= 13);
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

// ------------------------------------------------------------------------------------------------
// The level below the controls: the prototype's *tests* (STUDIO-07047)
//
// `STUDIO-07041` found that the inventory accounted for surfaces -- panels, menus, toolbars,
// shortcuts -- and that the level below it, the controls inside a panel, was unaccounted for. There
// is a level below *that*, and it is the one deleting the prototype actually costs.
//
// `ApplicationTests.cpp` runs the prototype's application over a null UI and asserts on what it
// does to the document: gizmo drags and their undo merging, prefab revert and apply, tilemap brush
// and fill, asset drops onto typed slots, 3D navigation, crash recovery. Almost all of it is
// *shared* code reached through the prototype's panels, and almost none of it is about Dear ImGui.
// Deleting that file without this would delete a third of the suite and nothing would say which
// third.
//
// So every case is accounted for here: covered elsewhere (a file and a symbol, checked against the
// file), or recorded as a gap with the task that closes it, or marked as going away with the
// prototype because it is about the prototype's own presentation.
//
// **What this found first was not a missing test.** It was eight things the native shell cannot do
// at all -- a manipulator in the 3D view, an asset reloaded after an external edit, a loaded
// plugin, `--scene`, a list property that can be added to, a numeric field that can be dragged, a
// grid plane, a reparent by dragging -- each of them shared code that nobody is running. That is
// the crash-recovery gap of `STUDIO-07020` repeating, for the same reason: none of them is a
// surface, so no inventory of surfaces could see them.
// ------------------------------------------------------------------------------------------------

namespace
{
    /** @brief One case of the prototype's suite, and where its behaviour is covered without it. */
    struct PrototypeCase
    {
        /** @brief The case in `ApplicationTests.cpp` or `UiTests.cpp`. */
        const char* name;
        /** @brief Where the same behaviour is covered, or null. */
        const char* file;
        /** @brief A symbol that must appear in that file -- normally the covering case's name. */
        const char* symbol;
        /** @brief The task that closes it, for a behaviour nothing covers yet. */
        const char* task;
        /** @brief True when the case is about the prototype's own presentation and goes with it. */
        bool goesWithThePrototype;
    };

    /** @brief Every case in the prototype's two end-to-end files, and its answer. */
    const std::vector<PrototypeCase>& prototypeCases()
    {
        static const std::vector<PrototypeCase> cases = {
            {"ApplicationStartsWithAUsableEmptyScene", "tests/StudioNativeParityTests.cpp", "AStudioOpenedWithNoProjectStartsOnASceneSomethingCanBeSeenIn", nullptr, false},
            {"ApplicationDrawsEveryPanelEachFrame", "tests/StudioEmptyStateTests.cpp", "EveryPanelSaysSomethingWhenThereIsNothingToShow", nullptr, false},
            {"ApplicationHonoursItsFrameLimit", nullptr, nullptr, "STUDIO-07049", false},
            {"ApplicationWalksADeepHierarchyWithoutCrashing", "tests/StudioOutlinerPanelTests.cpp", "ADeepSceneIsDrawnRatherThanDescended", nullptr, false},
            {"SelectionIsPrunedWhenItsEntityIsDeleted", "tests/StudioNativeParityTests.cpp", "DeletingAnEntityTakesItOutOfTheSelection", nullptr, false},
            {"FullProjectRoundTripThroughTheApplication", "tests/ProjectAndAssetTests.cpp", "ProjectRoundTripsThroughAFile", nullptr, false},
            {"ApplicationReportsAMissingProjectRatherThanCrashing", "tests/StudioNativeParityTests.cpp", "AProjectThatIsNotThereIsReportedRatherThanOpened", nullptr, false},
            {"AGizmoDragMovesTheSelectedEntityAlongTheGrabbedAxis", "tests/StudioViewportPanelTests.cpp", "DraggingATranslateArmMovesTheEntityAlongThatAxisOnly", nullptr, false},
            {"AGizmoDragOnTheCentreHandleMovesOnBothAxes", "tests/StudioNativeParityTests.cpp", "TheCentreHandleMovesOnBothAxesAtOnce", nullptr, false},
            {"AGizmoDragIsOneUndoEntryThatReturnsToWhereItStarted", "tests/StudioViewportPanelTests.cpp", "AWholeDragIsOneUndoEntryRatherThanOnePerFrame", nullptr, false},
            {"TwoGizmoDragsAreTwoUndoEntries", "tests/StudioViewportPanelTests.cpp", "TwoGroupDragsAreTwoUndoEntriesRatherThanOne", nullptr, false},
            {"AGizmoDragSurvivesThePointerLeavingTheViewport", "tests/StudioNativeParityTests.cpp", "ADragSurvivesThePointerLeavingTheViewport", nullptr, false},
            {"AGizmoPressDoesNotAlsoChangeTheSelection", "tests/StudioViewportPanelTests.cpp", "PressingATranslateArmStartsADragRatherThanSelecting", nullptr, false},
            {"PressingAHandleWithoutMovingLeavesTheHistoryAlone", "tests/StudioNativeParityTests.cpp", "APressOnAHandleThatMovesNothingLeavesTheHistoryAlone", nullptr, false},
            {"APressAwayFromEveryHandleStillSelects", "tests/StudioNativeParityTests.cpp", "APressAwayFromEveryHandleStillPicks", nullptr, false},
            {"ShortcutsDriveUndoAndRedo", "tests/StudioShellActionTests.cpp", "TheKeyboardAndTheMenuReachTheSameAction", nullptr, false},
            {"DeleteRemovesTheSelectionAndClearsIt", "tests/StudioShellActionTests.cpp", "DeletingAnEntityIsUndoable", nullptr, false},
            {"DuplicateCopiesTheSubtreeWithFreshIdsAndSelectsTheCopy", "tests/StudioNativeParityTests.cpp", "DuplicateCopiesTheSubtreeWithFreshIdsAndSelectsTheCopy", nullptr, false},
            {"FrameSelectedBringsTheSelectionIntoView", "tests/StudioViewportPanelTests.cpp", "FocusSelectedMovesTheCameraOntoWhatIsSelected", nullptr, false},
            {"TheComparisonHarnessIsOffUnlessAskedFor", "tests/StudioComparisonPanelTests.cpp", "WithNoProjectThereIsNothingToCompareAndThePanelSaysSo", nullptr, false},
            {"TheComparisonHarnessReportsWhenItCannotRun", "tests/StudioComparisonPanelTests.cpp", "AProblemIsSaidBeforeTheButtonRatherThanAfterPressingIt", nullptr, false},
            {"GizmoModeShortcutsSwitchTheManipulator", "tests/StudioViewportPanelTests.cpp", "TheTransformShortcutsReachTheGizmoThroughTheRegistry", nullptr, false},
            {"ARotateDragTurnsTheSelectedEntity", "tests/StudioViewportPanelTests.cpp", "TheRotateAndScaleManipulatorsDragTheirOwnProperty", nullptr, false},
            {"ARotateDragIsOneUndoEntryThatReturnsToWhereItStarted", "tests/StudioViewportPanelTests.cpp", "AWholeDragIsOneUndoEntryRatherThanOnePerFrame", nullptr, false},
            {"APressInsideTheRotateRingStillReachesThePicker", "tests/ViewportTests.cpp", "RotateGizmoHitTestGrabsTheRingAndNotItsInterior", nullptr, false},
            {"AScaleDragResizesTheSelectedEntity", "tests/StudioViewportPanelTests.cpp", "TheRotateAndScaleManipulatorsDragTheirOwnProperty", nullptr, false},
            {"DuplicatingASelectionIsOneUndoEntry", "tests/StudioNativeParityTests.cpp", "DuplicateCopiesTheSubtreeWithFreshIdsAndSelectsTheCopy", nullptr, false},
            {"DeletingASelectionIsOneUndoEntry", "tests/StudioNativeParityTests.cpp", "DeletingASelectionIsOneUndoEntry", nullptr, false},
            {"AGizmoDragOnAMultiSelectionMovesEveryEntityAsOneUndoEntry", "tests/StudioViewportPanelTests.cpp", "DraggingAMultiSelectionMovesEveryEntityByTheSameAmount", nullptr, false},
            {"AMultiSelectionGizmoLeavesAChildOfASelectedParentAlone", "tests/ViewportTests.cpp", "ASelectionsRootsExcludeDescendantsOfOtherSelectedEntities", nullptr, false},
            {"TwoInspectorDragsOfOneFieldAreTwoUndoEntries", nullptr, nullptr, "STUDIO-07055", false},
            {"HoldingTheSnapModifierRoundsAGizmoDragToTheGrid", "tests/StudioNativeParityTests.cpp", "TheSnapModifierRoundsADragToTheVisibleGrid", nullptr, false},
            {"CtrlClickingAddsToTheSelectionAndClickingEmptySpaceWithItDoesNot", "tests/StudioViewportPanelTests.cpp", "CtrlClickingASecondSpriteAddsItToTheSelection", nullptr, false},
            {"TheViewportToolbarShowsAndSetsTheGizmoModeAndSpace", "tests/StudioViewportPanelTests.cpp", "TheToolbarsTransformButtonsChooseTheManipulator", nullptr, false},
            {"TheGizmoSpaceShortcutTogglesBothWays", "tests/StudioNativeParityTests.cpp", "TheGizmoSpaceShortcutTogglesBothWays", nullptr, false},
            {"ALocalSpaceDragFollowsTheEntitysOwnAxis", "tests/StudioNativeParityTests.cpp", "ALocalSpaceDragFollowsTheEntitysOwnAxis", nullptr, false},
            {"AnArmedShortcutFiresExactlyOnce", "tests/StudioNativeParityTests.cpp", "AnArmedShortcutFiresExactlyOnce", nullptr, false},
            {"AddingAComponentThroughTheInspectorIsUndoable", "tests/StudioDetailsPanelTests.cpp", "AComponentCanBeAddedToTheSelectedEntityAndUndone", nullptr, false},
            {"TheAddPickerDropsAUniqueComponentTheEntityAlreadyHas", "tests/StudioNativeParityTests.cpp", "TheAddPickerDropsAUniqueComponentTheEntityAlreadyHas", nullptr, false},
            {"ARequiredComponentGetsNoRemoveButton", "tests/StudioNativeParityTests.cpp", "ARequiredComponentGetsNoRemoveButton", nullptr, false},
            {"RemovingAComponentThroughTheInspectorIsUndoable", "tests/StudioDetailsPanelTests.cpp", "AComponentIsRemovedFromItsOwnHeaderAndUndone", nullptr, false},
            {"RotationIsEditedAsDegreesAndStoredAsAQuaternion", "tests/StudioDetailsPanelTests.cpp", "AQuaternionIsEditedAsAnglesRatherThanAsFourRawNumbers", nullptr, false},
            {"TheInspectorKeepsTheAnglesTheUserTypedAtGimbalLock", "tests/StudioDetailsPanelTests.cpp", "TheAnglesTheUserTypedSurviveGimbalLock", nullptr, false},
            {"TheAngleCacheStopsApplyingOnceSomethingElseChangesTheRotation", "tests/StudioDetailsPanelTests.cpp", "TheAngleCacheIsAbandonedTheInstantSomethingElseProducesADifferentQuaternion", nullptr, false},
            {"DoubleClickingAHierarchyNodeRenamesItInPlace", "tests/StudioRenameTests.cpp", "F2RenamesTheSelectionAndRaisesTheOutlinerToDoIt", nullptr, false},
            {"AnEmptyRenameIsTreatedAsASlipAndKeepsTheOldName", "tests/StudioRenameTests.cpp", "AnEmptyNameIsRefusedRatherThanApplied", nullptr, false},
            {"DraggingAnEntityOntoAnotherReparentsIt", nullptr, nullptr, "STUDIO-07058", false},
            {"DroppingAParentOntoItsOwnChildIsRefusedWithoutAnUndoEntry", nullptr, nullptr, "STUDIO-07058", false},
            {"DroppingAnEntityOntoItselfDoesNothing", nullptr, nullptr, "STUDIO-07058", false},
            {"CtrlClickExtendsTheHierarchySelection", "tests/StudioNativeParityTests.cpp", "CtrlClickExtendsTheOutlinerSelection", nullptr, false},
            {"TheHierarchyKeepsDrawingWhileAReparentIsPending", nullptr, nullptr, "STUDIO-07058", false},
            {"DroppingATextureOntoASpriteSlotSetsIt", "tests/StudioDragDropTests.cpp", "ReleasingOverAMatchingTargetDeliversThePayload", nullptr, false},
            {"ASlotRefusesAnAssetOfTheWrongKindAndSaysWhy", "tests/StudioDragDropTests.cpp", "ATargetOfTheWrongTypeSaysSoRatherThanIgnoringIt", nullptr, false},
            {"ASlotWithNoDeclaredKindTakesAnything", "tests/StudioDragDropTests.cpp", "ATargetOfTheRightTypeLightsUpBeforeTheDrop", nullptr, false},
            {"TheMaterialOverrideSlotTakesAMaterialAndRefusesEverythingElse", "tests/StudioDragDropTests.cpp", "ATargetOfTheWrongTypeSaysSoRatherThanIgnoringIt", nullptr, false},
            {"TheConsoleCopiesWhatItIsShowing", "tests/StudioLogPanelTests.cpp", "FilteringCountsAndTextAgreeWithEachOther", nullptr, false},
            {"TheConsoleClearButtonEmptiesIt", "tests/StudioLogPanelTests.cpp", "ClearingTheLogIsReportedRatherThanDoneBehindTheOwnersBack", nullptr, false},
            {"TheConsoleScrollLockIsRememberedAcrossFrames", "tests/StudioLogPanelTests.cpp", "FollowingNewOutputStopsTheMomentTheUserScrollsAway", nullptr, false},
            {"TheConsoleSeverityFilterIsRememberedAcrossFrames", "tests/StudioLogPanelTests.cpp", "TheSeverityOfALineIsVisibleWithoutReadingIt", nullptr, false},
            {"RelinkingFromTheReportFixesEveryReferenceAtOnce", "tests/StudioProblemsPanelTests.cpp", "DroppingAnAssetOnABrokenRowAsksToRelinkItRatherThanClearIt", nullptr, false},
            {"TheReportSaysSoWhenNothingIsBroken", "tests/StudioProblemsPanelTests.cpp", "ACleanSceneSaysSoRatherThanShowingNothing", nullptr, false},
            {"SelectingAnAssetSwitchesTheInspectorToItsImportSettings", "tests/StudioDetailsPanelTests.cpp", "SelectingAnAssetShowsItRatherThanTheSceneSettings", nullptr, false},
            {"EditingAnImportSettingGoesThroughTheUndoStack", "tests/StudioDetailsPanelTests.cpp", "AnImporterSettingIsEditedThroughTheHistoryLikeEveryOtherProperty", nullptr, false},
            {"AnExternallyEditedAssetIsReloadedAndReported", nullptr, nullptr, "STUDIO-07051", false},
            {"TheAssetBrowserShowsAFolderTree", "tests/StudioContentBrowserTests.cpp", "FoldersComeFromPathsAndParentsComeFirst", nullptr, false},
            {"TheAssetBrowserFilterHidesWhatDoesNotMatch", nullptr, nullptr, "STUDIO-35042", false},
            {"DroppingAnAssetOnAFolderMovesTheFileAndNotAnyScene", "tests/ProjectAndAssetTests.cpp", "AssetDatabaseKeepsIdentityAcrossAMove", nullptr, false},
            {"RenamingAnAssetKeepsItInItsFolder", "tests/ProjectAndAssetTests.cpp", "AssetDatabaseKeepsIdentityAcrossAMove", nullptr, false},
            {"ARenameContainingASeparatorIsRefusedWithAReason", "tests/ProjectAndAssetTests.cpp", "AssetDatabaseKeepsIdentityAcrossAMove", nullptr, false},
            {"TheValidationPanelReportsAnIssueAndSelectsItsEntity", "tests/StudioProblemsPanelTests.cpp", "ClickingAnIssueRowAsksToSelectTheEntityAtFault", nullptr, false},
            {"TheHistoryPanelListsEveryEntryIncludingTheUndoneOnes", "tests/StudioHistoryPanelTests.cpp", "UndoneEntriesAreMarkedRatherThanHidden", nullptr, false},
            {"ClickingAHistoryRowMovesTheCursorToIt", "tests/StudioHistoryPanelTests.cpp", "ClickingARowAsksToNavigateRatherThanNavigatingMidList", nullptr, false},
            {"AnEmptyHistorySaysSoRatherThanDrawingNothing", "tests/StudioHistoryPanelTests.cpp", "AnEmptyHistoryStillHasThePositionItStartedFrom", nullptr, false},
            {"AnUnsavedSceneIsSnapshottedAndTheSnapshotGoesAwayOnSave", "tests/StudioRecoveryTests.cpp", "TheNativeShellWritesSnapshotsWhileTheSceneIsUnsaved", nullptr, false},
            {"RecoveredWorkIsOfferedRatherThanRestoredBehindTheUsersBack", "tests/StudioRecoveryTests.cpp", "WorkFromAPreviousSessionIsOfferedRatherThanFound", nullptr, false},
            {"DiscardingARecoveredSceneRemovesTheSnapshotForGood", "tests/StudioRecoveryTests.cpp", "DiscardingRemovesTheSnapshotAndTheOffer", nullptr, false},
            {"AutosaveCanBeTurnedOffEntirely", "tests/StudioRecoveryTests.cpp", "AnAutosaveIntervalOfZeroWritesNothing", nullptr, false},
            {"TheInspectorAddsRemovesAndReordersListElements", nullptr, nullptr, "STUDIO-07054", false},
            {"TheInspectorEditsTheProjectsLayersWhenNothingIsSelected", "tests/StudioSceneSettingsTests.cpp", "ALayerCanBeAddedAndTheChangeIsUndoable", nullptr, false},
            {"APrefabIsMadeFromASelectionAndDroppedBackIntoTheScene", "tests/SceneTests.cpp", "InstantiatingAPrefabGivesFreshIdsAndKeepsTheLink", nullptr, false},
            {"TheInspectorReportsOverridesAndRevertsOrAppliesThem", "tests/StudioPrefabSectionTests.cpp", "RevertingThrowsTheChangesAwayAndUndoBringsThemBack", nullptr, false},
            {"ApplyingAnInstantiatedInstanceKeepsEveryLinkIntact", "tests/StudioPrefabSectionTests.cpp", "ApplyingWritesTheChangesIntoThePrefabFile", nullptr, false},
            {"TheBrushPaintsAcrossADragAsOneUndoEntry", "tests/StudioTilemapToolTests.cpp", "ADragAcrossSeveralCellsIsOneUndoEntry", nullptr, false},
            {"TheEraserClearsAndTheBrushDoesNotSelectOrPickTheCamera", "tests/StudioTilemapToolTests.cpp", "TheEraserClearsTheCellRatherThanWritingZero", nullptr, false},
            {"TheBrushSaysSoWhenTheSelectionHasNoTilemap", "tests/StudioTilemapToolTests.cpp", "PaintingWithNoTilemapSelectedSaysSoOncePerPress", nullptr, false},
            {"TheEyedropperTakesATileAndGoesBackToPainting", "tests/StudioTilemapToolTests.cpp", "TheEyedropperTakesTheTileAndThenGoesBackToPainting", nullptr, false},
            {"AFillCoversTheDraggedRectangleAsOneUndoEntry", "tests/StudioTilemapToolTests.cpp", "AFillCoversTheDraggedRectangleOnReleaseAsOneEntry", nullptr, false},
            {"AFillDraggedBackwardsAndPastTheEdgeStillFillsWhatExists", "tests/StudioTilemapToolTests.cpp", "AFillDraggedBackwardsStillFillsTheRectangle", nullptr, false},
            {"TheInspectorPreviewsAnAnimationWithoutPuttingItInTheDocument", "tests/StudioSpritePreviewTests.cpp", "PreviewingPutsNothingIntoTheDocument", nullptr, false},
            {"TheDiagnosticsPanelReportsWhatThisBuildIsAndCanDo", "tests/StudioDiagnosticsPanelTests.cpp", "EveryRendererStudioKnowsIsListedWithItsHostTier", nullptr, false},
            {"TheInspectorPreviewsAClipWithTheSettingsTheComponentDeclares", "tests/StudioAudioPreviewTests.cpp", "AnAudioSourcePlaysItsClipWithItsOwnVolumePanAndPitch", nullptr, false},
            {"AnEntityWithNoClipIsToldRatherThanOfferedADeadButton", "tests/StudioAudioPreviewTests.cpp", "AnAudioSourceWithNoClipSaysSoRatherThanOfferingSilence", nullptr, false},
            {"TheBuildPanelExplainsItselfBeforeOfferingToBuild", "tests/StudioBuildPanelTests.cpp", "WithNoProjectThePanelSaysSoRatherThanDrawingAnEmptyForm", nullptr, false},
            {"TheThreeDimensionalViewIsAToggleThatLeavesTheTwoDimensionalCameraAlone", "tests/StudioViewport3DTests.cpp", "TheTwoViewsAreExclusiveAndSayWhichIsShowing", nullptr, false},
            {"TheGridPlaneIsOfferedOnlyWhereItChangesSomething", nullptr, nullptr, "STUDIO-07056", false},
            {"ThreeDimensionalNavigationOrbitsFliesAndPans", "tests/StudioViewport3DTests.cpp", "ADragOrbitsTheCameraWithoutMovingThePivot", nullptr, false},
            {"TheGizmoKeysFlyRatherThanSwitchingManipulatorInTheThreeDimensionalView", "tests/StudioNativeParityTests.cpp", "FlyingWithTheGizmoKeysDoesNotAlsoSwitchTheManipulator", nullptr, false},
            {"TheDigitsSelectTheViewAndKeepWorkingWhileFlying", "tests/StudioNativeParityTests.cpp", "TheDigitsSelectTheViewRatherThanOnlyTheMenu", nullptr, false},
            {"FramingAndPickingFollowWhicheverCameraIsOnScreen", "tests/StudioViewport3DTests.cpp", "TheFirstSwitchToThreeDimensionsFramesTheSceneAndLaterOnesDoNot", nullptr, false},
            {"AProjectsOwnSnapStepWinsOverTheVisibleGrid", "tests/StudioNativeParityTests.cpp", "AProjectsOwnSnapStepWinsOverTheVisibleGrid", nullptr, false},
            {"AThreeDimensionalGizmoDragMovesTheEntityAndUndoesAsOneEntry", nullptr, nullptr, "STUDIO-07050", false},
            {"AThreeDimensionalTurnCarriesAWholeSelectionAboutItsPivot", nullptr, nullptr, "STUDIO-07050", false},
            {"AThreeDimensionalScaleDragResizesTheEntityAndUndoesAsOneEntry", nullptr, nullptr, "STUDIO-07050", false},
            {"AThreeDimensionalDragMovesAWholeSelectionAsOneUndoEntry", nullptr, nullptr, "STUDIO-07050", false},
            {"APerPartMaterialRowIsEditedFieldByField", nullptr, nullptr, "STUDIO-07054", false},

            // `UiTests.cpp`: the Dear ImGui implementation of `StudioUi`, which is the one
            // thing in either file that really is about Dear ImGui. It goes with it.
            {"ImGuiUiProducesValidDrawDataForTheWholeStudio", nullptr, nullptr, nullptr, true},
            {"ImGuiUiRequestsItsFontAtlasThroughTheTextureProtocol", nullptr, nullptr, nullptr, true},
            {"ImGuiUiStaysValidAcrossManyFramesWithInput", nullptr, nullptr, nullptr, true},
            {"ImGuiUiExitsWhenTheWindowIsClosed", nullptr, nullptr, nullptr, true},
            {"ImGuiUiRoutesLogMessagesIntoTheSharedModel", nullptr, nullptr, nullptr, true},
            {"ImGuiUiEmitsAQuadForEveryVisibleGlyph", nullptr, nullptr, nullptr, true},
            {"ImGuiUiRequestsAnUpdateWhenNewGlyphsAppear", nullptr, nullptr, nullptr, true},
        };
        return cases;
    }

    /** @brief The names of every `CNA_STUDIO_TEST` in @p relativePath. */
    std::vector<std::string> testNamesIn(const std::string& relativePath)
    {
        const std::string text = readSource(relativePath);
        std::vector<std::string> names;

        constexpr std::string_view kMarker = "CNA_STUDIO_TEST(";
        std::size_t at = text.find(kMarker);
        while (at != std::string::npos)
        {
            const std::size_t open = at + kMarker.size();
            const std::size_t close = text.find(')', open);
            if (close == std::string::npos) { break; }
            names.push_back(text.substr(open, close - open));
            at = text.find(kMarker, close);
        }
        return names;
    }
}

CNA_STUDIO_TEST(EveryCaseOfThePrototypesSuiteIsAccountedFor)
{
    // The half that catches a case being added to the prototype, or one being renamed, without the
    // accounting following it. A case nobody has answered for is a case that disappears quietly
    // when the file does.
    for (const char* file : {"tests/ApplicationTests.cpp", "tests/UiTests.cpp"})
    {
        for (const std::string& name : testNamesIn(file))
        {
            const bool accounted = std::any_of(
                prototypeCases().begin(), prototypeCases().end(),
                [&name](const PrototypeCase& entry) { return name == entry.name; });
            if (!accounted)
            {
                CnaStudioTest::reportFailure(__FILE__, __LINE__,
                    std::string{file} + "'s '" + name + "' is not in the prototype-case table. "
                    "Deleting that file would delete it with nothing saying so: record where the "
                    "behaviour is covered without the prototype, or the task that will cover it.");
            }
        }
    }

    // And the other way: a row naming a case that is no longer there is a row that has stopped
    // meaning anything.
    const std::vector<std::string> application = testNamesIn("tests/ApplicationTests.cpp");
    const std::vector<std::string> ui = testNamesIn("tests/UiTests.cpp");
    CNA_STUDIO_EXPECT(!application.empty());

    for (const PrototypeCase& entry : prototypeCases())
    {
        const bool exists =
            std::find(application.begin(), application.end(), entry.name) != application.end()
            || std::find(ui.begin(), ui.end(), entry.name) != ui.end();
        if (!exists)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                std::string{"the prototype-case table names '"} + entry.name
                + "', which neither ApplicationTests.cpp nor UiTests.cpp holds any more.");
        }
    }
}

CNA_STUDIO_TEST(EveryAnsweredPrototypeCaseNamesATestThatIsActuallyThere)
{
    // The half that goes stale silently: a case recorded as covered whose cover was renamed or
    // removed. Checked against the file, because a table saying it is covered is exactly what
    // would be wrong.
    for (const PrototypeCase& entry : prototypeCases())
    {
        if (entry.file == nullptr) { continue; }

        const std::string text = readSource(entry.file);
        if (text.empty())
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                std::string{"'"} + entry.name + "' is recorded as covered by " + entry.file
                + ", which does not exist.");
            continue;
        }
        if (text.find(entry.symbol) == std::string::npos)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                std::string{"'"} + entry.name + "' is recorded as covered by '" + entry.symbol
                + "' in " + entry.file + ", which no longer contains it.");
        }
    }
}

CNA_STUDIO_TEST(EveryUnansweredPrototypeCaseNamesTheTaskThatWillCoverIt)
{
    // A gap with no task against it is a gap nobody will close -- and the ids have to be real rows
    // in the plan, checked the same way the Inspector sections' are.
    const std::string phase7 = readSource("plans/phase-07-panel-migration.md");
    const std::string phase35 = readSource("plans/phase-35-polish.md");
    CNA_STUDIO_EXPECT(!phase7.empty());
    CNA_STUDIO_EXPECT(!phase35.empty());

    std::size_t gaps = 0;
    std::size_t goes = 0;
    for (const PrototypeCase& entry : prototypeCases())
    {
        if (entry.goesWithThePrototype) { ++goes; continue; }
        if (entry.file != nullptr) { continue; }
        ++gaps;

        CNA_STUDIO_EXPECT(entry.task != nullptr);
        if (entry.task == nullptr) { continue; }
        if (phase7.find(entry.task) == std::string::npos
            && phase35.find(entry.task) == std::string::npos)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                std::string{"'"} + entry.name + "' names " + entry.task
                + ", which is not a task in Phase 7 or Phase 35.");
        }
    }

    // Stated so that closing one is a deliberate edit rather than something nobody notices, the
    // way the Inspector sections' count is. Seventeen when the accounting was taken.
    CNA_STUDIO_EXPECT_EQ(gaps, std::size_t{15});

    // And the seven that are genuinely about Dear ImGui: they are the only ones the deletion may
    // simply take with it.
    CNA_STUDIO_EXPECT_EQ(goes, std::size_t{7});
}
