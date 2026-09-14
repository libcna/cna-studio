// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Viewport/CnaSceneRenderer.hpp
 * @brief Draws a scene document into an offscreen texture, through CNA's public API.
 *
 * The counterpart to CnaUiRenderer: that one draws the editor's *chrome*, this one draws the
 * *content*. Both go through `Microsoft::Xna::Framework::*` only.
 *
 * Rendering to a `RenderTarget2D` rather than straight to the back buffer is what lets the result
 * appear as an image inside a docked ImGui panel — the viewport is a panel like any other, and the
 * user can move, resize and tab it. It also means the scene is drawn at exactly the panel's size,
 * so nothing is stretched.
 *
 * The passes are kept separate and ordered, per ANALYSIS.md's adoption of the original design:
 * grid, then the game's own content, then the editor's overlay. Editor artefacts — grid lines,
 * selection outlines, icons, gizmos — are never entities in the scene, so a build can never ship
 * with them.
 */

#include <memory>
#include <string>
#include <vector>

#include "CNA/Editor/Assets/AssetDatabase.hpp"
#include "CNA/Editor/Core/ComponentDescriptor.hpp"
#include "CNA/Editor/Scene/EditorCamera2D.hpp"
#include "CNA/Editor/Scene/SceneModels.hpp"
#include "CNA/Editor/Scene/SceneWireframe.hpp"
#include "CNA/Editor/Viewport/CnaModelPass.hpp"
#include "CNA/Editor/Ui/UiDrawData.hpp"
#include "CNA/Editor/Viewport/EditorViewport.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;
    class Texture2D;
}

namespace CNA::Editor
{
    class SceneDocument;
    class CnaUiRenderer;

    /** @brief Per-frame counters for the scene pass. */
    struct SceneRenderStats
    {
        std::size_t spritesDrawn = 0;
        std::size_t spritesSkipped = 0;
        std::size_t gridLines = 0;
        std::size_t texturesLoaded = 0;

        /** @brief Icons drawn for entities the viewport cannot render, such as cameras and lights. */
        std::size_t iconsDrawn = 0;

        /** @brief Sprites whose texture asset could not be loaded, drawn as a placeholder. */
        std::size_t missingTextures = 0;

        /** @brief Non-empty tilemap cells drawn this frame, after culling to the viewport. */
        std::size_t tilesDrawn = 0;
    };

    /**
     * @brief Renders a scene into an offscreen target sized to the viewport panel.
     *
     * Textures are loaded lazily, once per asset id, and kept until reset(). A texture that fails
     * to load is remembered as failed so a broken asset costs one attempt rather than one per
     * frame — an editor that retries a missing file sixty times a second is an editor that stalls.
     */
    class CnaSceneRenderer
    {
    public:
        CnaSceneRenderer();
        ~CnaSceneRenderer();

        CnaSceneRenderer(const CnaSceneRenderer&) = delete;
        CnaSceneRenderer& operator=(const CnaSceneRenderer&) = delete;

        /**
         * @brief Binds the renderer to a device, an asset database and a component registry.
         *
         * The registry supplies declared defaults. A tilemap authored by hand may omit its tile
         * size, and reading zero there would draw nothing with no explanation -- the descriptor is
         * the only thing that knows what the field means when the file does not say.
         */
        void initialize(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                        const AssetDatabase& assets,
                        const ComponentRegistry& components);

        /** @brief Releases the render target and every loaded texture. */
        void shutdown();

        /**
         * @brief Draws @p scene at @p camera into an offscreen target of the given pixel size.
         *
         * Restores the previous render target before returning, so the caller's own drawing is
         * unaffected.
         *
         * @param selection Entities to outline. Drawn in the overlay pass, never as scene content.
         * @param gizmoMode Which manipulator to draw on the first selected entity, if any.
         * @param gizmoSpace Which frame that manipulator's arms point along.
         */
        SceneRenderStats render(const SceneDocument& scene,
                                const EditorCamera2D& camera,
                                int width,
                                int height,
                                const std::vector<Uuid>& selection,
                                GizmoMode gizmoMode = GizmoMode::None,
                                GizmoSpace gizmoSpace = GizmoSpace::World,
                                const AnimationPreview& preview = {});

        /**
         * @brief Draws @p scene the way the *game* sees it: into the current target, no overlays.
         *
         * What `cna-player` uses. Two differences from render(), and both are the point:
         *
         * - **No editor artefacts.** No grid, no icons, no selection outline, no gizmo. The
         *   separation was already enforced by the pass structure, so this is a matter of not
         *   running two of the three passes rather than of filtering anything out.
         * - **The current render target**, normally the back buffer, rather than an offscreen one.
         *   The player's window shows the game and nothing else, so there is nothing to compose it
         *   with -- and drawing straight to the back buffer is what makes
         *   `GraphicsDevice::GetBackBufferData` a screenshot of the game (plan.md ED-510).
         *
         * The caller clears; the game's own camera decides the colour, and this class has no
         * opinion about it.
         */
        SceneRenderStats renderGameView(const SceneDocument& scene,
                                        const EditorCamera2D& camera,
                                        int width,
                                        int height);

        /**
         * @brief Draws @p segments into the offscreen target, over the same background (ED-400).
         *
         * The whole of the 3D viewport's drawing. Everything it shows is a line, and which lines
         * was decided in `cna-editor-scene` where it can be tested with no GPU -- so this is one
         * `Begin`, a loop over `drawLine`, and an `End`.
         */
        void renderWireframe(const std::vector<WireSegment>& segments, int width, int height);

        /**
         * @brief Draws @p models solid and @p segments over them: the whole 3D view (ED-402).
         *
         * Two passes into one target, in this order and not the other. The wireframe carries the
         * editor's *overlay* -- grid, gizmo arms, selection outline -- and an overlay a model could
         * occlude would leave a user unable to see the handle they are dragging the moment it
         * passed behind geometry. So models are drawn depth-tested and the lines are laid over them
         * with the test off.
         *
         * The target is recreated with a depth buffer for this, since the one the 2D view uses has
         * none: sprites sort by draw order and models sort per pixel.
         */
        ModelPassStats renderScene3D(const SceneModelBatch& models,
                                     const SceneSpriteBatch3D& sprites,
                                     const std::vector<WireSegment>& segments,
                                     int width, int height);

        /** @brief Drops the GPU buffers for @p assetId, or all of them when it is nil. */
        void invalidateModel(const Uuid& assetId);

        /**
         * @brief Renders @p mesh into a square thumbnail target and returns it (ED-406).
         *
         * Its own target rather than the viewport's: the browser draws thumbnails in the same
         * frame the viewport draws the scene, and one target serving both would have each
         * overwriting the other's pixels between the two `image()` calls that display them.
         *
         * Returns nullptr when there is no device or no model pass.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D* renderModelThumbnail(
            const Uuid& assetId, const MeshData& mesh, int extent);

        /** @brief Which effect the model pass got: "PbrEffect", "BasicEffect" or "none". */
        [[nodiscard]] const std::string& getModelEffectName() const;

        /**
         * @brief Registers the rendered target with @p uiRenderer and returns its UI texture id.
         *
         * The id is what the viewport panel passes to `EditorUi::image()`. Zero when nothing has
         * been rendered yet.
         */
        UiTextureId shareWithUi(CnaUiRenderer& uiRenderer);

        /**
         * @brief Drops the cached texture for @p assetId, or every texture when @p assetId is nil.
         *
         * Also clears the failed-load memory for it: a texture that failed because the file was
         * missing must get another chance once the file comes back, or restoring it would appear
         * to do nothing.
         */
        void invalidateTexture(const Uuid& assetId);

        /**
         * @brief Returns the texture for @p assetId, loading it if it is not already in.
         *
         * The same cache the scene pass uses, so a thumbnail costs nothing once the sprite that
         * uses it has been drawn -- and a project of a hundred textures does not load them twice.
         * Returns nullptr when the asset is unknown or its file will not open.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::Texture2D* getOrLoadTexture(const Uuid& assetId);

        /** @brief Returns the texel size of @p assetId, or (0, 0) when it cannot be resolved. */
        [[nodiscard]] EditorVector2 getSpriteSize(const Uuid& assetId) const;

        /** @brief Returns a SpriteSizeProvider bound to this renderer, for picking and framing. */
        [[nodiscard]] SpriteSizeProvider makeSizeProvider() const;

        /** @brief Returns the counters from the most recent render(). */
        [[nodiscard]] const SceneRenderStats& getLastStats() const { return lastStats_; }

    private:
        /**
         * @brief The one implementation behind render() and renderGameView().
         *
         * Both draw the same content pass, and a second copy of it would be a second place for
         * "what the game looks like" to drift from what the editor shows -- which is the single
         * property the editor exists to guarantee.
         */
        SceneRenderStats renderPasses(const SceneDocument& scene,
                                      const EditorCamera2D& camera,
                                      int width,
                                      int height,
                                      const std::vector<Uuid>& selection,
                                      GizmoMode gizmoMode,
                                      GizmoSpace gizmoSpace,
                                      const AnimationPreview& preview,
                                      bool editorOverlays,
                                      bool offscreen);

        struct Impl;
        std::unique_ptr<Impl> impl_;
        SceneRenderStats lastStats_;
    };

    class EditorViewport;

    /**
     * @brief Creates the CNA-backed scene viewport.
     *
     * @param device The graphics device; must outlive the viewport.
     * @param assets Where sprite textures are resolved from.
     * @param components Supplies declared property defaults for the components it draws.
     * @param uiRenderer The UI renderer the rendered target is shared through, so the viewport
     *        panel can display it as an ordinary image.
     */
    std::unique_ptr<EditorViewport> createCnaEditorViewport(
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
        const AssetDatabase& assets,
        const ComponentRegistry& components,
        CnaUiRenderer& uiRenderer);
}
