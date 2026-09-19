// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Scene/SceneModels.hpp
 * @brief What the 3D view should draw solid, decided without a GPU (plan.md ED-402).
 *
 * The same shape as `SceneWireframe.hpp` and for the same reason: *which* models to draw, *where*,
 * and *lit by what* are decisions, and decisions belong in this CNA-free module where CI can check
 * them with no device. The viewport is handed a finished list and does nothing but upload and
 * draw it.
 *
 * That split is what makes ED-402 testable at all. A model pass written straight into the
 * viewport could only be verified by looking at a screenshot, and the things most likely to be
 * wrong -- the world matrix, which mirror is applied, whether an entity is lit from the right
 * side -- are all arithmetic that a test can pin exactly.
 *
 * **The one convention that must not drift**, and the reason this file names it twice: the
 * view-projection here is `StudioCamera3D::getViewProjectionMatrix()`, the *same* matrix the
 * wireframe, the picker and the gizmos already go through, and it already contains the Y mirror
 * that converts XNA's Y-up 3D frame to this editor's Y-down world. A model pass that mirrored
 * again would draw models upside down relative to the grid and the gizmos around them; one that
 * built its own matrix would drift from what a user can click on. Neither is theoretical -- both
 * are the first two ways this task can go wrong.
 */

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <functional>
#include <vector>

#include "CNA/Studio/Core/StudioMatrix.hpp"
#include "CNA/Studio/Core/MeshData.hpp"
#include "CNA/Studio/Core/Uuid.hpp"
#include "CNA/Studio/Scene/StudioCamera3D.hpp"
#include "CNA/Studio/Scene/SceneEnvironment.hpp"
#include "CNA/Studio/Scene/SceneLighting.hpp"

namespace CNA::Studio
{
    class SceneDocument;
    class StudioEntity;

    /**
     * @brief Supplies an authored material by asset id, or nothing when it is not available.
     *
     * The material counterpart of `MeshProvider`, and nothing is a normal answer for the same
     * reasons: an asset not scanned yet, or a reference to a file that has gone.
     */
    using MaterialProvider = std::function<std::optional<MeshMaterial>(const Uuid& assetId)>;

    /** @brief One model to draw: its geometry, where it goes, and what lights it. */
    struct ModelDraw
    {
        Uuid entityId;

        /** @brief The model asset. The viewport keys its GPU buffers on this, never on the entity. */
        Uuid modelId;

        /**
         * @brief The geometry, owned by the mesh cache.
         *
         * Borrowed for the frame this batch is drawn in, exactly as `MeshProvider` promises. Never
         * null in a batch: an entity whose mesh has not been imported is left out rather than
         * carried with a null, because a consumer that has to check would be one that could forget.
         */
        const MeshData* mesh = nullptr;

        /** @brief Model space to world space, from the entity's own world transform. */
        StudioMatrix world;

        /**
         * @brief The lighting resolved at this entity's position.
         *
         * Per entity rather than per scene, because that is what makes a point light behave like
         * one -- see `SceneLighting.hpp`. It is the same answer for every directional-only scene,
         * and computing it per draw costs a few multiplications against a list that is at most as
         * long as the scene's lights.
         */
        EffectLighting lighting;

        /**
         * @brief The material asset a `ModelRenderer` overrides its model's own with, or none.
         *
         * `CNA.ModelRenderer` has declared this reference since Phase 1 and nothing could satisfy
         * it until ED-403 gave materials a file of their own. When set, it replaces the material of
         * **every** part -- which is what a single override can mean, and why ED-410's per-mesh
         * list is a separate row rather than a refinement of this one.
         *
         * Carried as a resolved material rather than as an id, so the viewport does not need a
         * second way to read an asset: the batch is built where the database is.
         */
        std::optional<MeshMaterial> materialOverride;

        /**
         * @brief Per-part overrides, keyed by the part's name (ED-410).
         *
         * Applied *after* `materialOverride` and only to the parts it names, so the two compose the
         * way a user would predict: the single override is "this whole model", the list is "except
         * these parts". A name that matches nothing is left here rather than dropped, so validation
         * can report it -- which is the reason the list is keyed by name at all. An index-keyed
         * list survives a reimport that inserts a part by silently pointing at a different one,
         * and nothing can notice.
         */
        std::vector<std::pair<std::string, MeshMaterial>> partMaterials;

        /** @brief True when this entity is in the selection, so the viewport can mark it. */
        bool selected = false;
    };

    /** @brief Everything the viewport needs for one solid pass. */
    struct SceneModelBatch
    {
        std::vector<ModelDraw> draws;

        /**
         * @brief The camera's own view-projection -- mirror included, applied once.
         *
         * Carried in the batch rather than recomputed by the viewport so that there is exactly one
         * answer to "where does this go on screen", shared by the solid pass, the wireframe drawn
         * over it and the picking that decides what a click hit.
         */
        StudioMatrix viewProjection;

        /**
         * @brief The same camera, split -- because some effects need the view on its own.
         *
         * `viewProjection` stays authoritative and is what everything positions against; these two
         * exist because CNA's `PbrEffect` *inverts the view matrix* to recover the eye position for
         * its specular term, and an identity view would put the eye at the origin and light every
         * model from the wrong place. `projection` carries the Y mirror, exactly as
         * `StudioCamera3D` puts it there, so multiplying these two reproduces `viewProjection`
         * rather than approximating it. `TheModelBatchsSplitCameraMultipliesBackToItsProduct` pins
         * that, because two descriptions of one camera that could drift is precisely the bug this
         * whole seam was arranged to avoid.
         */
        StudioMatrix view;
        StudioMatrix projection;

        /**
         * @brief The scene's ambient and fog (ED-407), carried so the viewport need not ask twice.
         *
         * On the batch rather than on each draw: fog is a property of the level, so every model in
         * one frame is fogged by the same numbers, and a per-draw copy would be the same struct
         * repeated once per entity with nothing able to make them differ.
         */
        SceneEnvironment environment;

        /** @brief Entities that name a model whose geometry is not available yet. */
        std::size_t pendingMeshes = 0;

        /** @brief Total triangles across every draw, before any culling the device does. */
        std::size_t triangleCount = 0;
    };

    /**
     * @brief Returns everything in @p scene that a solid model pass should draw.
     *
     * @param meshProvider Where geometry comes from; an entity is left out when this returns
     *        nullptr, and counted in `pendingMeshes` so the viewport can say how many models are
     *        still importing rather than leaving a user wondering where their crate went.
     * @param selection Entities to mark as selected. Order is not significant.
     * @param materialProvider Resolves a material asset id to the material it holds, or nothing.
     *        Injected for the same reason `meshProvider` is: this module has no asset database and
     *        an entity naming a material that has not loaded should draw with its model's own
     *        rather than not at all.
     *
     * Disabled entities are skipped, matching every other pass. An entity with a `ModelRenderer`
     * but no transform is skipped too: there is nowhere to put it, and the origin is not a guess
     * this function is entitled to make.
     */
    [[nodiscard]] SceneModelBatch buildSceneModelBatch(const SceneDocument& scene,
                                                       const StudioCamera3D& camera,
                                                       const MeshProvider& meshProvider,
                                                       const std::vector<Uuid>& selection = {},
                                                       const MaterialProvider& materialProvider = {});

    /**
     * @brief Returns the mesh @p entity's `ModelRenderer` names, or nullptr when there is none.
     *
     * Every step is a real "no": no component, no asset reference, no provider, nothing imported
     * yet. All four mean the same thing to a caller that draws or measures -- there is no geometry
     * here -- so they are one return value rather than four.
     *
     * Public, and here rather than in each caller, because three modules now ask the same question
     * and a scene where the wireframe, the bounds and the model batch disagreed about which mesh an
     * entity has would be a scene drawn in one place and clicked in another. `buildSceneModelBatch`
     * keeps its own two-step form: it has to tell "no `ModelRenderer` at all" from "a
     * `ModelRenderer` whose mesh has not landed yet" so it can report the second as pending, and
     * that is the one distinction this function deliberately collapses.
     */
    [[nodiscard]] const MeshData* findEntityMesh(const StudioEntity& entity,
                                                 const MeshProvider& meshProvider);
}
