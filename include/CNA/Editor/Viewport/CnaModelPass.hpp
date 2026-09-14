// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Viewport/CnaModelPass.hpp
 * @brief Draws imported models as solid, lit geometry through CNA's public API (plan.md ED-402).
 *
 * The CNA half of ED-402. What to draw was decided in `cna-editor-scene`
 * (`SceneModels.hpp`) where CI can check it with no device; this uploads that decision and issues
 * the draw calls. Four things are decided *here*, because each is about the device rather than
 * about the document.
 *
 * **1. `PbrEffect` where the build has one, `BasicEffect` where it does not.** The owner chose PBR
 * with a fallback, and the fallback is not defensive programming for its own sake: `PbrEffect` is
 * `NOXNA`, a CNA extension, and this editor supports fourteen backends of three support tiers
 * (F-02). The effect is constructed once and the choice reported through `ModelPassStats::effect`
 * so the Diagnostics panel can say which one a build actually got -- "why does it look different
 * on this machine" deserves an answer that is not a screenshot comparison.
 *
 * **2. The render target needs a depth buffer, and the 2D one has none.** `CnaSceneRenderer`'s
 * target is created with the two-argument `RenderTarget2D` constructor, which is documented as
 * "no depth buffer" -- correct for sprites, which sort by draw order, and useless for models,
 * which sort per pixel. Without one a crate renders with its back faces punched through its front.
 * So the 3D view asks for a target *with* depth, and the renderer recreates the target when the
 * requirement changes rather than keeping two.
 *
 * **3. Cull counter-clockwise faces -- which is right *because* of the Y mirror, not despite it.**
 * The importer winds triangles counter-clockwise seen from outside, in world space
 * (`meshWindingMatchesNormals`). The view-projection then mirrors Y, and mirroring one axis
 * reverses apparent winding, so an outward-facing triangle arrives at the rasteriser clockwise --
 * which `CullCounterClockwiseFace`, XNA's own default, keeps. Two reversals that cancel is exactly
 * the kind of reasoning that is worth writing down and then *checking*, so the constant below is
 * one line to change and the screenshot in NEXT.md is what confirms it.
 *
 * **4. Buffers are cached per model asset, never per entity.** Ten crates are one vertex buffer
 * drawn ten times with ten world matrices. Keyed by `Uuid` for the reason D-08 gives everywhere
 * else: an asset that moves keeps its id.
 */

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

#include "CNA/Editor/Core/Uuid.hpp"
#include "CNA/Editor/Scene/SceneModels.hpp"
#include "CNA/Editor/Scene/SceneSprites3D.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    class GraphicsDevice;
    class Texture2D;
}

namespace CNA::Editor
{
    class AssetDatabase;

    /** @brief What one model pass did, for the diagnostics panel and the tests. */
    struct ModelPassStats
    {
        /** @brief Models drawn, counting one per entity rather than per shared asset. */
        std::size_t modelsDrawn = 0;

        /** @brief Triangles submitted, before whatever the device culls. */
        std::size_t trianglesDrawn = 0;

        /** @brief Vertex buffers created this pass, which is non-zero only as models first appear. */
        std::size_t buffersCreated = 0;

        /** @brief Draws whose material named a texture that could not be resolved. */
        std::size_t missingTextures = 0;

        /** @brief Sprite quads drawn after the models, blended back to front. */
        std::size_t spritesDrawn = 0;

        /**
         * @brief Which effect the pass is using: "PbrEffect", "BasicEffect", or "none".
         *
         * Reported rather than assumed, because it varies by build and it is the first thing worth
         * knowing when a model looks different on one machine than another.
         */
        std::string effect = "none";
    };

    /**
     * @brief Uploads and draws the models a `SceneModelBatch` names.
     *
     * Owns its GPU resources and releases them on `shutdown`. Not copyable: it holds device
     * objects whose lifetime is the device's.
     */
    class CnaModelPass
    {
    public:
        CnaModelPass();
        ~CnaModelPass();

        CnaModelPass(const CnaModelPass&) = delete;
        CnaModelPass& operator=(const CnaModelPass&) = delete;

        /**
         * @brief Binds the pass to a device and the database its textures are resolved through.
         *
         * Constructs the effect here rather than lazily, so a build that cannot make a `PbrEffect`
         * has said so before the first frame instead of during it.
         */
        void initialize(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
                        const AssetDatabase& assets);

        /** @brief Releases every buffer, texture and effect. */
        void shutdown();

        /**
         * @brief Draws @p batch into the *current* render target, which must have depth.
         *
         * The caller owns the target and the clear: this pass runs between the background and the
         * wireframe overlay, and a pass that cleared would erase the one and a pass that set its
         * own target would have nowhere to put the other.
         */
        ModelPassStats render(const SceneModelBatch& batch);

        /**
         * @brief Draws @p sprites as textured quads, after @p batch's models.
         *
         * After, and with **depth writing off while depth testing stays on**. Both halves matter.
         * Testing on is what lets a model stand in front of a sprite behind it. Writing off is what
         * stops a sprite's own transparent corners from punching a hole in the depth buffer that a
         * sprite drawn later cannot draw through -- the classic symptom being a rectangle of
         * background around every sprite that overlaps another.
         *
         * @param resolveTexture How a sprite's `Uuid` becomes a texture. Injected rather than
         *        looked up here, because the renderer beside this one already caches every sprite
         *        texture in the project and a second cache would load them all twice.
         */
        ModelPassStats renderSprites(
            const SceneSpriteBatch3D& sprites, const SceneModelBatch& batch,
            const std::function<Microsoft::Xna::Framework::Graphics::Texture2D*(const Uuid&)>&
                resolveTexture);

        /** @brief Drops the GPU buffers for @p assetId, or all of them when it is nil. */
        void invalidateModel(const Uuid& assetId);

        /** @brief True when an effect was constructed and the pass can draw. */
        [[nodiscard]] bool isReady() const;

        /** @brief Which effect was constructed: "PbrEffect", "BasicEffect" or "none". */
        [[nodiscard]] const std::string& getEffectName() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
