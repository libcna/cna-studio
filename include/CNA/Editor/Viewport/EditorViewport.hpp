// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Viewport/EditorViewport.hpp
 * @brief The scene preview surface, and the seam where CNA is allowed to appear.
 *
 * This is the **only** module in the editor that may link CNA. Everything else -- the document
 * model, the undo stack, the asset database, the panels -- is CNA-free, which is what lets the
 * editor build and its tests run with no CNA checkout, no window and no GPU (ANALYSIS.md
 * decision D-03).
 *
 * The interface is deliberately coarse: one `render()` call that returns a texture id the panel
 * can display. An earlier draft exposed the passes individually (`renderGrid`, `renderScene`,
 * `renderSelectionOutline`, …) and that turned out to be the wrong seam -- the *ordering* of those
 * passes is a property of the renderer, not a decision for the panel, and every implementation
 * would have had to be trusted to call them in the right order. The ordering still matters and is
 * still enforced; it is simply enforced in one place now, inside the implementation.
 *
 * Note what is absent: the camera lives in `cna-editor-scene` as `EditorCamera2D`, not here.
 * Picking, framing and grid spacing all need it, and none of those should require a CNA build.
 */

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "CNA/Editor/Core/EditorMath.hpp"
#include "CNA/Editor/Core/ImageDiff.hpp"
#include "CNA/Editor/Core/Uuid.hpp"
#include "CNA/Editor/Scene/EditorCamera2D.hpp"
#include "CNA/Editor/Scene/SceneModels.hpp"
#include "CNA/Editor/Scene/SceneSprites3D.hpp"
#include "CNA/Editor/Scene/SceneWireframe.hpp"
#include "CNA/Editor/Scene/SpriteAnimation.hpp"
#include "CNA/Editor/Scene/TransformGizmos.hpp"
#include "CNA/Editor/Ui/UiDrawData.hpp"

namespace CNA::Editor
{
    class SceneDocument;

    /** @brief One graphics feature, and whether this build's backend has it. */
    struct ViewportCapability
    {
        std::string name;
        bool supported = false;
    };

    /** @brief The manipulator currently bound to the mouse. */
    enum class GizmoMode
    {
        None,
        Translate,
        Rotate,
        Scale
    };

    /** @brief Returns the display name of @p mode. */
    const char* toString(GizmoMode mode);

    /** @brief Counters from the most recent viewport render, for the profiler and for tests. */
    struct ViewportStats
    {
        std::size_t spritesDrawn = 0;
        std::size_t spritesSkipped = 0;
        std::size_t gridLines = 0;
        std::size_t missingTextures = 0;
    };

    /**
     * @brief The scene preview.
     *
     * Abstract so the panel layer can be built and tested against NullEditorViewport, and so a
     * CNA-backed implementation can be swapped in without any panel changing.
     */
    class EditorViewport
    {
    public:
        virtual ~EditorViewport() = default;

        /** @brief Returns a short name for the implementation, e.g. "cna-easygl" or "null". */
        [[nodiscard]] virtual const char* getBackendName() const = 0;

        /**
         * @brief Draws @p scene into an offscreen surface of @p width by @p height pixels.
         *
         * Passes run in a fixed order inside the implementation: grid, then the game's own content,
         * then the editor's overlay. Editor artefacts are never entities in the scene, so a build
         * can never ship with them.
         *
         * @param gizmoSpace Which frame the drawn gizmo's arms point along. Passed beside the mode
         *        rather than folded into it, because the two are independent choices: a user picks
         *        a manipulator far more often than they change the space it works in.
         * @param preview Which animation frame to draw for the entity being previewed. Passed in
         *        rather than read from the document, because playback is editor state and must not
         *        travel in a scene -- the same reason the selection is passed rather than stored.
         * @return A UI texture id the viewport panel can display, or zero when nothing was drawn.
         */
        virtual UiTextureId render(const SceneDocument& scene,
                                   int width,
                                   int height,
                                   const std::vector<Uuid>& selection,
                                   GizmoMode gizmoMode,
                                   GizmoSpace gizmoSpace = GizmoSpace::World,
                                   const AnimationPreview& preview = {}) = 0;

        /**
         * @brief Returns what the backend this build was compiled against can actually do.
         *
         * Empty for a viewport with no device. The names are CNA's own `GraphicsCapability`
         * entries, carried as strings so that nothing outside the CNA-linking module has to know
         * that enumeration exists -- and so the list keeps working when CNA adds an entry.
         */
        [[nodiscard]] virtual std::vector<ViewportCapability> getBackendCapabilities() const
        {
            return {};
        }

        /**
         * @brief Returns the texel size of @p assetId, or (0, 0) when it cannot be resolved.
         *
         * Picking and framing need it, and only the viewport has loaded the textures.
         */
        [[nodiscard]] virtual EditorVector2 getSpriteSize(const Uuid& assetId) const = 0;

        /** @brief Returns a SpriteSizeProvider bound to this viewport. */
        [[nodiscard]] SpriteSizeProvider makeSizeProvider() const
        {
            return [this](const Uuid& assetId) { return getSpriteSize(assetId); };
        }

        /** @brief Returns the editor camera. */
        [[nodiscard]] virtual EditorCamera2D& getCamera() = 0;
        [[nodiscard]] virtual const EditorCamera2D& getCamera() const = 0;

        /**
         * @brief Returns the 3D editor camera (plan.md ED-400).
         *
         * Kept beside the 2D one rather than replacing it, and both are alive at once: a user who
         * switches to the 3D view, orbits, and switches back must find their 2D framing exactly as
         * they left it. One camera converted back and forth could not promise that -- the 2D view
         * has no way to represent a pitch.
         */
        [[nodiscard]] virtual EditorCamera3D& getCamera3D() = 0;
        [[nodiscard]] virtual const EditorCamera3D& getCamera3D() const = 0;

        /**
         * @brief Draws @p segments into an offscreen target and returns it as a UI texture.
         *
         * The whole of the 3D viewport's drawing, because everything it shows is a line and the
         * decisions about which lines were made in `cna-editor-scene` where they can be tested
         * (SceneWireframe.hpp). A viewport with no device draws nothing and says so by returning
         * zero, exactly as render() does.
         */
        virtual UiTextureId renderWireframe(const std::vector<WireSegment>& segments, int width, int height)
        {
            (void)segments;
            (void)width;
            (void)height;
            return 0;
        }

        /**
         * @brief Draws @p models solid with @p segments over them, and returns the UI texture.
         *
         * The 3D view once ED-402 gave it geometry to draw. The default implementation *ignores
         * the models and draws the wireframe alone*, which is not a stub: a viewport with no CNA
         * has no vertex buffer to upload into, and the wireframe is exactly what this view drew
         * before models existed. So the standalone build, the null viewport and every headless
         * test keep working and keep showing something true.
         */
        virtual UiTextureId renderScene3D(const SceneModelBatch& models,
                                          const SceneSpriteBatch3D& sprites,
                                          const std::vector<WireSegment>& segments,
                                          int width, int height)
        {
            (void)models;
            (void)sprites;
            return renderWireframe(segments, width, height);
        }

        /**
         * @brief Renders @p mesh as a square thumbnail and returns it as a UI texture (ED-406).
         *
         * @param mesh The geometry, passed in rather than looked up: the mesh cache lives in
         *        `EditorContext` because a `MeshData` needs no CNA, and a viewport that reached
         *        for it would be the CNA-linking module depending on the context.
         * @param extent The square's side in pixels. Rendered at that size rather than rendered
         *        large and scaled down, since a thumbnail is drawn every frame the browser is open.
         *
         * The camera frames the model's own bounds from a fixed three-quarter angle -- the pose in
         * which a box looks like a box. Head-on is the one angle where a cube and a flat square are
         * the same picture, which is exactly what a thumbnail is there to tell apart.
         *
         * Returns zero in a build with no model pass, which the browser treats as "no thumbnail"
         * exactly as it treats an image it cannot decode.
         */
        virtual UiTextureId getModelThumbnail(const Uuid& assetId, const MeshData& mesh, int extent)
        {
            (void)assetId;
            (void)mesh;
            (void)extent;
            return 0;
        }

        /** @brief Which effect the model pass uses, or "none" in a build that has no model pass. */
        [[nodiscard]] virtual std::string getModelEffectName() const { return "none"; }

        /**
         * @brief Returns true when render()'s texture must be sampled bottom-up.
         *
         * A render target's texture origin is not the same on every graphics API: OpenGL-family
         * backends put it bottom-left, Direct3D-family top-left. CNA does not normalise this for a
         * render target used as a sampled texture, so the viewport has to say which convention its
         * result follows. See docs/SPIKE-IMGUI-CNA.md gap G-03.
         */
        [[nodiscard]] virtual bool isRenderTextureFlippedVertically() const { return false; }

        /**
         * @brief Drops any cached GPU resource for @p assetId, so it is loaded afresh.
         *
         * Textures are cached for the life of the viewport, which is right until somebody edits
         * one in another program. Without this the editor goes on showing art that no longer
         * exists, and the only fix is to restart it.
         *
         * A nil id means "everything", which is what a project-wide rescan wants.
         */
        virtual void invalidateAsset(const Uuid& assetId) { (void)assetId; }

        /**
         * @brief Returns a UI texture id previewing @p assetId, or zero when it has none.
         *
         * Only image assets have one. The id stays the same across frames for the same asset, so
         * the panel can ask every frame without the UI seeing a different texture each time.
         */
        virtual UiTextureId getAssetThumbnail(const Uuid& assetId)
        {
            (void)assetId;
            return kUiTextureNone;
        }

        /**
         * @brief Reads an image file into memory, or returns an empty buffer.
         *
         * Here rather than in a file utility because decoding a PNG needs a graphics API, and this
         * is the module allowed to have one (D-03). What needs it is the backend comparison
         * (plan.md ED-510): it compares frames written by *other processes*, so somebody has to
         * turn those files back into pixels.
         */
        [[nodiscard]] virtual ImageBuffer readImageFile(const std::string& path) const
        {
            (void)path;
            return {};
        }

        /** @brief Writes @p image to @p path as a PNG. Returns false when it cannot. */
        virtual bool writeImageFile(const std::string& path, const ImageBuffer& image)
        {
            (void)path;
            (void)image;
            return false;
        }

        /** @brief Returns the counters from the most recent render(). */
        [[nodiscard]] virtual ViewportStats getLastStats() const = 0;
    };

    /**
     * @brief A viewport that renders nothing but still does all the geometry.
     *
     * Used by `--headless`, by every unit test, and as the fallback when the editor is built
     * without CNA. It maintains a real camera and honours resizes, so the panel layer, the picking
     * path and the framing logic are all exercised without a GPU; only the pixels are missing.
     */
    class NullEditorViewport final : public EditorViewport
    {
    public:
        [[nodiscard]] const char* getBackendName() const override { return "null"; }

        UiTextureId render(const SceneDocument& scene,
                           int width,
                           int height,
                           const std::vector<Uuid>& selection,
                           GizmoMode gizmoMode,
                           GizmoSpace gizmoSpace = GizmoSpace::World,
                           const AnimationPreview& preview = {}) override;

        [[nodiscard]] EditorVector2 getSpriteSize(const Uuid& assetId) const override
        {
            (void)assetId;
            return EditorVector2{};
        }

        [[nodiscard]] EditorCamera2D& getCamera() override { return camera_; }
        [[nodiscard]] const EditorCamera2D& getCamera() const override { return camera_; }

        [[nodiscard]] EditorCamera3D& getCamera3D() override { return camera3D_; }
        [[nodiscard]] const EditorCamera3D& getCamera3D() const override { return camera3D_; }

        UiTextureId renderWireframe(const std::vector<WireSegment>& segments, int width,
                                    int height) override;

        [[nodiscard]] ViewportStats getLastStats() const override { return stats_; }

        /** @brief Returns how many times render() has been called. */
        [[nodiscard]] std::uint64_t getRenderCount() const { return renderCount_; }

        [[nodiscard]] int getWidth() const { return width_; }
        [[nodiscard]] int getHeight() const { return height_; }

        /** @brief Returns how many line segments the last renderWireframe() was given. */
        [[nodiscard]] std::size_t getLastWireframeSegments() const { return wireframeSegments_; }

    private:
        EditorCamera2D camera_;
        EditorCamera3D camera3D_;
        ViewportStats stats_;
        std::uint64_t renderCount_ = 0;
        std::size_t wireframeSegments_ = 0;
        int width_ = 0;
        int height_ = 0;
    };
}
