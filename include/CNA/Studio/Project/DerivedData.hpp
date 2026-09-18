// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/Project/DerivedData.hpp
 * @brief Where things Studio can regenerate are allowed to live.
 *
 * `plan.md` STUDIO-09015.
 *
 * ### The rule, in one sentence
 *
 * **A project directory holds only what a person authored.** Everything Studio can make again from
 * that — thumbnails, decoded pixels, compiled intermediates, scan indexes — lives under the user's
 * Studio state directory, keyed by project, and never inside the project at all.
 *
 * ### Why not a `Library/` folder in the project, which is what Unity does
 *
 * Because "not version-controlled" then depends on a `.gitignore` entry, and a rule enforced by a
 * file somebody can delete is a rule that will eventually be broken by somebody who did not know it
 * existed. A new contributor clones, opens the project, and commits a cache directory; the diff is
 * enormous, nobody reads it, and from then on the repository carries derived data that conflicts on
 * every merge. Keeping derived data out of the tree entirely makes the property true by
 * construction: there is nothing to ignore.
 *
 * What the in-project version buys is a cache that travels with the project between machines and
 * survives a wiped user profile. Both are real and neither is worth the failure above — a cache
 * that has to be rebuilt is an inconvenience measured in seconds, and a repository full of
 * generated files is a permanent tax on everybody who touches it.
 *
 * ### The one thing that *is* written beside the source, and why it is not derived data
 *
 * `.cnaasset` sidecars. They hold an asset's stable id and its import settings — a *decision*
 * somebody made, not something Studio can regenerate — and a scene references assets by that id, so
 * losing a sidecar breaks scenes. They are authored data that happens to be written by a tool, they
 * belong beside the file they describe, and they belong in version control.
 *
 * That is the whole of the distinction, and it is deliberately not a function: a predicate over
 * project-relative paths would answer "yes" for everything, which is a rule nobody can get wrong
 * and therefore a rule worth no code. What enforces it is
 * `DerivedDataTests.cpp`, which drives a real project through a Studio session and fails on any
 * file that appears in it other than the ones a person or a sidecar put there.
 */

#pragma once

#include <string>

namespace CNA::Studio
{
    /**
     * @brief Where derived data for @p projectFilePath belongs.
     *
     * `<state>/derived/<project name>-<hash>/`, mirroring how the asset shortcuts are stored. The
     * name makes the directory readable by a person who opens it; the hash keeps two projects
     * called `Game` in different places from sharing one.
     *
     * @param projectFilePath The `.cnaproject` file. Empty returns empty.
     * @return An absolute path, which may not exist yet, or empty when this machine has nowhere to
     *         keep user state at all.
     */
    [[nodiscard]] std::string studioDerivedDataDirectory(const std::string& projectFilePath);

    /**
     * @brief Whether @p path is inside @p projectRoot.
     *
     * The question the rule above is enforced with. Compares lexically normalised paths rather than
     * strings: `"/a/b/../b/c"` is inside `"/a/b"`, and a check that said otherwise would pass a
     * test and fail on a real path.
     *
     * @param path The path to test. May not exist.
     * @param projectRoot The project directory.
     * @return True when @p path is @p projectRoot or below it.
     */
    [[nodiscard]] bool studioPathIsInsideProject(const std::string& path,
                                                 const std::string& projectRoot);

}
