// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Scene/GameCamera.hpp"

#include <optional>

#include "CNA/Editor/Scene/BuiltinComponents.hpp"
#include "CNA/Editor/Scene/SceneDocument.hpp"
#include "CNA/Editor/Scene/SceneTransform.hpp"

namespace CNA::Editor
{
    namespace
    {
        /** @brief Returns the scene's primary camera component, or nullptr. */
        const EditorEntity* findPrimaryCamera(const SceneDocument& scene)
        {
            const EditorEntity* firstCamera = nullptr;

            for (const EditorEntity& entity : scene.getEntities())
            {
                // A disabled entity is not in the game at all, so its camera is not either.
                if (!entity.isEnabled()) { continue; }

                const EditorComponent* camera = entity.findComponent(BuiltinComponentIds::kCamera);
                if (camera == nullptr) { continue; }

                if (camera->getProperty("isPrimary").get<bool>(true)) { return &entity; }
                if (firstCamera == nullptr) { firstCamera = &entity; }
            }

            // No camera claims to be primary, but one exists. Using it beats drawing from the
            // origin: a scene whose only camera has the flag cleared is a mistake the user can see
            // immediately when the view is theirs, and cannot see at all when it is not.
            return firstCamera;
        }
    }

    GameView computeGameView(const SceneDocument& scene, const EditorVector2& viewportSize)
    {
        GameView view;
        view.camera.setViewportSize(viewportSize);

        const EditorEntity* entity = findPrimaryCamera(scene);
        if (entity == nullptr) { return view; }

        const EditorComponent* camera = entity->findComponent(BuiltinComponentIds::kCamera);
        view.cameraId = entity->getId();
        view.clearColor = camera->getProperty("clearColor").get<EditorColor>(view.clearColor);

        if (const std::optional<WorldTransform> world = computeWorldTransform(scene, entity->getId()))
        {
            view.camera.setCenter(EditorVector2{world->position.x, world->position.y});
        }

        // Height, not width: `orthographicSize` is the visible height in world units, so the zoom
        // is pixels per world unit vertically and the width follows from the aspect. A resize then
        // shows more of the world rather than stretching what was already on screen.
        const float orthographicSize = camera->getProperty("orthographicSize").get<float>(600.0f);
        if (orthographicSize > 0.0f && viewportSize.y > 0.0f)
        {
            view.camera.setZoom(viewportSize.y / orthographicSize);
        }
        return view;
    }
}
