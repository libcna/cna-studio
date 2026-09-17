// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Project/ProjectPackaging.hpp
 * @brief What an export is asked for and what it reports back — in no particular language.
 *
 * `plan.md` STUDIO-02082. Split out of `ProjectExport.hpp` when the language seam arrived, because
 * these three are the *vocabulary* of exporting and the function that does it is not: a caller
 * fills in a request and reads a result without ever learning which build system wrote the tree.
 *
 * The invariant an export defends is in `CNA/Studio/Project/LanguageAdapter.hpp` and in
 * `docs/ARCHITECTURE.md` §1: an exported project is an ordinary project for CNA or one of its
 * bindings, and at no point needs Studio to be installed, on the PATH, or anywhere on the machine.
 */

#include <string>
#include <string_view>
#include <vector>

namespace CNA::Studio
{
    /** @brief What to export, and where to put it. */
    struct StudioExportRequest
    {
        /** @brief Directory the exported project is written into. Created if absent. */
        std::string outputDirectory;

        /**
         * @brief Name of the generated project and its executable.
         *
         * Defaults to the project's own name, reduced to an identifier. A build target cannot be
         * called `Hello Sprites`, and a game whose executable is named after a project whose name
         * happens to contain a space is not a reason to fail the export.
         */
        std::string targetName;

        /**
         * @brief Overwrite files already in @ref outputDirectory rather than refusing.
         *
         * Off by default. Export writes a whole tree, and a tree written over a directory somebody
         * chose by mistake is not something an undo can help with.
         */
        bool overwrite = false;
    };

    /** @brief What an export produced, and why it failed if it did. */
    struct StudioExportResult
    {
        /** @brief Every file written, relative to the output directory, in write order. */
        std::vector<std::string> writtenFiles;

        /** @brief Problems that did not stop the export, such as an asset that would not copy. */
        std::vector<std::string> warnings;

        /** @brief Empty when the export succeeded. */
        std::string errorMessage;

        [[nodiscard]] bool succeeded() const { return errorMessage.empty(); }
    };

    /**
     * @brief Reduces @p name to something a build system will accept as a target name.
     *
     * Generic rather than per-language: every build system this is likely to meet wants an
     * identifier, and a project called `2048` should get the same executable name whichever
     * language it is written in. Exposed because the export tests assert on the executable's name,
     * and duplicating the rule in a test would let the two drift.
     *
     * @param name Any project name.
     * @return An identifier: alphanumerics and underscores, never starting with a digit.
     */
    [[nodiscard]] std::string studioExportTargetName(std::string_view name);
}
