// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/UiCore/StudioIcons.hpp
 * @brief Studio's editor icons, drawn as vector paths rather than sampled from an atlas.
 *
 * `plan.md` STUDIO-04008, and the decision recorded on `STUDIO-04009`.
 *
 * ### Why not an icon font
 *
 * The obvious route is to vendor one. It costs another few hundred kilobytes, another licence to
 * track, a second atlas to manage, and — the part that matters most — a visual language designed
 * for somebody else's product. Drawing two dozen shapes over the primitives the draw list already
 * has costs none of that, is crisp at *every* DPI scale rather than at the sizes somebody baked,
 * and leaves the set Studio's own.
 *
 * ### Authored on a 16-unit grid, drawn at any size
 *
 * Every path is written in a 0..16 square and mapped onto whatever rectangle it is asked for. That
 * is what makes one definition serve a 14-pixel toolbar and a 32-pixel one at 200% — and it is why
 * these are described as strokes and polygons rather than as pixels. Stroke width scales with the
 * icon and is clamped to at least one physical pixel, because a hairline that rounds to zero is a
 * hairline that disappears.
 *
 * ### Only what Studio uses
 *
 * The set grows when a control needs one, not in anticipation. An icon nobody draws is a shape
 * nobody has reviewed, and a set assembled up front is mostly that.
 */

#pragma once

#include "CNA/Studio/UiCore/StudioFrame.hpp"
#include "CNA/Studio/UiCore/StudioTheme.hpp"
#include "CNA/Studio/UiCore/UiRect.hpp"

#include <cstdint>
#include <string_view>

namespace CNA::Studio
{
    /** @brief Every icon Studio draws. */
    enum class StudioIcon : std::uint8_t
    {
        /** @brief No icon; draws nothing. */
        None,

        // File and project
        Save,
        Folder,
        File,

        // Editing
        Undo,
        Redo,
        Delete,
        Duplicate,

        // Transform tools
        Translate,
        Rotate,
        Scale,

        // View
        Grid,
        Focus,

        // Play and build
        Play,
        Pause,
        Step,
        Stop,
        Build,
        Package,

        // Chrome
        Close,
        ChevronRight,
        ChevronDown,
        Search,
        Warning,
        Error,
        Info,

        // --- Scene contents (STUDIO-35030) ---------------------------------------------------
        //
        // What a World Outliner row *is*, at a glance. A list of names in one weight is a list the
        // eye has to read; a list with a camera, a light and three meshes in it is one it can scan.
        // Drawn from the same 0..16 grid and the same primitives as the action icons, so the set
        // stays one visual language rather than becoming two.
        /** @brief A generic entity: something in the scene with a transform and nothing else. */
        Entity,
        /** @brief A camera. */
        Camera,
        /** @brief A light. */
        Light,
        /** @brief A mesh or model. */
        Mesh,
        /** @brief A 2D sprite. */
        Sprite,
        /** @brief An instance of a prefab. */
        Prefab,

        // --- Asset kinds ------------------------------------------------------------------------
        /** @brief A texture or image asset. */
        Texture,
        /** @brief A material asset. */
        Material,
        /** @brief An audio asset. */
        Audio,
        /** @brief A scene asset. */
        Scene,

        // --- Row affordances --------------------------------------------------------------------
        /** @brief Shown in the scene and in a build. */
        Visible,
        /** @brief Hidden. The same eye with a stroke through it, so the pair reads as one control. */
        Hidden,
        /** @brief Locked against selection and editing. */
        Lock,
        /** @brief Unlocked. */
        Unlock,
        /** @brief Add something. */
        Add,
        /** @brief The selection tool. */
        Select,

        /** @brief Number of declared icons; not itself one. */
        Count
    };

    /** @brief The stable name of an icon, for preferences and tests. */
    [[nodiscard]] std::string_view studioIconName(StudioIcon icon);

    /** @brief Parses a name produced by @ref studioIconName; returns false for an unknown one. */
    [[nodiscard]] bool parseStudioIcon(std::string_view name, StudioIcon& out);

    /**
     * @brief Draws @p icon inside @p bounds.
     *
     * Draw-pass only, like every other pure-drawing helper: an icon routes no input, and the
     * control that carries it has already done the hit-testing.
     *
     * The icon is centred in @p bounds and drawn square at the smaller of its two extents, so a
     * caller can pass a button's whole rectangle without first working out where the glyph goes.
     *
     * @param frame Frame to draw into.
     * @param bounds Rectangle to draw within.
     * @param icon Which icon.
     * @param color Colour to draw it in.
     */
    void studioDrawIcon(StudioFrame& frame, const UiRect& bounds, StudioIcon icon,
                        StudioColor color);

    /**
     * @brief The icon a registered action carries, from its id.
     *
     * The mapping lives here rather than on `StudioAction` so that `cna-studio-ui-core`'s widget
     * layer stays the only thing that knows what a picture of "undo" looks like — and so a command
     * registered by a plugin gets a sensible default rather than needing to know about this
     * enumeration at all.
     *
     * @param actionId An action id, e.g. `"studio.edit.undo"`.
     * @return Its icon, or @ref StudioIcon::None when it has none.
     */
    [[nodiscard]] StudioIcon studioIconForAction(std::string_view actionId);
}
