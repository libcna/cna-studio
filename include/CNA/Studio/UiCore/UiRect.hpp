// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/UiCore/UiRect.hpp
 * @brief The rectangle every layout and hit test is expressed in.
 *
 * `plan.md` STUDIO-03011, STUDIO-03015.
 *
 * Stored as position and size in logical units, which is what layout reasons in. `UiClipRect`
 * stores edges instead, because that is what clipping and the renderer want; converting happens
 * at that boundary rather than forcing one representation to serve both badly.
 *
 * The `split*` operations are the core of the layout engine and are deliberately *consuming*: each
 * one takes a slice off an edge and returns it, leaving the receiver as the remainder. A shell
 * layout then reads as the sequence of decisions it actually is -- take the menu bar off the top,
 * then the status bar off the bottom, then the outliner off the left, and whatever is left is the
 * viewport -- instead of as a page of coordinate arithmetic in which an off-by-one is invisible.
 */

#include <algorithm>

namespace CNA::Studio
{
    /** @brief Padding applied inside a rectangle, per edge. */
    struct UiEdges
    {
        float left = 0.0f;
        float top = 0.0f;
        float right = 0.0f;
        float bottom = 0.0f;

        /** @brief Constructs zero insets. */
        constexpr UiEdges() = default;

        /** @brief Constructs equal insets on every edge. */
        explicit constexpr UiEdges(float all) : left(all), top(all), right(all), bottom(all) {}

        /** @brief Constructs horizontal and vertical insets. */
        constexpr UiEdges(float horizontal, float vertical)
            : left(horizontal), top(vertical), right(horizontal), bottom(vertical) {}

        /** @brief Constructs per-edge insets. */
        constexpr UiEdges(float l, float t, float r, float b) : left(l), top(t), right(r), bottom(b) {}

        /** @brief Total horizontal inset. */
        [[nodiscard]] constexpr float horizontal() const { return left + right; }
        /** @brief Total vertical inset. */
        [[nodiscard]] constexpr float vertical() const { return top + bottom; }
    };

    /**
     * @brief An axis-aligned rectangle in logical units.
     *
     * A rectangle with a negative width or height is *not* representable as a valid area: every
     * operation clamps rather than producing one. That matters because a panel squeezed past its
     * minimum size is a normal thing for a user to do with a splitter, and the result must be an
     * empty rectangle that draws nothing rather than an inverted one that draws everywhere.
     */
    struct UiRect
    {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;

        constexpr UiRect() = default;
        constexpr UiRect(float x_, float y_, float w, float h) : x(x_), y(y_), width(w), height(h) {}

        [[nodiscard]] constexpr float left() const { return x; }
        [[nodiscard]] constexpr float top() const { return y; }
        [[nodiscard]] constexpr float right() const { return x + width; }
        [[nodiscard]] constexpr float bottom() const { return y + height; }
        [[nodiscard]] constexpr float centerX() const { return x + width * 0.5f; }
        [[nodiscard]] constexpr float centerY() const { return y + height * 0.5f; }

        /** @brief Reports whether this rectangle covers no area. */
        [[nodiscard]] constexpr bool isEmpty() const { return width <= 0.0f || height <= 0.0f; }

        /** @brief Reports whether a point is inside, with the left and top edges inclusive. */
        [[nodiscard]] constexpr bool contains(float px, float py) const
        {
            return px >= x && px < right() && py >= y && py < bottom();
        }

        /** @brief Returns this rectangle shrunk by @p edges, clamped to empty rather than inverted. */
        [[nodiscard]] constexpr UiRect inset(const UiEdges& edges) const
        {
            return UiRect{x + edges.left, y + edges.top,
                          std::max(0.0f, width - edges.horizontal()),
                          std::max(0.0f, height - edges.vertical())};
        }

        /** @brief Returns this rectangle shrunk by @p amount on every edge. */
        [[nodiscard]] constexpr UiRect inset(float amount) const { return inset(UiEdges{amount}); }

        /** @brief Returns this rectangle grown by @p amount on every edge. */
        [[nodiscard]] constexpr UiRect expand(float amount) const
        {
            return UiRect{x - amount, y - amount,
                          std::max(0.0f, width + amount * 2.0f),
                          std::max(0.0f, height + amount * 2.0f)};
        }

        /** @brief Returns the overlap of this rectangle and @p other, or an empty rectangle. */
        [[nodiscard]] constexpr UiRect intersect(const UiRect& other) const
        {
            const float l = std::max(x, other.x);
            const float t = std::max(y, other.y);
            const float r = std::min(right(), other.right());
            const float b = std::min(bottom(), other.bottom());
            if (r <= l || b <= t) { return UiRect{l, t, 0.0f, 0.0f}; }
            return UiRect{l, t, r - l, b - t};
        }

        /**
         * @brief Takes @p amount off the top edge and returns it, leaving the remainder.
         *
         * Asking for more than there is yields everything and leaves nothing, rather than an
         * inverted remainder -- a window dragged smaller than its own chrome must produce empty
         * panels, not panels drawn at negative sizes.
         *
         * @param amount Height to take.
         * @return The slice taken.
         */
        constexpr UiRect splitTop(float amount)
        {
            const float taken = std::clamp(amount, 0.0f, height);
            const UiRect slice{x, y, width, taken};
            y += taken;
            height -= taken;
            return slice;
        }

        /**
         * @brief Takes @p amount off the bottom edge and returns it.
         * @param amount Height to take.
         * @return The slice taken.
         */
        constexpr UiRect splitBottom(float amount)
        {
            const float taken = std::clamp(amount, 0.0f, height);
            height -= taken;
            return UiRect{x, y + height, width, taken};
        }

        /**
         * @brief Takes @p amount off the left edge and returns it.
         * @param amount Width to take.
         * @return The slice taken.
         */
        constexpr UiRect splitLeft(float amount)
        {
            const float taken = std::clamp(amount, 0.0f, width);
            const UiRect slice{x, y, taken, height};
            x += taken;
            width -= taken;
            return slice;
        }

        /**
         * @brief Takes @p amount off the right edge and returns it.
         * @param amount Width to take.
         * @return The slice taken.
         */
        constexpr UiRect splitRight(float amount)
        {
            const float taken = std::clamp(amount, 0.0f, width);
            width -= taken;
            return UiRect{x + width, y, taken, height};
        }

        /** @brief Returns this rectangle with its coordinates rounded to whole pixels. */
        [[nodiscard]] UiRect pixelSnapped() const;

        friend constexpr bool operator==(const UiRect& a, const UiRect& b)
        {
            return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
        }
        friend constexpr bool operator!=(const UiRect& a, const UiRect& b) { return !(a == b); }
    };
} // namespace CNA::Studio
