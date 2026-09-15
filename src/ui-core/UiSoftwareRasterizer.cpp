// SPDX-License-Identifier: MS-PL
/**
 * @file UiSoftwareRasterizer.cpp
 * @brief CPU rasterisation of UI geometry, and a minimal PNG encoder for CI artefacts.
 */

#include "CNA/Studio/UiCore/UiSoftwareRasterizer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <cstdio>
#include <cstring>
#include <map>
#include <utility>

namespace CNA::Studio
{
    namespace
    {
        /** @brief Unpacks a `UiVertex` colour into components. */
        struct Rgba { int r, g, b, a; };

        Rgba unpack(std::uint32_t packed)
        {
            return Rgba{static_cast<int>(packed & 0xFFu),
                        static_cast<int>((packed >> 8) & 0xFFu),
                        static_cast<int>((packed >> 16) & 0xFFu),
                        static_cast<int>((packed >> 24) & 0xFFu)};
        }

        /** @brief Source-over alpha blend of one pixel, in 8-bit integer arithmetic. */
        void blendPixel(std::uint8_t* dst, const Rgba& src)
        {
            if (src.a <= 0) { return; }
            if (src.a >= 255)
            {
                dst[0] = static_cast<std::uint8_t>(src.r);
                dst[1] = static_cast<std::uint8_t>(src.g);
                dst[2] = static_cast<std::uint8_t>(src.b);
                dst[3] = 255;
                return;
            }
            const int inv = 255 - src.a;
            dst[0] = static_cast<std::uint8_t>((src.r * src.a + dst[0] * inv + 127) / 255);
            dst[1] = static_cast<std::uint8_t>((src.g * src.a + dst[1] * inv + 127) / 255);
            dst[2] = static_cast<std::uint8_t>((src.b * src.a + dst[2] * inv + 127) / 255);
            dst[3] = static_cast<std::uint8_t>(std::min(255, src.a + dst[3] * inv / 255));
        }

        /**
         * The rasterizer honours texture requests for the same reason the CNA renderer does: the
         * golden images are the only place the UI's *appearance* is asserted, and a rasterizer
         * that drew every glyph as a flat rectangle would let real text regress without a single
         * test noticing -- which is exactly what happened until `UiTextureTable` was given a life
         * longer than one frame.
         */

        /**
         * @brief Samples a texture with nearest-neighbour filtering.
         *
         * Nearest rather than bilinear, deliberately. The UI draws glyph quads at exactly their
         * rasterised pixel size, so every sample lands on a texel centre and the two filters agree
         * -- except that bilinear's rounding would differ between this rasterizer and a GPU's, and
         * a golden image that disagreed with a real renderer by one least-significant bit per
         * pixel would be worse than useless.
         */
        Rgba sampleTexture(const UiTextureTable::Entry& texture, float u, float v)
        {
            if (texture.pixels() == nullptr || texture.width <= 0 || texture.height <= 0)
            {
                return Rgba{255, 255, 255, 255};
            }
            const int x = std::clamp(static_cast<int>(u * static_cast<float>(texture.width)),
                                     0, texture.width - 1);
            const int y = std::clamp(static_cast<int>(v * static_cast<float>(texture.height)),
                                     0, texture.height - 1);
            const std::uint8_t* texel =
                texture.pixels() + static_cast<std::size_t>(y) * texture.pitch
                + static_cast<std::size_t>(x) * 4;
            return Rgba{texel[0], texel[1], texel[2], texel[3]};
        }

        /** @brief Twice the signed area of a triangle; its sign gives the winding. */
        float edgeFunction(float ax, float ay, float bx, float by, float cx, float cy)
        {
            return (cx - ax) * (by - ay) - (cy - ay) * (bx - ax);
        }

        /**
         * @brief Reports whether an edge is a "top" or "left" edge of its triangle.
         *
         * With the inside at `edgeFunction > 0`, the inward normal of the edge A->B is
         * `(dy, -dx)`. A left edge is one whose interior lies to its right, i.e. `dy > 0`; a top
         * edge is horizontal with its interior below, i.e. `dy == 0 && dx < 0`.
         */
        bool isTopLeftEdge(float ax, float ay, float bx, float by)
        {
            const float dx = bx - ax;
            const float dy = by - ay;
            if (dy > 0.0f) { return true; }
            return dy == 0.0f && dx < 0.0f;
        }

        /**
         * @brief Fills one triangle with barycentric-interpolated vertex colour.
         *
         * Pixel centres are sampled at (x + 0.5, y + 0.5) under the standard **top-left fill
         * rule**, which is not a refinement here but a correctness requirement. The UI emits every
         * rectangle as two triangles sharing a diagonal, and a naive `w >= 0` test covers the
         * pixels exactly on that diagonal *twice*. For opaque fills the second write is invisible;
         * for anything translucent the pixel is blended twice and comes out wrong -- 50% white over
         * black lands at 192 instead of 128, along a diagonal seam across every control.
         *
         * The rule makes shared edges belong to exactly one triangle: an edge is included when it
         * is a top or left edge, and excluded otherwise, so two triangles meeting along a diagonal
         * agree on who owns it.
         *
         * Winding is normalised first, because the rule is stated in terms of an interior at
         * `edgeFunction > 0` and the UI does not guarantee a consistent winding.
         */
        void fillTriangle(ImageBuffer& image, const UiVertex& v0, const UiVertex& v1,
                          const UiVertex& v2, const UiClipRect& clip,
                          const UiTextureTable::Entry* texture)
        {
            float area = edgeFunction(v0.x, v0.y, v1.x, v1.y, v2.x, v2.y);
            if (std::abs(area) < 1e-6f) { return; }

            // Normalise winding so the interior is consistently at edgeFunction > 0.
            UiVertex a = v0;
            UiVertex b = v1;
            UiVertex c = v2;
            if (area < 0.0f)
            {
                std::swap(b, c);
                area = -area;
            }

            const int minX = std::max(static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))),
                                      std::max(0, static_cast<int>(std::floor(clip.left))));
            const int maxX = std::min(static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))),
                                      std::min(image.width, static_cast<int>(std::ceil(clip.right))));
            const int minY = std::max(static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))),
                                      std::max(0, static_cast<int>(std::floor(clip.top))));
            const int maxY = std::min(static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))),
                                      std::min(image.height, static_cast<int>(std::ceil(clip.bottom))));

            // Each barycentric weight belongs to the edge opposite its vertex.
            const bool topLeft0 = isTopLeftEdge(b.x, b.y, c.x, c.y);
            const bool topLeft1 = isTopLeftEdge(c.x, c.y, a.x, a.y);
            const bool topLeft2 = isTopLeftEdge(a.x, a.y, b.x, b.y);

            const Rgba ca = unpack(a.rgba);
            const Rgba cb = unpack(b.rgba);
            const Rgba cc = unpack(c.rgba);
            const float invArea = 1.0f / area;

            for (int y = minY; y < maxY; ++y)
            {
                for (int x = minX; x < maxX; ++x)
                {
                    const float px = static_cast<float>(x) + 0.5f;
                    const float py = static_cast<float>(y) + 0.5f;

                    const float e0 = edgeFunction(b.x, b.y, c.x, c.y, px, py);
                    const float e1 = edgeFunction(c.x, c.y, a.x, a.y, px, py);
                    const float e2 = edgeFunction(a.x, a.y, b.x, b.y, px, py);

                    // A point exactly on an edge is inside only when that edge is top-left.
                    if (e0 < 0.0f || (e0 == 0.0f && !topLeft0)) { continue; }
                    if (e1 < 0.0f || (e1 == 0.0f && !topLeft1)) { continue; }
                    if (e2 < 0.0f || (e2 == 0.0f && !topLeft2)) { continue; }

                    const float w0 = e0 * invArea;
                    const float w1 = e1 * invArea;
                    const float w2 = e2 * invArea;

                    Rgba src{
                        static_cast<int>(std::lround(w0 * ca.r + w1 * cb.r + w2 * cc.r)),
                        static_cast<int>(std::lround(w0 * ca.g + w1 * cb.g + w2 * cc.g)),
                        static_cast<int>(std::lround(w0 * ca.b + w1 * cb.b + w2 * cc.b)),
                        static_cast<int>(std::lround(w0 * ca.a + w1 * cb.a + w2 * cc.a))};

                    if (texture != nullptr)
                    {
                        const float u = w0 * a.u + w1 * b.u + w2 * c.u;
                        const float v = w0 * a.v + w1 * b.v + w2 * c.v;
                        const Rgba texel = sampleTexture(*texture, u, v);
                        // Modulate, exactly as the CNA renderer's BasicEffect does with a textured
                        // vertex-coloured draw: the atlas carries coverage in alpha and white in
                        // colour, so the vertex colour is what the glyph ends up being.
                        src.r = src.r * texel.r / 255;
                        src.g = src.g * texel.g / 255;
                        src.b = src.b * texel.b / 255;
                        src.a = src.a * texel.a / 255;
                    }

                    blendPixel(&image.pixels[(static_cast<std::size_t>(y) * image.width + x) * 4], src);
                }
            }
        }
    } // namespace

    void UiTextureTable::apply(const UiDrawData& drawData)
    {
        for (const UiTextureRequest& request : drawData.textureRequests)
        {
            if (request.action == UiTextureAction::Destroy)
            {
                entries_.erase(request.texture);
                continue;
            }
            if (request.pixels == nullptr || request.width <= 0 || request.height <= 0) { continue; }

            const int sourcePitch = request.pitch > 0 ? request.pitch : request.width * 4;

            Entry& entry = entries_[request.texture];
            if (request.action == UiTextureAction::Create
                || entry.width != request.width || entry.height != request.height)
            {
                // A Create, or an Update for a texture this table has never seen at this size --
                // which is what a grown font atlas looks like if its Create were ever missed.
                // Either way the old storage describes a different texture.
                entry.width = request.width;
                entry.height = request.height;
                entry.pitch = request.width * 4;
                entry.storage.assign(
                    static_cast<std::size_t>(entry.pitch) * static_cast<std::size_t>(entry.height),
                    0);
            }

            // Copied row by row into the region the request names. An Update's `pixels` is the
            // top-left of that region rather than of the texture, and its `pitch` still strides a
            // whole source row -- so the source walks by `sourcePitch` and the destination by the
            // texture's own.
            const int updateWidth = request.updateWidth > 0 ? request.updateWidth : request.width;
            const int updateHeight = request.updateHeight > 0 ? request.updateHeight : request.height;

            // A region starting outside the texture is refused rather than clamped: the row offset
            // below is unsigned, so a negative left edge would not draw in the wrong place, it
            // would write before the buffer.
            if (request.updateX < 0 || request.updateX >= entry.width) { continue; }

            for (int row = 0; row < updateHeight; ++row)
            {
                const int y = request.updateY + row;
                if (y < 0 || y >= entry.height) { continue; }

                const std::uint8_t* source =
                    request.pixels + static_cast<std::ptrdiff_t>(row) * sourcePitch;
                const std::size_t destination =
                    static_cast<std::size_t>(y) * static_cast<std::size_t>(entry.pitch)
                    + static_cast<std::size_t>(request.updateX) * 4;

                const int columns = std::min(updateWidth, entry.width - request.updateX);
                if (columns <= 0) { continue; }
                std::memcpy(entry.storage.data() + destination, source,
                            static_cast<std::size_t>(columns) * 4);
            }
        }
    }

    const UiTextureTable::Entry* UiTextureTable::find(UiTextureId id) const
    {
        const auto found = entries_.find(id);
        return found == entries_.end() ? nullptr : &found->second;
    }

    ImageBuffer rasterizeUiDrawData(const UiDrawData& drawData, StudioColor clearColor)
    {
        // A table built from this frame alone, which for a single frame is the same thing. Callers
        // that render more than one frame want the overload below: the atlas is requested once and
        // a table rebuilt per frame would lose it, drawing every glyph as a solid rectangle.
        UiTextureTable textures;
        return rasterizeUiDrawData(drawData, clearColor, textures);
    }

    ImageBuffer rasterizeUiDrawData(const UiDrawData& drawData, StudioColor clearColor,
                                    UiTextureTable& textures)
    {
        const int width = static_cast<int>(std::lround(drawData.displayWidth
                                                       * drawData.framebufferScaleX));
        const int height = static_cast<int>(std::lround(drawData.displayHeight
                                                        * drawData.framebufferScaleY));
        if (width <= 0 || height <= 0) { return ImageBuffer{}; }

        ImageBuffer image;
        image.width = width;
        image.height = height;
        image.pixels.resize(static_cast<std::size_t>(width) * height * 4);
        for (std::size_t i = 0; i < image.pixels.size(); i += 4)
        {
            image.pixels[i + 0] = clearColor.r;
            image.pixels[i + 1] = clearColor.g;
            image.pixels[i + 2] = clearColor.b;
            image.pixels[i + 3] = clearColor.a;
        }

        const float scaleX = drawData.framebufferScaleX;
        const float scaleY = drawData.framebufferScaleY;

        // Texture requests first, exactly as the CNA renderer applies them before drawing. The
        // pixels stay owned by whoever produced the request; this only records where they are.
        textures.apply(drawData);

        for (const UiDrawList& list : drawData.lists)
        {
            for (const UiDrawCommand& command : list.commands)
            {
                // Clip rectangles are in logical units, like vertex positions; both are scaled to
                // framebuffer pixels here, exactly as the CNA renderer does.
                UiClipRect clip{command.clipRect.left * scaleX, command.clipRect.top * scaleY,
                                command.clipRect.right * scaleX, command.clipRect.bottom * scaleY};
                clip = clip.clampTo(static_cast<float>(width), static_cast<float>(height));
                if (clip.isEmpty()) { continue; }

                // A command naming a texture that was never uploaded draws untextured rather than
                // being skipped: dropping it would hide the mistake, and drawing it flat makes the
                // missing upload visible as a solid block where the glyphs belong. Visible, but
                // only to somebody looking -- which is why that case is now a test rather than a
                // hope (`AMultiFrameCaptureStillHasItsFontAtlas`).
                const UiTextureTable::Entry* texture = command.texture != kUiTextureNone
                    ? textures.find(command.texture)
                    : nullptr;

                for (std::uint32_t i = 0; i + 2 < command.indexCount; i += 3)
                {
                    const std::size_t i0 = command.indexOffset + i;
                    if (i0 + 2 >= list.indices.size()) { break; }

                    const std::size_t a = command.vertexOffset + list.indices[i0];
                    const std::size_t b = command.vertexOffset + list.indices[i0 + 1];
                    const std::size_t c = command.vertexOffset + list.indices[i0 + 2];
                    if (a >= list.vertices.size() || b >= list.vertices.size()
                        || c >= list.vertices.size())
                    {
                        continue;
                    }

                    UiVertex v0 = list.vertices[a];
                    UiVertex v1 = list.vertices[b];
                    UiVertex v2 = list.vertices[c];
                    v0.x *= scaleX; v0.y *= scaleY;
                    v1.x *= scaleX; v1.y *= scaleY;
                    v2.x *= scaleX; v2.y *= scaleY;

                    fillTriangle(image, v0, v1, v2, clip, texture);
                }
            }
        }
        return image;
    }

    namespace
    {
        /** @brief Appends a big-endian 32-bit value. */
        void put32(std::vector<std::uint8_t>& out, std::uint32_t value)
        {
            out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFu));
            out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFu));
            out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
            out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
        }

        std::uint32_t crc32(const std::uint8_t* data, std::size_t length)
        {
            static std::array<std::uint32_t, 256> table{};
            static bool built = false;
            if (!built)
            {
                for (std::uint32_t n = 0; n < 256; ++n)
                {
                    std::uint32_t c = n;
                    for (int k = 0; k < 8; ++k)
                    {
                        c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                    }
                    table[n] = c;
                }
                built = true;
            }
            std::uint32_t c = 0xFFFFFFFFu;
            for (std::size_t i = 0; i < length; ++i)
            {
                c = table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
            }
            return c ^ 0xFFFFFFFFu;
        }

        std::uint32_t adler32(const std::uint8_t* data, std::size_t length)
        {
            std::uint32_t a = 1;
            std::uint32_t b = 0;
            for (std::size_t i = 0; i < length; ++i)
            {
                a = (a + data[i]) % 65521u;
                b = (b + a) % 65521u;
            }
            return (b << 16) | a;
        }


        // --- Deflate (RFC 1951), fixed-Huffman ------------------------------------------------
        //
        // Real compression rather than stored blocks. A 1920x1080 capture was eight megabytes of
        // literal pixels, which is enough artifact traffic that CI would eventually be asked to
        // stop keeping them -- and a visual test whose evidence is thrown away is a visual test
        // nobody can review.
        //
        // Fixed Huffman rather than dynamic: the tables are in the specification instead of in the
        // file, so there is no tree to build, serialise and get wrong, and for this input the gain
        // is almost all in the LZ77 matching anyway. A screenshot filtered by `Up` is mostly zeros,
        // and zeros become one length/distance pair per run whichever tree codes them.

        /** @brief Packs bits LSB-first into bytes, which is deflate's convention. */
        class BitWriter
        {
        public:
            explicit BitWriter(std::vector<std::uint8_t>& out) : out_(out) {}

            /** @brief Writes @p count low bits of @p value, least significant first. */
            void bits(std::uint32_t value, int count)
            {
                for (int i = 0; i < count; ++i)
                {
                    bits_ |= static_cast<std::uint32_t>((value >> i) & 1u) << held_;
                    if (++held_ == 8)
                    {
                        out_.push_back(static_cast<std::uint8_t>(bits_ & 0xFFu));
                        bits_ = 0;
                        held_ = 0;
                    }
                }
            }

            /**
             * @brief Writes a Huffman code, most significant bit first.
             *
             * The one place deflate reverses itself: the bit *stream* is filled from the least
             * significant end, and Huffman codes are packed starting from their most significant
             * bit. Getting this backwards produces a file that decodes to nothing recognisable and
             * looks, from the encoder's side, entirely correct.
             */
            void code(std::uint32_t value, int count)
            {
                for (int i = count - 1; i >= 0; --i) { bits((value >> i) & 1u, 1); }
            }

            /** @brief Pads to a byte boundary with zeros. */
            void flush()
            {
                if (held_ != 0) { out_.push_back(static_cast<std::uint8_t>(bits_ & 0xFFu)); }
                bits_ = 0;
                held_ = 0;
            }

        private:
            std::vector<std::uint8_t>& out_;
            std::uint32_t bits_ = 0;
            int held_ = 0;
        };

        /** @brief Writes a literal byte in the fixed literal/length tree. */
        void putLiteral(BitWriter& out, std::uint8_t value)
        {
            if (value <= 143) { out.code(0x30u + value, 8); }
            else { out.code(0x190u + value - 144u, 9); }
        }

        /** @brief Writes a symbol of the fixed literal/length tree by its index. */
        void putSymbol(BitWriter& out, std::uint32_t symbol)
        {
            if (symbol <= 143) { out.code(0x30u + symbol, 8); }
            else if (symbol <= 255) { out.code(0x190u + symbol - 144u, 9); }
            else if (symbol <= 279) { out.code(symbol - 256u, 7); }
            else { out.code(0xC0u + symbol - 280u, 8); }
        }

        struct RangeCode
        {
            std::uint32_t symbol;
            int extraBits;
            int base;
        };

        /** @brief The length code for @p length, 3..258. */
        RangeCode lengthCode(int length)
        {
            static const RangeCode kLengths[] = {
                {257, 0, 3},   {258, 0, 4},   {259, 0, 5},   {260, 0, 6},   {261, 0, 7},
                {262, 0, 8},   {263, 0, 9},   {264, 0, 10},  {265, 1, 11},  {266, 1, 13},
                {267, 1, 15},  {268, 1, 17},  {269, 2, 19},  {270, 2, 23},  {271, 2, 27},
                {272, 2, 31},  {273, 3, 35},  {274, 3, 43},  {275, 3, 51},  {276, 3, 59},
                {277, 4, 67},  {278, 4, 83},  {279, 4, 99},  {280, 4, 115}, {281, 5, 131},
                {282, 5, 163}, {283, 5, 195}, {284, 5, 227}, {285, 0, 258}};

            RangeCode chosen = kLengths[0];
            for (const RangeCode& candidate : kLengths)
            {
                if (candidate.base <= length) { chosen = candidate; }
            }
            return chosen;
        }

        /** @brief The distance code for @p distance, 1..32768. */
        RangeCode distanceCode(int distance)
        {
            static const RangeCode kDistances[] = {
                {0, 0, 1},      {1, 0, 2},      {2, 0, 3},      {3, 0, 4},      {4, 1, 5},
                {5, 1, 7},      {6, 2, 9},      {7, 2, 13},     {8, 3, 17},     {9, 3, 25},
                {10, 4, 33},    {11, 4, 49},    {12, 5, 65},    {13, 5, 97},    {14, 6, 129},
                {15, 6, 193},   {16, 7, 257},   {17, 7, 385},   {18, 8, 513},   {19, 8, 769},
                {20, 9, 1025},  {21, 9, 1537},  {22, 10, 2049}, {23, 10, 3073}, {24, 11, 4097},
                {25, 11, 6145}, {26, 12, 8193}, {27, 12, 12289}, {28, 13, 16385},
                {29, 13, 24577}};

            RangeCode chosen = kDistances[0];
            for (const RangeCode& candidate : kDistances)
            {
                if (candidate.base <= distance) { chosen = candidate; }
            }
            return chosen;
        }

        /**
         * @brief Compresses @p raw into one fixed-Huffman deflate block.
         *
         * Greedy LZ77 over a 32 KiB window with a hashed chain of recent positions. Greedy rather
         * than lazy: the extra pass buys a few per cent on text and almost nothing on a filtered
         * screenshot, which is what this encoder is for.
         *
         * @param raw Bytes to compress.
         * @param out Receives the deflate stream, appended and byte-aligned.
         */
        void deflateFixed(const std::vector<std::uint8_t>& raw, std::vector<std::uint8_t>& out)
        {
            constexpr int kWindow = 32768;
            constexpr int kMinMatch = 3;
            constexpr int kMaxMatch = 258;
            constexpr int kHashBits = 15;
            constexpr int kHashSize = 1 << kHashBits;
            constexpr int kMaxChain = 128;

            BitWriter writer{out};
            writer.bits(1, 1);   // final block
            writer.bits(1, 2);   // fixed Huffman

            const auto size = static_cast<int>(raw.size());
            std::vector<int> head(static_cast<std::size_t>(kHashSize), -1);
            std::vector<int> prev(raw.size(), -1);

            const auto hashAt = [&](int at) {
                const std::uint32_t a = raw[static_cast<std::size_t>(at)];
                const std::uint32_t b = raw[static_cast<std::size_t>(at) + 1];
                const std::uint32_t c = raw[static_cast<std::size_t>(at) + 2];
                return static_cast<int>(((a << 10) ^ (b << 5) ^ c) & (kHashSize - 1));
            };

            int at = 0;
            while (at < size)
            {
                int bestLength = 0;
                int bestDistance = 0;

                if (at + kMinMatch <= size)
                {
                    const int slot = hashAt(at);
                    int candidate = head[static_cast<std::size_t>(slot)];
                    int steps = 0;

                    while (candidate >= 0 && steps < kMaxChain)
                    {
                        const int distance = at - candidate;
                        if (distance <= 0 || distance > kWindow) { break; }

                        int length = 0;
                        const int limit = std::min(kMaxMatch, size - at);
                        while (length < limit
                               && raw[static_cast<std::size_t>(candidate + length)]
                                      == raw[static_cast<std::size_t>(at + length)])
                        {
                            ++length;
                        }

                        if (length > bestLength)
                        {
                            bestLength = length;
                            bestDistance = distance;
                            if (length >= kMaxMatch) { break; }
                        }

                        candidate = prev[static_cast<std::size_t>(candidate)];
                        ++steps;
                    }

                    prev[static_cast<std::size_t>(at)] = head[static_cast<std::size_t>(slot)];
                    head[static_cast<std::size_t>(slot)] = at;
                }

                if (bestLength >= kMinMatch)
                {
                    const RangeCode length = lengthCode(bestLength);
                    putSymbol(writer, length.symbol);
                    if (length.extraBits > 0)
                    {
                        writer.bits(static_cast<std::uint32_t>(bestLength - length.base),
                                    length.extraBits);
                    }

                    const RangeCode distance = distanceCode(bestDistance);
                    writer.code(distance.symbol, 5);
                    if (distance.extraBits > 0)
                    {
                        writer.bits(static_cast<std::uint32_t>(bestDistance - distance.base),
                                    distance.extraBits);
                    }

                    // Every position inside the match is still a place a later match could start
                    // from, so they all go into the chain -- skipping them is what makes a fast
                    // encoder compress a long run of zeros badly.
                    for (int i = 1; i < bestLength; ++i)
                    {
                        const int inside = at + i;
                        if (inside + kMinMatch > size) { break; }
                        const int slot = hashAt(inside);
                        prev[static_cast<std::size_t>(inside)] = head[static_cast<std::size_t>(slot)];
                        head[static_cast<std::size_t>(slot)] = inside;
                    }
                    at += bestLength;
                }
                else
                {
                    putLiteral(writer, raw[static_cast<std::size_t>(at)]);
                    ++at;
                }
            }

            putSymbol(writer, 256);   // end of block
            writer.flush();
        }

        /** @brief PNG's Paeth predictor. */
        int paeth(int a, int b, int c)
        {
            const int p = a + b - c;
            const int pa = std::abs(p - a);
            const int pb = std::abs(p - b);
            const int pc = std::abs(p - c);
            if (pa <= pb && pa <= pc) { return a; }
            return pb <= pc ? b : c;
        }

        void appendChunk(std::vector<std::uint8_t>& out, const char type[4],
                         const std::vector<std::uint8_t>& payload)
        {
            put32(out, static_cast<std::uint32_t>(payload.size()));
            const std::size_t crcStart = out.size();
            out.insert(out.end(), type, type + 4);
            out.insert(out.end(), payload.begin(), payload.end());
            put32(out, crc32(out.data() + crcStart, out.size() - crcStart));
        }
    } // namespace

    std::vector<std::uint8_t> encodeImageAsPng(const ImageBuffer& image)
    {
        if (!image.isWellFormed()) { return {}; }

        // Scanlines, each prefixed by the filter that predicted it best. Filtering is where most
        // of the saving comes from on a screenshot: a row identical to the one above it becomes a
        // row of zeros under `Up`, and a run of flat colour becomes zeros under `Sub`. The
        // compressor then has almost nothing left to say about either.
        constexpr int kBytesPerPixel = 4;
        const auto stride = static_cast<std::size_t>(image.width) * kBytesPerPixel;

        std::vector<std::uint8_t> raw;
        raw.reserve(static_cast<std::size_t>(image.height) * (1 + stride));

        std::vector<std::uint8_t> candidate(stride);
        std::vector<std::uint8_t> chosen(stride);

        for (int y = 0; y < image.height; ++y)
        {
            const std::uint8_t* row = image.pixels.data() + static_cast<std::size_t>(y) * stride;
            const std::uint8_t* above =
                y > 0 ? image.pixels.data() + static_cast<std::size_t>(y - 1) * stride : nullptr;

            std::uint8_t bestFilter = 0;
            std::size_t bestScore = std::numeric_limits<std::size_t>::max();

            for (int filter = 0; filter < 5; ++filter)
            {
                // Up, Average and Paeth all read the row above; on the first row there is none, and
                // the specification says to treat it as zeros -- which makes Up identical to None
                // and the other two worse. Skipped rather than computed and discarded.
                if (above == nullptr && (filter == 2 || filter == 3 || filter == 4)) { continue; }

                std::size_t score = 0;
                for (std::size_t i = 0; i < stride; ++i)
                {
                    const int left = i >= static_cast<std::size_t>(kBytesPerPixel)
                        ? row[i - kBytesPerPixel] : 0;
                    const int up = above != nullptr ? above[i] : 0;
                    const int upLeft = (above != nullptr && i >= static_cast<std::size_t>(kBytesPerPixel))
                        ? above[i - kBytesPerPixel] : 0;

                    int predicted = 0;
                    switch (filter)
                    {
                        case 1: predicted = left; break;
                        case 2: predicted = up; break;
                        case 3: predicted = (left + up) / 2; break;
                        case 4: predicted = paeth(left, up, upLeft); break;
                        default: predicted = 0; break;
                    }

                    const auto value = static_cast<std::uint8_t>((row[i] - predicted) & 0xFF);
                    candidate[i] = value;

                    // The standard heuristic: sum the residuals as *signed* bytes, so a small
                    // negative difference counts as small. Summing them unsigned would rate -1,
                    // which is the commonest residual there is, as the worst possible value.
                    score += static_cast<std::size_t>(value < 128 ? value : 256 - value);
                }

                if (score < bestScore)
                {
                    bestScore = score;
                    bestFilter = static_cast<std::uint8_t>(filter);
                    chosen.swap(candidate);
                }
            }

            raw.push_back(bestFilter);
            raw.insert(raw.end(), chosen.begin(), chosen.end());
        }

        // zlib stream: the two-byte header, one deflate block, then Adler-32 of the *unfiltered*
        // filtered bytes -- that is, of exactly what a decoder will have reconstructed.
        std::vector<std::uint8_t> z;
        z.push_back(0x78);
        z.push_back(0x01);
        // Appended into the same buffer rather than returned and copied: one allocation instead of
        // two for what is the largest thing this function builds.
        deflateFixed(raw, z);
        put32(z, adler32(raw.data(), raw.size()));

        std::vector<std::uint8_t> png{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

        std::vector<std::uint8_t> ihdr;
        put32(ihdr, static_cast<std::uint32_t>(image.width));
        put32(ihdr, static_cast<std::uint32_t>(image.height));
        ihdr.push_back(8);   // bit depth
        ihdr.push_back(6);   // colour type: RGBA
        ihdr.push_back(0);   // deflate
        ihdr.push_back(0);   // adaptive filtering
        ihdr.push_back(0);   // no interlace
        appendChunk(png, "IHDR", ihdr);
        appendChunk(png, "IDAT", z);
        appendChunk(png, "IEND", {});
        return png;
    }

    bool writeImageAsPng(const ImageBuffer& image, const std::string& path)
    {
        const std::vector<std::uint8_t> png = encodeImageAsPng(image);
        if (png.empty()) { return false; }

        std::FILE* file = std::fopen(path.c_str(), "wb");
        if (file == nullptr) { return false; }
        const std::size_t written = std::fwrite(png.data(), 1, png.size(), file);
        const bool closed = std::fclose(file) == 0;
        return closed && written == png.size();
    }
} // namespace CNA::Studio
