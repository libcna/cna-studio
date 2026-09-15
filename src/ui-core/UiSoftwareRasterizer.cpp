// SPDX-License-Identifier: MS-PL
/**
 * @file UiSoftwareRasterizer.cpp
 * @brief CPU rasterisation of UI geometry, and a minimal PNG encoder for CI artefacts.
 */

#include "CNA/Studio/UiCore/UiSoftwareRasterizer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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
            if (texture.pixels == nullptr || texture.width <= 0 || texture.height <= 0)
            {
                return Rgba{255, 255, 255, 255};
            }
            const int x = std::clamp(static_cast<int>(u * static_cast<float>(texture.width)),
                                     0, texture.width - 1);
            const int y = std::clamp(static_cast<int>(v * static_cast<float>(texture.height)),
                                     0, texture.height - 1);
            const std::uint8_t* texel =
                texture.pixels + static_cast<std::size_t>(y) * texture.pitch
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

            Entry entry;
            entry.width = request.width;
            entry.height = request.height;
            entry.pitch = request.pitch > 0 ? request.pitch : request.width * 4;
            entry.pixels = request.pixels;
            entries_[request.texture] = entry;
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

        // Raw scanlines, each prefixed by filter type 0. No filtering: this file exists to be
        // looked at when a test fails, and an encoder anyone can verify by reading is worth more
        // here than a smaller file.
        std::vector<std::uint8_t> raw;
        raw.reserve(static_cast<std::size_t>(image.height) * (1 + image.width * 4));
        for (int y = 0; y < image.height; ++y)
        {
            raw.push_back(0);
            const std::size_t rowStart = static_cast<std::size_t>(y) * image.width * 4;
            raw.insert(raw.end(), image.pixels.begin() + static_cast<std::ptrdiff_t>(rowStart),
                       image.pixels.begin() + static_cast<std::ptrdiff_t>(rowStart + image.width * 4));
        }

        // zlib stream with stored (uncompressed) deflate blocks.
        std::vector<std::uint8_t> z;
        z.push_back(0x78);
        z.push_back(0x01);
        constexpr std::size_t kMaxBlock = 65535;
        for (std::size_t offset = 0; offset < raw.size(); offset += kMaxBlock)
        {
            const std::size_t length = std::min(kMaxBlock, raw.size() - offset);
            const bool last = (offset + length) >= raw.size();
            z.push_back(last ? 1 : 0);
            z.push_back(static_cast<std::uint8_t>(length & 0xFFu));
            z.push_back(static_cast<std::uint8_t>((length >> 8) & 0xFFu));
            const std::uint16_t inverse = static_cast<std::uint16_t>(~static_cast<std::uint16_t>(length));
            z.push_back(static_cast<std::uint8_t>(inverse & 0xFFu));
            z.push_back(static_cast<std::uint8_t>((inverse >> 8) & 0xFFu));
            z.insert(z.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                     raw.begin() + static_cast<std::ptrdiff_t>(offset + length));
        }
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
