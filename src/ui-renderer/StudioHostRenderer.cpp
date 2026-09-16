// SPDX-License-Identifier: MS-PL
/**
 * @file StudioHostRenderer.cpp
 * @brief Which CNA renderer this binary was compiled against.
 */

#include "CNA/Studio/UiRenderer/StudioHostRenderer.hpp"

#include "CNA/GraphicsRendererType.hpp"

namespace CNA::Studio
{
    std::string studioHostCnaRendererName()
    {
        return std::string{CNA::getCurrentGraphicsRendererName()};
    }
} // namespace CNA::Studio
