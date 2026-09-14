// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Scene/SceneSprites3D.hpp"

#include <algorithm>
#include <cmath>

#include "CNA/Editor/Scene/BuiltinComponents.hpp"
#include "CNA/Editor/Scene/SceneDocument.hpp"

namespace CNA::Editor
{
    namespace
    {
        /** @brief Which of a sprite's axes `spriteEffects` mirrors. */
        struct SpriteFlips
        {
            bool horizontal = false;
            bool vertical = false;
        };

        SpriteFlips readFlips(const EditorComponent& sprite)
        {
            const std::string& name =
                sprite.getProperty("spriteEffects").get<PropertyValue::EnumValue>().name;

            // Spelled exactly as the descriptor's enum options are, and matched rather than parsed:
            // an unrecognised value means no flip, which is the same answer XNA's `SpriteEffects`
            // gives for `None` and the only one that cannot make a sprite face the wrong way.
            return SpriteFlips{name == "FlipHorizontally" || name == "FlipBoth",
                               name == "FlipVertically" || name == "FlipBoth"};
        }
    }

    SceneSpriteBatch3D buildSceneSpriteQuads(const SceneDocument& scene, const EditorCamera3D& camera,
                                             const SpriteSizeProvider& sizeProvider,
                                             const AnimationPreview& preview,
                                             const std::vector<Uuid>& selection,
                                             const ComponentRegistry* components)
    {
        SceneSpriteBatch3D batch;
        if (!sizeProvider) { return batch; }

        const EditorVector3 eye = camera.getEye();

        for (const EditorEntity& entity : scene.getEntities())
        {
            if (!entity.isEnabled()) { continue; }

            const EditorComponent* sprite = entity.findComponent(BuiltinComponentIds::kSpriteRenderer);
            if (sprite == nullptr) { continue; }

            const std::optional<WorldTransform> world = computeWorldTransform(scene, entity.getId());
            if (!world.has_value()) { continue; }

            // An animated sprite is sized and framed by its *sheet*, exactly as the 2D content pass
            // does it -- and every animated entity but the previewed one shows frame zero, because
            // an editor playing every clip at once would be unreadable.
            const EditorComponent* animation =
                entity.findComponent(BuiltinComponentIds::kSpriteAnimation);
            const bool animated = animation != nullptr;

            SpriteAnimationClip clip;
            if (animated)
            {
                // The descriptor supplies declared defaults, exactly as the 2D content pass reads
                // them: a clip authored by hand may omit its frame size, and reading zero there
                // draws nothing with no explanation of why.
                clip = readSpriteAnimationClip(
                    *animation, components != nullptr
                                    ? components->find(BuiltinComponentIds::kSpriteAnimation)
                                    : nullptr);
            }
            const bool playable = animated && !clip.isEmpty();

            const Uuid textureId =
                playable
                    ? animation->getProperty(SpriteAnimationKeys::kSheet)
                          .get<PropertyValue::AssetReference>()
                          .id
                    : sprite->getProperty("texture").get<PropertyValue::AssetReference>().id;

            const EditorVector2 sheetSize = sizeProvider(textureId);
            if (sheetSize.x <= 0.0f || sheetSize.y <= 0.0f)
            {
                // Counted, not guessed at: a quad of a made-up size would be drawn somewhere the
                // picker does not agree with, which is worse than a sprite that is missing and
                // says so.
                ++batch.skipped;
                continue;
            }

            EditorRectangle source =
                sprite->getProperty("sourceRectangle").get<EditorRectangle>();
            if (playable)
            {
                const std::size_t position =
                    preview.isActive() && preview.entityId == entity.getId()
                        ? std::min(preview.position, clip.frames.size() - 1)
                        : 0;
                source = clip.getFrameRectangle(position);
            }

            // An empty source rectangle means the whole texture, which is what both the 2D pass and
            // `SpriteBatch` itself take it to mean.
            const EditorVector2 drawnSize =
                source.isEmpty() ? sheetSize
                                 : EditorVector2{static_cast<float>(source.width),
                                                 static_cast<float>(source.height)};

            const EditorVector2 origin = sprite->getProperty("origin").get<EditorVector2>();

            // In world units: texels times the entity's own scale, with the origin -- which is in
            // texels, as `SpriteBatch` takes it -- deciding where the entity's position sits inside
            // the quad rather than being an offset applied to it.
            const float left = -origin.x * world->scale.x;
            const float top = -origin.y * world->scale.y;
            const float right = (drawnSize.x - origin.x) * world->scale.x;
            const float bottom = (drawnSize.y - origin.y) * world->scale.y;

            // Z rotation alone, because that is all `SpriteBatch` can apply and therefore all the
            // game will show. See this file's second decision.
            const float angle = zRotationOf(world->rotation);
            const float cosine = std::cos(angle);
            const float sine = std::sin(angle);

            const auto place = [&](float x, float y)
            {
                return EditorVector3{world->position.x + x * cosine - y * sine,
                                     world->position.y + x * sine + y * cosine,
                                     world->position.z};
            };

            SpriteQuad3D quad;
            quad.entityId = entity.getId();
            quad.textureId = textureId;
            quad.corners = {place(left, top), place(right, top), place(right, bottom),
                            place(left, bottom)};
            quad.tint = sprite->getProperty("tint").get<EditorColor>();
            quad.selected =
                std::find(selection.begin(), selection.end(), entity.getId()) != selection.end();

            const float u0 = source.isEmpty() ? 0.0f : static_cast<float>(source.x) / sheetSize.x;
            const float v0 = source.isEmpty() ? 0.0f : static_cast<float>(source.y) / sheetSize.y;
            const float u1 = source.isEmpty()
                                 ? 1.0f
                                 : static_cast<float>(source.x + source.width) / sheetSize.x;
            const float v1 = source.isEmpty()
                                 ? 1.0f
                                 : static_cast<float>(source.y + source.height) / sheetSize.y;

            const SpriteFlips flips = readFlips(*sprite);
            const float leftU = flips.horizontal ? u1 : u0;
            const float rightU = flips.horizontal ? u0 : u1;
            const float topV = flips.vertical ? v1 : v0;
            const float bottomV = flips.vertical ? v0 : v1;

            quad.texCoords = {EditorVector2{leftU, topV}, EditorVector2{rightU, topV},
                              EditorVector2{rightU, bottomV}, EditorVector2{leftU, bottomV}};

            const EditorVector3 centre{
                (quad.corners[0].x + quad.corners[2].x) * 0.5f,
                (quad.corners[0].y + quad.corners[2].y) * 0.5f,
                (quad.corners[0].z + quad.corners[2].z) * 0.5f};
            const EditorVector3 toEye = subtract(centre, eye);
            quad.cameraDistance = std::sqrt(dot(toEye, toEye));

            batch.quads.push_back(quad);
        }

        // Furthest first. Transparency does not commute: two overlapping quads blended in the wrong
        // order give a different picture, and back-to-front is the order that gives the right one.
        // Stable, so two sprites at the same distance keep document order and the picture does not
        // flicker between frames for reasons nothing in the scene explains.
        std::stable_sort(batch.quads.begin(), batch.quads.end(),
                         [](const SpriteQuad3D& a, const SpriteQuad3D& b)
                         { return a.cameraDistance > b.cameraDistance; });

        return batch;
    }
}
