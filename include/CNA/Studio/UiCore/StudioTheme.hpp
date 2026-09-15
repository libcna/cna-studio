// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/UiCore/StudioTheme.hpp
 * @brief The design token model: the single source of visual truth for the CNA Studio UI.
 *
 * `plan.md` STUDIO-03004, STUDIO-03005, STUDIO-03006.
 *
 * Every colour, size, spacing and font in the Studio UI comes from here. A widget that picks its
 * own shade of grey is the mechanism by which a professional tool degrades into a programmer's
 * debug UI: the drift is invisible one widget at a time and obvious across a whole window. So the
 * rule is absolute -- **no literal colour and no literal pixel size at a widget call site** -- and
 * it is enforceable rather than aspirational, because of two decisions below.
 *
 * ### Why tokens are enums and a table, not named struct members
 *
 * A `struct Theme { StudioColor panelBackground; ... }` cannot be *enumerated*. This model can: a
 * test walks every value from 0 to `Count` and asserts the theme defines it. That turns "did anyone
 * forget to give the new token a value in the light theme?" from a question answered by reading
 * into one answered by CI. Adding a token to the enum breaks every incomplete theme on the same
 * commit, which is exactly when it is cheap to fix.
 *
 * ### Why interactive states are their own tokens rather than computed
 *
 * The tempting shortcut is one base colour per surface plus "hover is 8% lighter". It does not
 * survive contact with a dark theme: 8% lighter than a near-black panel is invisible, while 8%
 * lighter than an accent colour is garish, and neither can be tuned without changing the rule for
 * everything. Every interactive state therefore gets a real token that a designer can set.
 *
 * ### Why metrics are logical pixels scaled on read
 *
 * Metrics are authored at 100% and multiplied by the DPI scale inside metric(). One place applies
 * scaling, so no widget can forget to, and no widget can apply it twice. Values that must stay
 * visible at any scale -- hairline borders and separators -- are clamped to at least one physical
 * pixel, because a 1px border at 75% scale that rounds to 0 disappears.
 */

#include "CNA/Studio/Core/StudioMath.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace CNA::Studio
{
    /**
     * @brief A semantic colour role.
     *
     * Roles name a *purpose*, never an appearance: `Accent`, not `Blue`. A theme is free to make
     * the accent orange, and every widget that asked for the accent follows.
     *
     * Values are contiguous from zero and `Count` is the end sentinel, so a theme can be validated
     * by iteration. Append new roles before `Count`.
     */
    enum class StudioColorRole : std::uint16_t
    {
        // --- Background layers, in depth order ------------------------------------------------
        /** @brief Behind everything; the shell itself. */
        AppBackground,
        /** @brief The body of a docked panel. */
        PanelBackground,
        /** @brief A panel's title bar and tab strip. */
        PanelHeader,
        /** @brief The active tab in a tab strip. */
        PanelHeaderActive,
        /**
         * @brief The strip a tab sits *in*, behind and between the tabs.
         *
         * `STUDIO-35020`. Darker than both the tabs and the panel body, so a tab reads as a tab.
         * With one token for the strip and its tabs, the active tab's fill merged into the strip
         * and the strip merged into the panel — three surfaces at one value, which is what makes a
         * dark UI look like a wireframe of itself.
         */
        TabStripBackground,
        /** @brief An inactive tab: raised out of the strip, sunk below the active one. */
        TabInactive,
        /**
         * @brief The chrome the application owns: menu bar, toolbar, status bar.
         *
         * Distinct from a panel's header because they are not panels — they belong to the window,
         * and a user should be able to see where the application ends and the workspace begins.
         */
        WindowChrome,
        /** @brief A menu, dropdown or popup surface: raised above a panel. */
        PopupBackground,
        /** @brief A tooltip surface. */
        TooltipBackground,
        /** @brief A modal dimming veil over the content beneath it. */
        ModalOverlay,

        // --- Controls, one token per interactive state -----------------------------------------
        /** @brief A button, field or checkbox surface at rest. */
        ControlBackground,
        /** @brief The pointer is over it. */
        ControlBackgroundHover,
        /** @brief It is being pressed. */
        ControlBackgroundPressed,
        /** @brief It is selected or checked. */
        ControlBackgroundSelected,
        /** @brief It cannot be interacted with. */
        ControlBackgroundDisabled,

        // --- Accent: the one colour that carries the product's identity -------------------------
        /** @brief Primary action, active state, selection emphasis. */
        Accent,
        /** @brief Accent under the pointer. */
        AccentHover,
        /** @brief Accent while pressed. */
        AccentPressed,
        /** @brief Text and icons drawn on top of an accent fill. */
        AccentForeground,

        // --- Text -------------------------------------------------------------------------------
        /** @brief Body text and labels. */
        TextPrimary,
        /** @brief Supporting text: units, hints, secondary values. */
        TextSecondary,
        /** @brief Text belonging to a disabled control. */
        TextDisabled,
        /** @brief Text on a strongly coloured fill. */
        TextInverse,
        /** @brief An actionable reference to a file, entity or asset. */
        TextLink,

        // --- Structure --------------------------------------------------------------------------
        /** @brief The outline of a control. */
        Border,
        /** @brief A border that needs to read as a real edge, e.g. a popup against a panel. */
        BorderStrong,
        /** @brief A rule between groups of content. */
        Separator,
        /**
         * @brief The outline that separates one docked panel from the next.
         *
         * `STUDIO-35021`. Not `Separator`, which is a rule *inside* content and is deliberately
         * faint. This one has to survive being the only thing between two panels of nearly the
         * same colour, which is the whole job of panel chrome.
         */
        PanelOutline,
        /**
         * @brief Every other row in a list, tree or table.
         *
         * Very close to the panel background on purpose: the eye needs the horizontal run to be
         * traceable across a wide row, and it does not need to be told there are stripes. A visible
         * stripe is a 1990s table.
         */
        RowAlternate,
        /** @brief The row under the pointer. Distinct from selection, which outranks it. */
        RowHover,
        /** @brief The keyboard focus indicator. Never the same token as selection. */
        FocusRing,
        /** @brief Selected rows in a list, tree or table, while that view has focus. */
        Selection,
        /** @brief Selected rows in a view that has lost focus. */
        SelectionInactive,

        // --- Status -----------------------------------------------------------------------------
        /** @brief An operation succeeded. */
        Success,
        /** @brief Something is wrong but not blocking. */
        Warning,
        /** @brief Something failed. */
        Error,
        /** @brief Neutral information. */
        Info,

        // --- Scrollbars -------------------------------------------------------------------------
        /** @brief The trough a scrollbar thumb runs in. */
        ScrollbarTrack,
        /** @brief The draggable thumb. */
        ScrollbarThumb,
        /** @brief The thumb under the pointer. */
        ScrollbarThumbHover,

        // --- Viewport ---------------------------------------------------------------------------
        /** @brief The 3D viewport's clear colour. */
        ViewportBackground,
        /** @brief The viewport's minor grid lines. */
        ViewportGrid,
        /** @brief The viewport's major grid lines and principal axes. */
        ViewportGridMajor,
        /** @brief The outline drawn around a selected object. */
        ViewportSelectionOutline,

        // --- Axes (STUDIO-35032) ----------------------------------------------------------------
        //
        // One set of three, used by the transform gizmo, the orientation widget and the X/Y/Z
        // labels on every vector field. They have to be the same three colours in all three places
        // or the inspector is teaching a mapping the viewport then contradicts -- which is worse
        // than no colour coding, because the user learns it and is then wrong.
        //
        // Red, green, blue in axis order is the convention every 3D tool shares, and a tool that
        // chose differently would be asking its users to unlearn something true everywhere else.
        // Desaturated from the primaries: a saturated red field label beside a saturated green one
        // vibrates, and a property grid is read for hours.
        /** @brief The X axis. */
        AxisX,
        /** @brief The Y axis. */
        AxisY,
        /** @brief The Z axis. */
        AxisZ,
        /** @brief A fourth component, where one exists: W, or alpha. Deliberately neutral. */
        AxisW,

        /** @brief Number of declared roles; not itself a role. */
        Count
    };

    /**
     * @brief A named size or spacing value, authored in logical pixels at 100% scale.
     *
     * Sizes come from a small scale rather than being chosen per widget. A tool in which one panel
     * pads by 6 and its neighbour by 7 looks unfinished for a reason nobody can name, and the fix
     * after the fact is a hundred small edits.
     */
    enum class StudioMetric : std::uint16_t
    {
        /** @brief 2px. Hairline gaps. */
        SpacingXSmall,
        /** @brief 4px. Between tightly related items. */
        SpacingSmall,
        /** @brief 8px. The default gap. */
        SpacingMedium,
        /** @brief 12px. Between groups. */
        SpacingLarge,
        /** @brief 16px. Between sections. */
        SpacingXLarge,

        /** @brief Inner padding of a panel body. */
        PanelPadding,
        /** @brief Height of a panel's title bar. */
        PanelHeaderHeight,

        /** @brief Height of a standard button, field or combo box. */
        ControlHeight,
        /** @brief Height of a compact control, e.g. inside a toolbar. */
        ControlHeightSmall,
        /** @brief Horizontal padding inside a control. */
        ControlPaddingHorizontal,

        /** @brief Height of one row in a list, tree or table. */
        RowHeight,
        /** @brief Horizontal indent per level of tree nesting. */
        IndentWidth,

        /** @brief A standard inline icon. */
        IconSize,
        /** @brief A small icon, e.g. a tree disclosure arrow. */
        IconSizeSmall,
        /** @brief A large icon, e.g. a content-browser thumbnail overlay. */
        IconSizeLarge,

        /** @brief Control outline thickness. Clamped to at least one physical pixel. */
        BorderWidth,
        /** @brief Rule thickness. Clamped to at least one physical pixel. */
        SeparatorThickness,
        /** @brief Focus indicator thickness. Clamped to at least one physical pixel. */
        FocusRingWidth,
        /** @brief Corner radius. Zero produces a square-cornered theme. */
        CornerRadius,

        /** @brief Scrollbar thickness. */
        ScrollbarThickness,
        /** @brief Splitter grab thickness between docked panels. */
        SplitterThickness,
        /** @brief Height of the application menu bar. */
        MenuBarHeight,
        /** @brief Height of the main toolbar. */
        ToolbarHeight,
        /** @brief Height of the status bar. */
        StatusBarHeight,
        /** @brief Height of one tab in a tab strip. */
        TabHeight,

        /** @brief Smallest clickable extent. A control never resolves smaller than this. */
        MinimumHitTarget,

        /** @brief Number of declared metrics; not itself a metric. */
        Count
    };

    /** @brief A typographic role. */
    enum class StudioFontRole : std::uint16_t
    {
        /** @brief Default UI text. */
        Body,
        /** @brief Secondary and dense text. */
        BodySmall,
        /** @brief A panel or section heading. */
        Heading,
        /** @brief A subsection heading. */
        Subheading,
        /** @brief Small supporting text: units, counts, hints. */
        Caption,
        /** @brief Code, logs, identifiers and numeric columns that must align. */
        Monospace,
        /** @brief Number of declared font roles; not itself a role. */
        Count
    };

    /**
     * @brief The interactive state of a control.
     *
     * Passed to colourForState() so that a widget describes *what it is doing* and the theme
     * decides what that looks like.
     */
    enum class StudioControlState : std::uint8_t
    {
        Normal,
        Hover,
        Pressed,
        Selected,
        Disabled
    };

    /** @brief A resolved font: family, size in logical pixels, and weight. */
    struct StudioFontStyle
    {
        /** @brief Font family name, resolved against the loaded font set. */
        std::string family;
        /** @brief Size in logical pixels at 100% scale. */
        float sizePx = 13.0f;
        /** @brief CSS-style weight, 400 regular, 600 semibold, 700 bold. */
        int weight = 400;

        friend bool operator==(const StudioFontStyle& lhs, const StudioFontStyle& rhs)
        {
            return lhs.family == rhs.family && lhs.sizePx == rhs.sizePx && lhs.weight == rhs.weight;
        }
    };

    /**
     * @brief A complete set of design tokens, plus the DPI scale they are resolved at.
     *
     * A theme is a value. Copy it, change a token, and you have a variant -- which is how a
     * user-customised theme is built without a second code path.
     *
     * Construct through dark() or light() rather than default-constructing: a default-constructed
     * theme is deliberately an obvious magenta, so a widget that draws with an unconfigured theme
     * is impossible to miss in a screenshot. Silence would be worse.
     */
    class StudioTheme
    {
    public:
        /** @brief Constructs a placeholder theme whose every colour is an obvious magenta. */
        StudioTheme();

        /** @brief The CNA Studio dark theme. The product default. */
        [[nodiscard]] static StudioTheme dark();

        /** @brief The CNA Studio light theme. */
        [[nodiscard]] static StudioTheme light();

        /** @brief This theme's name, for preferences and diagnostics. */
        [[nodiscard]] std::string_view name() const { return name_; }

        /** @brief Sets this theme's name. */
        void setName(std::string name) { name_ = std::move(name); }

        /**
         * @brief Resolves a colour role.
         * @param role Role to resolve.
         * @return The role's colour, or the placeholder magenta for an out-of-range role.
         */
        [[nodiscard]] StudioColor color(StudioColorRole role) const;

        /**
         * @brief Sets a colour role.
         * @param role Role to set.
         * @param value Colour to use.
         */
        void setColor(StudioColorRole role, StudioColor value);

        /**
         * @brief Resolves a metric, already multiplied by the DPI scale.
         *
         * Thicknesses that must stay visible (border, separator, focus ring) are clamped to at
         * least one physical pixel: a hairline that rounds to zero is a hairline that vanishes.
         *
         * @param metric Metric to resolve.
         * @return The scaled value in physical pixels.
         */
        [[nodiscard]] int metric(StudioMetric metric) const;

        /**
         * @brief Returns a metric's authored value, without DPI scaling.
         * @param metric Metric to read.
         * @return The logical-pixel value as authored.
         */
        [[nodiscard]] int logicalMetric(StudioMetric metric) const;

        /**
         * @brief Sets a metric's authored logical-pixel value.
         * @param metric Metric to set.
         * @param logicalPixels Value at 100% scale.
         */
        void setMetric(StudioMetric metric, int logicalPixels);

        /**
         * @brief Resolves a font role, with its size already multiplied by the DPI scale.
         * @param role Role to resolve.
         * @return The scaled font style.
         */
        [[nodiscard]] StudioFontStyle font(StudioFontRole role) const;

        /**
         * @brief Sets a font role's authored style.
         * @param role Role to set.
         * @param style Style at 100% scale.
         */
        void setFont(StudioFontRole role, StudioFontStyle style);

        /** @brief The current DPI scale. 1.0 is 100%. */
        [[nodiscard]] float scale() const { return scale_; }

        /**
         * @brief Sets the DPI scale.
         *
         * Clamped to a sane range: a scale of zero would collapse every control to nothing, and a
         * negative one is meaningless. Studio supports 100% through 200% as a matter of policy;
         * the clamp is wider so that an unusual display does not produce a broken window.
         *
         * @param scale Scale factor, 1.0 being 100%.
         */
        void setScale(float scale);

        /**
         * @brief Returns the control background for an interactive state.
         *
         * This is the function that makes "no widget invents a shade" practical: a button asks for
         * the background of the state it is in, rather than deciding that pressed means darker.
         *
         * @param state The control's current state.
         * @return The background colour for that state.
         */
        [[nodiscard]] StudioColor controlBackground(StudioControlState state) const;

        /**
         * @brief Returns the text colour appropriate to an interactive state.
         * @param state The control's current state.
         * @return The text colour for that state.
         */
        [[nodiscard]] StudioColor controlText(StudioControlState state) const;

        /**
         * @brief Returns the accent colour for an interactive state.
         * @param state The control's current state.
         * @return The accent colour for that state.
         */
        [[nodiscard]] StudioColor accent(StudioControlState state) const;

        /**
         * @brief Reports whether every colour role has been given a value.
         *
         * Used by the theme-completeness test. A theme that left a role at the placeholder is
         * incomplete, and this says so by iteration rather than by anyone remembering to look.
         *
         * @param outMissingRole Receives the first unset role when the result is false.
         * @return True when no role is still the placeholder.
         */
        [[nodiscard]] bool isComplete(StudioColorRole* outMissingRole = nullptr) const;

    private:
        static constexpr std::size_t kColorCount = static_cast<std::size_t>(StudioColorRole::Count);
        static constexpr std::size_t kMetricCount = static_cast<std::size_t>(StudioMetric::Count);
        static constexpr std::size_t kFontCount = static_cast<std::size_t>(StudioFontRole::Count);

        std::string name_{"placeholder"};
        float scale_ = 1.0f;
        StudioColor colors_[kColorCount]{};
        int metrics_[kMetricCount]{};
        StudioFontStyle fonts_[kFontCount]{};
    };

    /** @brief The colour a default-constructed theme uses, so unconfigured drawing is unmissable. */
    inline constexpr StudioColor kStudioPlaceholderColor{255, 0, 255, 255};

    /**
     * @brief Returns a stable English name for a colour role, for diagnostics and theme editing.
     * @param role Role to name.
     * @return A stable identifier such as `"PanelBackground"`.
     */
    [[nodiscard]] std::string_view studioColorRoleName(StudioColorRole role);

    /**
     * @brief Returns a stable English name for a metric.
     * @param metric Metric to name.
     * @return A stable identifier such as `"ControlHeight"`.
     */
    [[nodiscard]] std::string_view studioMetricName(StudioMetric metric);

    /**
     * @brief Returns a stable English name for a font role.
     * @param role Role to name.
     * @return A stable identifier such as `"Body"`.
     */
    [[nodiscard]] std::string_view studioFontRoleName(StudioFontRole role);
} // namespace CNA::Studio
