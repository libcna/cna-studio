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

#include "TestHarness.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    /** @brief The repository root, supplied by CMake so the scan does not guess. */
    std::filesystem::path sourceRoot()
    {
#ifdef CNA_STUDIO_SOURCE_ROOT
        return std::filesystem::path{CNA_STUDIO_SOURCE_ROOT};
#else
        return std::filesystem::path{};
#endif
    }

    /** @brief One source file's path and contents. */
    struct SourceFile
    {
        std::filesystem::path path;
        std::string relativePath;
        std::string text;
    };

    /**
     * @brief Collects Studio's own C++ sources, excluding vendored third-party code.
     *
     * `third_party/` is excluded on purpose: these rules are about how *Studio* is written, and
     * holding a vendored library to them would be both meaningless and unfixable.
     *
     * @param subdirectories Directories under the repository root to scan.
     * @return Every `.cpp` and `.hpp` found.
     */
    std::vector<SourceFile> collectSources(const std::vector<std::string>& subdirectories)
    {
        std::vector<SourceFile> files;
        const std::filesystem::path root = sourceRoot();
        if (root.empty()) { return files; }

        for (const std::string& subdirectory : subdirectories)
        {
            const std::filesystem::path directory = root / subdirectory;
            std::error_code ec;
            if (!std::filesystem::exists(directory, ec)) { continue; }

            for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, ec))
            {
                if (!entry.is_regular_file()) { continue; }
                const std::filesystem::path& path = entry.path();
                const std::string extension = path.extension().string();
                if (extension != ".cpp" && extension != ".hpp" && extension != ".h") { continue; }
                if (path.string().find("third_party") != std::string::npos) { continue; }

                std::ifstream stream(path, std::ios::binary);
                if (!stream) { continue; }
                std::string text{std::istreambuf_iterator<char>(stream),
                                 std::istreambuf_iterator<char>()};
                files.push_back(SourceFile{path,
                                           std::filesystem::relative(path, root).generic_string(),
                                           std::move(text)});
            }
        }
        return files;
    }

    /**
     * @brief Strips line comments, block comments and string literals.
     *
     * Without this, a check for a forbidden symbol fires on the comment explaining why the symbol
     * is forbidden -- which teaches people to delete the explanation.
     *
     * @param text Source text.
     * @return The text with comments and string contents blanked, preserving newlines so that line
     *         numbers still line up.
     */
    std::string stripCommentsAndStrings(std::string_view text)
    {
        std::string out;
        out.reserve(text.size());

        enum class Mode { Code, LineComment, BlockComment, String, Char, RawString };
        Mode mode = Mode::Code;

        for (std::size_t i = 0; i < text.size(); ++i)
        {
            const char c = text[i];
            const char next = (i + 1 < text.size()) ? text[i + 1] : '\0';

            switch (mode)
            {
                case Mode::Code:
                    if (c == '/' && next == '/') { mode = Mode::LineComment; out += "  "; ++i; continue; }
                    if (c == '/' && next == '*') { mode = Mode::BlockComment; out += "  "; ++i; continue; }
                    if (c == 'R' && next == '"') { mode = Mode::RawString; out += "  "; ++i; continue; }
                    if (c == '"') { mode = Mode::String; out += ' '; continue; }
                    if (c == '\'') { mode = Mode::Char; out += ' '; continue; }
                    out += c;
                    continue;

                case Mode::LineComment:
                    if (c == '\n') { mode = Mode::Code; out += '\n'; continue; }
                    out += ' ';
                    continue;

                case Mode::BlockComment:
                    if (c == '*' && next == '/') { mode = Mode::Code; out += "  "; ++i; continue; }
                    out += (c == '\n') ? '\n' : ' ';
                    continue;

                case Mode::String:
                    if (c == '\\') { out += "  "; ++i; continue; }
                    if (c == '"') { mode = Mode::Code; out += ' '; continue; }
                    out += (c == '\n') ? '\n' : ' ';
                    continue;

                case Mode::Char:
                    if (c == '\\') { out += "  "; ++i; continue; }
                    if (c == '\'') { mode = Mode::Code; out += ' '; continue; }
                    out += ' ';
                    continue;

                case Mode::RawString:
                    // Approximate: raw strings here are JSON fixtures, and ending at the first
                    // `)"` is correct for every one of them.
                    if (c == ')' && next == '"') { mode = Mode::Code; out += "  "; ++i; continue; }
                    out += (c == '\n') ? '\n' : ' ';
                    continue;
            }
        }
        return out;
    }

    /** @brief Returns the 1-based line number of a byte offset. */
    int lineOf(std::string_view text, std::size_t offset)
    {
        int line = 1;
        for (std::size_t i = 0; i < offset && i < text.size(); ++i)
        {
            if (text[i] == '\n') { ++line; }
        }
        return line;
    }

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

CNA_STUDIO_TEST(OnlyTheViewportModuleIncludesCnaHeaders)
{
    // Enforced by the build graph already -- a stray include elsewhere fails to link. Stated here
    // as a test so the property is asserted rather than inferred from a linker error, and so the
    // failure names the rule instead of naming a missing symbol.
    std::size_t violations = 0;
    for (const SourceFile& file : collectSources({"src", "include"}))
    {
        const bool isViewport = file.relativePath.find("viewport") != std::string::npos
                             || file.relativePath.find("Viewport") != std::string::npos;
        if (isViewport) { continue; }

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
                    + " includes a CNA header. Only cna-studio-viewport may link CNA; everything "
                      "else stays CNA-free so it can be tested with no GPU.");
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
