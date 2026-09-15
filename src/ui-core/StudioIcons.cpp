// SPDX-License-Identifier: MS-PL
/**
 * @file StudioIcons.cpp
 * @brief The icon paths.
 */

#include "CNA/Studio/UiCore/StudioIcons.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace CNA::Studio
{
    namespace
    {
        /** @brief The grid every path is authored on. */
        constexpr float kGrid = 16.0f;

        /**
         * @brief Maps the authoring grid onto the rectangle an icon was asked for.
         *
         * Square and centred: an icon stretched to a non-square button is an icon that looks wrong
         * in one dimension, and a caller passing a whole button rectangle should not have to do
         * this arithmetic to avoid it.
         */
        struct IconSpace
        {
            float originX = 0.0f;
            float originY = 0.0f;
            float scale = 1.0f;

            [[nodiscard]] float x(float gridX) const { return originX + gridX * scale; }
            [[nodiscard]] float y(float gridY) const { return originY + gridY * scale; }

            /** @brief A stroke width in grid units, in pixels, never thinner than one. */
            [[nodiscard]] float stroke(float gridWidth) const
            {
                return std::max(1.0f, gridWidth * scale);
            }
        };

        IconSpace spaceFor(const UiRect& bounds)
        {
            IconSpace space;
            const float extent = std::min(bounds.width, bounds.height);
            if (extent <= 0.0f) { return space; }

            space.scale = extent / kGrid;
            // Snapped, so a stroke authored on a grid line lands on a pixel boundary rather than
            // straddling two and coming out grey.
            space.originX = std::round(bounds.centerX() - extent * 0.5f);
            space.originY = std::round(bounds.centerY() - extent * 0.5f);
            return space;
        }

        void line(StudioFrame& frame, const IconSpace& space, float x0, float y0, float x1, float y1,
                  StudioColor color, float width = 1.6f)
        {
            frame.drawList().drawLine(space.x(x0), space.y(y0), space.x(x1), space.y(y1), color,
                                      space.stroke(width));
        }

        void triangle(StudioFrame& frame, const IconSpace& space, float x0, float y0, float x1,
                      float y1, float x2, float y2, StudioColor color)
        {
            frame.drawList().fillTriangle(space.x(x0), space.y(y0), space.x(x1), space.y(y1),
                                          space.x(x2), space.y(y2), color);
        }

        void box(StudioFrame& frame, const IconSpace& space, float x0, float y0, float x1, float y1,
                 StudioColor color)
        {
            frame.drawList().fillRect(
                UiRect{space.x(x0), space.y(y0), (x1 - x0) * space.scale, (y1 - y0) * space.scale},
                color);
        }

        void outline(StudioFrame& frame, const IconSpace& space, float x0, float y0, float x1,
                     float y1, StudioColor color, float width = 1.6f)
        {
            frame.drawList().strokeRect(
                UiRect{space.x(x0), space.y(y0), (x1 - x0) * space.scale, (y1 - y0) * space.scale},
                color, space.stroke(width));
        }

        /**
         * @brief An open arc, as a ribbon between an inner and an outer radius.
         *
         * Not a polyline of thick segments. Each such segment is its own quad, so consecutive ones
         * overlap on the inside of the curve and leave a notch on the outside -- which at a
         * toolbar's size reads as a lumpy, hand-drawn stroke and is the first thing that makes an
         * icon set look amateur. A ribbon shares its vertices between segments and has neither
         * problem.
         *
         * Segments proportional to the sweep rather than a fixed count: a quarter turn drawn with
         * as many as a full one is wasted geometry, and a full one drawn with as few as a quarter
         * is visibly a polygon.
         */
        void arc(StudioFrame& frame, const IconSpace& space, float cx, float cy, float radius,
                 float fromDegrees, float toDegrees, StudioColor color, float width = 1.6f)
        {
            const float sweep = std::abs(toDegrees - fromDegrees);
            const int segments = std::max(4, static_cast<int>(sweep / 9.0f));

            // Half-widths in grid units, so the stroke scales with the icon like everything else --
            // but floored in *pixels*, because a hairline that rounds to zero disappears.
            const float pixelWidth = std::max(1.0f, width * space.scale);
            const float halfGrid = (pixelWidth / space.scale) * 0.5f;
            const float inner = std::max(0.0f, radius - halfGrid);
            const float outer = radius + halfGrid;

            float previousInnerX = 0.0f;
            float previousInnerY = 0.0f;
            float previousOuterX = 0.0f;
            float previousOuterY = 0.0f;

            for (int i = 0; i <= segments; ++i)
            {
                const float t = static_cast<float>(i) / static_cast<float>(segments);
                const float degrees = fromDegrees + (toDegrees - fromDegrees) * t;
                const float radians = degrees * 3.14159265358979323846f / 180.0f;
                const float dirX = std::cos(radians);
                const float dirY = std::sin(radians);

                const float innerX = cx + dirX * inner;
                const float innerY = cy + dirY * inner;
                const float outerX = cx + dirX * outer;
                const float outerY = cy + dirY * outer;

                if (i > 0)
                {
                    triangle(frame, space, previousInnerX, previousInnerY, previousOuterX,
                             previousOuterY, outerX, outerY, color);
                    triangle(frame, space, previousInnerX, previousInnerY, outerX, outerY, innerX,
                             innerY, color);
                }

                previousInnerX = innerX;
                previousInnerY = innerY;
                previousOuterX = outerX;
                previousOuterY = outerY;
            }
        }

        /** @brief A downward chevron, shared by the two chevron icons under a rotation. */
        void chevron(StudioFrame& frame, const IconSpace& space, StudioColor color, bool down)
        {
            if (down)
            {
                line(frame, space, 4.0f, 6.0f, 8.0f, 10.0f, color, 1.8f);
                line(frame, space, 8.0f, 10.0f, 12.0f, 6.0f, color, 1.8f);
            }
            else
            {
                line(frame, space, 6.0f, 4.0f, 10.0f, 8.0f, color, 1.8f);
                line(frame, space, 10.0f, 8.0f, 6.0f, 12.0f, color, 1.8f);
            }
        }

        /** @brief The names, indexed by the enumeration so the two cannot drift. */
        constexpr std::array<std::string_view, static_cast<std::size_t>(StudioIcon::Count)> kNames{
            "none",   "save",     "folder",  "file",      "undo",   "redo",
            "delete", "duplicate", "translate", "rotate",  "scale",  "grid",
            "focus",  "play",     "pause",   "step",      "stop",    "build",     "package", "close",
            "chevronRight", "chevronDown", "search", "warning", "error", "info",
        };
    }

    std::string_view studioIconName(StudioIcon icon)
    {
        const auto index = static_cast<std::size_t>(icon);
        return index < kNames.size() ? kNames[index] : kNames[0];
    }

    bool parseStudioIcon(std::string_view name, StudioIcon& out)
    {
        for (std::size_t i = 0; i < kNames.size(); ++i)
        {
            if (kNames[i] == name) { out = static_cast<StudioIcon>(i); return true; }
        }
        return false;
    }

    StudioIcon studioIconForAction(std::string_view actionId)
    {
        // A table rather than a naming convention. "studio.edit.delete" and "studio.file.save" have
        // nothing in common structurally, and a convention that worked for both would constrain
        // every future id to be named after its picture.
        struct Mapping { std::string_view id; StudioIcon icon; };
        static constexpr Mapping kMappings[]{
            {"studio.file.save", StudioIcon::Save},
            {"studio.file.saveAll", StudioIcon::Save},
            {"studio.file.openProject", StudioIcon::Folder},
            {"studio.file.newProject", StudioIcon::File},
            {"studio.edit.undo", StudioIcon::Undo},
            {"studio.edit.redo", StudioIcon::Redo},
            {"studio.edit.delete", StudioIcon::Delete},
            {"studio.edit.duplicate", StudioIcon::Duplicate},
            {"studio.view.translate", StudioIcon::Translate},
            {"studio.view.rotate", StudioIcon::Rotate},
            {"studio.view.scale", StudioIcon::Scale},
            {"studio.view.toggleGrid", StudioIcon::Grid},
            {"studio.view.focusSelected", StudioIcon::Focus},
            {"studio.play.play", StudioIcon::Play},
            {"studio.play.pause", StudioIcon::Pause},
            {"studio.play.step", StudioIcon::Step},
            {"studio.play.stop", StudioIcon::Stop},
            {"studio.build.build", StudioIcon::Build},
        };

        for (const Mapping& mapping : kMappings)
        {
            if (mapping.id == actionId) { return mapping.icon; }
        }
        return StudioIcon::None;
    }

    void studioDrawIcon(StudioFrame& frame, const UiRect& bounds, StudioIcon icon, StudioColor color)
    {
        if (!frame.isDrawPass() || icon == StudioIcon::None || color.a == 0) { return; }

        const IconSpace space = spaceFor(bounds);
        if (space.scale <= 0.0f) { return; }

        switch (icon)
        {
            case StudioIcon::Save:
                // A floppy disc, which is the one icon everybody still reads instantly even though
                // nobody has seen the object in twenty years.
                outline(frame, space, 2.5f, 2.5f, 13.5f, 13.5f, color);
                box(frame, space, 5.0f, 2.5f, 11.0f, 6.5f, color);
                outline(frame, space, 5.0f, 8.5f, 11.0f, 13.5f, color, 1.2f);
                break;

            case StudioIcon::Folder:
                line(frame, space, 2.0f, 4.5f, 6.5f, 4.5f, color, 1.4f);
                line(frame, space, 6.5f, 4.5f, 7.5f, 6.0f, color, 1.4f);
                outline(frame, space, 2.0f, 5.5f, 14.0f, 13.0f, color);
                break;

            case StudioIcon::File:
                outline(frame, space, 3.5f, 2.0f, 12.5f, 14.0f, color);
                line(frame, space, 5.5f, 6.0f, 10.5f, 6.0f, color, 1.2f);
                line(frame, space, 5.5f, 8.5f, 10.5f, 8.5f, color, 1.2f);
                line(frame, space, 5.5f, 11.0f, 8.5f, 11.0f, color, 1.2f);
                break;

            case StudioIcon::Undo:
                // An arrow curving back on itself. The head is a filled triangle rather than two
                // strokes: two strokes meeting at an acute angle leave a notch at small sizes.
                arc(frame, space, 8.0f, 9.0f, 4.5f, 200.0f, 340.0f, color);
                triangle(frame, space, 3.5f, 9.0f, 6.5f, 6.5f, 6.5f, 11.0f, color);
                break;

            case StudioIcon::Redo:
                arc(frame, space, 8.0f, 9.0f, 4.5f, 200.0f, 340.0f, color);
                triangle(frame, space, 12.5f, 9.0f, 9.5f, 6.5f, 9.5f, 11.0f, color);
                break;

            case StudioIcon::Delete:
                line(frame, space, 3.0f, 4.0f, 13.0f, 4.0f, color, 1.4f);
                line(frame, space, 6.5f, 4.0f, 6.5f, 2.5f, color, 1.4f);
                line(frame, space, 9.5f, 4.0f, 9.5f, 2.5f, color, 1.4f);
                line(frame, space, 6.5f, 2.5f, 9.5f, 2.5f, color, 1.4f);
                outline(frame, space, 4.0f, 4.0f, 12.0f, 13.5f, color, 1.4f);
                line(frame, space, 8.0f, 6.0f, 8.0f, 11.5f, color, 1.2f);
                break;

            case StudioIcon::Duplicate:
                outline(frame, space, 2.5f, 2.5f, 10.0f, 10.0f, color, 1.4f);
                outline(frame, space, 6.0f, 6.0f, 13.5f, 13.5f, color, 1.4f);
                break;

            case StudioIcon::Translate:
                // Four arrows from a centre: what a move gizmo looks like from above.
                line(frame, space, 8.0f, 3.0f, 8.0f, 13.0f, color, 1.4f);
                line(frame, space, 3.0f, 8.0f, 13.0f, 8.0f, color, 1.4f);
                triangle(frame, space, 8.0f, 1.5f, 6.0f, 4.5f, 10.0f, 4.5f, color);
                triangle(frame, space, 8.0f, 14.5f, 6.0f, 11.5f, 10.0f, 11.5f, color);
                triangle(frame, space, 1.5f, 8.0f, 4.5f, 6.0f, 4.5f, 10.0f, color);
                triangle(frame, space, 14.5f, 8.0f, 11.5f, 6.0f, 11.5f, 10.0f, color);
                break;

            case StudioIcon::Rotate:
                arc(frame, space, 8.0f, 8.0f, 5.0f, 40.0f, 320.0f, color);
                triangle(frame, space, 12.6f, 5.0f, 9.6f, 4.0f, 11.4f, 7.6f, color);
                break;

            case StudioIcon::Scale:
                // Two squares, one small and one large, with nothing between them. The obvious
                // drawing is a diagonal arrow joining them -- and at a sixteen-pixel toolbar the
                // small square becomes three pixels, the arrow becomes a smudge, and the whole
                // thing reads as a dot beside a box. Every element here is large enough to survive
                // the size these are actually drawn at.
                box(frame, space, 1.5f, 9.5f, 6.5f, 14.5f, color);
                outline(frame, space, 8.0f, 1.5f, 14.5f, 8.0f, color, 1.6f);
                break;

            case StudioIcon::Grid:
                line(frame, space, 2.5f, 6.0f, 13.5f, 6.0f, color, 1.2f);
                line(frame, space, 2.5f, 10.0f, 13.5f, 10.0f, color, 1.2f);
                line(frame, space, 6.0f, 2.5f, 6.0f, 13.5f, color, 1.2f);
                line(frame, space, 10.0f, 2.5f, 10.0f, 13.5f, color, 1.2f);
                break;

            case StudioIcon::Focus:
                // Corner brackets around a centre dot: "frame this".
                line(frame, space, 2.5f, 5.5f, 2.5f, 2.5f, color, 1.4f);
                line(frame, space, 2.5f, 2.5f, 5.5f, 2.5f, color, 1.4f);
                line(frame, space, 13.5f, 5.5f, 13.5f, 2.5f, color, 1.4f);
                line(frame, space, 13.5f, 2.5f, 10.5f, 2.5f, color, 1.4f);
                line(frame, space, 2.5f, 10.5f, 2.5f, 13.5f, color, 1.4f);
                line(frame, space, 2.5f, 13.5f, 5.5f, 13.5f, color, 1.4f);
                line(frame, space, 13.5f, 10.5f, 13.5f, 13.5f, color, 1.4f);
                line(frame, space, 13.5f, 13.5f, 10.5f, 13.5f, color, 1.4f);
                box(frame, space, 7.0f, 7.0f, 9.0f, 9.0f, color);
                break;

            case StudioIcon::Play:
                triangle(frame, space, 4.5f, 2.5f, 4.5f, 13.5f, 13.0f, 8.0f, color);
                break;

            case StudioIcon::Pause:
                // Two bars, the international pause, at the same height as Play's triangle so the
                // pair does not jump when one replaces the other in the eye.
                box(frame, space, 4.5f, 2.5f, 7.0f, 13.5f, color);
                box(frame, space, 9.0f, 2.5f, 11.5f, 13.5f, color);
                break;

            case StudioIcon::Step:
                // Play with a wall in front of it: one frame and stop. Same triangle as Play,
                // narrowed to leave room for the bar, so the two read as a family.
                triangle(frame, space, 3.5f, 2.5f, 3.5f, 13.5f, 10.5f, 8.0f, color);
                box(frame, space, 11.5f, 2.5f, 13.5f, 13.5f, color);
                break;

            case StudioIcon::Stop:
                box(frame, space, 4.0f, 4.0f, 12.0f, 12.0f, color);
                break;

            case StudioIcon::Build:
                // A hammer: head across the top, handle descending right of centre. Square-on
                // rather than angled, because an angled head over an angled handle is what the
                // scale icon already is and two icons told apart by which diagonal is which read
                // as one. Off-centre rather than symmetrical, because a bar with a stalk under its
                // middle is a letter T.
                box(frame, space, 2.0f, 2.5f, 12.0f, 6.5f, color);
                box(frame, space, 8.5f, 6.5f, 11.0f, 14.0f, color);
                break;

            case StudioIcon::Package:
                outline(frame, space, 2.5f, 5.0f, 13.5f, 13.5f, color);
                line(frame, space, 2.5f, 5.0f, 8.0f, 2.0f, color, 1.4f);
                line(frame, space, 8.0f, 2.0f, 13.5f, 5.0f, color, 1.4f);
                line(frame, space, 8.0f, 2.0f, 8.0f, 13.5f, color, 1.2f);
                break;

            case StudioIcon::Close:
                line(frame, space, 4.5f, 4.5f, 11.5f, 11.5f, color, 1.6f);
                line(frame, space, 11.5f, 4.5f, 4.5f, 11.5f, color, 1.6f);
                break;

            case StudioIcon::ChevronRight: chevron(frame, space, color, false); break;
            case StudioIcon::ChevronDown: chevron(frame, space, color, true); break;

            case StudioIcon::Search:
                arc(frame, space, 7.0f, 7.0f, 4.0f, 0.0f, 360.0f, color, 1.4f);
                line(frame, space, 10.0f, 10.0f, 13.5f, 13.5f, color, 1.6f);
                break;

            case StudioIcon::Warning:
                // Outlined with a mark inside, not a solid triangle. A filled one has nowhere to
                // put the exclamation without a second colour, and a warning triangle with nothing
                // in it is a shape rather than a sign.
                line(frame, space, 8.0f, 2.0f, 14.5f, 13.5f, color, 1.6f);
                line(frame, space, 14.5f, 13.5f, 1.5f, 13.5f, color, 1.6f);
                line(frame, space, 1.5f, 13.5f, 8.0f, 2.0f, color, 1.6f);
                box(frame, space, 7.3f, 6.0f, 8.7f, 10.0f, color);
                box(frame, space, 7.3f, 11.0f, 8.7f, 12.4f, color);
                break;

            case StudioIcon::Error:
                arc(frame, space, 8.0f, 8.0f, 6.0f, 0.0f, 360.0f, color, 1.6f);
                line(frame, space, 5.5f, 5.5f, 10.5f, 10.5f, color, 1.6f);
                line(frame, space, 10.5f, 5.5f, 5.5f, 10.5f, color, 1.6f);
                break;

            case StudioIcon::Info:
                arc(frame, space, 8.0f, 8.0f, 6.0f, 0.0f, 360.0f, color, 1.4f);
                box(frame, space, 7.3f, 4.0f, 8.7f, 5.4f, color);
                box(frame, space, 7.3f, 6.8f, 8.7f, 12.0f, color);
                break;

            case StudioIcon::None:
            case StudioIcon::Count:
                break;
        }
    }
}
