// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Scene/SceneModels.hpp"

#include <algorithm>

#include "CNA/Editor/Scene/BuiltinComponents.hpp"
#include "CNA/Editor/Scene/SceneDocument.hpp"
#include "CNA/Editor/Scene/SceneTransform.hpp"

namespace CNA::Editor
{
    namespace
    {
        /**
         * @brief Composes @p transform into the matrix that takes model space to world space.
         *
         * The same scale-rotate-translate order `SceneWireframe` composes in, and the same order
         * `computeWorldTransform` assumes when it accumulates a hierarchy. If it ever differed, a
         * model would be drawn solid in one place and wire-outlined in another.
         *
         * `AModelDrawsWhereTheSceneTransformSaysItIs` pins the property rather than the sixteen
         * numbers: a local point pushed through this matrix must land where composing the entity's
         * own scale, rotation and position puts it. That is what catches the mistake worth
         * catching -- rotate-then-scale instead of scale-then-rotate, which is invisible until an
         * entity is both rotated and non-uniformly scaled.
         */
        EditorMatrix toWorldMatrix(const WorldTransform& transform)
        {
            return multiply(multiply(createScale(transform.scale),
                                     createFromQuaternion(transform.rotation)),
                            createTranslation(transform.position));
        }
    }

    SceneModelBatch buildSceneModelBatch(const SceneDocument& scene, const EditorCamera3D& camera,
                                         const MeshProvider& meshProvider,
                                         const std::vector<Uuid>& selection,
                                         const MaterialProvider& materialProvider)
    {
        SceneModelBatch batch;
        batch.environment = scene.getEnvironment();
        batch.viewProjection = camera.getViewProjectionMatrix();
        batch.view = camera.getViewMatrix();

        // The mirror lives in the projection (EditorCamera3D::getViewProjectionMatrix says why),
        // so it has to be folded in here too or the split would not multiply back to the product.
        batch.projection = multiply(camera.getProjectionMatrix(),
                                    createScale(EditorVector3{1.0f, -1.0f, 1.0f}));

        if (!meshProvider) { return batch; }

        // Collected once for the whole batch, resolved per draw. Collecting is a walk of every
        // entity; resolving is arithmetic over what that walk found, and doing the walk per model
        // would make a scene of a hundred models cost a hundred walks of itself.
        const std::vector<SceneLight> lights = collectSceneLights(scene);

        for (const EditorEntity& entity : scene.getEntities())
        {
            if (!entity.isEnabled()) { continue; }

            const EditorComponent* renderer = entity.findComponent(BuiltinComponentIds::kModelRenderer);
            if (renderer == nullptr) { continue; }

            const Uuid modelId = renderer->getProperty("model").get<PropertyValue::AssetReference>().id;
            if (!modelId.isValid()) { continue; }

            const std::optional<WorldTransform> world = computeWorldTransform(scene, entity.getId());
            if (!world.has_value()) { continue; }

            const MeshData* mesh = meshProvider(modelId);
            if (mesh == nullptr || mesh->isEmpty())
            {
                // Counted rather than dropped silently. "Still importing" and "this entity has no
                // model" look identical on screen, and only one of them is worth waiting for.
                ++batch.pendingMeshes;
                continue;
            }

            ModelDraw draw;
            draw.entityId = entity.getId();
            draw.modelId = modelId;
            draw.mesh = mesh;
            draw.world = toWorldMatrix(*world);
            draw.lighting = computeEffectLighting(lights, world->position);

            // The scene's ambient overrides the lighting's own default. It is a property of the
            // level -- a cave is dark -- so it belongs to the scene rather than to whichever
            // lights happen to be in it.
            draw.lighting.ambientColor =
                EditorVector3{static_cast<float>(scene.getEnvironment().ambientColor.r) / 255.0f,
                              static_cast<float>(scene.getEnvironment().ambientColor.g) / 255.0f,
                              static_cast<float>(scene.getEnvironment().ambientColor.b) / 255.0f};
            // The override, when the entity names one and it can be resolved. An entity pointing at
            // a material that has not loaded draws with its model's own rather than not at all --
            // the same answer `MeshProvider` returning nullptr gets from the mesh side.
            if (materialProvider)
            {
                const Uuid materialId =
                    renderer->getProperty("material").get<PropertyValue::AssetReference>().id;
                if (materialId.isValid()) { draw.materialOverride = materialProvider(materialId); }

                // ED-410: the per-part list, resolved the same way. Each entry is a structure of
                // a part name and a material reference -- which is what `PropertyType::Structure`
                // was finally built for, ED-311 having deliberately left it unbuilt until
                // something real asked.
                const PropertyValue& listValue = renderer->getProperty("materials");
                if (listValue.getType() == PropertyType::List)
                {
                    for (const PropertyValue& item : listValue.get<PropertyValue::ListValue>().items)
                    {
                        if (item.getType() != PropertyType::Structure) { continue; }

                        const auto& structure = item.get<PropertyValue::StructureValue>();
                        const PropertyValue* partName = structure.find("part");
                        const PropertyValue* partMaterial = structure.find("material");
                        if (partName == nullptr || partMaterial == nullptr) { continue; }

                        const std::string name = partName->get<std::string>();
                        const Uuid id = partMaterial->get<PropertyValue::AssetReference>().id;
                        if (name.empty() || !id.isValid()) { continue; }

                        if (const std::optional<MeshMaterial> resolved = materialProvider(id))
                        {
                            draw.partMaterials.emplace_back(name, *resolved);
                        }
                    }
                }
            }

            draw.selected =
                std::find(selection.begin(), selection.end(), entity.getId()) != selection.end();

            batch.triangleCount += mesh->getTriangleCount();
            batch.draws.push_back(draw);
        }

        return batch;
    }
}
