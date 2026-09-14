// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Core/MeshData.hpp
 * @brief The seam that carries mesh geometry from an importer to whatever draws it (plan.md ED-405).
 *
 * This file is the *interface* between the glTF importer in `cna-editor-assets` and the two things
 * that will draw what it produces: the CNA-free wireframe in `cna-editor-scene`, and -- when
 * ED-402 arrives -- the `VertexBuffer`/`BasicEffect` pass in `cna-editor-viewport`. plan.md's
 * ED-405 row says to design that seam before writing the parser, because getting it wrong means
 * writing ED-402 twice. Three decisions make it up, and each is here rather than in the parser
 * precisely so that both consumers inherit the same answer.
 *
 * **1. It lives in `cna-editor-core`, which is not where either party lives.** The producer is
 * `cna-editor-assets` and one consumer is `cna-editor-scene`, and those two modules link nothing
 * but core -- neither can see the other, and neither should. Core is the only place a type can sit
 * where both can reach it, which is the same reason `EditorMatrix` is in core while the camera
 * that uses it is in scene.
 *
 * **2. The geometry is already in the editor's world convention.** glTF is Y-up and right-handed;
 * this editor's world is Y-down, because its 2D side is and the two views must agree about which
 * way is down (plan.md's ED-400 convention). So the importer mirrors Y *into the data*, once, and
 * every consumer draws what it is given. The alternative -- mirroring at draw time -- means the
 * wireframe path and the CNA path each have to remember, and the day one of them forgets is the
 * day models are upside down in one view and not the other. Mirroring one axis also reverses
 * triangle winding, so the importer reverses it back; see `MeshPart::indices`.
 *
 * **3. A vertex is laid out to match XNA's `VertexPositionNormalTexture` field for field**, so
 * ED-402's upload is a copy rather than a translation. That is the whole of the concession this
 * CNA-free file makes to CNA, and it costs nothing: position, normal and one texture coordinate is
 * what a glTF primitive carries anyway and what a `BasicEffect` can light.
 *
 * What is deliberately *not* here is a node hierarchy. glTF has one; the importer bakes it into
 * vertex positions and hands over a flat list of parts. A hierarchy is only worth keeping if
 * something animates it, nothing does, and a schema designed against no consumer is exactly the
 * mistake ED-311's `NestedStructure` is still parked to avoid. When skeletal animation is a real
 * task, the hierarchy comes back as its own field beside `parts`, and nothing already written
 * needs to change.
 */

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "CNA/Editor/Core/EditorMath.hpp"
#include "CNA/Editor/Core/Uuid.hpp"

namespace CNA::Editor
{
    /**
     * @brief One vertex: position, normal, one texture coordinate.
     *
     * The field order and types match `Microsoft::Xna::Framework::Graphics::VertexPositionNormalTexture`
     * exactly, which is what lets ED-402 hand a span of these to a `VertexBuffer` without walking
     * them. Anything a glTF primitive carries beyond these three -- vertex colours, tangents, a
     * second UV set, joint weights -- is dropped by the importer, because `BasicEffect` cannot use
     * it and this editor has nothing else to draw with yet.
     */
    struct MeshVertex
    {
        EditorVector3 position;

        /**
         * @brief Unit-length, and pointing out of the surface after the importer's Y mirror.
         *
         * Defaulted to +Z rather than to zero: a mesh whose glTF carried no normals gets flat
         * ones computed per face, but a zero vector would light as black and read as a bug in the
         * renderer rather than as missing data.
         */
        EditorVector3 normal{0.0f, 0.0f, 1.0f};

        EditorVector2 texCoord;
    };

    /**
     * @brief One drawable run of triangles sharing a single material.
     *
     * The unit is a glTF *primitive* rather than a glTF mesh, because a primitive is the largest
     * span with one material, and one material is what one `BasicEffect` pass can draw.
     */
    struct MeshPart
    {
        /** @brief The glTF mesh's name, suffixed when a mesh had more than one primitive. */
        std::string name;

        std::vector<MeshVertex> vertices;

        /**
         * @brief A triangle list: three indices per triangle, into `vertices`.
         *
         * Wound so that `cross(v1 - v0, v2 - v0)` points the same way as the vertices' normals --
         * that is, counter-clockwise seen from outside, which is what glTF specifies and what
         * `CullMode::CullCounterClockwiseFace` expects. Preserving that through the importer's Y
         * mirror takes an explicit reversal, since mirroring one axis flips the handedness of
         * every triangle in the file. `meshWindingMatchesNormals` checks the property, and the
         * test suite asserts it rather than assuming it.
         *
         * Always triangles. glTF can also carry strips, fans, lines and points; the importer
         * converts strips and fans, and reports the rest as skipped rather than drawing them
         * wrongly.
         */
        std::vector<std::uint32_t> indices;

        /**
         * @brief Index into `MeshData::materials`, or -1 when the primitive named no material.
         *
         * -1 is a real answer rather than a missing one: glTF says an unnamed material means the
         * default material, and a consumer that sees -1 should draw with its own default rather
         * than skip the part.
         */
        int materialIndex = -1;

        /** @brief Number of triangles, which is `indices.size() / 3`. */
        [[nodiscard]] std::size_t getTriangleCount() const { return indices.size() / 3; }
    };

    /**
     * @brief A material, reduced to what a `BasicEffect` can express.
     *
     * glTF materials are metallic-roughness PBR and `BasicEffect` is Blinn-Phong, so this is a
     * lossy conversion and is meant to be. The alternative is carrying PBR fields that nothing in
     * this editor can render, which would put the loss in the renderer instead of here, where it
     * can at least be described. ED-403 is where a richer material model belongs if one is wanted.
     */
    struct MeshMaterial
    {
        std::string name;

        /** @brief glTF's base-colour factor, RGB. */
        EditorVector3 diffuseColor{1.0f, 1.0f, 1.0f};

        /** @brief glTF's emissive factor, RGB. */
        EditorVector3 emissiveColor{0.0f, 0.0f, 0.0f};

        /**
         * @brief Derived from metallic-roughness rather than authored.
         *
         * glTF has no specular colour. A metallic surface reflects its own base colour and a
         * dielectric reflects white, which is the one line of the PBR model that survives the
         * trip to Blinn-Phong intact and is worth keeping for it.
         */
        EditorVector3 specularColor{0.0f, 0.0f, 0.0f};

        /** @brief Blinn-Phong exponent, mapped from glTF roughness. */
        float specularPower = 16.0f;

        /** @brief glTF's base-colour alpha. */
        float alpha = 1.0f;

        /**
         * @brief The base-colour texture's URI, relative to the model file, or empty.
         *
         * A path and not an asset id, because the importer reads a file on disk and knows nothing
         * about the asset database -- resolving this to a `Uuid` is the caller's job, and the
         * caller is the one that has an `AssetDatabase` to resolve it against. A texture embedded
         * in a `.glb` has no URI at all and reports empty, which is honest: there is no file to
         * point at.
         */
        std::string diffuseTexturePath;

        /**
         * @brief glTF's metallic factor, 0 for a dielectric and 1 for a metal.
         *
         * This and the four fields below are what ED-402 added, and they are the reason to read
         * the paragraph above about lossiness twice: the Blinn-Phong fields are *still* filled in
         * and still lossy, because CNA's `PbrEffect` is an extension (`NOXNA`) and a build whose
         * backend has no PBR path falls back to `BasicEffect`. So a material carries both
         * descriptions of itself, and each renderer reads the one it can use.
         *
         * Carrying both is not duplication to be cleaned up later. `specularColor` and
         * `specularPower` are *derived* from these two by `convertMaterial`, so they cannot
         * disagree -- and deriving them at draw time instead would put the conversion in the one
         * place that must not have to know about glTF at all.
         */
        float metallic = 0.0f;

        /** @brief glTF's roughness factor, 0 for a mirror and 1 for fully diffuse. */
        float roughness = 1.0f;

        /**
         * @brief The tangent-space normal map's URI, relative to the model file, or empty.
         *
         * A path and not a `Uuid`, for the reason `diffuseTexturePath` gives: the importer has no
         * asset database and must not pretend to. A `BasicEffect` fallback ignores this, which is
         * the honest degradation -- a normal map cannot be approximated into a Blinn-Phong term.
         */
        std::string normalTexturePath;

        /**
         * @brief The occlusion-roughness-metallic map's URI, or empty.
         *
         * glTF packs occlusion in R, roughness in G and metallic in B, and permits the occlusion
         * map to be a *separate* image. Only the packed form is carried: `PbrEffect` takes one
         * metallic-roughness texture and one occlusion texture, and a file that separates them
         * gets a warning rather than a silently half-applied material.
         */
        std::string metallicRoughnessTexturePath;

        /** @brief The emissive map's URI, or empty. Multiplies `emissiveColor`. */
        std::string emissiveTexturePath;
    };

    /**
     * @brief A whole model: its parts, its materials and its extent.
     *
     * Everything here is in editor-world units and editor-world orientation -- Y already mirrored,
     * `ModelImportSettings::scaleFactor` already applied. A consumer draws it as it stands.
     */
    struct MeshData
    {
        std::vector<MeshPart> parts;
        std::vector<MeshMaterial> materials;

        /**
         * @brief The axis-aligned extent of every vertex in `parts`.
         *
         * Two loose `EditorVector3` rather than the `WorldBounds3D` the 3D camera uses, because
         * that type lives in `cna-editor-scene` and this file may not depend on it -- scene is a
         * consumer here, not a dependency. Converting is `WorldBounds3D{data.boundsMin,
         * data.boundsMax}`, which scene does at the one place it needs to.
         *
         * Empty when the model has no vertices: `boundsMin` is then greater than `boundsMax` on
         * every axis, which is exactly what `WorldBounds3D::isEmpty` tests for, so the conversion
         * keeps meaning the same thing.
         */
        EditorVector3 boundsMin;
        EditorVector3 boundsMax;

        /** @brief True when the model has no drawable geometry at all. */
        [[nodiscard]] bool isEmpty() const;

        /** @brief Total vertices across every part. */
        [[nodiscard]] std::size_t getVertexCount() const;

        /** @brief Total triangles across every part. */
        [[nodiscard]] std::size_t getTriangleCount() const;
    };

    /**
     * @brief Supplies an imported model's geometry, or nullptr when it is not available.
     *
     * The other half of the seam: `MeshData` is what crosses it, and this is how a consumer asks.
     * Declared here, in core, rather than beside either party, because the producer
     * (`cna-editor-assets`, which caches these) and the consumers (`cna-editor-scene`'s wireframe,
     * and `cna-editor-viewport` when ED-402 lands) cannot see each other and must not need to.
     *
     * A pointer rather than a value: a mesh is megabytes and this is called once per entity per
     * frame, so returning by value would make copying the most expensive thing the 3D view does.
     * The pointer must stay valid for the duration of the call.
     *
     * nullptr is a normal answer, not a failure -- an asset not imported yet, or a reference to a
     * file that has gone. A consumer falls back to whatever it drew before there were meshes.
     */
    using MeshProvider = std::function<const MeshData*(const Uuid& assetId)>;

    /**
     * @brief Recomputes `boundsMin` and `boundsMax` from the vertices in `parts`.
     *
     * Called by the importer once it has finished transforming everything. Exposed because a
     * consumer that alters geometry -- ED-402 has none, but a mesh-editing tool would -- needs the
     * same answer computed the same way rather than its own loop.
     */
    void recomputeMeshBounds(MeshData& data);

    /**
     * @brief Returns true when every triangle in @p part is wound to agree with its own normals.
     *
     * The property `MeshPart::indices` promises, expressed as a check: for each triangle, the face
     * normal from the winding must point within ninety degrees of the vertex normals. Getting this
     * wrong is the specific, silent failure that mirroring an axis causes -- the model looks
     * correct in wireframe, where winding does not matter, and is inside-out the moment ED-402
     * turns backface culling on. Cheap to assert and expensive to discover later, so the test
     * suite asserts it.
     *
     * Degenerate triangles -- zero area, or a vertex normal of zero length -- are skipped rather
     * than failed: they have no winding to disagree with.
     */
    [[nodiscard]] bool meshWindingMatchesNormals(const MeshPart& part);
}
