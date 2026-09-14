// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Assets/ModelImport.hpp
 * @brief Reading a glTF file into the editor's own mesh representation (plan.md ED-405).
 *
 * The producer half of the seam `CNA/Editor/Core/MeshData.hpp` describes; read that file first,
 * because the decisions this one implements are recorded there. What is decided *here* is
 * narrower: which library does the parsing, and what the importer does with the parts of glTF the
 * editor cannot draw.
 *
 * **cgltf does the parsing, vendored in `third_party/cgltf/`.** It is the same library and the
 * same version CNA vendors, which is what plan.md's ED-405 row means by building on CNA's own
 * integration -- but it is a *copy*, not a reach across the sibling checkout, for two reasons.
 * The default build of this repository has no CNA checkout at all (D-03), and this file is in
 * `cna-editor-assets`, one of the CNA-free modules. CNA's own reader would fail a third test
 * anyway: it is `CNA::Internal::GltfImport`, and D-01 forbids reaching into CNA's internals. The
 * same reasoning that produced gap G-04 for `SpriteFont` applies here, with a better ending --
 * glTF has a parser that is not CNA's to withhold.
 *
 * **What cannot be drawn is reported, never guessed at.** A primitive that is a line list, a
 * material that needs a shader model this editor has not got, a texture embedded in a `.glb` with
 * no URI to point at -- each of those is counted in `ModelImportResult::warnings` and left out,
 * rather than approximated into something that looks like it worked. A model that silently loses
 * a third of itself is the kind of bug that gets found in a shipped game.
 */

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "CNA/Editor/Core/MeshData.hpp"

namespace CNA::Editor
{
    /**
     * @brief The importer settings that change the geometry, read from an asset's sidecar.
     *
     * A subset of what `ImporterIds::kModel` declares: the fields here are the ones that alter
     * what comes out of `loadModel`. `importMaterials` and `importAnimations` are declared on the
     * importer too but are not settings this function reads -- see the notes on each below.
     */
    struct ModelImportSettings
    {
        /**
         * @brief Multiplies every position, so a model authored in centimetres need not be scaled
         *        on every entity that uses it.
         *
         * Applied to positions only. Normals are direction vectors and a uniform scale leaves them
         * pointing where they were.
         */
        float scaleFactor = 1.0f;

        /**
         * @brief Read the file's materials, or leave `MeshData::materials` empty.
         *
         * Off is a real workflow rather than a toggle for its own sake: a model brought in for its
         * shape, to be materialled in the editor, should not arrive with a list of PBR materials
         * flattened into Blinn-Phong approximations that someone then has to delete.
         */
        bool importMaterials = true;
    };

    /** @brief One thing the importer could not do, in words a person can act on. */
    struct ModelImportWarning
    {
        /** @brief What was skipped -- a mesh name, a material name, or the file itself. */
        std::string subject;

        /** @brief Why, phrased as what the editor cannot do rather than what the file did wrong. */
        std::string reason;
    };

    /**
     * @brief What `loadModel` produced, including what it could not.
     *
     * `mesh` is meaningful only when `succeeded` is true. A failed load reports its reason in
     * `warnings` and returns an empty mesh rather than a partial one, because half a model drawn
     * with no indication that it is half is worse than no model and an error.
     */
    struct ModelImportResult
    {
        bool succeeded = false;

        MeshData mesh;

        std::vector<ModelImportWarning> warnings;

        /**
         * @brief Primitives left out because their topology is not triangles.
         *
         * Counted separately from `warnings` because a file can carry hundreds and one line per
         * primitive would bury everything else. `warnings` gets one entry summarising them.
         */
        std::size_t skippedPrimitives = 0;
    };

    /**
     * @brief Reads @p absolutePath as glTF or GLB and returns it in the editor's world convention.
     *
     * Both container forms, chosen by content rather than by extension -- cgltf sniffs the GLB
     * magic itself, so a `.gltf` that is really a `.glb` loads anyway. External buffers and any
     * `.bin` beside the file are loaded relative to it; base64 data URIs are decoded in place.
     *
     * The geometry comes back baked flat and mirrored, exactly as `MeshData` promises: node
     * transforms folded into positions, Y negated to convert glTF's Y-up frame to this editor's
     * Y-down world, triangle winding reversed to survive that mirror, and `scaleFactor` applied.
     * A consumer draws the result without knowing any of it happened.
     *
     * Animations are not read. `ImporterIds::kModel` declares an `importAnimations` setting and
     * this function ignores it, which is the honest state of things rather than an oversight: an
     * animation needs a skeleton to drive, `MeshData` deliberately has no node hierarchy yet, and
     * building one against no consumer is the mistake ED-311 is parked to avoid. When skeletal
     * animation becomes a task, it arrives as fields beside `MeshData::parts` and this signature
     * does not change.
     */
    [[nodiscard]] ModelImportResult loadModel(const std::string& absolutePath,
                                              const ModelImportSettings& settings = {});

    /**
     * @brief The facts a model asset reports about itself, for the inspector.
     *
     * Facts, never settings -- the same distinction `SpriteFontDescription` draws, and for the
     * same reason: these are answers read out of the file, and an editable copy would be a second
     * answer to a question the file has already settled.
     */
    struct ModelDescription
    {
        std::size_t partCount = 0;
        std::size_t vertexCount = 0;
        std::size_t triangleCount = 0;
        std::size_t materialCount = 0;

        /** @brief The model's extent in editor world units, after `scaleFactor`. */
        EditorVector3 size;
    };

    /**
     * @brief Reads @p absolutePath's facts, or returns nothing when it is not a model this
     *        importer understands.
     *
     * A full parse, because glTF states none of these in a header -- a triangle count is the sum
     * of every primitive's, and there is no way to know it without reading them. That makes this
     * the most expensive fact-gathering the editor does, which is why `applyImporterFacts` writes
     * the result to the sidecar and compares before rewriting, as the texture and sprite-font
     * paths already do.
     */
    [[nodiscard]] std::optional<ModelDescription> readModelDescription(
        const std::string& absolutePath, const ModelImportSettings& settings = {});
}
