// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Assets/MaterialDocument.hpp
 * @brief A `.cnamaterial`: a material somebody authored, rather than one a model brought with it
 *        (plan.md ED-403).
 *
 * A glTF file carries its own materials and `MeshData::materials` holds them. This is the other
 * kind: a material that is an *asset*, with an id, that a `ModelRenderer` can point at to override
 * what its model came with. `CNA.ModelRenderer` has declared exactly that reference since Phase 1
 * and nothing has ever been able to satisfy it -- the inspector offered a picker with nothing to
 * pick.
 *
 * **It is a new format at version 1, not a change to an existing one.** Nothing already written
 * gains or loses a field, so no `formatVersion` moves and no migration chain has anything to do --
 * the same shape `.cnarecovery` arrived in (ED-903).
 *
 * **The fields are `MeshMaterial`'s, deliberately.** A material asset that could express things an
 * imported material cannot would be a material the model pass has to handle twice, and the extra
 * expressiveness would be bounded by the same `BasicEffect`/`PbrEffect` pair regardless. So this
 * converts to a `MeshMaterial` and the pass stays one code path. What it adds is what an *asset*
 * needs and a mesh's own material does not: a name a person chose, and texture references by
 * `Uuid` rather than by a path relative to some model file.
 *
 * **It carries no id of its own**, which is worth stating because the first version did and it was
 * wrong. An asset's identity is the `Uuid` its `.cnaasset` sidecar holds -- that is what D-08 means
 * by identity being an id and never a path, and what every scene reference already uses. A second
 * id inside the file would be a second answer to "which material is this", free to disagree with
 * the first, and the disagreement would surface as a `ModelRenderer` pointing at a material the
 * database can find and the file denies being. The same reasoning ED-300 applies to prefab
 * overrides: one description of a fact, not two.
 *
 * That last difference is the one to keep in mind. A `MeshMaterial` carries texture *paths*,
 * because the glTF importer has no asset database and must not pretend to (`MeshData.hpp`). A
 * material asset is written by the editor, which does have one, so it carries ids -- and an id
 * survives the texture being renamed or moved, which a path does not (D-08).
 */

#include <string>

#include "CNA/Editor/Core/Json.hpp"
#include "CNA/Editor/Core/MeshData.hpp"
#include "CNA/Editor/Core/Uuid.hpp"

namespace CNA::Editor
{
    /** @brief An authored material, as stored in a `.cnamaterial` file. */
    struct MaterialDocument
    {
        /** @brief The `formatVersion` this build writes, and the highest it can read. */
        static constexpr int kFormatVersion = 1;

        /** @brief What a person calls it. Defaults to the file's stem when one is created. */
        std::string name = "Material";

        EditorVector3 diffuseColor{1.0f, 1.0f, 1.0f};
        EditorVector3 emissiveColor{0.0f, 0.0f, 0.0f};

        /**
         * @brief Metallic-roughness, kept even when the build draws through `BasicEffect`.
         *
         * The same bargain the importer strikes: which effect draws is a property of the build
         * (gap G-05), so a material that stored only one description of itself would render as
         * something else entirely on the other one. `toMeshMaterial` derives the Blinn-Phong pair
         * from these rather than storing a second, driftable copy.
         */
        float metallic = 0.0f;
        float roughness = 1.0f;

        float alpha = 1.0f;

        /** @brief Texture assets by id, never by path -- see this file's header. Nil for none. */
        Uuid diffuseTexture;
        Uuid normalTexture;
        Uuid metallicRoughnessTexture;
        Uuid emissiveTexture;

        /**
         * @brief Converts to the form the model pass already draws.
         *
         * The texture *paths* come back empty: this side speaks in ids and the caller is the one
         * holding the database that can resolve them. Filling in a path here would be inventing a
         * second way for a texture to be named, which is the mistake `MeshData.hpp` avoids from
         * the other direction.
         */
        [[nodiscard]] MeshMaterial toMeshMaterial() const;

        [[nodiscard]] JsonValue toJson() const;

        /**
         * @brief Reads @p json, keeping defaults for anything absent.
         *
         * @return False when the document declares a `formatVersion` this build cannot read, which
         *         is the only hard failure. Every other absence is a default, because a material
         *         written by a future editor with three more fields should still load as the
         *         material it mostly is rather than as nothing at all.
         */
        bool loadFromJson(const JsonValue& json);
    };
}
