// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Scene/SceneLighting.hpp"

#include <algorithm>
#include <cmath>

#include "CNA/Editor/Scene/BuiltinComponents.hpp"
#include "CNA/Editor/Scene/SceneDocument.hpp"
#include "CNA/Editor/Scene/SceneTransform.hpp"

namespace CNA::Editor
{
    namespace
    {
        /** @brief Returns the light kind @p name spells, defaulting to Directional. */
        SceneLightKind parseLightKind(const std::string& name)
        {
            if (name == "Point") { return SceneLightKind::Point; }
            if (name == "Spot") { return SceneLightKind::Spot; }

            // Directional for anything unrecognised, rather than skipping the light. A scene
            // written by a newer editor with a fourth kind in it should still light something:
            // the wrong kind of light is a visible, correctable state, and no light at all reads
            // as the component being ignored.
            return SceneLightKind::Directional;
        }

        /** @brief Returns @p color scaled by @p intensity and @p falloff, in 0..1 components. */
        EditorVector3 toLinearScaled(const EditorColor& color, float intensity, float falloff)
        {
            const float factor = std::max(0.0f, intensity) * std::max(0.0f, falloff) / 255.0f;
            return EditorVector3{static_cast<float>(color.r) * factor,
                                 static_cast<float>(color.g) * factor,
                                 static_cast<float>(color.b) * factor};
        }

        /** @brief Returns how much of @p light reaches @p targetWorld, in 0..1. */
        float falloffAt(const SceneLight& light, const EditorVector3& targetWorld)
        {
            // A directional light is the sun: it does not get further away.
            if (light.kind == SceneLightKind::Directional) { return 1.0f; }

            const float range = std::max(light.range, 1e-4f);
            const EditorVector3 offset = subtract(targetWorld, light.position);
            const float distance = std::sqrt(dot(offset, offset));
            if (distance >= range) { return 0.0f; }

            // Smooth to zero at the range rather than inverse-square clipped at it. Inverse-square
            // is the physical answer and the wrong one here: it never reaches zero, so a lamp with
            // a range would still tint everything outside it, and the range control in the
            // inspector would do nothing a user could see. This falls off and then stops.
            const float t = 1.0f - distance / range;
            return t * t;
        }

        /** @brief Returns the direction @p light illuminates @p targetWorld from. */
        EditorVector3 directionAt(const SceneLight& light, const EditorVector3& targetWorld)
        {
            if (light.kind == SceneLightKind::Directional) { return light.direction; }

            // A point or spot light aimed at the thing being drawn -- which is what makes the
            // approximation work at all, and also its limit: the whole object is lit as though it
            // sat at the point this was resolved against.
            const EditorVector3 offset = subtract(targetWorld, light.position);
            const float length = std::sqrt(dot(offset, offset));
            if (length < 1e-4f) { return light.direction; }

            return scale(offset, 1.0f / length);
        }

        /** @brief Returns the perceptual weight of a colour, for ranking lights against each other. */
        float brightnessOf(const EditorVector3& color)
        {
            // Rec. 601 luma. Any monotonic weighting would order lights the same way most of the
            // time; this one at least orders a green light above a blue one of the same numbers,
            // which matches what somebody looking at the viewport would call brighter.
            return 0.299f * color.x + 0.587f * color.y + 0.114f * color.z;
        }
    }

    const char* toString(SceneLightKind kind)
    {
        switch (kind)
        {
            case SceneLightKind::Point: return "Point";
            case SceneLightKind::Spot: return "Spot";
            default: return "Directional";
        }
    }

    std::vector<SceneLight> collectSceneLights(const SceneDocument& scene)
    {
        std::vector<SceneLight> lights;

        for (const EditorEntity& entity : scene.getEntities())
        {
            if (!entity.isEnabled()) { continue; }

            const EditorComponent* component = entity.findComponent(BuiltinComponentIds::kLight);
            if (component == nullptr) { continue; }

            const std::optional<WorldTransform> transform =
                computeWorldTransform(scene, entity.getId());
            if (!transform.has_value()) { continue; }

            SceneLight light;
            light.entityId = entity.getId();
            light.kind = parseLightKind(
                component->getProperty("kind").get<PropertyValue::EnumValue>().name);
            light.position = transform->position;
            light.direction =
                normalize(rotate(transform->rotation, EditorVector3{0.0f, 0.0f, 1.0f}));
            light.color = component->getProperty("color").get<EditorColor>();
            light.intensity = component->getProperty("intensity").get<float>();
            light.range = component->getProperty("range").get<float>();

            lights.push_back(light);
        }

        return lights;
    }

    EffectLighting computeEffectLighting(const std::vector<SceneLight>& lights,
                                         const EditorVector3& targetWorld)
    {
        EffectLighting lighting;

        struct Candidate
        {
            EffectDirectionalLight light;
            float brightness = 0.0f;
        };

        std::vector<Candidate> candidates;
        candidates.reserve(lights.size());

        for (const SceneLight& light : lights)
        {
            const float falloff = falloffAt(light, targetWorld);
            if (falloff <= 0.0f) { continue; }

            const EditorVector3 color = toLinearScaled(light.color, light.intensity, falloff);
            const float brightness = brightnessOf(color);
            if (brightness <= 0.0f) { continue; }

            EffectDirectionalLight effectLight;
            effectLight.direction = directionAt(light, targetWorld);
            effectLight.diffuseColor = color;
            effectLight.specularColor = color;

            candidates.push_back(Candidate{effectLight, brightness});
        }

        // Nothing reaches this point in the world. Say "use the default" rather than "use these
        // zero lights": the two are the same arithmetic and completely different on screen, and
        // the one a user can act on is the model they can see.
        if (candidates.empty()) { return lighting; }

        // Brightest first, and only where they differ -- `std::stable_sort` so that two lights of
        // equal brightness keep document order and the picture does not change from frame to frame
        // for reasons nothing in the scene explains.
        std::stable_sort(candidates.begin(), candidates.end(),
                         [](const Candidate& a, const Candidate& b)
                         { return a.brightness > b.brightness; });

        lighting.useDefaultLighting = false;
        lighting.lightCount = std::min(candidates.size(), lighting.lights.size());
        for (std::size_t i = 0; i < lighting.lightCount; ++i)
        {
            lighting.lights[i] = candidates[i].light;
        }

        return lighting;
    }
}
