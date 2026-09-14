// SPDX-License-Identifier: MS-PL
/**
 * @file StudioEmbeddedFonts.cpp
 * @brief Binds the typeface enumeration to the arrays CMake generates from the vendored files.
 *
 * The arrays themselves are generated at build time by `cmake/EmbedBinary.cmake` from
 * `third_party/fonts`, rather than committed. The font files are the source of truth, and a
 * committed copy of their bytes in another encoding would be a second one — free to drift, and
 * impossible to review.
 */

#include "CNA/Studio/UiCore/StudioEmbeddedFonts.hpp"

namespace CNA::Studio
{
    namespace Generated
    {
        extern const unsigned char kPlexSansRegular[];
        extern const unsigned long kPlexSansRegularSize;
        extern const unsigned char kPlexSansSemiBold[];
        extern const unsigned long kPlexSansSemiBoldSize;
        extern const unsigned char kPlexMonoRegular[];
        extern const unsigned long kPlexMonoRegularSize;
    } // namespace Generated

    StudioEmbeddedFont studioEmbeddedFont(StudioTypeface typeface)
    {
        switch (typeface)
        {
            case StudioTypeface::SansRegular:
                return {Generated::kPlexSansRegular,
                        static_cast<std::size_t>(Generated::kPlexSansRegularSize)};
            case StudioTypeface::SansSemiBold:
                return {Generated::kPlexSansSemiBold,
                        static_cast<std::size_t>(Generated::kPlexSansSemiBoldSize)};
            case StudioTypeface::Monospace:
                return {Generated::kPlexMonoRegular,
                        static_cast<std::size_t>(Generated::kPlexMonoRegularSize)};
            case StudioTypeface::Count:
                break;
        }
        return {};
    }
} // namespace CNA::Studio
