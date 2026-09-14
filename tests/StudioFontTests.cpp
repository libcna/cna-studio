// SPDX-License-Identifier: MS-PL
/**
 * @file StudioFontTests.cpp
 * @brief Glyph rasterization, metrics, kerning, atlas packing and the text that comes out.
 *
 * All headless, and all of it real: these run the shipped typefaces through the real rasterizer
 * and assert on the glyphs that come back. A font system tested against a stub would pass with the
 * fonts missing, which is exactly the failure worth catching.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/UiCore/StudioEmbeddedFonts.hpp"
#include "CNA/Studio/UiCore/StudioFontAtlas.hpp"
#include "CNA/Studio/UiCore/StudioFrame.hpp"
#include "CNA/Studio/UiCore/StudioWidgets.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using namespace CNA::Studio;

namespace
{
    /** @brief A body-sized style in the shipped sans face. */
    StudioFontStyle bodyStyle(float sizePx = 13.0f)
    {
        StudioFontStyle style;
        style.family = "Studio Sans";
        style.sizePx = sizePx;
        style.weight = 400;
        return style;
    }
}

// ------------------------------------------------------------------------------------------------
// The shipped typefaces (STUDIO-04009)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(EveryShippedTypefaceIsEmbeddedAndParses)
{
    // A font that failed to embed would show as a UI with no text at all, so this asserts the
    // bytes are there *and* that the rasterizer accepts them -- an empty array parses as nothing.
    StudioFontAtlas atlas;
    for (int i = 0; i < static_cast<int>(StudioTypeface::Count); ++i)
    {
        const auto typeface = static_cast<StudioTypeface>(i);
        const StudioEmbeddedFont embedded = studioEmbeddedFont(typeface);

        CNA_STUDIO_EXPECT(embedded.bytes != nullptr);
        CNA_STUDIO_EXPECT(embedded.size > 10000);
        CNA_STUDIO_EXPECT(!studioTypefaceName(typeface).empty());

        // The TrueType signature: 0x00010000 for glyf outlines.
        CNA_STUDIO_EXPECT_EQ(static_cast<int>(embedded.bytes[0]), 0);
        CNA_STUDIO_EXPECT_EQ(static_cast<int>(embedded.bytes[1]), 1);

        const StudioFontFace& face = atlas.face(typeface, 16.0f);
        CNA_STUDIO_EXPECT(face.ascent() > 0.0f);
        CNA_STUDIO_EXPECT(face.descent() > 0.0f);
        CNA_STUDIO_EXPECT(face.lineHeight() >= face.ascent() + face.descent());
        CNA_STUDIO_EXPECT(face.glyph(U'A') != nullptr);
    }
}

CNA_STUDIO_TEST(AThemeStyleResolvesToATypefaceByFamilyThenByWeight)
{
    // The theme names roles, not vendors: "Studio Sans" and "Studio Mono" are what a theme asks
    // for, and the atlas binds them to what Studio actually ships. A theme naming a family nobody
    // has still gets Studio's font at the right weight -- a missing glyph is a hole, a missing
    // font is a blank window.
    StudioFontStyle mono = bodyStyle();
    mono.family = "Studio Mono";
    CNA_STUDIO_EXPECT(resolveStudioTypeface(mono) == StudioTypeface::Monospace);

    StudioFontStyle bold = bodyStyle();
    bold.weight = 600;
    CNA_STUDIO_EXPECT(resolveStudioTypeface(bold) == StudioTypeface::SansSemiBold);

    StudioFontStyle unknown = bodyStyle();
    unknown.family = "Nothing Anybody Has";
    CNA_STUDIO_EXPECT(resolveStudioTypeface(unknown) == StudioTypeface::SansRegular);
}

CNA_STUDIO_TEST(TheMonospaceFaceIsActuallyMonospaced)
{
    // A "monospace" that is not is worse than no monospace: log columns and numeric tables are
    // laid out on the assumption, and a proportional font silently ruins both.
    StudioFontAtlas atlas;
    const StudioFontFace& face = atlas.face(StudioTypeface::Monospace, 14.0f);

    const float reference = face.glyph(U'M')->advance;
    for (const char32_t codepoint : {U'i', U'W', U'0', U'.', U'_', U'@'})
    {
        CNA_STUDIO_EXPECT(std::abs(face.glyph(codepoint)->advance - reference) < 0.01f);
    }
}

// ------------------------------------------------------------------------------------------------
// Glyphs and the atlas (STUDIO-04005)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(AGlyphWithInkIsPackedAndAGlyphWithoutOneIsNot)
{
    StudioFontAtlas atlas;
    const StudioFontFace& face = atlas.face(StudioTypeface::SansRegular, 20.0f);

    const StudioGlyph* letter = face.glyph(U'A');
    CNA_STUDIO_EXPECT(letter != nullptr);
    CNA_STUDIO_EXPECT(letter->hasInk());
    CNA_STUDIO_EXPECT(letter->advance > 0.0f);
    CNA_STUDIO_EXPECT(letter->u1 > letter->u0);
    CNA_STUDIO_EXPECT(letter->v1 > letter->v0);
    CNA_STUDIO_EXPECT(letter->bearingY < 0.0f);   // ink sits above the baseline

    const StudioGlyph* space = face.glyph(U' ');
    CNA_STUDIO_EXPECT(space != nullptr);
    CNA_STUDIO_EXPECT(!space->hasInk());
    CNA_STUDIO_EXPECT(space->advance > 0.0f);
}

CNA_STUDIO_TEST(RasterisedGlyphsActuallyHaveCoverageInTheAtlas)
{
    // The assertion a metrics-only test cannot make: that pixels were written. A rasterizer that
    // computed every extent correctly and drew nothing would pass everything above.
    StudioFontAtlas atlas;
    const StudioFontFace& face = atlas.face(StudioTypeface::SansRegular, 24.0f);
    const StudioGlyph* glyph = face.glyph(U'H');
    CNA_STUDIO_EXPECT(glyph != nullptr && glyph->hasInk());

    const int x = static_cast<int>(glyph->u0 * StudioFontAtlas::kAtlasSize);
    const int y = static_cast<int>(glyph->v0 * StudioFontAtlas::kAtlasSize);

    std::size_t opaque = 0;
    for (int row = 0; row < glyph->height; ++row)
    {
        for (int column = 0; column < glyph->width; ++column)
        {
            const std::size_t index =
                (static_cast<std::size_t>(y + row) * StudioFontAtlas::kAtlasSize
                 + static_cast<std::size_t>(x + column)) * 4;
            if (atlas.pixels()[index + 3] > 200) { ++opaque; }
        }
    }
    // An 'H' is two stems and a bar: a good fraction of its box is ink, and none of it is blank.
    CNA_STUDIO_EXPECT(opaque > static_cast<std::size_t>(glyph->width));
}

CNA_STUDIO_TEST(TheAtlasReservesAnOpaqueWhiteTexelForUntexturedGeometry)
{
    StudioFontAtlas atlas;
    const int x = static_cast<int>(atlas.whitePixelU() * StudioFontAtlas::kAtlasSize);
    const int y = static_cast<int>(atlas.whitePixelV() * StudioFontAtlas::kAtlasSize);
    const std::size_t index =
        (static_cast<std::size_t>(y) * StudioFontAtlas::kAtlasSize
         + static_cast<std::size_t>(x)) * 4;

    CNA_STUDIO_EXPECT_EQ(static_cast<int>(atlas.pixels()[index + 0]), 255);
    CNA_STUDIO_EXPECT_EQ(static_cast<int>(atlas.pixels()[index + 1]), 255);
    CNA_STUDIO_EXPECT_EQ(static_cast<int>(atlas.pixels()[index + 2]), 255);
    CNA_STUDIO_EXPECT_EQ(static_cast<int>(atlas.pixels()[index + 3]), 255);
}

CNA_STUDIO_TEST(GlyphsPackedIntoTheAtlasNeverOverlap)
{
    // Overlapping glyphs read as characters with pieces of their neighbours attached, which looks
    // like a rendering bug anywhere except in the packer where it actually is.
    StudioFontAtlas atlas;
    const StudioFontFace& face = atlas.face(StudioTypeface::SansRegular, 18.0f);

    std::vector<const StudioGlyph*> glyphs;
    for (char32_t c = U'!'; c <= U'~'; ++c)
    {
        if (const StudioGlyph* glyph = face.glyph(c); glyph != nullptr && glyph->hasInk())
        {
            glyphs.push_back(glyph);
        }
    }
    CNA_STUDIO_EXPECT(glyphs.size() > 80);

    const auto overlaps = [](const StudioGlyph& a, const StudioGlyph& b) {
        return a.u0 < b.u1 && b.u0 < a.u1 && a.v0 < b.v1 && b.v0 < a.v1;
    };
    std::size_t collisions = 0;
    for (std::size_t i = 0; i < glyphs.size(); ++i)
    {
        for (std::size_t j = i + 1; j < glyphs.size(); ++j)
        {
            if (overlaps(*glyphs[i], *glyphs[j])) { ++collisions; }
        }
    }
    CNA_STUDIO_EXPECT_EQ(collisions, std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(atlas.droppedGlyphs(), std::size_t{0});
    CNA_STUDIO_EXPECT(atlas.occupancy() > 0.0f);
    CNA_STUDIO_EXPECT(atlas.occupancy() < 1.0f);
}

CNA_STUDIO_TEST(TheAtlasUploadRequestIsWellFormedAndClearsTheDirtyFlag)
{
    StudioFontAtlas atlas;
    atlas.prepare(bodyStyle(), "Save");
    CNA_STUDIO_EXPECT(atlas.hasPendingUpload());

    const UiTextureRequest request = atlas.takeUploadRequest();
    CNA_STUDIO_EXPECT(request.action == UiTextureAction::Create);
    CNA_STUDIO_EXPECT_EQ(request.texture, StudioFontAtlas::kTextureId);
    CNA_STUDIO_EXPECT_EQ(request.width, StudioFontAtlas::kAtlasSize);
    CNA_STUDIO_EXPECT_EQ(request.height, StudioFontAtlas::kAtlasSize);
    CNA_STUDIO_EXPECT_EQ(request.pitch, StudioFontAtlas::kAtlasSize * 4);
    CNA_STUDIO_EXPECT(request.pixels == atlas.pixels().data());
    CNA_STUDIO_EXPECT(!atlas.hasPendingUpload());

    atlas.prepare(bodyStyle(), "Package...");
    CNA_STUDIO_EXPECT(atlas.hasPendingUpload());
}

CNA_STUDIO_TEST(EachSizeIsRasterisedSeparatelyRatherThanScaled)
{
    // Scaling one master size is exactly the blurry text that makes an application look amateur at
    // 125% and 150% -- the two most common DPI scales on Windows laptops.
    StudioFontAtlas atlas;
    const StudioFontFace& small = atlas.face(StudioTypeface::SansRegular, 13.0f);
    const StudioFontFace& large = atlas.face(StudioTypeface::SansRegular, 26.0f);

    CNA_STUDIO_EXPECT(&small != &large);
    CNA_STUDIO_EXPECT(large.ascent() > small.ascent() * 1.7f);
    CNA_STUDIO_EXPECT(large.glyph(U'W')->width > small.glyph(U'W')->width);

    // Asking twice for the same size returns the same face rather than rasterising it again.
    CNA_STUDIO_EXPECT(&atlas.face(StudioTypeface::SansRegular, 13.0f) == &small);
    // And sizes that round to the same whole pixel share one face, so a DPI scale that lands on
    // 13.02 does not double the atlas.
    CNA_STUDIO_EXPECT(&atlas.face(StudioTypeface::SansRegular, 13.02f) == &small);
}

// ------------------------------------------------------------------------------------------------
// Measurement, kerning and baselines (STUDIO-04006)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(MeasurementIsTheSumOfAdvancesPlusKerning)
{
    StudioFontAtlas atlas;
    const StudioFontFace& face = atlas.face(StudioTypeface::SansRegular, 16.0f);
    const StudioFontStyle style = bodyStyle(16.0f);

    const float expected = face.glyph(U'A')->advance + face.kerning(U'A', U'V')
                         + face.glyph(U'V')->advance;
    CNA_STUDIO_EXPECT(std::abs(atlas.measure(style, "AV").width - expected) < 0.01f);

    CNA_STUDIO_EXPECT_EQ(atlas.measure(style, "").width, 0.0f);
    CNA_STUDIO_EXPECT(atlas.measure(style, "Content Browser").width
                      > atlas.measure(style, "Build").width);
}

CNA_STUDIO_TEST(KerningPullsTheClassicPairsTogether)
{
    // Without kerning a UI's text looks subtly loose in exactly the places a reader notices, which
    // is why it is here rather than on a list of refinements -- and why it decided which fonts
    // Studio ships. Two otherwise excellent candidates carry no kerning data at all, which this
    // test would have caught only because it asserts on real adjustments rather than on the code
    // path having been called.
    StudioFontAtlas atlas;
    const StudioFontFace& face = atlas.face(StudioTypeface::SansRegular, 32.0f);

    // Which pairs a face kerns is the type designer's decision, so this asserts that the pairs
    // that classically need it get it -- not that every pair anyone might list does.
    std::size_t kerned = 0;
    for (const auto& pair : {std::pair<char32_t, char32_t>{U'A', U'V'}, {U'P', U','},
                             {U'F', U'.'}, {U'L', U'T'}, {U'r', U'.'}})
    {
        const float adjustment = face.kerning(pair.first, pair.second);
        CNA_STUDIO_EXPECT(adjustment <= 0.0f);
        if (adjustment < 0.0f) { ++kerned; }
    }
    CNA_STUDIO_EXPECT(kerned >= 3);

    // And the pair is narrower than the two glyphs laid out with no kerning at all.
    const StudioFontStyle style = bodyStyle(32.0f);
    const float measured = atlas.measure(style, "AV").width;
    const float unkerned = face.glyph(U'A')->advance + face.glyph(U'V')->advance;
    CNA_STUDIO_EXPECT(measured < unkerned);
}

CNA_STUDIO_TEST(BaselinesPutDescendersBelowAndCapitalsAbove)
{
    StudioFontAtlas atlas;
    const StudioFontFace& face = atlas.face(StudioTypeface::SansRegular, 28.0f);

    const StudioGlyph* capital = face.glyph(U'H');
    const StudioGlyph* descender = face.glyph(U'g');

    // bearingY is the offset from the baseline to the top of the ink, downwards positive.
    CNA_STUDIO_EXPECT(capital->bearingY < 0.0f);
    CNA_STUDIO_EXPECT(capital->bearingY + static_cast<float>(capital->height) <= 1.0f);
    CNA_STUDIO_EXPECT(descender->bearingY + static_cast<float>(descender->height) > 1.0f);
    CNA_STUDIO_EXPECT(static_cast<float>(descender->height) + descender->bearingY
                      <= face.descent() + 1.0f);
}

CNA_STUDIO_TEST(MeasurementIsDeterministic)
{
    // A golden image is a coin toss without this.
    StudioFontAtlas first;
    StudioFontAtlas second;
    const StudioFontStyle style = bodyStyle();

    for (const char* text : {"Save All", "World Outliner", "Ctrl+Shift+S", "\xC3\xA9\xC3\xA8"})
    {
        CNA_STUDIO_EXPECT_EQ(first.measure(style, text).width, second.measure(style, text).width);
        CNA_STUDIO_EXPECT_EQ(first.measure(style, text).width, first.measure(style, text).width);
    }
}

// ------------------------------------------------------------------------------------------------
// UTF-8 through the whole path (STUDIO-03026)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(Utf8DecodingHandlesEveryLengthAndRefusesToLoop)
{
    const auto decode = [](std::string_view text) {
        std::vector<char32_t> result;
        std::size_t offset = 0;
        while (offset < text.size())
        {
            const std::size_t before = offset;
            result.push_back(StudioFontAtlas::decodeUtf8(text, offset));
            // The property that matters most: the offset always advances. A decoder that can stand
            // still turns one corrupt byte into a hang.
            CNA_STUDIO_EXPECT(offset > before);
        }
        return result;
    };

    CNA_STUDIO_EXPECT_EQ(decode("Ab").size(), std::size_t{2});
    CNA_STUDIO_EXPECT_EQ(static_cast<std::uint32_t>(decode("\xC3\xA9")[0]), 0x00E9u);
    CNA_STUDIO_EXPECT_EQ(static_cast<std::uint32_t>(decode("\xE4\xB8\x96")[0]), 0x4E16u);
    CNA_STUDIO_EXPECT_EQ(static_cast<std::uint32_t>(decode("\xF0\x9F\x8E\xAE")[0]), 0x1F3AEu);

    // Malformed input: a lone continuation byte, a truncated sequence, an invalid lead.
    for (const char* bad : {"\x80", "\xC3", "\xE4\xB8", "\xFF\xFE", "a\xC3\x28"})
    {
        const std::vector<char32_t> decoded = decode(bad);
        CNA_STUDIO_EXPECT(!decoded.empty());
    }
}

CNA_STUDIO_TEST(ACodePointTheFontHasNoOutlineForBecomesAVisibleReplacement)
{
    // A silent gap reads as a spacing bug. A box the user can report is worth far more.
    StudioFontAtlas atlas;
    const StudioFontFace& face = atlas.face(StudioTypeface::SansRegular, 20.0f);

    const StudioGlyph* missing = face.glyph(U'\U0001F3AE');   // 🎮, not in Open Sans
    CNA_STUDIO_EXPECT(missing != nullptr);
    CNA_STUDIO_EXPECT(missing->advance > 0.0f);
}

CNA_STUDIO_TEST(AccentedTextMeasuresWiderThanItsUnaccentedLength)
{
    StudioFontAtlas atlas;
    const StudioFontStyle style = bodyStyle(16.0f);
    // "Groesse" against "Größe": five code points against seven, so the shorter string must not
    // measure wider merely because it has more bytes.
    const float withAccent = atlas.measure(style, "Gr\xC3\xB6\xC3\x9F""e").width;
    const float without = atlas.measure(style, "Groesse").width;
    CNA_STUDIO_EXPECT(withAccent > 0.0f);
    CNA_STUDIO_EXPECT(withAccent < without);
    // And the accented glyphs really are distinct outlines rather than fallbacks to the base
    // letter: an 'ö' is taller than an 'o' by its diaeresis.
    const StudioFontFace& face = atlas.face(StudioTypeface::SansRegular, 32.0f);
    CNA_STUDIO_EXPECT(face.glyph(U'\u00F6')->height > face.glyph(U'o')->height);
}

// ------------------------------------------------------------------------------------------------
// Text through a frame
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(DrawingTextEmitsGlyphQuadsAgainstTheAtlas)
{
    StudioFontAtlas atlas;
    StudioFrame frame;
    frame.setFontAtlas(&atlas);

    UiInputState input;
    input.displayWidth = 200.0f;
    input.displayHeight = 50.0f;

    runStudioFrame(frame, input, [](StudioFrame& f) {
        studioDrawText(f, UiRect{5.0f, 5.0f, 190.0f, 20.0f}, "Save All", StudioFontRole::Body,
                       StudioColor{255, 255, 255, 255});
    });

    const UiDrawData& data = frame.drawData();
    CNA_STUDIO_EXPECT_EQ(data.lists.size(), std::size_t{1});
    CNA_STUDIO_EXPECT(data.lists.front().vertices.size() >= 4 * 7);   // seven inked glyphs

    bool sampledTheAtlas = false;
    for (const UiDrawCommand& command : data.lists.front().commands)
    {
        if (command.texture == StudioFontAtlas::kTextureId) { sampledTheAtlas = true; }
    }
    CNA_STUDIO_EXPECT(sampledTheAtlas);

    // And the atlas upload rides along in the same frame, so the renderer has the pixels before it
    // has the quads that sample them.
    CNA_STUDIO_EXPECT_EQ(data.textureRequests.size(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(data.textureRequests.front().texture, StudioFontAtlas::kTextureId);
}

CNA_STUDIO_TEST(FillsAndGlyphsShareOneDrawCall)
{
    // STUDIO-04003's requirement under the condition that actually threatens it. Before the atlas
    // reserved a white texel, a label inside a button cost three draw calls: fill, glyphs, fill.
    StudioFontAtlas atlas;
    StudioFrame frame;
    frame.setFontAtlas(&atlas);

    UiInputState input;
    input.displayWidth = 400.0f;
    input.displayHeight = 100.0f;

    runStudioFrame(frame, input, [](StudioFrame& f) {
        if (!f.isDrawPass()) { return; }
        for (int i = 0; i < 4; ++i)
        {
            const auto x = static_cast<float>(i) * 90.0f + 5.0f;
            f.drawList().fillRect(UiRect{x, 5.0f, 80.0f, 24.0f}, StudioColor{60, 60, 70, 255});
            studioDrawText(f, UiRect{x + 4.0f, 5.0f, 72.0f, 24.0f}, "Rotate",
                           StudioFontRole::Body, StudioColor{230, 230, 230, 255});
        }
    });

    std::size_t commands = 0;
    for (const UiDrawList& list : frame.drawData().lists) { commands += list.commands.size(); }
    CNA_STUDIO_EXPECT_EQ(commands, std::size_t{1});
}

CNA_STUDIO_TEST(AFrameWithNoAtlasFallsBackToTheMeasuredBlock)
{
    // Not a silent nothing: a build whose fonts failed to embed must look unfinished rather than
    // look like a UI with no labels.
    StudioFrame frame;
    UiInputState input;
    input.displayWidth = 200.0f;
    input.displayHeight = 50.0f;

    runStudioFrame(frame, input, [](StudioFrame& f) {
        studioDrawText(f, UiRect{5.0f, 5.0f, 190.0f, 20.0f}, "Save", StudioFontRole::Body,
                       StudioColor{255, 255, 255, 255});
    });

    CNA_STUDIO_EXPECT(frame.drawData().getTotalVertexCount() > 0);
    CNA_STUDIO_EXPECT(frame.drawData().textureRequests.empty());
}

CNA_STUDIO_TEST(TruncationWithRealGlyphsStillFitsTheBox)
{
    StudioFontAtlas atlas;
    StudioFrame frame;
    frame.setFontAtlas(&atlas);

    const StudioFontStyle style = frame.theme().font(StudioFontRole::Body);
    const std::string fitted =
        studioTruncateText(frame, style, "Content Browser and a great deal more", 70.0f);

    CNA_STUDIO_EXPECT(fitted.find("\xE2\x80\xA6") != std::string::npos);
    CNA_STUDIO_EXPECT(frame.measureText(style, fitted).width <= 70.0f);
}

CNA_STUDIO_TEST(TextScalesWithDpiRatherThanBeingStretched)
{
    StudioFontAtlas atlas;

    StudioTheme at100 = StudioTheme::dark();
    StudioTheme at200 = StudioTheme::dark();
    at200.setScale(2.0f);

    const StudioFontFace& small = atlas.face(at100.font(StudioFontRole::Body));
    const StudioFontFace& large = atlas.face(at200.font(StudioFontRole::Body));

    CNA_STUDIO_EXPECT(&small != &large);
    CNA_STUDIO_EXPECT_EQ(large.sizePx(), small.sizePx() * 2.0f);

    // Twice the size, about twice the ink: a real rasterization at the larger size rather than a
    // scaled copy of the smaller one.
    const float ratio = static_cast<float>(large.glyph(U'H')->height)
                      / static_cast<float>(small.glyph(U'H')->height);
    CNA_STUDIO_EXPECT(ratio > 1.8f);
    CNA_STUDIO_EXPECT(ratio < 2.2f);
}
