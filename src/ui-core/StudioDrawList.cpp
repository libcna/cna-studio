// SPDX-License-Identifier: MS-PL
/**
 * @file StudioDrawList.cpp
 * @brief Primitive emission and draw-call batching.
 */

#include "CNA/Studio/UiCore/StudioDrawList.hpp"

#include <algorithm>
#include <cmath>

namespace CNA::Studio
{
    namespace
    {
        /** @brief Segments per corner arc. Eight is smooth at the radii a UI uses (2-8px). */
        constexpr int kCornerSegments = 8;

        /** @brief Converts a layout rectangle to the edge form clipping and the renderer use. */
        UiClipRect toClipRect(const UiRect& rect)
        {
            return UiClipRect{rect.left(), rect.top(), rect.right(), rect.bottom()};
        }
    } // namespace

    std::uint32_t packUiColor(StudioColor color)
    {
        return static_cast<std::uint32_t>(color.r)
             | (static_cast<std::uint32_t>(color.g) << 8)
             | (static_cast<std::uint32_t>(color.b) << 16)
             | (static_cast<std::uint32_t>(color.a) << 24);
    }

    StudioDrawList::StudioDrawList()
    {
        data_.lists.emplace_back();
    }

    void StudioDrawList::begin(float displayWidth, float displayHeight, float framebufferScale)
    {
        glyphCount_ = 0;
        data_.clearGeometry();
        if (data_.lists.empty()) { data_.lists.emplace_back(); }

        data_.displayX = 0.0f;
        data_.displayY = 0.0f;
        data_.displayWidth = displayWidth;
        data_.displayHeight = displayHeight;
        data_.framebufferScaleX = framebufferScale;
        data_.framebufferScaleY = framebufferScale;

        clipStack_.clear();
        clipStack_.push_back(UiRect{0.0f, 0.0f, displayWidth, displayHeight});
        commandOpen_ = false;
    }

    void StudioDrawList::end()
    {
        commandOpen_ = false;
    }

    void StudioDrawList::setDefaultTexture(UiTextureId texture, float u, float v)
    {
        defaultTexture_ = texture;
        defaultU_ = u;
        defaultV_ = v;
        commandOpen_ = false;
    }

    void StudioDrawList::pushClip(const UiRect& rect)
    {
        // Intersect rather than replace: a scroll area inside a panel inside a dock must be
        // clipped by all three, and making that the caller's job is how content escapes its panel.
        clipStack_.push_back(currentClip().intersect(rect));
        commandOpen_ = false;
    }

    void StudioDrawList::popClip()
    {
        // The root clip is the display and is not poppable; an over-pop would leave the stack
        // empty and every later primitive unclipped.
        if (clipStack_.size() > 1) { clipStack_.pop_back(); }
        commandOpen_ = false;
    }

    UiRect StudioDrawList::currentClip() const
    {
        if (clipStack_.empty()) { return UiRect{0.0f, 0.0f, data_.displayWidth, data_.displayHeight}; }
        return clipStack_.back();
    }

    void StudioDrawList::ensureCommand(UiTextureId texture)
    {
        UiDrawList& list = data_.lists.back();
        const UiClipRect clip = toClipRect(currentClip());

        if (commandOpen_ && !list.commands.empty())
        {
            const UiDrawCommand& open = list.commands.back();
            if (open.texture == texture && open.clipRect == clip) { return; }
        }

        UiDrawCommand command;
        command.indexOffset = static_cast<std::uint32_t>(list.indices.size());
        command.indexCount = 0;
        command.vertexOffset = 0;
        command.clipRect = clip;
        command.texture = texture;
        list.commands.push_back(command);
        commandOpen_ = true;
    }

    bool StudioDrawList::isClippedAway(float x0, float y0, float x1, float y1) const
    {
        // Geometry entirely outside the clip is dropped here rather than left for the scissor test
        // to discard. Two reasons, and neither is micro-optimisation: a virtualised list that
        // describes a thousand rows to show twenty would otherwise fill a vertex buffer with
        // 980 invisible ones, and a test asking "is this row actually hidden" has nothing to
        // assert on when the answer is buried in a scissor rectangle the UI core never applies.
        const UiRect clip = currentClip();
        return x1 <= clip.left() || x0 >= clip.right() || y1 <= clip.top() || y0 >= clip.bottom();
    }

    void StudioDrawList::addQuad(float x0, float y0, float x1, float y1, StudioColor color)
    {
        if (x1 <= x0 || y1 <= y0 || color.a == 0) { return; }
        if (isClippedAway(x0, y0, x1, y1)) { return; }

        ensureCommand(defaultTexture_);
        UiDrawList& list = data_.lists.back();

        const auto base = static_cast<std::uint16_t>(list.vertices.size());
        const std::uint32_t packed = packUiColor(color);

        // All four corners on the same texel. With a white pixel reserved in the font atlas this
        // is a no-op that lets fills batch with glyphs; with no atlas the texture is
        // kUiTextureNone and the UVs are simply unused.
        const float u = defaultU_;
        const float v = defaultV_;
        list.vertices.push_back(UiVertex{x0, y0, u, v, packed});
        list.vertices.push_back(UiVertex{x1, y0, u, v, packed});
        list.vertices.push_back(UiVertex{x1, y1, u, v, packed});
        list.vertices.push_back(UiVertex{x0, y1, u, v, packed});

        const std::uint16_t order[6] = {0, 1, 2, 0, 2, 3};
        for (const std::uint16_t offset : order)
        {
            list.indices.push_back(static_cast<std::uint16_t>(base + offset));
        }
        list.commands.back().indexCount += 6;
    }

    void StudioDrawList::addTexturedQuad(float x0, float y0, float x1, float y1, float u0, float v0,
                                         float u1, float v1, UiTextureId texture, StudioColor color)
    {
        if (x1 <= x0 || y1 <= y0 || color.a == 0) { return; }
        if (isClippedAway(x0, y0, x1, y1)) { return; }

        ensureCommand(texture);
        UiDrawList& list = data_.lists.back();

        const auto base = static_cast<std::uint16_t>(list.vertices.size());
        const std::uint32_t packed = packUiColor(color);

        list.vertices.push_back(UiVertex{x0, y0, u0, v0, packed});
        list.vertices.push_back(UiVertex{x1, y0, u1, v0, packed});
        list.vertices.push_back(UiVertex{x1, y1, u1, v1, packed});
        list.vertices.push_back(UiVertex{x0, y1, u0, v1, packed});

        const std::uint16_t order[6] = {0, 1, 2, 0, 2, 3};
        for (const std::uint16_t offset : order)
        {
            list.indices.push_back(static_cast<std::uint16_t>(base + offset));
        }
        list.commands.back().indexCount += 6;
    }

    void StudioDrawList::drawGlyph(const UiRect& rect, float u0, float v0, float u1, float v1,
                                   UiTextureId texture, StudioColor color)
    {
        ++glyphCount_;
        addTexturedQuad(rect.left(), rect.top(), rect.right(), rect.bottom(), u0, v0, u1, v1,
                        texture, color);
    }

    void StudioDrawList::drawImage(const UiRect& rect, UiTextureId texture, bool flipVertically,
                                   StudioColor tint)
    {
        drawGlyph(rect, 0.0f, flipVertically ? 1.0f : 0.0f, 1.0f, flipVertically ? 0.0f : 1.0f,
                  texture, tint);
    }

    void StudioDrawList::addTextureRequest(const UiTextureRequest& request)
    {
        data_.textureRequests.push_back(request);
    }

    void StudioDrawList::addTriangle(float x0, float y0, float x1, float y1, float x2, float y2,
                                     StudioColor color)
    {
        if (color.a == 0) { return; }
        if (isClippedAway(std::min({x0, x1, x2}), std::min({y0, y1, y2}),
                          std::max({x0, x1, x2}), std::max({y0, y1, y2})))
        {
            return;
        }

        ensureCommand(defaultTexture_);
        UiDrawList& list = data_.lists.back();

        const auto base = static_cast<std::uint16_t>(list.vertices.size());
        const std::uint32_t packed = packUiColor(color);
        list.vertices.push_back(UiVertex{x0, y0, defaultU_, defaultV_, packed});
        list.vertices.push_back(UiVertex{x1, y1, defaultU_, defaultV_, packed});
        list.vertices.push_back(UiVertex{x2, y2, defaultU_, defaultV_, packed});
        list.indices.push_back(base);
        list.indices.push_back(static_cast<std::uint16_t>(base + 1));
        list.indices.push_back(static_cast<std::uint16_t>(base + 2));
        list.commands.back().indexCount += 3;
    }

    void StudioDrawList::fillRect(const UiRect& rect, StudioColor color)
    {
        const UiRect snapped = rect.pixelSnapped();
        addQuad(snapped.left(), snapped.top(), snapped.right(), snapped.bottom(), color);
    }

    void StudioDrawList::strokeRect(const UiRect& rect, StudioColor color, float thickness)
    {
        if (thickness <= 0.0f || rect.isEmpty()) { return; }

        const UiRect r = rect.pixelSnapped();
        const float t = std::min({thickness, r.width, r.height});

        // Inside the bounds, not centred on them: a bordered control must occupy exactly the space
        // layout gave it, or a column of them drifts by half a pixel each.
        addQuad(r.left(),      r.top(),          r.right(),     r.top() + t,      color);
        addQuad(r.left(),      r.bottom() - t,   r.right(),     r.bottom(),       color);
        addQuad(r.left(),      r.top() + t,      r.left() + t,  r.bottom() - t,   color);
        addQuad(r.right() - t, r.top() + t,      r.right(),     r.bottom() - t,   color);
    }

    void StudioDrawList::fillRoundedRect(const UiRect& rect, StudioColor color, float radius)
    {
        const UiRect r = rect.pixelSnapped();
        if (r.isEmpty() || color.a == 0) { return; }

        // A radius larger than half the shorter side would fold the corner arcs through each
        // other. Clamping keeps a pill shape at the limit rather than producing self-intersecting
        // geometry that renders as a dark smear.
        const float maxRadius = std::min(r.width, r.height) * 0.5f;
        const float rad = std::clamp(radius, 0.0f, maxRadius);
        if (rad <= 0.5f) { addQuad(r.left(), r.top(), r.right(), r.bottom(), color); return; }

        // Three bands: the full-width middle, and the insets above and below it.
        addQuad(r.left(), r.top() + rad, r.right(), r.bottom() - rad, color);
        addQuad(r.left() + rad, r.top(), r.right() - rad, r.top() + rad, color);
        addQuad(r.left() + rad, r.bottom() - rad, r.right() - rad, r.bottom(), color);

        struct Corner { float cx, cy, startAngle; };
        const Corner corners[4] = {
            {r.left() + rad,  r.top() + rad,     3.14159265f},          // top-left
            {r.right() - rad, r.top() + rad,     3.14159265f * 1.5f},   // top-right
            {r.right() - rad, r.bottom() - rad,  0.0f},                 // bottom-right
            {r.left() + rad,  r.bottom() - rad,  3.14159265f * 0.5f},   // bottom-left
        };

        constexpr float kQuarter = 3.14159265f * 0.5f;
        for (const Corner& corner : corners)
        {
            for (int segment = 0; segment < kCornerSegments; ++segment)
            {
                const float a0 = corner.startAngle
                               + kQuarter * (static_cast<float>(segment) / kCornerSegments);
                const float a1 = corner.startAngle
                               + kQuarter * (static_cast<float>(segment + 1) / kCornerSegments);
                addTriangle(corner.cx, corner.cy,
                            corner.cx + std::cos(a0) * rad, corner.cy + std::sin(a0) * rad,
                            corner.cx + std::cos(a1) * rad, corner.cy + std::sin(a1) * rad,
                            color);
            }
        }
    }

    void StudioDrawList::fillTriangle(float x0, float y0, float x1, float y1, float x2, float y2,
                                      StudioColor color)
    {
        addTriangle(x0, y0, x1, y1, x2, y2, color);
    }

    void StudioDrawList::drawLine(float x0, float y0, float x1, float y1, StudioColor color,
                                  float thickness)
    {
        if (thickness <= 0.0f) { return; }

        // Axis-aligned lines are the overwhelming majority in an editor UI -- separators, borders,
        // grid rules -- and drawing them as quads keeps them crisp. A general rotated line would
        // need the same quad rotated, and no caller needs one yet.
        const float half = thickness * 0.5f;
        if (std::abs(y1 - y0) < 0.001f)
        {
            const float top = std::round(y0 - half);
            addQuad(std::min(x0, x1), top, std::max(x0, x1), top + std::max(1.0f, thickness), color);
            return;
        }
        if (std::abs(x1 - x0) < 0.001f)
        {
            const float left = std::round(x0 - half);
            addQuad(left, std::min(y0, y1), left + std::max(1.0f, thickness), std::max(y0, y1), color);
            return;
        }

        // Diagonal: a quad perpendicular to the direction. No anti-aliasing yet; that arrives with
        // the font atlas in Phase 4, which is where the alpha texture to do it with comes from.
        const float dx = x1 - x0;
        const float dy = y1 - y0;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (length < 0.001f) { return; }
        const float nx = -dy / length * half;
        const float ny = dx / length * half;

        // The same default texture and the same reserved white texel every other primitive uses.
        //
        // This branch used `kUiTextureNone` and zeroed UVs, which looked harmless -- "with no atlas
        // the UVs are simply unused" -- and was not: both UI render backends skip a command whose
        // texture id they cannot resolve, because a command naming a texture that was never created
        // would otherwise sample whatever happens to be bound. `kUiTextureNone` resolves to
        // nothing, so **every diagonal line in Studio was dropped on a real device**. It survived
        // because almost nothing drew one -- until an icon set made of them did, and the icons came
        // out as their axis-aligned parts alone.
        //
        // The software rasterizer drew them correctly the whole time, which is why no headless
        // capture showed it. That is the failure mode a preview harness has: it is a second
        // implementation, and the two agreeing is the thing being tested rather than a given.
        ensureCommand(defaultTexture_);
        UiDrawList& list = data_.lists.back();
        const auto base = static_cast<std::uint16_t>(list.vertices.size());
        const std::uint32_t packed = packUiColor(color);
        const float u = defaultU_;
        const float v = defaultV_;
        list.vertices.push_back(UiVertex{x0 + nx, y0 + ny, u, v, packed});
        list.vertices.push_back(UiVertex{x1 + nx, y1 + ny, u, v, packed});
        list.vertices.push_back(UiVertex{x1 - nx, y1 - ny, u, v, packed});
        list.vertices.push_back(UiVertex{x0 - nx, y0 - ny, u, v, packed});
        const std::uint16_t order[6] = {0, 1, 2, 0, 2, 3};
        for (const std::uint16_t offset : order)
        {
            list.indices.push_back(static_cast<std::uint16_t>(base + offset));
        }
        list.commands.back().indexCount += 6;
    }

    void StudioDrawList::drawHorizontalSeparator(const UiRect& rect, StudioColor color,
                                                 float thickness)
    {
        addQuad(rect.left(), std::round(rect.top()), rect.right(),
                std::round(rect.top()) + std::max(1.0f, thickness), color);
    }

    void StudioDrawList::drawFocusRing(const UiRect& rect, StudioColor color, float thickness)
    {
        strokeRect(rect, color, std::max(1.0f, thickness));
    }

    void StudioDrawList::drawTextPlaceholder(const UiRect& rect, StudioColor color)
    {
        // Deliberately faint. Scaffolding that looks finished is scaffolding that ships; this is
        // meant to read as "text goes here and does not yet" at a glance.
        StudioColor faded = color;
        faded.a = static_cast<std::uint8_t>(color.a / 3);
        fillRect(rect, faded);
    }

    std::size_t StudioDrawList::commandCount() const
    {
        return data_.getTotalCommandCount();
    }

    std::size_t StudioDrawList::vertexCount() const
    {
        return data_.getTotalVertexCount();
    }
} // namespace CNA::Studio
