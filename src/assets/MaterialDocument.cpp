// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Assets/MaterialDocument.hpp"

#include <algorithm>
#include <cmath>

namespace CNA::Editor
{
    namespace
    {
        JsonValue vectorToJson(const EditorVector3& vector)
        {
            JsonValue array = JsonValue::makeArray();
            array.append(JsonValue{static_cast<double>(vector.x)});
            array.append(JsonValue{static_cast<double>(vector.y)});
            array.append(JsonValue{static_cast<double>(vector.z)});
            return array;
        }

        EditorVector3 vectorFromJson(const JsonValue& json, const EditorVector3& fallback)
        {
            if (!json.isArray() || json.getElements().size() < 3) { return fallback; }

            const std::vector<JsonValue>& elements = json.getElements();
            return EditorVector3{
                static_cast<float>(elements[0].asNumber(static_cast<double>(fallback.x))),
                static_cast<float>(elements[1].asNumber(static_cast<double>(fallback.y))),
                static_cast<float>(elements[2].asNumber(static_cast<double>(fallback.z)))};
        }

        /** @brief Writes an id, or omits it: a nil reference and an absent one mean the same thing. */
        void setTexture(JsonValue& object, const char* key, const Uuid& id)
        {
            if (!id.isValid()) { return; }
            object.set(key, JsonValue{id.toString()});
        }
    }

    MeshMaterial MaterialDocument::toMeshMaterial() const
    {
        MeshMaterial material;
        material.name = name;
        material.diffuseColor = diffuseColor;
        material.emissiveColor = emissiveColor;
        material.alpha = alpha;
        material.metallic = std::clamp(metallic, 0.0f, 1.0f);
        material.roughness = std::clamp(roughness, 0.0f, 1.0f);

        // Derived here rather than stored, exactly as the glTF importer derives them: a material
        // holding both descriptions independently is a material whose two halves can disagree, and
        // the disagreement only shows on whichever effect the user is not looking at.
        const float metallicFactor = material.metallic;
        material.specularColor =
            EditorVector3{metallicFactor * diffuseColor.x + (1.0f - metallicFactor) * 0.04f,
                          metallicFactor * diffuseColor.y + (1.0f - metallicFactor) * 0.04f,
                          metallicFactor * diffuseColor.z + (1.0f - metallicFactor) * 0.04f};

        const float clampedRoughness = std::clamp(roughness, 0.03f, 1.0f);
        material.specularPower =
            std::clamp(2.0f / (clampedRoughness * clampedRoughness) - 2.0f, 1.0f, 1024.0f);

        // The texture paths stay empty on purpose: this document speaks in ids, and resolving one
        // to a file is the caller's job because the caller is what holds the asset database.
        return material;
    }

    JsonValue MaterialDocument::toJson() const
    {
        JsonValue root = JsonValue::makeObject();
        root.set("formatVersion", JsonValue{kFormatVersion});
        root.set("name", JsonValue{name});
        root.set("diffuseColor", vectorToJson(diffuseColor));
        root.set("emissiveColor", vectorToJson(emissiveColor));
        root.set("metallic", JsonValue{static_cast<double>(metallic)});
        root.set("roughness", JsonValue{static_cast<double>(roughness)});
        root.set("alpha", JsonValue{static_cast<double>(alpha)});

        setTexture(root, "diffuseTexture", diffuseTexture);
        setTexture(root, "normalTexture", normalTexture);
        setTexture(root, "metallicRoughnessTexture", metallicRoughnessTexture);
        setTexture(root, "emissiveTexture", emissiveTexture);

        return root;
    }

    bool MaterialDocument::loadFromJson(const JsonValue& json)
    {
        // The one hard failure. A file from a future editor may hold fields this build would
        // silently drop on the next save, and quietly rewriting somebody's material with less in
        // it than they put there is worse than refusing to open it.
        const int version = static_cast<int>(json["formatVersion"].asNumber(kFormatVersion));
        if (version > kFormatVersion) { return false; }

        const MaterialDocument defaults;

        name = json["name"].asString(defaults.name);
        diffuseColor = vectorFromJson(json["diffuseColor"], defaults.diffuseColor);
        emissiveColor = vectorFromJson(json["emissiveColor"], defaults.emissiveColor);
        metallic = static_cast<float>(json["metallic"].asNumber(defaults.metallic));
        roughness = static_cast<float>(json["roughness"].asNumber(defaults.roughness));
        alpha = static_cast<float>(json["alpha"].asNumber(defaults.alpha));

        diffuseTexture = Uuid::parse(json["diffuseTexture"].asString(""));
        normalTexture = Uuid::parse(json["normalTexture"].asString(""));
        metallicRoughnessTexture = Uuid::parse(json["metallicRoughnessTexture"].asString(""));
        emissiveTexture = Uuid::parse(json["emissiveTexture"].asString(""));

        return true;
    }
}
