// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Scene/SceneEnvironment.hpp
 * @brief A scene's ambient light and fog (plan.md ED-407).
 *
 * Two settings that belong to a *level* rather than to anything in it. A cave is dark and a moor is
 * foggy, and neither fact is a property of any entity standing in one -- which is why this is a
 * field on the scene document rather than a component on some designated entity. The alternative,
 * an "Environment" entity the scene is expected to contain, is how a scene comes to have a
 * mandatory entity that must not be deleted, and how a scene missing it comes to render in a way
 * nobody can explain from what is in the hierarchy.
 *
 * **It is an additive field and stays one.** A `.cnascene` written before ED-407 has no
 * `environment` object; it reads as the defaults below and is written back out *without* one. That
 * second half matters as much as the first: a loader that read a default and then serialised it
 * would turn every existing scene into a modified file the first time it was opened.
 *
 * What fog can do here is bounded by what XNA's `IEffectFog` can be told -- a colour, a start
 * distance and an end distance, linear between them. There is no exponential fog and no height
 * fog, because both effects this editor draws through implement that one interface and nothing
 * else. A richer fog model would have to arrive as a shader, which is ED-504 territory.
 */

#include "CNA/Editor/Core/EditorMath.hpp"

namespace CNA::Editor
{
    /** @brief A scene's ambient light and distance fog. */
    struct SceneEnvironment
    {
        /**
         * @brief The light everything gets regardless of where the lamps are.
         *
         * Dark rather than black by default, and deliberately the same value `EffectLighting`
         * carries: an ambient of pure black means a surface facing away from every light is
         * rendered as an unlit silhouette, which reads as a hole in the model rather than as a
         * shadowed side.
         */
        EditorColor ambientColor{13, 13, 15, 255};

        bool fogEnabled = false;

        EditorColor fogColor{120, 130, 145, 255};

        /**
         * @brief Where fog starts and where it is total, in world units.
         *
         * Linear between the two, which is the whole of what `IEffectFog` offers. `fogStart` equal
         * to `fogEnd` is degenerate and the effects treat it as "no fog at all" rather than as a
         * hard cut, so the editor does not have to guard against it -- but the Validation panel
         * says so, because a fog that is switched on and does nothing is a setting a user will
         * otherwise keep re-checking.
         */
        float fogStart = 400.0f;
        float fogEnd = 1600.0f;

        /** @brief True when this is exactly the default, so a scene need not write it. */
        [[nodiscard]] bool isDefault() const
        {
            const SceneEnvironment fresh;
            return ambientColor == fresh.ambientColor && fogEnabled == fresh.fogEnabled
                   && fogColor == fresh.fogColor && fogStart == fresh.fogStart
                   && fogEnd == fresh.fogEnd;
        }
    };
}
