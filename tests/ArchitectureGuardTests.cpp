// SPDX-License-Identifier: MS-PL
/**
 * @file ArchitectureGuardTests.cpp
 * @brief Makes the architecture's invariants enforceable by machinery rather than by review.
 *
 * `plan.md` STUDIO-02032, STUDIO-02033, STUDIO-02034, and `docs/ARCHITECTURE.md` §10.
 *
 * Every rule here is one that a comment has never once prevented anyone from breaking. They are
 * checked by scanning the source tree, which is crude but has the property that matters: a
 * violation fails the build on the commit that introduces it, when it is cheap to fix, rather than
 * being found months later by someone wondering why Studio will not build on a new renderer.
 *
 * A scan can produce a false positive -- the word `vkCreateDevice` inside a comment explaining why
 * Studio must not call it, for instance. Each check therefore ignores comments and strings where
 * that matters, and the failure message names the file, the line and the rule, so a genuine
 * exception can be made deliberately rather than by loosening the pattern until it stops
 * complaining.
 */

#include "SourceScan.hpp"
#include "TestHarness.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <cstring>
#include <map>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using CnaStudioTest::Scan::SourceFile;
    using CnaStudioTest::Scan::collectSources;
    using CnaStudioTest::Scan::lineOf;
    using CnaStudioTest::Scan::sourceRoot;
    using CnaStudioTest::Scan::stripCommentsAndStrings;

    /**
     * @brief Fails the current test for every occurrence of @p needle in Studio's own code.
     *
     * @param subdirectories Directories to scan.
     * @param needle Forbidden text.
     * @param rule Why it is forbidden, included in the failure so the message is actionable.
     * @return Number of violations found.
     */
    std::size_t expectAbsent(const std::vector<std::string>& subdirectories,
                             std::string_view needle, std::string_view rule)
    {
        std::size_t violations = 0;
        for (const SourceFile& file : collectSources(subdirectories))
        {
            const std::string code = stripCommentsAndStrings(file.text);
            std::size_t position = code.find(needle);
            while (position != std::string::npos)
            {
                ++violations;
                CnaStudioTest::reportFailure(__FILE__, __LINE__,
                    file.relativePath + ":" + std::to_string(lineOf(code, position)) + " contains '"
                    + std::string{needle} + "'. " + std::string{rule});
                position = code.find(needle, position + 1);
            }
        }
        return violations;
    }
} // namespace

CNA_STUDIO_TEST(TheGuardScanCanActuallySeeTheSourceTree)
{
    // Every check below silently passes if the scan finds no files, which would make this whole
    // file a very convincing no-op. This is the check that the checks are running.
    CNA_STUDIO_EXPECT(!sourceRoot().empty());
    const std::vector<SourceFile> files = collectSources({"src", "include"});
    CNA_STUDIO_EXPECT(files.size() > 100);
}

CNA_STUDIO_TEST(TheGuardScanIgnoresCommentsAndStrings)
{
    // Proves the stripper works, so a failure below is a real violation and not a comment
    // mentioning the thing it forbids. Without this, the fix people reach for is deleting the
    // explanatory comment.
    const std::string sample =
        "int a; // CNA::Internal::Foo\n"
        "/* CNA::Internal::Bar */\n"
        "const char* s = \"CNA::Internal::Baz\";\n"
        "int CNA_Internal_real;\n";
    const std::string stripped = stripCommentsAndStrings(sample);

    CNA_STUDIO_EXPECT(stripped.find("CNA::Internal") == std::string::npos);
    CNA_STUDIO_EXPECT(stripped.find("CNA_Internal_real") != std::string::npos);
    // Line numbers must survive, or every failure message points at the wrong place.
    CNA_STUDIO_EXPECT_EQ(std::count(stripped.begin(), stripped.end(), '\n'),
                         std::count(sample.begin(), sample.end(), '\n'));
}

CNA_STUDIO_TEST(NoStudioCodeReachesIntoCnaInternals)
{
    // `docs/ARCHITECTURE.md` §1: Studio uses CNA's public API only. Reaching into CNA::Internal
    // would hide a CNA gap instead of reporting it, and the gaps register is the whole point of
    // Studio being one of CNA's largest consumers.
    CNA_STUDIO_EXPECT_EQ(expectAbsent({"src", "include"}, "CNA::Internal",
        "Studio uses CNA's public API only. If CNA cannot do what is needed, file it in "
        "docs/CNA-GAPS.md rather than reaching past the API."), std::size_t{0});
}

CNA_STUDIO_TEST(NoStudioCodeCallsAGraphicsBackendDirectly)
{
    // Backend-specific behaviour belongs in CNA. A direct call here would make Studio need
    // per-renderer source, which is exactly the thing the capability contract exists to avoid.
    const std::vector<std::string> scan{"src", "include"};
    const std::string rule =
        "Backend-specific graphics code belongs in CNA, not in Studio. If Studio needs it, that is "
        "a missing CNA abstraction -- file it in docs/CNA-GAPS.md.";

    std::size_t violations = 0;
    for (const char* symbol : {"vkCreate", "vkCmd", "vkQueue",
                               "ID3D11Device", "ID3D12Device", "IDirect3D",
                               "glGenBuffers", "glDrawArrays", "glDrawElements", "glBindTexture",
                               "MTLDevice", "wgpuDevice"})
    {
        violations += expectAbsent(scan, symbol, rule);
    }
    CNA_STUDIO_EXPECT_EQ(violations, std::size_t{0});
}

CNA_STUDIO_TEST(NoStudioCodeIncludesABackendHeader)
{
    const std::vector<std::string> scan{"src", "include"};
    std::size_t violations = 0;
    for (const char* header : {"<vulkan/", "<d3d11.h>", "<d3d12.h>", "<GL/gl.h>",
                               "<GLES3/", "<Metal/", "<webgpu/"})
    {
        violations += expectAbsent(scan, header,
            "Only CNA may include a graphics backend's headers.");
    }
    CNA_STUDIO_EXPECT_EQ(violations, std::size_t{0});
}

CNA_STUDIO_TEST(OnlyTheTwoCnaLinkedModulesIncludeCnaHeaders)
{
    // Enforced by the build graph already -- a stray include elsewhere fails to link. Stated here
    // as a test so the property is asserted rather than inferred from a linker error, and so the
    // failure names the rule instead of naming a missing symbol.
    //
    // Two modules, since STUDIO-04001 split the UI GPU renderer out of the viewport: they answer
    // different questions (docs/UI-RENDER-PATH.md layers 2 and 3) and were only together by
    // history. Two is still a closed list -- adding a third means editing this line, which is
    // exactly the review this guard is for.
    std::size_t violations = 0;
    for (const SourceFile& file : collectSources({"src", "include"}))
    {
        const bool isCnaLinked = file.relativePath.find("viewport") != std::string::npos
                              || file.relativePath.find("Viewport") != std::string::npos
                              || file.relativePath.find("ui-renderer") != std::string::npos
                              || file.relativePath.find("UiRenderer") != std::string::npos;
        if (isCnaLinked) { continue; }

        const std::string code = stripCommentsAndStrings(file.text);
        for (const char* cnaInclude : {"<Microsoft/Xna/", "\"Microsoft/Xna/",
                                       "<CNA/Graphics/", "<CNA/Platform/"})
        {
            const std::size_t position = code.find(cnaInclude);
            if (position != std::string::npos)
            {
                ++violations;
                CnaStudioTest::reportFailure(__FILE__, __LINE__,
                    file.relativePath + ":" + std::to_string(lineOf(code, position))
                    + " includes a CNA header. Only cna-studio-viewport and cna-studio-ui-renderer "
                      "may link CNA; everything else stays CNA-free so it can be tested with no "
                      "GPU.");
            }
        }
    }
    CNA_STUDIO_EXPECT_EQ(violations, std::size_t{0});
}

CNA_STUDIO_TEST(TheNativeStudioUiHasNoDearImGuiDependency)
{
    // The end state of the UI migration is that production Studio UI does not depend on Dear
    // ImGui at all (STUDIO-07099). That cannot be asserted for the whole application yet -- the
    // legacy panels are still ImGui -- but it can be asserted for the new UI from its first
    // commit, which is what stops the dependency creeping back in as panels are ported.
    std::size_t violations = 0;
    for (const SourceFile& file : collectSources({"src/ui-core", "include/CNA/Studio/UiCore"}))
    {
        const std::string code = stripCommentsAndStrings(file.text);
        for (const char* imgui : {"imgui.h", "ImGui::", "ImDrawList", "ImVec2"})
        {
            const std::size_t position = code.find(imgui);
            if (position != std::string::npos)
            {
                ++violations;
                CnaStudioTest::reportFailure(__FILE__, __LINE__,
                    file.relativePath + ":" + std::to_string(lineOf(code, position))
                    + " depends on Dear ImGui. The native Studio UI replaces it and must not "
                      "acquire a dependency on it.");
            }
        }
    }
    CNA_STUDIO_EXPECT_EQ(violations, std::size_t{0});
}

CNA_STUDIO_TEST(ThePlayerDependsOnNoUiRenderBackend)
{
    // `plan.md` STUDIO-04027. The player runs the user's game and draws no editor UI at all, so it
    // has no business linking one of Studio's UI render backends -- and for six phases it linked
    // the classic one, for a single static that answered a different question: which *CNA*
    // renderer the binary was compiled against.
    //
    // That is the conflation `docs/UI-RENDER-PATH.md` exists to name. It matters beyond tidiness:
    // a UI backend is choosable and deletable (this is the task that deletes one), and a game
    // runtime that depends on which one Studio picked is a game runtime that has to be rebuilt
    // when the editor changes its mind. `studioHostCnaRendererName()` answers layer 3 and nothing
    // else, which is why the player can call it and this can forbid the rest.
    const std::size_t violations =
        expectAbsent({"src/player"}, "CnaUiRenderer",
                     "The player draws no editor UI. If it needs which CNA renderer this build "
                     "uses, that is studioHostCnaRendererName() in UiRenderer/StudioHostRenderer.hpp.")
        + expectAbsent({"src/player"}, "StudioModernUiRenderer",
                       "The player draws no editor UI.")
        + expectAbsent({"src/player"}, "StudioUiRenderBackend",
                       "The player draws no editor UI.");
    CNA_STUDIO_EXPECT_EQ(violations, std::size_t{0});
}

CNA_STUDIO_TEST(TheNativeStudioUiIsCnaFree)
{
    // The property that keeps the UI workstream testable: layout, identity, focus and hit-testing
    // are all decided by code that runs in CI with no GPU. Only the pixels need a device.
    std::size_t violations = 0;
    for (const SourceFile& file : collectSources({"src/ui-core", "include/CNA/Studio/UiCore"}))
    {
        const std::string code = stripCommentsAndStrings(file.text);
        for (const char* cna : {"Microsoft/Xna/", "CNA/Graphics/", "CNA/Platform/"})
        {
            const std::size_t position = code.find(cna);
            if (position != std::string::npos)
            {
                ++violations;
                CnaStudioTest::reportFailure(__FILE__, __LINE__,
                    file.relativePath + ":" + std::to_string(lineOf(code, position))
                    + " links CNA. cna-studio-ui-core must stay CNA-free and headless-testable.");
            }
        }
    }
    CNA_STUDIO_EXPECT_EQ(violations, std::size_t{0});
}

CNA_STUDIO_TEST(EverySourceFileCarriesItsLicenceIdentifier)
{
    // A house rule matching CNA's own, and the kind that decays silently without a check.
    std::size_t violations = 0;
    for (const SourceFile& file : collectSources({"src", "include", "tests"}))
    {
        if (file.text.find("SPDX-License-Identifier") == std::string::npos)
        {
            ++violations;
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                file.relativePath + " has no SPDX-License-Identifier header.");
        }
    }
    CNA_STUDIO_EXPECT_EQ(violations, std::size_t{0});
}

CNA_STUDIO_TEST(NoTwoPublicHeadersDeclareTheSameTypeName)
{
    // Everything public lives in one namespace, CNA::Studio, so two headers declaring the same
    // type name is an ODR violation: each translation unit believes whichever definition it saw,
    // and the two disagree about layout.
    //
    // This guard exists because that happened. A second CNA::Studio::StudioCommand -- the undoable
    // document mutation already had the name -- compiled cleanly, linked cleanly, and corrupted
    // memory at run time. It presented as a std::string destructor freeing a pointer into the data
    // segment, in a test hundreds of cases away from either definition, and moved when unrelated
    // code changed the allocation pattern. Nothing about the symptom pointed at the cause.
    //
    // The scan looks for definitions at namespace indentation (four spaces), which is this
    // codebase's convention. Nested types are indented further and are correctly ignored: they are
    // scoped by their enclosing type and cannot collide.
    std::map<std::string, std::string> declaredIn;
    std::size_t violations = 0;

    for (const SourceFile& file : collectSources({"include"}))
    {
        const std::string code = stripCommentsAndStrings(file.text);
        std::size_t lineStart = 0;

        // Names are qualified by their enclosing namespace before being compared. Without this the
        // guard reports CNA::Studio::SceneLoadResult and CNA::Studio::Runtime::SceneLoadResult as
        // a collision, which they are not -- and a guard that cries wolf gets switched off.
        std::string currentNamespace;

        while (lineStart < code.size())
        {
            const std::size_t lineEnd = std::min(code.find('\n', lineStart), code.size());
            const std::string line = code.substr(lineStart, lineEnd - lineStart);
            lineStart = lineEnd + 1;

            if (line.rfind("namespace ", 0) == 0)
            {
                std::size_t end = 10;
                while (end < line.size()
                       && (std::isalnum(static_cast<unsigned char>(line[end])) != 0
                           || line[end] == '_' || line[end] == ':'))
                {
                    ++end;
                }
                const std::string name = line.substr(10, end - 10);
                // An anonymous or extension namespace block re-opening the same scope keeps it.
                if (!name.empty()) { currentNamespace = name; }
                continue;
            }

            if (line.rfind("    ", 0) != 0 || line.size() < 10) { continue; }
            if (line[4] == ' ') { continue; }   // nested: indented deeper

            std::string keyword;
            std::size_t nameStart = 0;
            for (const char* candidate : {"class ", "struct ", "enum class "})
            {
                const std::string prefix = std::string{"    "} + candidate;
                if (line.rfind(prefix, 0) == 0) { keyword = candidate; nameStart = prefix.size(); break; }
            }
            if (keyword.empty()) { continue; }

            std::size_t nameEnd = nameStart;
            while (nameEnd < line.size()
                   && (std::isalnum(static_cast<unsigned char>(line[nameEnd])) != 0
                       || line[nameEnd] == '_'))
            {
                ++nameEnd;
            }
            if (nameEnd == nameStart) { continue; }

            const std::string name = currentNamespace + "::" + line.substr(nameStart, nameEnd - nameStart);

            // A forward declaration repeats a name legitimately; only definitions collide.
            const std::string rest = line.substr(nameEnd);
            if (rest.find(';') != std::string::npos && rest.find('{') == std::string::npos)
            {
                continue;
            }

            const auto existing = declaredIn.find(name);
            if (existing != declaredIn.end() && existing->second != file.relativePath)
            {
                ++violations;
                CnaStudioTest::reportFailure(__FILE__, __LINE__,
                    "type '" + name + "' is defined in both " + existing->second + " and "
                    + file.relativePath + ". Both are in namespace CNA::Studio, so this is an ODR "
                      "violation: it compiles, links, and corrupts memory at run time.");
            }
            else
            {
                declaredIn[name] = file.relativePath;
            }
        }
    }

    // The scan must actually have found types, or it passes by finding nothing.
    CNA_STUDIO_EXPECT(declaredIn.size() > 50);
    CNA_STUDIO_EXPECT_EQ(violations, std::size_t{0});
}

CNA_STUDIO_TEST(NoStudioCodeHardCodesARendererName)
{
    // `docs/ARCHITECTURE.md` §2.2 and the roadmap's rule against hard-coding today's renderer
    // count: classification lives in one place, and scattered `if (name == "vulkan")` comparisons
    // are how adding a CNA renderer becomes an archaeology exercise.
    //
    // The catalogue itself is where the names legitimately live, so it is exempt -- an exemption
    // stated here rather than achieved by writing a pattern that happens not to match it.
    std::size_t violations = 0;
    for (const SourceFile& file : collectSources({"src", "include"}))
    {
        if (file.relativePath.find("RendererCatalog") != std::string::npos) { continue; }

        const std::string code = stripCommentsAndStrings(file.text);
        for (const char* comparison : {"== \"vulkan\"", "== \"easygl\"", "== \"directx11\"",
                                       "== \"opengl33\"", "== \"metal\""})
        {
            const std::size_t position = code.find(comparison);
            if (position != std::string::npos)
            {
                ++violations;
                CnaStudioTest::reportFailure(__FILE__, __LINE__,
                    file.relativePath + ":" + std::to_string(lineOf(code, position))
                    + " compares against a renderer name. Ask the catalogue in "
                      "CNA/Studio/Project/RendererCatalog.hpp instead.");
            }
        }
    }
    CNA_STUDIO_EXPECT_EQ(violations, std::size_t{0});
}

// -------------------------------------------------------------------------------------------
// Plan integrity
// -------------------------------------------------------------------------------------------
//
// The roadmap is only worth reading if its status is true, and the failure mode is not dishonesty
// but drift: a task gets its tick, the phase file's own header and `plan.md`'s table keep the
// number they had, and the discrepancy survives because nobody adds up a column by hand. These
// checks add it up. They assert arithmetic, never judgement -- whether a ✅ is *deserved* is a
// question no test can answer, and pretending otherwise would be worse than not checking.

namespace
{
    /** @brief One row of a phase file's task table. */
    struct PlanTask
    {
        std::string id;
        std::string status;
    };

    /** @brief Reads a whole file, or returns an empty string when it is not there. */
    std::string readFileOrEmpty(const std::filesystem::path& path)
    {
        std::ifstream stream{path, std::ios::binary};
        if (!stream) { return {}; }
        return std::string{std::istreambuf_iterator<char>{stream},
                           std::istreambuf_iterator<char>{}};
    }

    /** @brief Splits text into lines, dropping the line terminators. */
    std::vector<std::string> splitLines(const std::string& text)
    {
        std::vector<std::string> lines;
        std::string current;
        for (const char character : text)
        {
            if (character == '\n') { lines.push_back(current); current.clear(); }
            else if (character != '\r') { current.push_back(character); }
        }
        if (!current.empty()) { lines.push_back(current); }
        return lines;
    }

    /** @brief The cells of a Markdown table row, trimmed, or empty when the line is not one. */
    std::vector<std::string> tableCells(const std::string& line)
    {
        if (line.size() < 2 || line.front() != '|') { return {}; }

        std::vector<std::string> cells;
        std::string current;
        for (std::size_t index = 1; index < line.size(); ++index)
        {
            if (line[index] == '|') { cells.push_back(current); current.clear(); }
            else { current.push_back(line[index]); }
        }

        for (std::string& cell : cells)
        {
            const std::size_t first = cell.find_first_not_of(" \t");
            const std::size_t last = cell.find_last_not_of(" \t");
            cell = (first == std::string::npos) ? std::string{} : cell.substr(first, last - first + 1);
        }
        return cells;
    }

    /** @brief Strips the backticks Markdown uses to set an id in code style. */
    std::string withoutBackticks(std::string text)
    {
        text.erase(std::remove(text.begin(), text.end(), '`'), text.end());
        return text;
    }

    /**
     * @brief Reads the task rows of one phase file.
     *
     * A phase file's table is the authority on that phase: `plan.md` summarises it, and the
     * summary is what drifts.
     *
     * @param path The phase file.
     * @return Every `| `STUDIO-NNNNN` | … | status | … |` row, in file order.
     */
    std::vector<PlanTask> readPhaseTasks(const std::filesystem::path& path)
    {
        std::vector<PlanTask> tasks;
        for (const std::string& line : splitLines(readFileOrEmpty(path)))
        {
            const std::vector<std::string> cells = tableCells(line);
            if (cells.size() < 3) { continue; }

            const std::string id = withoutBackticks(cells[0]);
            if (id.rfind("STUDIO-", 0) != 0 || id.size() != 12) { continue; }
            if (id.find_first_not_of("0123456789", 7) != std::string::npos) { continue; }

            tasks.push_back(PlanTask{id, cells[2]});
        }
        return tasks;
    }
}

CNA_STUDIO_TEST(EveryPhaseFileAgreesWithItsOwnProgressHeader)
{
    // The header line each phase file carries above its table. It is written by hand and read by
    // everyone, which is the worst combination a number can have.
    std::size_t phasesChecked = 0;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator{sourceRoot() / "plans"})
    {
        if (entry.path().extension() != ".md") { continue; }

        const std::string text = readFileOrEmpty(entry.path());
        const std::string relative = "plans/" + entry.path().filename().string();

        const std::vector<PlanTask> tasks = readPhaseTasks(entry.path());
        if (tasks.empty()) { continue; }
        ++phasesChecked;

        const auto complete = static_cast<std::size_t>(
            std::count_if(tasks.begin(), tasks.end(),
                          [](const PlanTask& task) { return task.status == "✅"; }));

        const std::size_t headerStart = text.find("**Progress:** ");
        if (headerStart == std::string::npos)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                relative + " has a task table but no '**Progress:** N of M complete' header.");
            continue;
        }

        const std::string header =
            text.substr(headerStart, text.find('\n', headerStart) - headerStart);

        const std::string expected = "**Progress:** " + std::to_string(complete) + " of "
                                   + std::to_string(tasks.size()) + " complete";
        if (header.rfind(expected, 0) != 0)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                relative + " says '" + header + "' but its table holds " + std::to_string(tasks.size())
                + " tasks of which " + std::to_string(complete) + " are ✅. Expected it to start '"
                + expected + "'.");
        }
    }

    // A scan that found no phase files would report perfect agreement.
    CNA_STUDIO_EXPECT(phasesChecked >= 30);
}

CNA_STUDIO_TEST(TheMasterPlanTableAgreesWithEveryPhaseFile)
{
    // `plan.md`'s phase table is a summary of thirty-six files nobody re-reads when ticking a box
    // in one of them, so it is the number most likely to be wrong and the one most likely to be
    // quoted. Each row is checked against the file it links to, and the totals against the rows.
    std::size_t rowsChecked = 0;
    std::size_t totalTasks = 0;
    std::size_t totalComplete = 0;
    std::size_t declaredTotal = 0;

    for (const std::string& line : splitLines(readFileOrEmpty(sourceRoot() / "plan.md")))
    {
        const std::vector<std::string> cells = tableCells(line);

        if (cells.size() >= 2 && cells[0] == "**Total**")
        {
            declaredTotal = static_cast<std::size_t>(
                std::stoul(withoutBackticks(cells[1]).substr(2)));
            continue;
        }

        // | N | [Name](plans/phase-NN-….md) | `STUDIO-NNNNN` | status | tasks | complete | bar |
        if (cells.size() < 6) { continue; }
        const std::size_t linkStart = cells[1].find("(plans/");
        if (linkStart == std::string::npos) { continue; }

        const std::size_t linkEnd = cells[1].find(')', linkStart);
        const std::string relative =
            cells[1].substr(linkStart + 1, linkEnd - linkStart - 1);

        std::size_t declaredTasksInRow = 0;
        std::size_t declaredCompleteInRow = 0;
        try
        {
            declaredTasksInRow = static_cast<std::size_t>(std::stoul(cells[4]));
            declaredCompleteInRow = static_cast<std::size_t>(std::stoul(cells[5]));
        }
        catch (const std::exception&)
        {
            continue;
        }

        ++rowsChecked;
        totalTasks += declaredTasksInRow;
        totalComplete += declaredCompleteInRow;

        const std::vector<PlanTask> tasks = readPhaseTasks(sourceRoot() / relative);
        const auto complete = static_cast<std::size_t>(
            std::count_if(tasks.begin(), tasks.end(),
                          [](const PlanTask& task) { return task.status == "✅"; }));

        if (tasks.size() != declaredTasksInRow || complete != declaredCompleteInRow)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "plan.md says " + relative + " holds " + std::to_string(declaredTasksInRow)
                + " tasks with " + std::to_string(declaredCompleteInRow) + " complete, but the file "
                  "holds " + std::to_string(tasks.size()) + " with " + std::to_string(complete)
                + " complete.");
        }
    }

    CNA_STUDIO_EXPECT(rowsChecked >= 30);
    CNA_STUDIO_EXPECT_EQ(declaredTotal, totalTasks);

    // The headline figure, which is the one that ends up in a commit message or a status report.
    const std::string headline =
        std::to_string(totalComplete) + " of " + std::to_string(totalTasks) + " tasks complete";
    const std::string plan = readFileOrEmpty(sourceRoot() / "plan.md");
    if (plan.find("**" + headline + "**") == std::string::npos)
    {
        CnaStudioTest::reportFailure(__FILE__, __LINE__,
            "plan.md's headline does not read '**" + headline
            + "**', which is what its own phase table adds up to.");
    }
}

CNA_STUDIO_TEST(ThePlansStatusBreakdownAddsUpAndMatchesThePhaseFiles)
{
    // Found stale, by five tasks and by a total that did not add up to its own bottom row:
    // 162 + 10 + 310 + 2 + 4 is 488 under a header saying 490. Nothing checked it, because the
    // guard above checks the *phase table* and the headline and stops there -- so this table sat
    // beside a checked one looking exactly as authoritative and being wrong.
    static const std::pair<const char*, const char*> kRows[] = {
        {"✅", "| ✅ Complete | "},
        {"🔄", "| 🔄 In progress | "},
        {"⬜", "| ⬜ Not started | "},
        {"⛔", "| ⛔ Deferred | "},
        {"🔬", "| 🔬 Blocked | "},
    };

    std::map<std::string, std::size_t> actual;
    std::size_t totalTasks = 0;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator{sourceRoot() / "plans"})
    {
        if (entry.path().extension() != ".md") { continue; }
        const std::string name = entry.path().filename().string();
        if (name.size() < 8 || name.rfind("phase-", 0) != 0) { continue; }

        for (const PlanTask& task : readPhaseTasks(entry.path()))
        {
            ++actual[task.status];
            ++totalTasks;
        }
    }

    const std::string plan = readFileOrEmpty(sourceRoot() / "plan.md");
    std::size_t declaredSum = 0;
    for (const auto& [symbol, prefix] : kRows)
    {
        const std::size_t at = plan.find(prefix);
        if (at == std::string::npos)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                std::string{"plan.md has no status row starting '"} + prefix + "'.");
            continue;
        }
        const std::size_t declared =
            static_cast<std::size_t>(std::stoul(plan.substr(at + std::strlen(prefix))));
        declaredSum += declared;
        if (declared != actual[symbol])
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                std::string{"plan.md's status breakdown says "} + std::to_string(declared) + " "
                + symbol + " tasks; the phase files hold " + std::to_string(actual[symbol]) + ".");
        }
    }

    // And the column adds up to its own total, which is a separate failure: a breakdown can have
    // every row right and a bottom line that was typed rather than summed.
    CNA_STUDIO_EXPECT_EQ(declaredSum, totalTasks);
    if (plan.find("| **Total** | **" + std::to_string(totalTasks) + "** |") == std::string::npos)
    {
        CnaStudioTest::reportFailure(__FILE__, __LINE__,
            "plan.md's status breakdown does not total " + std::to_string(totalTasks) + ".");
    }
}

CNA_STUDIO_TEST(TheHandoffsOwnArithmeticMatchesThePhaseFiles)
{
    // The handoff is what somebody reads first, and a count in it that is one session stale is
    // worse than no count: it is a number they will quote. plan.md's arithmetic is already checked
    // against the phase files above; this checks the handoff against the same source, so the two
    // cannot say different things about the same day's work.
    //
    // Deliberately only the *numbers*. The prose is a judgement about what was built and no test
    // can hold it to anything -- but a headline saying "140 of 479" while the plan says 159 of 486
    // is a fact, and facts are checkable.
    std::size_t totalTasks = 0;
    std::size_t totalComplete = 0;
    std::map<int, std::pair<std::size_t, std::size_t>> perPhase;

    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator{sourceRoot() / "plans"})
    {
        if (entry.path().extension() != ".md") { continue; }

        const std::string name = entry.path().filename().string();
        if (name.size() < 8 || name.rfind("phase-", 0) != 0) { continue; }
        const int phase = std::stoi(name.substr(6, 2));

        const std::vector<PlanTask> tasks = readPhaseTasks(entry.path());
        const auto complete = static_cast<std::size_t>(
            std::count_if(tasks.begin(), tasks.end(),
                          [](const PlanTask& task) { return task.status == "✅"; }));

        totalTasks += tasks.size();
        totalComplete += complete;
        perPhase[phase] = {tasks.size(), complete};
    }

    const std::string handoff = readFileOrEmpty(sourceRoot() / "HANDOFF.md");
    CNA_STUDIO_EXPECT(!handoff.empty());

    const std::string headline = "**" + std::to_string(totalComplete) + " of "
                               + std::to_string(totalTasks) + " tasks are complete.**";
    if (handoff.find(headline) == std::string::npos)
    {
        CnaStudioTest::reportFailure(__FILE__, __LINE__,
            "HANDOFF.md's headline does not read '" + headline
            + "', which is what the phase files add up to.");
    }

    // Every "**Phase N — Name** (C of T)" it claims, against what that phase file holds. Only the
    // phases it mentions: the handoff summarises what has been worked on rather than listing all
    // thirty-six, and demanding a line for a phase nobody has started would be noise.
    std::size_t phrasesChecked = 0;
    for (const auto& [phase, counts] : perPhase)
    {
        const std::string marker = "**Phase " + std::to_string(phase) + " \u2014 ";
        std::size_t at = handoff.find(marker);
        if (at == std::string::npos) { continue; }

        const std::size_t open = handoff.find('(', at);
        const std::size_t close = handoff.find(')', open);
        if (open == std::string::npos || close == std::string::npos) { continue; }

        const std::string claim = handoff.substr(open + 1, close - open - 1);
        const std::string expected = std::to_string(counts.second) + " of "
                                   + std::to_string(counts.first);
        ++phrasesChecked;

        if (claim.rfind(expected, 0) != 0)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "HANDOFF.md says Phase " + std::to_string(phase) + " is '" + claim
                + "', but its phase file holds " + expected + ".");
        }
    }

    // A handoff that mentioned no phase at all would pass every check above by saying nothing.
    CNA_STUDIO_EXPECT(phrasesChecked >= 8);
}

CNA_STUDIO_TEST(NoTaskIdIsUsedTwiceAcrossTheWholePlan)
{
    // Ids are promised to be stable and never reused (plan.md, 'Id scheme'). A collision breaks
    // every reference to the id -- in commit messages, in code comments, in this test suite -- and
    // is invisible until someone follows one of them to the wrong task.
    std::map<std::string, std::string> seenIn;
    std::size_t collisions = 0;

    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator{sourceRoot() / "plans"})
    {
        if (entry.path().extension() != ".md") { continue; }
        const std::string relative = "plans/" + entry.path().filename().string();

        std::map<std::string, std::size_t> countsInThisFile;
        for (const PlanTask& task : readPhaseTasks(entry.path()))
        {
            ++countsInThisFile[task.id];

            const auto existing = seenIn.find(task.id);
            if (existing != seenIn.end() && existing->second != relative)
            {
                ++collisions;
                CnaStudioTest::reportFailure(__FILE__, __LINE__,
                    "task id " + task.id + " appears in both " + existing->second + " and "
                    + relative + ". Ids are never reused.");
            }
            seenIn[task.id] = relative;
        }

        for (const auto& [id, count] : countsInThisFile)
        {
            if (count > 1)
            {
                ++collisions;
                CnaStudioTest::reportFailure(__FILE__, __LINE__,
                    "task id " + id + " has " + std::to_string(count) + " rows in " + relative + ".");
            }
        }

        // A task's id must belong to the phase whose file it lives in, or the id scheme's promise
        // that `STUDIO-06020` is phase 6's twentieth task means nothing.
        const std::string stem = entry.path().filename().string();
        if (stem.rfind("phase-", 0) == 0)
        {
            const std::string phaseNumber = stem.substr(6, 2);
            for (const PlanTask& task : readPhaseTasks(entry.path()))
            {
                if (task.id.substr(7, 2) != phaseNumber)
                {
                    ++collisions;
                    CnaStudioTest::reportFailure(__FILE__, __LINE__,
                        "task " + task.id + " lives in " + relative
                        + ", whose ids must all start STUDIO-" + phaseNumber + ".");
                }
            }
        }
    }

    CNA_STUDIO_EXPECT(seenIn.size() > 300);
    CNA_STUDIO_EXPECT_EQ(collisions, std::size_t{0});
}
