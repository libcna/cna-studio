// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Viewport/EditorViewport.hpp"

#include "CNA/Editor/Scene/BuiltinComponents.hpp"
#include "CNA/Editor/Scene/SceneDocument.hpp"

namespace CNA::Editor
{
    const char* toString(GizmoMode mode)
    {
        switch (mode)
        {
            case GizmoMode::None: return "None";
            case GizmoMode::Translate: return "Translate";
            case GizmoMode::Rotate: return "Rotate";
            case GizmoMode::Scale: return "Scale";
        }
        return "None";
    }

    UiTextureId NullEditorViewport::render(const SceneDocument& scene,
                                           int width,
                                           int height,
                                           const std::vector<Uuid>& selection,
                                           GizmoMode gizmoMode,
                                           GizmoSpace gizmoSpace,
                                           const AnimationPreview& preview)
    {
        (void)selection;
        (void)gizmoMode;
        (void)gizmoSpace;
        (void)preview;

        width_ = width;
        height_ = height;
        ++renderCount_;

        camera_.setViewportSize(EditorVector2{static_cast<float>(width), static_cast<float>(height)});

        // The geometry is walked even though nothing is drawn. That is the point: a headless run
        // and a real one then exercise the same transform and bounds code, so a crash or an
        // infinite parent chain is caught by CI rather than by the first user to open that scene.
        stats_ = ViewportStats{};
        const SpriteSizeProvider sizeProvider = makeSizeProvider();

        for (const EditorEntity& entity : scene.getEntities())
        {
            if (entity.findComponent(BuiltinComponentIds::kSpriteRenderer) == nullptr) { continue; }

            if (!entity.isEnabled())
            {
                ++stats_.spritesSkipped;
                continue;
            }

            if (computeEntityBounds2D(scene, entity.getId(), sizeProvider))
            {
                ++stats_.spritesDrawn;
                ++stats_.missingTextures;
            }
            else
            {
                ++stats_.spritesSkipped;
            }
        }

        // No texture: the panel renders no image, which is the honest result of drawing nothing.
        return kUiTextureNone;
    }

    UiTextureId NullEditorViewport::renderWireframe(const std::vector<WireSegment>& segments, int width,
                                                    int height)
    {
        // Counted rather than ignored: the geometry is the part worth testing, and a headless run
        // that built a wireframe and dropped it silently could not be told from one that built
        // nothing at all.
        wireframeSegments_ = segments.size();
        width_ = width;
        height_ = height;
        ++renderCount_;
        return kUiTextureNone;
    }
}
