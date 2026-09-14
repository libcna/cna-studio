// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Scene/EntityJson.hpp
 * @brief Reading and writing one entity, shared by every document that holds entities.
 *
 * Extracted from SceneDocument when prefabs arrived (plan.md ED-300). A `.cnaprefab` stores an
 * entity subtree in exactly the shape a `.cnascene` stores its entities, and it has to: an
 * instantiated prefab and a hand-authored entity must be indistinguishable once they are in a
 * scene, or the same entity would round-trip differently depending on where it came from.
 *
 * Two implementations of one encoding is the failure this avoids. They would drift, and the symptom
 * -- a prefab that loads in the editor and not in the game, or an override that appears out of
 * nowhere -- is the kind nobody can see until they diff two files by hand.
 */

#include <string>
#include <vector>

#include "CNA/Editor/Core/ComponentDescriptor.hpp"
#include "CNA/Editor/Core/Json.hpp"
#include "CNA/Editor/Scene/EditorEntity.hpp"

namespace CNA::Editor
{
    /**
     * @brief Serialises one entity to the object form documented in docs/FORMATS.md.
     *
     * Defaults are *not* written: a field equal to its declared default is left out, because a file
     * that accumulated every field anyone glanced at would make its every diff noise.
     */
    [[nodiscard]] JsonValue entityToJson(const EditorEntity& entity);

    /**
     * @brief Reads one entity written by entityToJson().
     *
     * @param json The serialised entity.
     * @param registry Resolves each component's property types. An unregistered type is read for
     *        byte fidelity rather than for meaning, so that opening and saving a document whose
     *        plugin is missing leaves the file as it was.
     * @param warnings Non-fatal problems are appended here. Nothing is ever dropped silently.
     */
    [[nodiscard]] EditorEntity entityFromJson(const JsonValue& json,
                                              const ComponentRegistry& registry,
                                              std::vector<std::string>& warnings);

    /**
     * @brief Reads a value with no descriptor to say what it is.
     *
     * Used for an unregistered component's properties and for editor state, where the goal is not
     * fidelity of *meaning* -- nothing knows what the field means -- but fidelity of bytes.
     */
    [[nodiscard]] PropertyValue readUntypedJson(const JsonValue& json);
}
