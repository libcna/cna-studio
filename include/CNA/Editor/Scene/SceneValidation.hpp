// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Scene/SceneValidation.hpp
 * @brief Structural checks over a scene document: the faults that compile fine and look wrong.
 *
 * A broken asset reference (MissingReferences.hpp) is only one way a scene can be wrong, and it is
 * the easy one -- the viewport draws a placeholder, so the user sees it. The faults here are the
 * quiet ones: two cameras both claiming to be primary, a sprite scaled to zero, an entity that
 * exists but does nothing. Each of them produces a game that runs and shows the wrong thing, which
 * is the failure mode an editor is supposed to catch before a build does.
 *
 * The rules are deliberately conservative. Every one of them describes a state that is *legal* --
 * nothing here refuses to save, nothing here is repaired automatically -- so a rule that fired on
 * a scene the user meant to write would be worse than no rule at all. Where a state is arguably
 * intentional, it is a Warning; where it makes the runtime's behaviour undefined or arbitrary, it
 * is an Error.
 *
 * CNA-free, registry-driven, and pure: the same scene always produces the same issues in the same
 * order, so the report can be diffed and asserted on.
 */

#include <string>
#include <vector>

#include "CNA/Editor/Core/ComponentDescriptor.hpp"
#include "CNA/Editor/Core/MeshData.hpp"
#include "CNA/Editor/Core/Uuid.hpp"

namespace CNA::Editor
{
    class SceneDocument;

    /** @brief One structural problem found in a scene. */
    struct SceneIssue
    {
        /** @brief How much the issue matters. */
        enum class Severity
        {
            /**
             * @brief Legal, probably unintended.
             *
             * The scene runs; something in it does nothing, or does less than the user expects.
             */
            Warning,

            /**
             * @brief The runtime's behaviour is arbitrary.
             *
             * Two primary cameras means the game picks one, and which one it picks is not the
             * user's decision. That is an error even though the file is well-formed.
             */
            Error
        };

        Severity severity = Severity::Warning;

        /**
         * @brief Stable machine-readable rule id, e.g. "duplicate-primary-camera".
         *
         * Stable so that tests, and eventually a project-level suppression list, can name a rule
         * without depending on its wording.
         */
        std::string ruleId;

        /** @brief The entity at fault, or the nil Uuid when the issue is about the scene itself. */
        Uuid entityId;

        /** @brief The entity's name at the time of the check, for display without a second lookup. */
        std::string entityName;

        /** @brief The component type at fault, or empty when the issue is not component-specific. */
        std::string componentTypeId;

        /** @brief One sentence, addressed to the user, saying what is wrong. */
        std::string message;
    };

    /** @brief Returns the display name of @p severity. */
    const char* toString(SceneIssue::Severity severity);

    /**
     * @brief Returns every structural problem in @p scene, in document order.
     *
     * Scene-wide issues come first, then per-entity issues in the document's own entity order, so
     * the report reads in the same order as the hierarchy panel.
     *
     * @param registry Supplies each component's schema. An unregistered component type is reported
     *        rather than skipped: a scene whose plugin failed to load is exactly the scene most
     *        likely to be broken, and silence there would be the wrong answer.
     */
    [[nodiscard]] std::vector<SceneIssue> validateScene(const SceneDocument& scene,
                                                        const ComponentRegistry& registry);

    /**
     * @brief Reports per-part material entries naming a part their model does not have (ED-410).
     *
     * Separate from `validateScene` because it needs the *geometry*, and the structural rules
     * deliberately need nothing but the document -- the same split that keeps `MissingReferences`
     * out of that function.
     *
     * **This rule is the reason the list is keyed by part name rather than by index.** An
     * index-keyed override that shifted after a reimport still points at a part, just the wrong
     * one, and there is nothing anywhere for a rule to notice. A name that matches nothing is
     * detectable, and this is what detects it.
     *
     * Silent about a model whose mesh is not available: "not imported yet" is not "wrong", and a
     * rule that fired while an asset scan was still running would report every model in the
     * project.
     */
    [[nodiscard]] std::vector<SceneIssue> validateModelPartMaterials(const SceneDocument& scene,
                                                                     const MeshProvider& meshes);

    /** @brief Returns how many of @p issues have the given severity. */
    [[nodiscard]] std::size_t countIssues(const std::vector<SceneIssue>& issues,
                                          SceneIssue::Severity severity);
}
