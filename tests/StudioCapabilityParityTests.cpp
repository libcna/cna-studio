// SPDX-License-Identifier: MS-PL
/**
 * @file StudioCapabilityParityTests.cpp
 * @brief The prototype's UI capabilities, and what answers each (plan.md STUDIO-07021).
 *
 * `docs/MIGRATION-INVENTORY.md` accounts for the prototype's surface. This accounts for what is
 * underneath it: the `StudioUi` interface every prototype panel is written against. A method with
 * no native answer is a thing the ported panels cannot do, whatever the inventory says about the
 * panel that used it — a panel can be ported, appear as ✅, and still be poorer than the one it
 * replaced because the capability it leaned on does not exist.
 *
 * The strongest check here is the last one: every row names a test, and that test has to *exist*.
 * A parity document is otherwise a list of claims, and the claim that costs nothing to write is
 * exactly the one nobody goes back to substantiate.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/UiCore/StudioShell.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    std::filesystem::path sourceRoot() { return std::filesystem::path{CNA_STUDIO_SOURCE_ROOT}; }

    std::string readFile(const std::string& relative)
    {
        std::ifstream stream{sourceRoot() / relative, std::ios::binary};
        std::stringstream buffer;
        buffer << stream.rdbuf();
        return buffer.str();
    }

    std::string trimmed(std::string text)
    {
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) { text.erase(0, 1); }
        while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
        {
            text.pop_back();
        }
        return text;
    }

    std::string withoutBackticks(std::string text)
    {
        std::string out;
        for (const char character : text) { if (character != '`') { out += character; } }
        return trimmed(std::move(out));
    }

    /** @brief One row of a parity table, with the header it sits under. */
    struct Row
    {
        std::vector<std::string> cells;
        std::size_t line = 0;

        /** @brief The first cell of the header row above it, e.g. `Capability`. */
        std::string table;
    };

    /**
     * @brief Every four-cell row of the document, tagged with the table it belongs to.
     *
     * Tagged rather than guessed at from the cells. The capability table and the docking table are
     * both four columns wide and neither's contents identify it, so a reader that told them apart
     * by what is *in* them would mistake one for the other the first time a row read oddly -- which
     * is exactly what happened, and it reported every prototype panel as a missing `StudioUi`
     * method.
     */
    std::vector<Row> parityRows()
    {
        std::vector<Row> rows;

        std::ifstream stream{sourceRoot() / "docs" / "UI-CAPABILITY-PARITY.md", std::ios::binary};
        std::string line;
        std::size_t number = 0;
        std::string table;

        while (std::getline(stream, line))
        {
            ++number;
            if (line.empty() || line.front() != '|') { continue; }
            if (line.find("---") != std::string::npos) { continue; }

            Row row;
            row.line = number;

            std::stringstream cells{line};
            std::string cell;
            while (std::getline(cells, cell, '|')) { row.cells.push_back(withoutBackticks(cell)); }

            if (!row.cells.empty() && row.cells.front().empty()) { row.cells.erase(row.cells.begin()); }
            while (!row.cells.empty() && row.cells.back().empty()) { row.cells.pop_back(); }

            if (row.cells.size() != 4) { continue; }

            // A header row names its table rather than being one of its rows.
            if (row.cells[0] == "Column" || row.cells[0] == "Capability"
                || row.cells[0] == "Prototype panel")
            {
                table = row.cells[0];
                continue;
            }

            row.table = table;
            rows.push_back(std::move(row));
        }
        return rows;
    }

    /** @brief Splits a capability cell listing several methods, e.g. `beginFrame / endFrame`. */
    std::vector<std::string> methodsOf(const std::string& cell)
    {
        std::vector<std::string> parts;
        std::size_t start = 0;
        for (;;)
        {
            const std::size_t cut = cell.find(" / ", start);
            if (cut == std::string::npos) { parts.push_back(trimmed(cell.substr(start))); break; }
            parts.push_back(trimmed(cell.substr(start, cut - start)));
            start = cut + 3;
        }
        return parts;
    }

    /** @brief Every `virtual` method `StudioUi` declares, in declaration order. */
    std::vector<std::string> studioUiCapabilities()
    {
        const std::string header = readFile("include/CNA/Studio/Ui/StudioUi.hpp");
        const std::size_t start = header.find("class StudioUi");
        if (start == std::string::npos) { return {}; }

        std::vector<std::string> names;
        const std::string body = header.substr(start);

        for (std::size_t at = body.find("virtual"); at != std::string::npos;
             at = body.find("virtual", at + 7))
        {
            // The declared name is the identifier immediately before the argument list. Stopping
            // at `;`, `{` or `=` keeps a default body's contents out of it.
            std::size_t end = at;
            std::size_t paren = std::string::npos;
            for (; end < body.size(); ++end)
            {
                if (body[end] == '(') { paren = end; break; }
                if (body[end] == ';' || body[end] == '{') { break; }
            }
            if (paren == std::string::npos) { continue; }

            std::size_t nameEnd = paren;
            while (nameEnd > at && (body[nameEnd - 1] == ' ' || body[nameEnd - 1] == '\n'))
            {
                --nameEnd;
            }
            std::size_t nameStart = nameEnd;
            while (nameStart > at
                   && (std::isalnum(static_cast<unsigned char>(body[nameStart - 1])) != 0
                       || body[nameStart - 1] == '_'))
            {
                --nameStart;
            }

            const std::string name = body.substr(nameStart, nameEnd - nameStart);
            if (name.empty() || name == "StudioUi") { continue; }

            // The destructor, which is not a capability.
            if (nameStart > 0 && body[nameStart - 1] == '~') { continue; }

            if (std::find(names.begin(), names.end(), name) == names.end())
            {
                names.push_back(name);
            }
        }
        return names;
    }

    /** @brief Every test case name the suite registers, read from the sources. */
    std::set<std::string> registeredTestNames()
    {
        std::set<std::string> names;
        std::error_code code;

        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator{sourceRoot() / "tests", code})
        {
            if (entry.path().extension() != ".cpp") { continue; }

            std::ifstream stream{entry.path(), std::ios::binary};
            std::string line;
            while (std::getline(stream, line))
            {
                const std::size_t at = line.find("CNA_STUDIO_TEST(");
                if (at == std::string::npos) { continue; }
                // The macro's own definition in the harness, not a case.
                if (line.find("#define") != std::string::npos) { continue; }

                const std::size_t open = at + std::string{"CNA_STUDIO_TEST("}.size();
                const std::size_t close = line.find(')', open);
                if (close == std::string::npos) { continue; }
                names.insert(trimmed(line.substr(open, close - open)));
            }
        }
        return names;
    }
}

CNA_STUDIO_TEST(EveryPrototypeUiCapabilityIsAccountedFor)
{
    const std::vector<std::string> capabilities = studioUiCapabilities();

    // An extractor that found nothing would report perfect coverage of nothing.
    CNA_STUDIO_EXPECT(capabilities.size() >= 30);

    std::set<std::string> listed;
    for (const Row& row : parityRows())
    {
        if (row.table != "Capability") { continue; }
        for (const std::string& method : methodsOf(row.cells[0])) { listed.insert(method); }
    }

    for (const std::string& capability : capabilities)
    {
        if (listed.count(capability) == 0)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "StudioUi::" + capability + " is something the prototype's panels can ask for, and "
                "UI-CAPABILITY-PARITY.md does not say what answers it natively -- so nothing has "
                "decided whether the ported panels can still do it.");
        }
    }
}

CNA_STUDIO_TEST(TheParityListNamesNoCapabilityThePrototypeHasNot)
{
    // The other direction. A row for a method that has been removed is a parity claim about an
    // interface nobody is writing against, and it would sit there reading as work done.
    const std::vector<std::string> capabilities = studioUiCapabilities();

    std::size_t checked = 0;
    for (const Row& row : parityRows())
    {
        if (row.table != "Capability") { continue; }
        for (const std::string& method : methodsOf(row.cells[0]))
        {
            ++checked;
            if (std::find(capabilities.begin(), capabilities.end(), method) == capabilities.end())
            {
                CnaStudioTest::reportFailure(__FILE__, __LINE__,
                    "UI-CAPABILITY-PARITY.md line " + std::to_string(row.line) + " lists '" + method
                    + "', which StudioUi does not declare.");
            }
        }
    }
    CNA_STUDIO_EXPECT(checked >= 30);
}

CNA_STUDIO_TEST(EveryProofNamedByTheParityListIsATestThatExists)
{
    // The check that makes this document worth having. Everything else it says is a claim, and the
    // claim that costs nothing to write is the one nobody goes back to substantiate.
    const std::set<std::string> tests = registeredTestNames();
    CNA_STUDIO_EXPECT(tests.size() >= 500);

    std::size_t checked = 0;
    for (const Row& row : parityRows())
    {
        if (row.table != "Capability") { continue; }
        const std::string& proof = row.cells[2];
        if (proof.empty() || proof == "—") { continue; }

        ++checked;
        if (tests.count(proof) == 0)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "UI-CAPABILITY-PARITY.md line " + std::to_string(row.line) + " says '"
                + row.cells[0] + "' is proven by '" + proof + "', which is not a test this suite "
                "registers.");
        }
    }
    CNA_STUDIO_EXPECT(checked >= 25);
}

CNA_STUDIO_TEST(EveryParityRowCarriesAStatusTheLegendDeclares)
{
    for (const Row& row : parityRows())
    {
        if (row.table != "Capability") { continue; }
        const std::string& status = row.cells[3];
        if (status != "✅" && status != "🔄" && status != "⬜")
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "UI-CAPABILITY-PARITY.md line " + std::to_string(row.line) + " has status '"
                + status + "', which the legend does not declare.");
        }

        // A row claiming proof has to be one of the two answered statuses: an unanswered capability
        // cannot have a test proving the answer.
        if (status == "⬜" && !row.cells[2].empty() && row.cells[2] != "—")
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "UI-CAPABILITY-PARITY.md line " + std::to_string(row.line) + " is unanswered and "
                "still names a proof.");
        }
    }
}

// --- Docking parity (plan.md STUDIO-07022) -----------------------------------------------------

CNA_STUDIO_TEST(ThePrototypesDockSidesAreWhereTheNativeDefaultLayoutPutsThem)
{
    // The whole of what the prototype's docking promises a panel is a side: `beginPanel("Inspector",
    // DockSide::Right)`. A user who knows where things are has to still find them there after the
    // migration, so this checks the *resolved geometry* -- where each panel's rectangle actually
    // lands relative to the dock area -- rather than the calls that built the tree. The tree can be
    // right and the layout wrong; only one of the two is what a user looks at.
    const std::string source = readFile("docs/UI-CAPABILITY-PARITY.md");
    CNA_STUDIO_EXPECT(!source.empty());

    CNA::Studio::StudioShell shell{CNA::Studio::StudioTheme::dark()};
    shell.resetLayout();

    CNA::Studio::UiInputState input;
    input.displayWidth = 1600.0f;
    input.displayHeight = 900.0f;
    input.mouseX = -1.0f;
    input.mouseY = -1.0f;
    input.mouseInWindow = false;
    shell.renderFrame(input);

    const CNA::Studio::UiRect viewport = shell.panelBounds("viewport");
    CNA_STUDIO_EXPECT(!viewport.isEmpty());
    if (viewport.isEmpty()) { return; }

    std::size_t checked = 0;
    for (const Row& row : parityRows())
    {
        if (row.table != "Prototype panel") { continue; }

        const std::string& side = row.cells[1];

        // The *group's* rectangle, not the panel's own. `panelBounds` answers only for the tab in
        // front, and every one of these panels shares a strip with others -- so asking it would
        // report six of the ten as placed nowhere, which is a fact about tabs rather than about
        // where the panel lives.
        const std::string& id = row.cells[2];
        const CNA::Studio::StudioDockNodeId leaf = shell.dockTree().findPanel(id);
        const CNA::Studio::UiRect bounds = leaf == CNA::Studio::kInvalidDockNode
            ? CNA::Studio::UiRect{}
            : shell.dockTree().node(leaf).bounds;
        if (bounds.isEmpty())
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "UI-CAPABILITY-PARITY.md line " + std::to_string(row.line) + " pairs '"
                + row.cells[0] + "' with the '" + id + "' panel, which the default layout does not "
                "place anywhere.");
            continue;
        }

        ++checked;

        // Measured against the centre panel rather than against the window, so the assertion is
        // "beside the viewport" -- which is what a side *means* -- rather than a fraction that
        // would have to be rewritten whenever the default proportions were tuned.
        bool correct = false;
        if (side == "Left") { correct = bounds.right() <= viewport.left() + 1.0f; }
        else if (side == "Right") { correct = bounds.left() >= viewport.right() - 1.0f; }
        else if (side == "Bottom") { correct = bounds.top() >= viewport.bottom() - 1.0f; }
        else { correct = bounds.contains(viewport.centerX(), viewport.centerY()); }

        if (!correct)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "the prototype opens '" + row.cells[0] + "' on the " + side + ", and the native '"
                + id + "' panel is not there -- a user who knows where it is would find it "
                "somewhere else after the migration.");
        }
    }

    // A table whose rows all stopped parsing would otherwise pass by checking nothing.
    CNA_STUDIO_EXPECT(checked >= 9);
}
