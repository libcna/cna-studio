// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/UiCore/StudioEmbeddedFonts.hpp
 * @brief The typefaces CNA Studio ships, compiled into the binary.
 *
 * `plan.md` STUDIO-04009.
 *
 * ### Why embedded rather than loaded from a file
 *
 * A tool that cannot draw text until it finds a file is a tool that shows a blank window when
 * somebody moves the executable, runs it from a build directory, or ships it without its data
 * folder. Text is not optional content — it is how every control identifies itself — so the fonts
 * are part of the binary and their availability is not a runtime question.
 *
 * The cost is about 570 KB of binary and well under a second of compile time; the alternative
 * costs a class of support problem nobody can reproduce.
 *
 * ### Why these fonts
 *
 * Both are under the SIL Open Font License 1.1, which permits redistribution in a binary without
 * restriction and without attribution in the UI. Provenance, upstream URLs and SHA-256 digests are
 * recorded in `THIRD_PARTY_NOTICES.md`, and the licence texts are vendored verbatim beside the
 * font files.
 *
 * **IBM Plex Sans** (Regular and SemiBold) and **IBM Plex Mono**, one family across the whole UI.
 * Three reasons, in order of how much they mattered:
 *
 * 1. **They carry kerning the rasterizer can read.** Most modern fonts express kerning as GPOS
 *    lookups, and the vendored rasterizer reads only the common `PairPos` form. Plex's kerning is
 *    exactly that form. Two otherwise excellent candidates were rejected on this alone: Open Sans
 *    and JetBrains Mono ship no `kern` feature at all, so text in them is set with no kerning
 *    whatsoever — which looks subtly loose in exactly the pairs a reader notices.
 * 2. **One family, three faces.** A sans and a monospace drawn by the same hand keep a log panel
 *    and the label above it looking like one application.
 * 3. **It was drawn for interfaces.** A large x-height, unambiguous letterforms at 12 and 13
 *    pixels — which is the size a dense editor UI actually uses — and a monospace with visibly
 *    distinct `0`/`O` and `1`/`l`/`I`. An editor's log and identifier columns are read far more
 *    carefully than its labels, and a monospace that confuses those causes real mistakes.
 */

#include "CNA/Studio/UiCore/StudioFontAtlas.hpp"

#include <cstddef>
#include <cstdint>

namespace CNA::Studio
{
    /** @brief One embedded font file. */
    struct StudioEmbeddedFont
    {
        /** @brief The TrueType bytes, or nullptr when this build embedded none. */
        const std::uint8_t* bytes = nullptr;
        /** @brief Length in bytes. */
        std::size_t size = 0;
    };

    /**
     * @brief Returns the embedded bytes of a shipped typeface.
     * @param typeface Typeface wanted.
     * @return The font file; `bytes` is null only for an out-of-range typeface.
     */
    [[nodiscard]] StudioEmbeddedFont studioEmbeddedFont(StudioTypeface typeface);
} // namespace CNA::Studio
