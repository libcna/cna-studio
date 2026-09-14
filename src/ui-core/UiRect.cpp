// SPDX-License-Identifier: MS-PL
/**
 * @file UiRect.cpp
 * @brief Out-of-line UiRect operations.
 */

#include "CNA/Studio/UiCore/UiRect.hpp"

#include <cmath>

namespace CNA::Studio
{
    UiRect UiRect::pixelSnapped() const
    {
        // Edges are rounded, then the size is derived from the rounded edges. Rounding position
        // and size independently lets a one-pixel separator become two pixels wide at one
        // position and zero at another, which reads as the rule flickering as a panel is dragged.
        const float l = std::round(x);
        const float t = std::round(y);
        const float r = std::round(right());
        const float b = std::round(bottom());
        return UiRect{l, t, std::max(0.0f, r - l), std::max(0.0f, b - t)};
    }
} // namespace CNA::Studio
