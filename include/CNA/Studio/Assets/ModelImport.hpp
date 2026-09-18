// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Assets/ModelImport.hpp
 * @brief Reading a glTF file into the editor's own mesh representation (plan.md ED-405).
 *
 * The producer half of the seam `CNA/Studio/Core/MeshData.hpp` describes; read that file first,
 * because the decisions this one implements are recorded there. What is decided *here* is
 * narrower: which library does the parsing, and what the importer does with the parts of glTF the
 * editor cannot draw.
 *
 * **cgltf does the parsing, vendored in `third_party/cgltf/`.** It is the same library and the
 * same version CNA vendors, which is what plan.md's ED-405 row means by building on CNA's own
 * integration -- but it is a *copy*, not a reach across the sibling checkout, for two reasons.
 * The default build of this repository has no CNA checkout at all (D-03), and this file is in
 * `cna-studio-assets`, one of the CNA-free modules. CNA's own reader would fail a third test
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

#include "CNA/Studio/Core/MeshData.hpp"

namespace CNA::Studio
{
    class JsonValue;

    /** @brief Where a mesh's normals come from. */
    enum class ModelNormals
    {
        /**
         * @brief The file's own, with flat ones computed only for a part that carries none.
         *
         * The default, and right for anything exported by a tool that knows what it is doing:
         * smoothing groups, split edges and averaged normals are decisions the artist made, and
         * recomputing them throws all of it away.
         */
        Import,

        /**
         * @brief Ignore the file's and compute flat ones for every part.
         *
         * For a file whose normals are wrong -- exported inside-out, or left as zeroes by a
         * converter -- which is otherwise a trip back through the authoring tool. Flat normals
         * duplicate every vertex per face, so a mesh gets larger, and it will look faceted
         * because that is what flat means.
         */
        Calculate,
    };

    /**
     * @brief The importer settings that change what comes out of `loadModel`.
     *
     * Read from an asset's sidecar with fromJson(). That one reader is the point: these were read
     * by hand at each call site, each of which honoured `scaleFactor` and quietly ignored
     * `importMaterials` -- so a setting the inspector offered had no effect anywhere
     * (`plan.md` STUDIO-10004).
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

        /** @brief Whether to trust the file's normals. See @ref ModelNormals. */
        ModelNormals normals = ModelNormals::Import;

        /**
         * @brief Reads these settings out of an asset's `importerSettings`.
         *
         * A field the sidecar does not carry keeps its declared default rather than becoming a
         * zero -- which is the difference between "the user has not chosen" and "the user chose
         * no materials".
         */
        [[nodiscard]] static ModelImportSettings fromJson(const JsonValue& importerSettings);
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

        /**
         * @brief How many animations the file carries. None of them are read.
         *
         * On the *result* although nothing was imported, because this is the only place with the
         * parsed file in hand -- and counting it costs one field read of something already parsed.
         * What it is for is on `ModelDescription::animationCount`.
         */
        std::size_t animationCount = 0;
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
     * Animations are not read, and there is no setting pretending otherwise: an animation needs a
     * skeleton to drive, `MeshData` deliberately has no node hierarchy yet, and building one
     * against no consumer is the mistake ED-311 is parked to avoid. What a file carries is
     * *reported* instead, as `ModelDescription::animationCount`. When skeletal animation becomes a
     * task, it arrives as fields beside `MeshData::parts` and this signature does not change.
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

        /**
         * @brief How many animations the file carries, none of which are imported yet.
         *
         * A fact rather than a setting, and it replaces one. `ImporterIds::kModel` used to declare
         * an `importAnimations` checkbox that nothing read: a control that does nothing teaches a
         * user that the editor lies, which is worse than the gap it was standing in for. A count
         * says the same thing truthfully -- "this file has four animations and Studio does not
         * read them yet" -- and it is the number somebody needs before deciding whether that
         * matters (`plan.md` STUDIO-10004, `STUDIO-21001`).
         */
        std::size_t animationCount = 0;

        /**
         * @brief Primitives left out because their topology is not triangles.
         *
         * Reported so that a model which quietly lost a third of itself stops being quiet. The
         * importer has counted these since it was written and put the number in a struct nobody
         * read; this is what carries it to the inspector.
         */
        std::size_t skippedPrimitives = 0;

        /** @brief The model's extent in editor world units, after `scaleFactor`. */
        StudioVector3 size;
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
