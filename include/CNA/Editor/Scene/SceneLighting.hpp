// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Scene/SceneLighting.hpp
 * @brief What lights a scene, reduced to what an XNA-shaped effect can be told (ED-402, ED-404).
 *
 * `CNA.Light` has been a component descriptor since Phase 1 and nothing has ever read it. This is
 * the half that reads it, and it is here -- CNA-free, in `cna-editor-scene` -- rather than in the
 * viewport for the reason everything else in this module is: what lights a scene is a fact about
 * the *document*, and a fact about a document can be tested without a GPU.
 *
 * **The reduction this file performs is the interesting part, and it is forced by the API.**
 * `IEffectLights` -- which both `BasicEffect` and CNA's `PbrEffect` implement -- is XNA's
 * fixed-function lighting: an ambient colour and exactly **three directional lights**. It has no
 * point light and no spot light. A scene may hold any number of all three kinds, so something has
 * to give, and there are only three honest options: draw nothing, ignore what does not fit, or
 * approximate. This picks the third and writes down exactly how, because an approximation nobody
 * can see the shape of is indistinguishable from a bug.
 *
 * - **A directional light is used as it is.** Its direction is its entity's own forward axis, so
 *   rotating the light entity in the viewport turns the light, which is the only behaviour a user
 *   would predict.
 * - **A point or spot light becomes a directional light aimed at whatever is being drawn**, and
 *   dimmed by how far outside its `range` that thing is. That is the classic XNA workaround and it
 *   is genuinely position-dependent: the same lamp lights two entities from two different angles,
 *   which is what a point light *does*. What it cannot do is fall off across one large model --
 *   the whole model is lit as though it were at its own origin.
 * - **A spot light's cone is not modelled at all**, only its direction and range. `IEffectLights`
 *   has nowhere to put a cone angle. Spot lights therefore light like point lights, and the
 *   Validation panel is where that should be said to a user rather than here.
 * - **Where more than three lights would apply, the three brightest at that point win.** Brightest
 *   rather than nearest or first-in-document: a distant sun matters more to how a model looks than
 *   a dim lamp beside it, and document order is not something a user arranges deliberately.
 *
 * When a scene has no enabled light at all, `computeEffectLighting` says so through
 * `EffectLighting::useDefaultLighting` and the renderer calls XNA's own `EnableDefaultLighting()`.
 * That is a decision, not a fallback for its own sake: a scene with no lights rendered black looks
 * exactly like a renderer that is broken, and the commonest scene of all -- one somebody has just
 * dropped a model into -- has no lights in it.
 */

#include <array>
#include <cstddef>
#include <vector>

#include "CNA/Editor/Core/EditorMath.hpp"
#include "CNA/Editor/Core/Uuid.hpp"

namespace CNA::Editor
{
    class SceneDocument;

    /** @brief Which of `CNA.Light`'s three kinds a light is. */
    enum class SceneLightKind
    {
        Directional,
        Point,
        Spot
    };

    /** @brief Returns the stable name of @p kind, matching the descriptor's enum options. */
    [[nodiscard]] const char* toString(SceneLightKind kind);

    /** @brief One `CNA.Light` in the scene, resolved into world space. */
    struct SceneLight
    {
        Uuid entityId;

        SceneLightKind kind = SceneLightKind::Directional;

        /** @brief World position, from the entity's transform. Meaningless for a directional light. */
        EditorVector3 position;

        /**
         * @brief Unit world direction the light points along: the entity's own forward axis.
         *
         * +Z rotated by the entity's world rotation. +Z because this editor's world is Y-down and
         * its 2D plane is XY, so the axis that points *into* the scene is the one a light with no
         * rotation should shine along -- an unrotated light shines the way the unrotated camera
         * looks, which is what makes a newly added light do something visible.
         */
        EditorVector3 direction{0.0f, 0.0f, 1.0f};

        /** @brief The light's colour, straight from the component. */
        EditorColor color{255, 255, 255, 255};

        /** @brief Multiplies the colour. Not clamped: over-bright is a legitimate authoring choice. */
        float intensity = 1.0f;

        /** @brief How far a point or spot light reaches, in world units. Ignored for directional. */
        float range = 10.0f;
    };

    /**
     * @brief Returns every enabled `CNA.Light` in @p scene, in document order.
     *
     * A light on a disabled entity is left out, and so is one whose entity has no transform: a
     * light with no position is not a light this editor can place, and silently giving it the
     * origin would put it somewhere the user never chose.
     */
    [[nodiscard]] std::vector<SceneLight> collectSceneLights(const SceneDocument& scene);

    /** @brief One directional light, in the form `IEffectLights` takes. */
    struct EffectDirectionalLight
    {
        /** @brief Unit direction the light travels along. */
        EditorVector3 direction{0.0f, 0.0f, 1.0f};

        /** @brief Colour premultiplied by intensity and by any distance falloff. */
        EditorVector3 diffuseColor{1.0f, 1.0f, 1.0f};

        /** @brief The specular colour, which is the diffuse one -- XNA's own default behaviour. */
        EditorVector3 specularColor{1.0f, 1.0f, 1.0f};
    };

    /** @brief Everything an `IEffectLights` needs, for one object at one place in the world. */
    struct EffectLighting
    {
        /**
         * @brief True when the scene had no enabled light and the caller should use XNA's default.
         *
         * When this is set the three slots below are untouched and must not be applied: XNA's
         * `EnableDefaultLighting()` sets its own, and half-applying both gives a scene lit by a
         * mixture the user cannot account for from anything on screen.
         */
        bool useDefaultLighting = true;

        /** @brief The ambient term: a floor under everything, so nothing is pure black. */
        EditorVector3 ambientColor{0.05f, 0.05f, 0.06f};

        /** @brief Up to three lights. `lightCount` says how many are filled in. */
        std::array<EffectDirectionalLight, 3> lights{};

        std::size_t lightCount = 0;
    };

    /**
     * @brief Reduces @p lights to what one object at @p targetWorld can be drawn with.
     *
     * @param targetWorld Where the object being lit is. Point and spot lights are resolved against
     *        it -- see this file's header for what that approximation can and cannot do.
     *
     * Returns `useDefaultLighting` when @p lights is empty, or when every light in it contributes
     * nothing at @p targetWorld (all out of range, or all at zero intensity). The second case
     * matters as much as the first: a scene whose only lamp is on the other side of the level
     * would otherwise render its models black, and "unlit" and "too far from the light" look
     * identical to somebody who has just added a model.
     */
    [[nodiscard]] EffectLighting computeEffectLighting(const std::vector<SceneLight>& lights,
                                                       const EditorVector3& targetWorld);
}
