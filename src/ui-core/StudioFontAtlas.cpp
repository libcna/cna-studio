// SPDX-License-Identifier: MS-PL
/**
 * @file StudioFontAtlas.cpp
 * @brief Glyph rasterization, shelf packing, metrics, kerning and atlas upload.
 */

#include "CNA/Studio/UiCore/StudioFontAtlas.hpp"

#include "CNA/Studio/UiCore/StudioEmbeddedFonts.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>

// The vendored rasterizer. Public domain / MIT (third_party/stb/LICENSE), included here and
// nowhere else so that everything above this line stays Studio's own code. Dear ImGui vendors its
// own copy inside `namespace ImStb`, so the two cannot collide even in a binary holding both --
// which was worth checking rather than assuming, given what a duplicate definition cost this
// repository once already.
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#if defined(__GNUC__)
// STBTT_STATIC gives every one of the library's ~40 entry points internal linkage, and Studio uses
// six of them -- so the rest are unused statics and -Wall says so, forty times, in a file whose
// warnings nobody can act on. Internal linkage is worth keeping: it is what makes a second copy of
// this library elsewhere in the binary impossible to collide with, and this repository has already
// paid once for a duplicate definition that linked cleanly and corrupted memory at run time.
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "stb_truetype.h"
#if defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif

namespace CNA::Studio
{
    namespace
    {
        /** @brief Padding between packed glyphs, so linear sampling cannot bleed across them. */
        constexpr int kGlyphPadding = 1;

        /** @brief The replacement character, drawn where a code point has no outline. */
        constexpr char32_t kReplacementCharacter = 0xFFFD;

        /** @brief Packs a code-point pair into one key for the kerning cache. */
        std::uint64_t kerningKey(char32_t left, char32_t right)
        {
            return (static_cast<std::uint64_t>(left) << 32) | static_cast<std::uint64_t>(right);
        }

        /** @brief Rounds a pixel size to a whole pixel, so two near-identical sizes share a face. */
        float quantizeSize(float sizePx)
        {
            return std::max(1.0f, std::round(sizePx));
        }

        /** @brief Packs a face identity and size into one key. */
        std::uint64_t faceKey(StudioTypeface typeface, float sizePx)
        {
            return (static_cast<std::uint64_t>(typeface) << 32)
                 | static_cast<std::uint64_t>(quantizeSize(sizePx));
        }
    } // namespace

    std::string_view studioTypefaceName(StudioTypeface typeface)
    {
        switch (typeface)
        {
            case StudioTypeface::SansRegular:  return "IBM Plex Sans Regular";
            case StudioTypeface::SansSemiBold: return "IBM Plex Sans SemiBold";
            case StudioTypeface::Monospace:    return "IBM Plex Mono Regular";
            case StudioTypeface::Count:        break;
        }
        return "";
    }

    StudioTypeface resolveStudioTypeface(const StudioFontStyle& style)
    {
        // By family when the theme names a shipped one, by weight otherwise. A theme asking for a
        // family Studio does not ship gets Studio's own font at the right weight rather than
        // nothing: a missing glyph is a visible hole, a missing font is a blank window.
        if (style.family.find("Mono") != std::string::npos
            || style.family.find("mono") != std::string::npos)
        {
            return StudioTypeface::Monospace;
        }
        return style.weight >= 600 ? StudioTypeface::SansSemiBold : StudioTypeface::SansRegular;
    }

    /** @brief The rasterizer state and the shelf packer, kept out of the header. */
    struct StudioFontAtlas::Impl
    {
        /** @brief One loaded typeface. */
        struct Typeface
        {
            stbtt_fontinfo info{};
            bool loaded = false;
        };

        std::array<Typeface, static_cast<std::size_t>(StudioTypeface::Count)> typefaces{};
        std::map<std::uint64_t, StudioFontFace> faces;

        // --- The shelf packer -----------------------------------------------------------------
        /** @brief Side of the atlas being packed into. A member because the atlas can grow. */
        int size = StudioFontAtlas::kInitialAtlasSize;
        /** @brief Left edge of the next glyph on the current shelf. */
        int penX = kGlyphPadding;
        /** @brief Top edge of the current shelf. */
        int penY = kGlyphPadding;
        /** @brief Height of the tallest glyph on the current shelf. */
        int shelfHeight = 0;
        /** @brief Total ink area packed, for the occupancy report. */
        std::size_t packedArea = 0;

        /** @brief Empties the atlas, at @p newSize. */
        void resetPacker(int newSize)
        {
            size = newSize;
            penX = kGlyphPadding;
            penY = kGlyphPadding;
            shelfHeight = 0;
            packedArea = 0;
        }

        /**
         * @brief Reserves a rectangle in the atlas.
         * @param width Ink width.
         * @param height Ink height.
         * @param outX Receives the left edge.
         * @param outY Receives the top edge.
         * @return False when the atlas is full.
         */
        bool pack(int width, int height, int& outX, int& outY)
        {
            if (width <= 0 || height <= 0) { outX = 0; outY = 0; return true; }
            if (width + kGlyphPadding * 2 > size) { return false; }

            if (penX + width + kGlyphPadding > size)
            {
                // Next shelf. Wasting the tail of this one is the trade shelf packing makes, and
                // for a few hundred glyphs of similar height it costs a few percent.
                penX = kGlyphPadding;
                penY += shelfHeight + kGlyphPadding;
                shelfHeight = 0;
            }
            if (penY + height + kGlyphPadding > size) { return false; }

            outX = penX;
            outY = penY;
            penX += width + kGlyphPadding;
            shelfHeight = std::max(shelfHeight, height);
            packedArea += static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
            return true;
        }
    };

    void StudioFontAtlas::reset(int size)
    {
        size_ = size;
        impl_->resetPacker(size);

        // RGBA rather than a single alpha channel: UiDrawData's texture contract is 32-bit RGBA,
        // and a UI that needed a second pixel format for text would need a second draw path for it
        // too. White with the coverage in alpha lets a glyph take the vertex colour, so one atlas
        // serves every text colour in the theme.
        pixels_.assign(static_cast<std::size_t>(size_) * static_cast<std::size_t>(size_) * 4, 0);
        for (std::size_t i = 0; i < pixels_.size(); i += 4)
        {
            pixels_[i] = 255;
            pixels_[i + 1] = 255;
            pixels_[i + 2] = 255;
        }

        // A small opaque white block, reserved before any glyph so its position is fixed. Every
        // untextured primitive samples its centre, which lets flat fills and glyph quads share one
        // draw call -- the difference between a shell frame costing eleven draw calls and thirty.
        //
        // Re-reserved on a growth rather than rescaled, for the same reason: "before any glyph" is
        // the property that makes it findable, and arithmetic on the old coordinates would be a
        // second answer to where it is.
        constexpr int kWhiteBlock = 4;
        int whiteX = 0;
        int whiteY = 0;
        if (impl_->pack(kWhiteBlock, kWhiteBlock, whiteX, whiteY))
        {
            for (int row = 0; row < kWhiteBlock; ++row)
            {
                for (int column = 0; column < kWhiteBlock; ++column)
                {
                    const std::size_t index =
                        (static_cast<std::size_t>(whiteY + row) * static_cast<std::size_t>(size_)
                         + static_cast<std::size_t>(whiteX + column)) * 4;
                    pixels_[index + 3] = 255;
                }
            }
            const float inverse = 1.0f / static_cast<float>(size_);
            whiteU_ = (static_cast<float>(whiteX) + kWhiteBlock * 0.5f) * inverse;
            whiteV_ = (static_cast<float>(whiteY) + kWhiteBlock * 0.5f) * inverse;
        }

        // A new texture rather than a region of the old one, whatever was pending.
        needsCreate_ = true;
        dirtyMinX_ = 0;
        dirtyMinY_ = 0;
        dirtyMaxX_ = 0;
        dirtyMaxY_ = 0;
        dirty_ = true;
    }

    void StudioFontAtlas::touch(int x, int y, int width, int height) const
    {
        if (width <= 0 || height <= 0) { return; }

        if (dirtyMaxX_ <= dirtyMinX_ || dirtyMaxY_ <= dirtyMinY_)
        {
            dirtyMinX_ = x;
            dirtyMinY_ = y;
            dirtyMaxX_ = x + width;
            dirtyMaxY_ = y + height;
            return;
        }

        dirtyMinX_ = std::min(dirtyMinX_, x);
        dirtyMinY_ = std::min(dirtyMinY_, y);
        dirtyMaxX_ = std::max(dirtyMaxX_, x + width);
        dirtyMaxY_ = std::max(dirtyMaxY_, y + height);
    }

    StudioFontAtlas::StudioFontAtlas() : impl_(std::make_unique<Impl>())
    {
        reset(kInitialAtlasSize);

        for (std::size_t i = 0; i < impl_->typefaces.size(); ++i)
        {
            const StudioEmbeddedFont font =
                studioEmbeddedFont(static_cast<StudioTypeface>(i));
            Impl::Typeface& typeface = impl_->typefaces[i];
            typeface.loaded = font.bytes != nullptr && font.size > 0
                && stbtt_InitFont(&typeface.info, font.bytes,
                                  stbtt_GetFontOffsetForIndex(font.bytes, 0)) != 0;
        }
    }

    StudioFontAtlas::~StudioFontAtlas() = default;

    const StudioFontFace& StudioFontAtlas::face(StudioTypeface typeface, float sizePx)
    {
        const float size = quantizeSize(sizePx);
        const std::uint64_t key = faceKey(typeface, size);

        const auto existing = impl_->faces.find(key);
        if (existing != impl_->faces.end()) { return existing->second; }

        StudioFontFace created;
        created.atlas_ = this;
        created.typeface_ = typeface;
        created.sizePx_ = size;

        const Impl::Typeface& loaded = impl_->typefaces[static_cast<std::size_t>(typeface)];
        if (loaded.loaded)
        {
            created.scale_ = stbtt_ScaleForPixelHeight(&loaded.info, size);

            int ascent = 0;
            int descent = 0;
            int lineGap = 0;
            stbtt_GetFontVMetrics(&loaded.info, &ascent, &descent, &lineGap);

            // Rounded to whole pixels. A baseline at a fractional y makes every glyph in a line
            // land on a half pixel, which is the difference between text that looks crisp and text
            // that looks slightly smeared at exactly the sizes a UI uses.
            created.ascent_ = std::round(static_cast<float>(ascent) * created.scale_);
            created.descent_ = std::round(static_cast<float>(-descent) * created.scale_);
            created.lineHeight_ = std::round(static_cast<float>(ascent - descent + lineGap)
                                             * created.scale_);
        }
        else
        {
            // No font: fall back to the proportions the font-free approximation uses, so a build
            // with a broken font resource lays out sanely rather than collapsing every line to
            // zero height.
            StudioFontStyle style;
            style.sizePx = size;
            const StudioTextMetrics approximate = approximateStudioTextMetrics(style, "M");
            created.ascent_ = approximate.ascent;
            created.descent_ = approximate.descent;
            created.lineHeight_ = approximate.lineHeight;
        }

        return impl_->faces.emplace(key, std::move(created)).first->second;
    }

    const StudioFontFace& StudioFontAtlas::face(const StudioFontStyle& style)
    {
        return face(resolveStudioTypeface(style), style.sizePx);
    }

    const StudioGlyph* StudioFontAtlas::rasterize(const StudioFontFace& face,
                                                  char32_t codepoint) const
    {
        const Impl::Typeface& loaded =
            impl_->typefaces[static_cast<std::size_t>(face.typeface_)];
        if (!loaded.loaded) { return nullptr; }

        int index = stbtt_FindGlyphIndex(&loaded.info, static_cast<int>(codepoint));
        if (index == 0)
        {
            // No outline. The replacement character stands in, so a missing glyph is a visible
            // box the user can report rather than a silent gap that reads as a spacing bug.
            if (codepoint == kReplacementCharacter) { return nullptr; }
            index = stbtt_FindGlyphIndex(&loaded.info, static_cast<int>(kReplacementCharacter));
            if (index == 0) { return nullptr; }
        }

        int advance = 0;
        int leftBearing = 0;
        stbtt_GetGlyphHMetrics(&loaded.info, index, &advance, &leftBearing);

        StudioGlyph glyph;
        glyph.codepoint = codepoint;
        glyph.advance = static_cast<float>(advance) * face.scale_;
        glyph.bearingX = std::round(static_cast<float>(leftBearing) * face.scale_);

        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        stbtt_GetGlyphBitmapBox(&loaded.info, index, face.scale_, face.scale_, &x0, &y0, &x1, &y1);

        glyph.width = x1 - x0;
        glyph.height = y1 - y0;
        glyph.bearingX = static_cast<float>(x0);
        glyph.bearingY = static_cast<float>(y0);

        if (glyph.hasInk())
        {
            int atlasX = 0;
            int atlasY = 0;
            if (!impl_->pack(glyph.width, glyph.height, atlasX, atlasY))
            {
                // Counted, and remembered as a reason to grow. A full atlas shows as text that
                // stops appearing partway down a panel, which is a mystery unless something says
                // so -- and saying so was all this did until `STUDIO-04018`. The growth itself
                // happens at the top of the next frame rather than here: see @ref growIfNeeded.
                ++dropped_;
                growthWanted_ = true;
                return nullptr;
            }

            std::vector<std::uint8_t> coverage(
                static_cast<std::size_t>(glyph.width) * static_cast<std::size_t>(glyph.height), 0);
            stbtt_MakeGlyphBitmap(&loaded.info, coverage.data(), glyph.width, glyph.height,
                                  glyph.width, face.scale_, face.scale_, index);

            for (int row = 0; row < glyph.height; ++row)
            {
                for (int column = 0; column < glyph.width; ++column)
                {
                    const std::size_t source =
                        static_cast<std::size_t>(row) * glyph.width + column;
                    const std::size_t destination =
                        (static_cast<std::size_t>(atlasY + row) * static_cast<std::size_t>(size_)
                         + static_cast<std::size_t>(atlasX + column)) * 4;
                    pixels_[destination + 3] = coverage[source];
                }
            }

            const float inverse = 1.0f / static_cast<float>(size_);
            glyph.u0 = static_cast<float>(atlasX) * inverse;
            glyph.v0 = static_cast<float>(atlasY) * inverse;
            glyph.u1 = static_cast<float>(atlasX + glyph.width) * inverse;
            glyph.v1 = static_cast<float>(atlasY + glyph.height) * inverse;
            touch(atlasX, atlasY, glyph.width, glyph.height);
            dirty_ = true;
        }

        return &face.glyphs_.emplace(codepoint, glyph).first->second;
    }

    float StudioFontAtlas::lookupKerning(const StudioFontFace& face, char32_t left,
                                         char32_t right) const
    {
        const Impl::Typeface& loaded =
            impl_->typefaces[static_cast<std::size_t>(face.typeface_)];
        if (!loaded.loaded) { return 0.0f; }

        const int adjustment = stbtt_GetCodepointKernAdvance(
            &loaded.info, static_cast<int>(left), static_cast<int>(right));
        return static_cast<float>(adjustment) * face.scale_;
    }

    const StudioGlyph* StudioFontFace::glyph(char32_t codepoint) const
    {
        const auto existing = glyphs_.find(codepoint);
        if (existing != glyphs_.end()) { return &existing->second; }
        if (atlas_ == nullptr) { return nullptr; }
        return atlas_->rasterize(*this, codepoint);
    }

    float StudioFontFace::kerning(char32_t left, char32_t right) const
    {
        const std::uint64_t key = kerningKey(left, right);
        const auto existing = kerning_.find(key);
        if (existing != kerning_.end()) { return existing->second; }
        if (atlas_ == nullptr) { return 0.0f; }

        const float value = atlas_->lookupKerning(*this, left, right);
        kerning_.emplace(key, value);
        return value;
    }

    char32_t StudioFontAtlas::decodeUtf8(std::string_view utf8, std::size_t& offset)
    {
        if (offset >= utf8.size()) { return 0; }

        const auto lead = static_cast<unsigned char>(utf8[offset]);
        const auto continuation = [&](std::size_t index) -> bool {
            return offset + index < utf8.size()
                && (static_cast<unsigned char>(utf8[offset + index]) & 0xC0U) == 0x80U;
        };
        const auto bits = [&](std::size_t index) -> char32_t {
            return static_cast<char32_t>(static_cast<unsigned char>(utf8[offset + index]) & 0x3FU);
        };

        if (lead < 0x80U) { ++offset; return lead; }
        if ((lead & 0xE0U) == 0xC0U && continuation(1))
        {
            const char32_t value = ((lead & 0x1FU) << 6) | bits(1);
            offset += 2;
            return value;
        }
        if ((lead & 0xF0U) == 0xE0U && continuation(1) && continuation(2))
        {
            const char32_t value = ((lead & 0x0FU) << 12) | (bits(1) << 6) | bits(2);
            offset += 3;
            return value;
        }
        if ((lead & 0xF8U) == 0xF0U && continuation(1) && continuation(2) && continuation(3))
        {
            const char32_t value =
                ((lead & 0x07U) << 18) | (bits(1) << 12) | (bits(2) << 6) | bits(3);
            offset += 4;
            return value;
        }

        // Malformed. One byte forward and a replacement character: a corrupt string renders as
        // boxes rather than looping forever or reading past the end.
        ++offset;
        return kReplacementCharacter;
    }

    StudioTextMetrics StudioFontAtlas::measure(const StudioFontStyle& style,
                                               std::string_view utf8) const
    {
        // const because measuring is conceptually a query, while rasterising a glyph is not. The
        // mutation is confined to caches, which is what `mutable` is for: a caller cannot observe
        // the difference, and forcing every layout to take a non-const atlas would push the
        // mutability out into the whole UI.
        auto* self = const_cast<StudioFontAtlas*>(this);
        const StudioFontFace& resolved = self->face(style);

        StudioTextMetrics metrics;
        metrics.ascent = resolved.ascent();
        metrics.descent = resolved.descent();
        metrics.lineHeight = resolved.lineHeight();

        float pen = 0.0f;
        char32_t previous = 0;
        std::size_t offset = 0;
        while (offset < utf8.size())
        {
            const char32_t codepoint = decodeUtf8(utf8, offset);
            if (codepoint == 0) { break; }

            if (previous != 0) { pen += resolved.kerning(previous, codepoint); }
            if (const StudioGlyph* glyph = resolved.glyph(codepoint)) { pen += glyph->advance; }
            previous = codepoint;
        }

        metrics.width = pen;
        return metrics;
    }

    void StudioFontAtlas::prepare(const StudioFontStyle& style, std::string_view utf8)
    {
        const StudioFontFace& resolved = face(style);
        std::size_t offset = 0;
        while (offset < utf8.size())
        {
            const char32_t codepoint = decodeUtf8(utf8, offset);
            if (codepoint == 0) { break; }
            (void) resolved.glyph(codepoint);
        }
    }

    UiTextureRequest StudioFontAtlas::takeUploadRequest()
    {
        UiTextureRequest request;
        request.texture = kTextureId;
        request.width = size_;
        request.height = size_;
        request.pitch = size_ * 4;

        if (needsCreate_ || dirtyMaxX_ <= dirtyMinX_ || dirtyMaxY_ <= dirtyMinY_)
        {
            // The first upload, the one after a growth, and the degenerate case of a dirty flag
            // with no rectangle behind it -- which should not happen and, if it does, costs a
            // whole upload rather than an upload of the wrong pixels.
            request.action = UiTextureAction::Create;
            request.updateX = 0;
            request.updateY = 0;
            request.updateWidth = size_;
            request.updateHeight = size_;
            request.pixels = pixels_.data();
        }
        else
        {
            request.action = UiTextureAction::Update;
            request.updateX = dirtyMinX_;
            request.updateY = dirtyMinY_;
            request.updateWidth = dirtyMaxX_ - dirtyMinX_;
            request.updateHeight = dirtyMaxY_ - dirtyMinY_;
            // The top-left of the *region*, with the pitch still striding a whole atlas row, which
            // is what `UiTextureRequest` means by those two fields together.
            request.pixels = pixels_.data()
                + (static_cast<std::size_t>(dirtyMinY_) * static_cast<std::size_t>(size_)
                   + static_cast<std::size_t>(dirtyMinX_)) * 4;
        }

        needsCreate_ = false;
        dirtyMinX_ = 0;
        dirtyMinY_ = 0;
        dirtyMaxX_ = 0;
        dirtyMaxY_ = 0;
        dirty_ = false;
        return request;
    }

    float StudioFontAtlas::occupancy() const
    {
        const float total = static_cast<float>(size_) * static_cast<float>(size_);
        return static_cast<float>(impl_->packedArea) / total;
    }

    bool StudioFontAtlas::growIfNeeded()
    {
        if (!growthWanted_) { return false; }
        growthWanted_ = false;

        // At the cap the drops stand, and `droppedGlyphs()` keeps reporting them. Growing past a
        // limit nobody chose is how a UI comes to allocate a quarter of a gigabyte for a font.
        if (size_ >= kMaxAtlasSize) { return false; }

        reset(size_ * 2);

        // The caches go with the old atlas. Every cached glyph holds texture coordinates
        // normalised by the *old* side, so keeping them would sample the right rectangle of the
        // wrong texture -- letters drawn out of pieces of other letters, which reads as a corrupt
        // font rather than as an atlas that moved.
        for (auto& [key, face] : impl_->faces)
        {
            (void)key;
            face.glyphs_.clear();
        }

        // And the count, because the glyphs it counted are about to be tried again. A count that
        // survived would report an atlas as full while it was filling up.
        dropped_ = 0;
        ++growths_;
        return true;
    }
} // namespace CNA::Studio
