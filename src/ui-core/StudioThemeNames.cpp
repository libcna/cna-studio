// SPDX-License-Identifier: MS-PL
/**
 * @file StudioThemeNames.cpp
 * @brief Stable English names for every design token.
 *
 * These are not decoration. A theme is edited, serialised into preferences and reported in
 * diagnostics, and all three need a spelling for a token that does not change when the enum is
 * reordered. Keeping the tables here rather than beside the enum means adding a token is a
 * two-file edit -- which is the point: the completeness tests fail until both halves are done, so
 * a token cannot reach a preferences file with no name to store it under.
 */

#include "CNA/Studio/UiCore/StudioTheme.hpp"

namespace CNA::Studio
{
    std::string_view studioColorRoleName(StudioColorRole role)
    {
        switch (role)
        {
            case StudioColorRole::AppBackground:            return "AppBackground";
            case StudioColorRole::PanelBackground:          return "PanelBackground";
            case StudioColorRole::PanelHeader:              return "PanelHeader";
            case StudioColorRole::PanelHeaderActive:        return "PanelHeaderActive";
            case StudioColorRole::PopupBackground:          return "PopupBackground";
            case StudioColorRole::TooltipBackground:        return "TooltipBackground";
            case StudioColorRole::ModalOverlay:             return "ModalOverlay";
            case StudioColorRole::ControlBackground:        return "ControlBackground";
            case StudioColorRole::ControlBackgroundHover:   return "ControlBackgroundHover";
            case StudioColorRole::ControlBackgroundPressed: return "ControlBackgroundPressed";
            case StudioColorRole::ControlBackgroundSelected: return "ControlBackgroundSelected";
            case StudioColorRole::ControlBackgroundDisabled: return "ControlBackgroundDisabled";
            case StudioColorRole::Accent:                   return "Accent";
            case StudioColorRole::AccentHover:              return "AccentHover";
            case StudioColorRole::AccentPressed:            return "AccentPressed";
            case StudioColorRole::AccentForeground:         return "AccentForeground";
            case StudioColorRole::TextPrimary:              return "TextPrimary";
            case StudioColorRole::TextSecondary:            return "TextSecondary";
            case StudioColorRole::TextDisabled:             return "TextDisabled";
            case StudioColorRole::TextInverse:              return "TextInverse";
            case StudioColorRole::TextLink:                 return "TextLink";
            case StudioColorRole::Border:                   return "Border";
            case StudioColorRole::BorderStrong:             return "BorderStrong";
            case StudioColorRole::Separator:                return "Separator";
            case StudioColorRole::FocusRing:                return "FocusRing";
            case StudioColorRole::Selection:                return "Selection";
            case StudioColorRole::SelectionInactive:        return "SelectionInactive";
            case StudioColorRole::Success:                  return "Success";
            case StudioColorRole::Warning:                  return "Warning";
            case StudioColorRole::Error:                    return "Error";
            case StudioColorRole::Info:                     return "Info";
            case StudioColorRole::ScrollbarTrack:           return "ScrollbarTrack";
            case StudioColorRole::ScrollbarThumb:           return "ScrollbarThumb";
            case StudioColorRole::ScrollbarThumbHover:      return "ScrollbarThumbHover";
            case StudioColorRole::ViewportBackground:       return "ViewportBackground";
            case StudioColorRole::ViewportGrid:             return "ViewportGrid";
            case StudioColorRole::ViewportGridMajor:        return "ViewportGridMajor";
            case StudioColorRole::ViewportSelectionOutline: return "ViewportSelectionOutline";
            case StudioColorRole::Count:                    break;
        }
        return "";
    }

    std::string_view studioMetricName(StudioMetric metric)
    {
        switch (metric)
        {
            case StudioMetric::SpacingXSmall:            return "SpacingXSmall";
            case StudioMetric::SpacingSmall:             return "SpacingSmall";
            case StudioMetric::SpacingMedium:            return "SpacingMedium";
            case StudioMetric::SpacingLarge:             return "SpacingLarge";
            case StudioMetric::SpacingXLarge:            return "SpacingXLarge";
            case StudioMetric::PanelPadding:             return "PanelPadding";
            case StudioMetric::PanelHeaderHeight:        return "PanelHeaderHeight";
            case StudioMetric::ControlHeight:            return "ControlHeight";
            case StudioMetric::ControlHeightSmall:       return "ControlHeightSmall";
            case StudioMetric::ControlPaddingHorizontal: return "ControlPaddingHorizontal";
            case StudioMetric::RowHeight:                return "RowHeight";
            case StudioMetric::IndentWidth:              return "IndentWidth";
            case StudioMetric::IconSize:                 return "IconSize";
            case StudioMetric::IconSizeSmall:            return "IconSizeSmall";
            case StudioMetric::IconSizeLarge:            return "IconSizeLarge";
            case StudioMetric::BorderWidth:              return "BorderWidth";
            case StudioMetric::SeparatorThickness:       return "SeparatorThickness";
            case StudioMetric::FocusRingWidth:           return "FocusRingWidth";
            case StudioMetric::CornerRadius:             return "CornerRadius";
            case StudioMetric::ScrollbarThickness:       return "ScrollbarThickness";
            case StudioMetric::SplitterThickness:        return "SplitterThickness";
            case StudioMetric::MenuBarHeight:            return "MenuBarHeight";
            case StudioMetric::ToolbarHeight:            return "ToolbarHeight";
            case StudioMetric::StatusBarHeight:          return "StatusBarHeight";
            case StudioMetric::TabHeight:                return "TabHeight";
            case StudioMetric::MinimumHitTarget:         return "MinimumHitTarget";
            case StudioMetric::Count:                    break;
        }
        return "";
    }

    std::string_view studioFontRoleName(StudioFontRole role)
    {
        switch (role)
        {
            case StudioFontRole::Body:       return "Body";
            case StudioFontRole::BodySmall:  return "BodySmall";
            case StudioFontRole::Heading:    return "Heading";
            case StudioFontRole::Subheading: return "Subheading";
            case StudioFontRole::Caption:    return "Caption";
            case StudioFontRole::Monospace:  return "Monospace";
            case StudioFontRole::Count:      break;
        }
        return "";
    }
} // namespace CNA::Studio
