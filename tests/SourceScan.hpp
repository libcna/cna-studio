// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file SourceScan.hpp
 * @brief Reading Studio's own source tree, for the guards that check how it is written.
 *
 * `plan.md` STUDIO-02032, STUDIO-02059.
 *
 * Some architectural rules cannot be expressed in the type system: "no module outside these two
 * links CNA", "no service reaches another through a global". They are checked by scanning the
 * source, which is crude but has the property that matters -- a violation fails the build on the
 * commit that introduces it rather than being found months later.
 *
 * This header holds the parts every such scan needs, so that a second guard does not arrive with a
 * second copy of the comment stripper. It lives in a header rather than a translation unit because
 * the suite is one binary and the alternative -- a `tests/support` library -- would be three build
 * files for four functions.
 */

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace CnaStudioTest::Scan
{
    /** @brief The repository root, supplied by CMake so the scan does not guess. */
    inline std::filesystem::path sourceRoot()
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
     * @return Every `.cpp`, `.hpp` and `.h` found.
     */
    inline std::vector<SourceFile> collectSources(const std::vector<std::string>& subdirectories)
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
        std::sort(files.begin(), files.end(),
                  [](const SourceFile& a, const SourceFile& b) { return a.relativePath < b.relativePath; });
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
    inline std::string stripCommentsAndStrings(std::string_view text)
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
    inline int lineOf(std::string_view text, std::size_t offset)
    {
        int line = 1;
        for (std::size_t i = 0; i < offset && i < text.size(); ++i)
        {
            if (text[i] == '\n') { ++line; }
        }
        return line;
    }
} // namespace CnaStudioTest::Scan
