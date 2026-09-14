// SPDX-License-Identifier: MS-PL
/**
 * @file StudioTheme.cpp
 * @brief The CNA Studio design tokens and the two shipped themes.
 *
 * The palettes below are original to CNA Studio. They are deliberately restrained: a professional
 * authoring tool is a background against which the user's content is the subject, so the chrome
 * stays low-contrast and desaturated and the accent is spent only where it carries meaning.
 */

#include "CNA/Studio/UiCore/StudioTheme.hpp"

#include <algorithm>
#include <cmath>

namespace CNA::Studio
{
    namespace
    {
        /** @brief Convenience for writing opaque palette entries. */
        constexpr StudioColor rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b)
        {
            return StudioColor{r, g, b, 255};
        }

        /** @brief Convenience for writing translucent palette entries. */
        constexpr StudioColor rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
        {
            return StudioColor{r, g, b, a};
        }

        /**
         * @brief Thicknesses that must never round away to nothing.
         *
         * A 1px border at a 0.75 scale rounds to 0 and the control loses its outline entirely. The
         * cost of clamping is that these are very slightly heavy below 100%; the cost of not
         * clamping is that they disappear, which is not a trade.
         */
        constexpr bool isHairline(StudioMetric metric)
        {
            return metric == StudioMetric::BorderWidth
                || metric == StudioMetric::SeparatorThickness
                || metric == StudioMetric::FocusRingWidth;
        }

        /** @brief The metric defaults shared by every theme, in logical pixels at 100%. */
        void applyMetricDefaults(StudioTheme& theme)
        {
            theme.setMetric(StudioMetric::SpacingXSmall, 2);
            theme.setMetric(StudioMetric::SpacingSmall, 4);
            theme.setMetric(StudioMetric::SpacingMedium, 8);
            theme.setMetric(StudioMetric::SpacingLarge, 12);
            theme.setMetric(StudioMetric::SpacingXLarge, 16);

            theme.setMetric(StudioMetric::PanelPadding, 8);
            theme.setMetric(StudioMetric::PanelHeaderHeight, 26);

            theme.setMetric(StudioMetric::ControlHeight, 24);
            theme.setMetric(StudioMetric::ControlHeightSmall, 20);
            theme.setMetric(StudioMetric::ControlPaddingHorizontal, 8);

            theme.setMetric(StudioMetric::RowHeight, 22);
            theme.setMetric(StudioMetric::IndentWidth, 16);

            theme.setMetric(StudioMetric::IconSize, 16);
            theme.setMetric(StudioMetric::IconSizeSmall, 12);
            theme.setMetric(StudioMetric::IconSizeLarge, 24);

            theme.setMetric(StudioMetric::BorderWidth, 1);
            theme.setMetric(StudioMetric::SeparatorThickness, 1);
            theme.setMetric(StudioMetric::FocusRingWidth, 1);
            theme.setMetric(StudioMetric::CornerRadius, 3);

            theme.setMetric(StudioMetric::ScrollbarThickness, 12);
            theme.setMetric(StudioMetric::SplitterThickness, 4);
            theme.setMetric(StudioMetric::MenuBarHeight, 24);
            theme.setMetric(StudioMetric::ToolbarHeight, 32);
            theme.setMetric(StudioMetric::StatusBarHeight, 22);
            theme.setMetric(StudioMetric::TabHeight, 26);

            theme.setMetric(StudioMetric::MinimumHitTarget, 16);
        }

        /**
         * @brief The font defaults shared by every theme.
         *
         * Family names are logical rather than file names: the renderer resolves them against the
         * font set it actually loaded, and falls back rather than failing. Which concrete faces
         * ship, and under which licences, is STUDIO-04009 -- naming a specific licensed font here
         * before that task has chosen one would be a promise this layer cannot keep.
         */
        void applyFontDefaults(StudioTheme& theme)
        {
            theme.setFont(StudioFontRole::Body,       StudioFontStyle{"Studio Sans", 13.0f, 400});
            theme.setFont(StudioFontRole::BodySmall,  StudioFontStyle{"Studio Sans", 12.0f, 400});
            theme.setFont(StudioFontRole::Heading,    StudioFontStyle{"Studio Sans", 15.0f, 600});
            theme.setFont(StudioFontRole::Subheading, StudioFontStyle{"Studio Sans", 13.0f, 600});
            theme.setFont(StudioFontRole::Caption,    StudioFontStyle{"Studio Sans", 11.0f, 400});
            theme.setFont(StudioFontRole::Monospace,  StudioFontStyle{"Studio Mono", 12.0f, 400});
        }
    } // namespace

    StudioTheme::StudioTheme()
    {
        for (std::size_t i = 0; i < kColorCount; ++i) { colors_[i] = kStudioPlaceholderColor; }
        applyMetricDefaults(*this);
        applyFontDefaults(*this);
    }

    StudioColor StudioTheme::color(StudioColorRole role) const
    {
        const auto index = static_cast<std::size_t>(role);
        if (index >= kColorCount) { return kStudioPlaceholderColor; }
        return colors_[index];
    }

    void StudioTheme::setColor(StudioColorRole role, StudioColor value)
    {
        const auto index = static_cast<std::size_t>(role);
        if (index >= kColorCount) { return; }
        colors_[index] = value;
    }

    int StudioTheme::logicalMetric(StudioMetric metric) const
    {
        const auto index = static_cast<std::size_t>(metric);
        if (index >= kMetricCount) { return 0; }
        return metrics_[index];
    }

    int StudioTheme::metric(StudioMetric metric) const
    {
        const int logical = logicalMetric(metric);
        if (logical == 0) { return 0; }

        const int scaled = static_cast<int>(std::lround(static_cast<float>(logical) * scale_));
        if (isHairline(metric)) { return std::max(1, scaled); }
        return scaled;
    }

    void StudioTheme::setMetric(StudioMetric metric, int logicalPixels)
    {
        const auto index = static_cast<std::size_t>(metric);
        if (index >= kMetricCount) { return; }
        metrics_[index] = logicalPixels;
    }

    StudioFontStyle StudioTheme::font(StudioFontRole role) const
    {
        const auto index = static_cast<std::size_t>(role);
        if (index >= kFontCount) { return StudioFontStyle{}; }

        StudioFontStyle scaled = fonts_[index];
        scaled.sizePx *= scale_;
        return scaled;
    }

    void StudioTheme::setFont(StudioFontRole role, StudioFontStyle style)
    {
        const auto index = static_cast<std::size_t>(role);
        if (index >= kFontCount) { return; }
        fonts_[index] = std::move(style);
    }

    void StudioTheme::setScale(float scale)
    {
        // Wider than the 100%-200% Studio supports as policy, because an unusual display should
        // produce an unusual window rather than a broken one. Zero and negative are not scales.
        scale_ = std::clamp(scale, 0.5f, 4.0f);
    }

    StudioColor StudioTheme::controlBackground(StudioControlState state) const
    {
        switch (state)
        {
            case StudioControlState::Hover:    return color(StudioColorRole::ControlBackgroundHover);
            case StudioControlState::Pressed:  return color(StudioColorRole::ControlBackgroundPressed);
            case StudioControlState::Selected: return color(StudioColorRole::ControlBackgroundSelected);
            case StudioControlState::Disabled: return color(StudioColorRole::ControlBackgroundDisabled);
            case StudioControlState::Normal:   break;
        }
        return color(StudioColorRole::ControlBackground);
    }

    StudioColor StudioTheme::controlText(StudioControlState state) const
    {
        if (state == StudioControlState::Disabled) { return color(StudioColorRole::TextDisabled); }
        return color(StudioColorRole::TextPrimary);
    }

    StudioColor StudioTheme::accent(StudioControlState state) const
    {
        switch (state)
        {
            case StudioControlState::Hover:   return color(StudioColorRole::AccentHover);
            case StudioControlState::Pressed: return color(StudioColorRole::AccentPressed);
            // A disabled control never draws in the accent: the accent means "this does something".
            case StudioControlState::Disabled: return color(StudioColorRole::ControlBackgroundDisabled);
            case StudioControlState::Selected:
            case StudioControlState::Normal:  break;
        }
        return color(StudioColorRole::Accent);
    }

    bool StudioTheme::isComplete(StudioColorRole* outMissingRole) const
    {
        for (std::size_t i = 0; i < kColorCount; ++i)
        {
            if (colors_[i] == kStudioPlaceholderColor)
            {
                if (outMissingRole != nullptr) { *outMissingRole = static_cast<StudioColorRole>(i); }
                return false;
            }
        }
        return true;
    }

    StudioTheme StudioTheme::dark()
    {
        StudioTheme t;
        t.setName("CNA Studio Dark");

        // Four background layers, each a clear step from the last. The steps are small in absolute
        // terms because a dark UI reads depth from small differences; making them larger produces
        // a striped look rather than a layered one.
        t.setColor(StudioColorRole::AppBackground,    rgb(24, 25, 28));
        t.setColor(StudioColorRole::PanelBackground,  rgb(32, 34, 38));
        t.setColor(StudioColorRole::PanelHeader,      rgb(38, 40, 45));
        t.setColor(StudioColorRole::PanelHeaderActive, rgb(46, 49, 55));
        t.setColor(StudioColorRole::PopupBackground,  rgb(42, 44, 50));
        t.setColor(StudioColorRole::TooltipBackground, rgb(52, 55, 62));
        t.setColor(StudioColorRole::ModalOverlay,     rgba(0, 0, 0, 128));

        t.setColor(StudioColorRole::ControlBackground,         rgb(48, 51, 57));
        t.setColor(StudioColorRole::ControlBackgroundHover,    rgb(58, 62, 69));
        t.setColor(StudioColorRole::ControlBackgroundPressed,  rgb(40, 43, 48));
        t.setColor(StudioColorRole::ControlBackgroundSelected, rgb(52, 78, 112));
        t.setColor(StudioColorRole::ControlBackgroundDisabled, rgb(38, 40, 44));

        // A desaturated blue. Saturated accents are exhausting in a tool someone uses all day, and
        // they compete with the colours in the user's own content -- which is the actual subject.
        t.setColor(StudioColorRole::Accent,           rgb(74, 144, 217));
        t.setColor(StudioColorRole::AccentHover,      rgb(92, 160, 230));
        t.setColor(StudioColorRole::AccentPressed,    rgb(58, 124, 194));
        t.setColor(StudioColorRole::AccentForeground, rgb(255, 255, 255));

        // Not pure white: #FFFFFF on a dark ground shimmers at small sizes.
        t.setColor(StudioColorRole::TextPrimary,   rgb(228, 230, 234));
        t.setColor(StudioColorRole::TextSecondary, rgb(150, 155, 163));
        t.setColor(StudioColorRole::TextDisabled,  rgb(102, 106, 113));
        t.setColor(StudioColorRole::TextInverse,   rgb(24, 25, 28));
        t.setColor(StudioColorRole::TextLink,      rgb(104, 170, 235));

        t.setColor(StudioColorRole::Border,            rgb(58, 61, 68));
        t.setColor(StudioColorRole::BorderStrong,      rgb(76, 80, 88));
        t.setColor(StudioColorRole::Separator,         rgb(45, 47, 53));
        // The focus ring is deliberately NOT the accent: focus and selection are different facts
        // and a user must be able to see both at once on the same row.
        t.setColor(StudioColorRole::FocusRing,         rgb(126, 186, 245));
        t.setColor(StudioColorRole::Selection,         rgb(52, 78, 112));
        t.setColor(StudioColorRole::SelectionInactive, rgb(56, 59, 66));

        t.setColor(StudioColorRole::Success, rgb(106, 176, 118));
        t.setColor(StudioColorRole::Warning, rgb(214, 168, 84));
        t.setColor(StudioColorRole::Error,   rgb(214, 104, 100));
        t.setColor(StudioColorRole::Info,    rgb(104, 156, 204));

        t.setColor(StudioColorRole::ScrollbarTrack,      rgb(30, 32, 36));
        t.setColor(StudioColorRole::ScrollbarThumb,      rgb(62, 66, 74));
        t.setColor(StudioColorRole::ScrollbarThumbHover, rgb(82, 87, 96));

        // Darker than the panels, so the viewport reads as a window into the scene rather than as
        // another panel.
        t.setColor(StudioColorRole::ViewportBackground,       rgb(42, 44, 48));
        t.setColor(StudioColorRole::ViewportGrid,             rgb(58, 61, 66));
        t.setColor(StudioColorRole::ViewportGridMajor,        rgb(76, 80, 86));
        t.setColor(StudioColorRole::ViewportSelectionOutline, rgb(255, 156, 48));

        return t;
    }

    StudioTheme StudioTheme::light()
    {
        StudioTheme t;
        t.setName("CNA Studio Light");

        t.setColor(StudioColorRole::AppBackground,     rgb(238, 239, 241));
        t.setColor(StudioColorRole::PanelBackground,   rgb(250, 250, 251));
        t.setColor(StudioColorRole::PanelHeader,       rgb(240, 241, 243));
        t.setColor(StudioColorRole::PanelHeaderActive, rgb(255, 255, 255));
        t.setColor(StudioColorRole::PopupBackground,   rgb(255, 255, 255));
        t.setColor(StudioColorRole::TooltipBackground, rgb(252, 252, 253));
        t.setColor(StudioColorRole::ModalOverlay,      rgba(0, 0, 0, 76));

        t.setColor(StudioColorRole::ControlBackground,         rgb(255, 255, 255));
        t.setColor(StudioColorRole::ControlBackgroundHover,    rgb(243, 244, 246));
        t.setColor(StudioColorRole::ControlBackgroundPressed,  rgb(230, 232, 235));
        t.setColor(StudioColorRole::ControlBackgroundSelected, rgb(210, 227, 246));
        t.setColor(StudioColorRole::ControlBackgroundDisabled, rgb(242, 242, 244));

        t.setColor(StudioColorRole::Accent,           rgb(29, 110, 184));
        t.setColor(StudioColorRole::AccentHover,      rgb(38, 126, 204));
        t.setColor(StudioColorRole::AccentPressed,    rgb(22, 92, 158));
        t.setColor(StudioColorRole::AccentForeground, rgb(255, 255, 255));

        t.setColor(StudioColorRole::TextPrimary,   rgb(28, 30, 34));
        t.setColor(StudioColorRole::TextSecondary, rgb(94, 99, 107));
        t.setColor(StudioColorRole::TextDisabled,  rgb(158, 162, 169));
        t.setColor(StudioColorRole::TextInverse,   rgb(255, 255, 255));
        t.setColor(StudioColorRole::TextLink,      rgb(20, 96, 170));

        t.setColor(StudioColorRole::Border,            rgb(210, 213, 218));
        t.setColor(StudioColorRole::BorderStrong,      rgb(180, 184, 190));
        t.setColor(StudioColorRole::Separator,         rgb(226, 228, 232));
        t.setColor(StudioColorRole::FocusRing,         rgb(29, 110, 184));
        t.setColor(StudioColorRole::Selection,         rgb(210, 227, 246));
        t.setColor(StudioColorRole::SelectionInactive, rgb(232, 233, 236));

        t.setColor(StudioColorRole::Success, rgb(38, 132, 58));
        t.setColor(StudioColorRole::Warning, rgb(168, 118, 20));
        t.setColor(StudioColorRole::Error,   rgb(186, 52, 48));
        t.setColor(StudioColorRole::Info,    rgb(38, 106, 168));

        t.setColor(StudioColorRole::ScrollbarTrack,      rgb(243, 244, 246));
        t.setColor(StudioColorRole::ScrollbarThumb,      rgb(200, 203, 208));
        t.setColor(StudioColorRole::ScrollbarThumbHover, rgb(176, 180, 186));

        t.setColor(StudioColorRole::ViewportBackground,       rgb(222, 224, 228));
        t.setColor(StudioColorRole::ViewportGrid,             rgb(206, 209, 214));
        t.setColor(StudioColorRole::ViewportGridMajor,        rgb(184, 188, 194));
        t.setColor(StudioColorRole::ViewportSelectionOutline, rgb(214, 116, 16));

        return t;
    }
} // namespace CNA::Studio
