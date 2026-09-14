// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Scene/BuiltinComponents.hpp
 * @brief The component types every CNA-native project starts with.
 *
 * These are descriptors, not C++ classes -- see ComponentDescriptor.hpp for why. The set is
 * deliberately small and 2D-first, matching plan.md Phase 1: Transform, SpriteRenderer, Camera,
 * AudioSource, plus the 3D types that only Phase 3 will make meaningful. Adding a built-in is a
 * one-function change here and requires no other code anywhere in the editor.
 */

#include <string>
#include <vector>

#include "CNA/Editor/Core/ComponentDescriptor.hpp"

namespace CNA::Editor
{
    /** @brief Stable type ids for the built-in components. */
    namespace BuiltinComponentIds
    {
        inline constexpr const char* kTransform = "CNA.Transform";
        inline constexpr const char* kSpriteRenderer = "CNA.SpriteRenderer";
        inline constexpr const char* kCamera = "CNA.Camera";
        inline constexpr const char* kAudioSource = "CNA.AudioSource";
        inline constexpr const char* kAudioListener = "CNA.AudioListener";
        inline constexpr const char* kModelRenderer = "CNA.ModelRenderer";
        inline constexpr const char* kLight = "CNA.Light";
        inline constexpr const char* kTags = "CNA.Tags";
        inline constexpr const char* kLayer = "CNA.Layer";
        inline constexpr const char* kTilemap = "CNA.Tilemap";
        inline constexpr const char* kSpriteAnimation = "CNA.SpriteAnimation";
    }

    /**
     * @brief The layer `CNA.Layer` offers before a project is open.
     *
     * Deliberately a second constant rather than a reference to `Project::kDefaultLayer`. This
     * module links `cna-editor-core` and nothing else, and reaching into the project module for one
     * string would trade a duplicated literal for a dependency the build graph is meant to forbid.
     * `TheDefaultLayerNameMatchesTheProjects` fails the moment the two disagree.
     */
    inline constexpr const char* kDefaultLayerName = "Default";

    /**
     * @brief Registers every built-in component descriptor into @p registry.
     *
     * Safe to call more than once: registration replaces by type id, so a second call simply
     * restores the built-in schema over anything a plugin overrode.
     */
    void registerBuiltinComponents(ComponentRegistry& registry);

    /**
     * @brief Re-registers `CNA.Layer` so its choices are @p layers.
     *
     * The layer list belongs to the project and changes while the editor is open, but a
     * descriptor's enum options are fixed at registration. Re-registering is exactly the escape
     * hatch ComponentRegistry documents for this: it replaces the descriptor and touches no loaded
     * document, so an entity on a layer that has just been renamed keeps its stored value and is
     * reported by scene validation rather than silently rewritten.
     *
     * @param layers The project's layer names. An empty list is ignored, since a component whose
     *        enum has no options is one nothing can be set to.
     */
    void applyProjectLayers(ComponentRegistry& registry, const std::vector<std::string>& layers);
}
