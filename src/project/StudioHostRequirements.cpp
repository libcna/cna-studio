// SPDX-License-Identifier: MS-PL
/**
 * @file StudioHostRequirements.cpp
 * @brief The Studio host capability contract, and its evaluation against a device.
 */

#include "CNA/Studio/Project/StudioHostRequirements.hpp"

#include <algorithm>

namespace CNA::Studio
{
    std::string_view studioCapabilityAnswerName(StudioCapabilityAnswer answer)
    {
        switch (answer)
        {
            case StudioCapabilityAnswer::Unknown:     return "unclassified";
            case StudioCapabilityAnswer::Unsupported: return "unsupported";
            case StudioCapabilityAnswer::Supported:   return "supported";
            case StudioCapabilityAnswer::Restricted:  return "restricted";
        }
        return "";
    }

    std::string_view studioHostProfileName(StudioHostProfile profile)
    {
        switch (profile)
        {
            case StudioHostProfile::Modern: return "modern";
        }
        return "";
    }

    std::string_view studioUiBackendChoiceName(StudioUiBackendChoice choice)
    {
        switch (choice)
        {
            case StudioUiBackendChoice::None:   return "none";
            case StudioUiBackendChoice::Modern: return "modern";
        }
        return "";
    }

    std::string_view studioHostModernApiReason()
    {
        return "The Studio UI is drawn through CNA's modern graphics API -- ShaderEffect over "
               "vertex and index buffers -- and every authoring panel that previews a material, a "
               "shader or a post-process compiles one. A build without the CNAEXT engine layer has "
               "no such API to call.";
    }

    std::string_view studioRequirementStatusName(StudioRequirementStatus status)
    {
        switch (status)
        {
            case StudioRequirementStatus::Satisfied:                return "satisfied";
            case StudioRequirementStatus::SatisfiedWithRestriction: return "satisfied (restricted)";
            case StudioRequirementStatus::Restricted:               return "restricted";
            case StudioRequirementStatus::Unsupported:              return "unsupported";
            case StudioRequirementStatus::Unclassified:             return "unclassified";
            case StudioRequirementStatus::BelowMinimum:             return "below minimum";
        }
        return "";
    }

    void StudioCapabilitySnapshot::setFeature(std::string feature, StudioCapabilityAnswer answer,
                                              std::string note)
    {
        const auto existing = std::find_if(features_.begin(), features_.end(),
            [&](const FeatureEntry& entry) { return entry.name == feature; });
        if (existing != features_.end())
        {
            existing->answer = answer;
            existing->note = std::move(note);
            return;
        }
        features_.push_back(FeatureEntry{std::move(feature), answer, std::move(note)});
    }

    StudioCapabilityAnswer StudioCapabilitySnapshot::feature(std::string_view feature) const
    {
        const auto found = std::find_if(features_.begin(), features_.end(),
            [&](const FeatureEntry& entry) { return entry.name == feature; });
        // A capability the renderer never mentioned is unclassified, which is the same answer CNA
        // gives for one it has not audited. There is no third state for "Studio asked about
        // something that does not exist", deliberately: a typo then fails the evaluation loudly at
        // start-up rather than passing quietly forever.
        return found == features_.end() ? StudioCapabilityAnswer::Unknown : found->answer;
    }

    std::string_view StudioCapabilitySnapshot::featureNote(std::string_view feature) const
    {
        const auto found = std::find_if(features_.begin(), features_.end(),
            [&](const FeatureEntry& entry) { return entry.name == feature; });
        return found == features_.end() ? std::string_view{} : std::string_view{found->note};
    }

    void StudioCapabilitySnapshot::setLimit(std::string limit, std::uint64_t value)
    {
        const auto existing = std::find_if(limits_.begin(), limits_.end(),
            [&](const auto& entry) { return entry.first == limit; });
        if (existing != limits_.end()) { existing->second = value; return; }
        limits_.emplace_back(std::move(limit), value);
    }

    std::optional<std::uint64_t> StudioCapabilitySnapshot::limit(std::string_view limit) const
    {
        const auto found = std::find_if(limits_.begin(), limits_.end(),
            [&](const auto& entry) { return entry.first == limit; });
        if (found == limits_.end()) { return std::nullopt; }
        return found->second;
    }

    namespace
    {
        /** @brief Builds the feature list. Free-standing rather than a literal in the accessor
         *  below purely so the static it initialises is built once, not re-evaluated at every
         *  call the way a function-local literal would read as inviting. */
        std::vector<StudioHostFeatureRequirement> buildFeatureRequirements()
        {
            // Introduced as the UI needs them, never speculatively. A contract padded with
            // everything Studio might one day want would refuse to start on renderers it works
            // perfectly well on, and the pressure that creates is to ignore the contract rather
            // than to fix it.
            return {
                {"ThreeDimensionalPipeline",
                 "The Studio UI is drawn as indexed, vertex-coloured triangles through the 3D "
                 "pipeline, and the scene viewport needs it outright.",
                 StudioRequirementSeverity::Required, /*restrictedIsEnough=*/false},

                {"DepthStencilBuffer",
                 "The scene viewport draws depth-sorted geometry; without a depth attachment it "
                 "shows the last triangle submitted rather than the nearest one.",
                 StudioRequirementSeverity::Required, /*restrictedIsEnough=*/true},

                {"ShaderEffects",
                 "The modern UI renderer draws through a ShaderEffect, and material and "
                 "shader-graph authoring compile the effects they preview.",
                 StudioRequirementSeverity::Required, /*restrictedIsEnough=*/true},

                {"ShaderEffectSourceExecution",
                 "A shader must actually determine the pixels; a host that accepts the source and "
                 "ignores it would draw the UI with whatever fixed path it fell back to, and would "
                 "show every material identically.",
                 StudioRequirementSeverity::Required, /*restrictedIsEnough=*/true},

                {"MultiSampleAntiAliasing",
                 "Viewport edge quality. Studio is usable without it; gizmo and wireframe edges "
                 "alias.",
                 StudioRequirementSeverity::Recommended, /*restrictedIsEnough=*/true},

                {"AnisotropicFiltering",
                 "Texture quality on surfaces seen at a grazing angle, which is most of a level.",
                 StudioRequirementSeverity::Recommended, /*restrictedIsEnough=*/true},

                {"WireFrameRasterization",
                 "The viewport's wireframe visualisation mode.",
                 StudioRequirementSeverity::Recommended, /*restrictedIsEnough=*/true},

                {"GpuTimers",
                 "The profiler's GPU timings. Without them it reports CPU time only.",
                 StudioRequirementSeverity::Recommended, /*restrictedIsEnough=*/true},
            };
        }
    } // namespace

    const std::vector<StudioHostFeatureRequirement>& studioHostFeatureRequirements(
        StudioHostProfile profile)
    {
        static_cast<void>(profile);
        static const std::vector<StudioHostFeatureRequirement> requirements =
            buildFeatureRequirements();
        return requirements;
    }

    const std::vector<StudioHostLimitRequirement>& studioHostLimitRequirements(
        StudioHostProfile profile)
    {
        // One list today. It takes the profile anyway, because the modern renderer will grow
        // limits the classic one has no opinion about -- uniform-block size, vertex input
        // bindings -- and a caller that had to start passing an argument at that point would be a
        // caller that forgot to in one of its four call sites.
        static_cast<void>(profile);
        static const std::vector<StudioHostLimitRequirement> requirements = {
            {"MaxTextureDimension", 2048,
             "The UI font atlas. 2048 is the smallest square that holds Studio's font set at 200% "
             "scale; below it, text cannot be drawn at all.",
             StudioRequirementSeverity::Required},
        };
        return requirements;
    }

    namespace
    {
        /** @brief Turns one capability answer into a requirement outcome. */
        StudioRequirementStatus statusOf(StudioCapabilityAnswer answer, bool restrictedIsEnough)
        {
            switch (answer)
            {
                case StudioCapabilityAnswer::Supported:
                    return StudioRequirementStatus::Satisfied;
                case StudioCapabilityAnswer::Restricted:
                    return restrictedIsEnough ? StudioRequirementStatus::SatisfiedWithRestriction
                                              : StudioRequirementStatus::Restricted;
                case StudioCapabilityAnswer::Unsupported:
                    return StudioRequirementStatus::Unsupported;
                case StudioCapabilityAnswer::Unknown:
                    break;
            }
            // Not a yes. Studio says which of the two it was rather than collapsing them, because
            // "this renderer cannot do it" and "nobody has checked whether this renderer can do it"
            // lead to completely different next steps.
            return StudioRequirementStatus::Unclassified;
        }

        /** @brief Appends one outcome line to a report. */
        void appendOutcome(std::string& text, const StudioRequirementOutcome& outcome)
        {
            text += "  ";
            text += outcome.severity == StudioRequirementSeverity::Required ? "[required]    "
                                                                           : "[recommended] ";
            text += outcome.subject;
            text += ": ";
            text += studioRequirementStatusName(outcome.status);
            if (!outcome.detail.empty()) { text += " -- " + outcome.detail; }
            text += "\n";
        }
    } // namespace

    std::vector<StudioRequirementOutcome> StudioHostEvaluation::unmetRequired() const
    {
        std::vector<StudioRequirementOutcome> result;
        for (const StudioRequirementOutcome& outcome : outcomes)
        {
            if (outcome.severity == StudioRequirementSeverity::Required && !outcome.isMet())
            {
                result.push_back(outcome);
            }
        }
        return result;
    }

    std::vector<StudioRequirementOutcome> StudioHostEvaluation::unmetRecommended() const
    {
        std::vector<StudioRequirementOutcome> result;
        for (const StudioRequirementOutcome& outcome : outcomes)
        {
            if (outcome.severity == StudioRequirementSeverity::Recommended && !outcome.isMet())
            {
                result.push_back(outcome);
            }
        }
        return result;
    }

    std::string StudioHostEvaluation::diagnostic() const
    {
        const std::vector<StudioRequirementOutcome> unmet = unmetRequired();
        if (unmet.empty()) { return {}; }

        std::string text = "CNA Studio cannot run on this build's graphics renderer.\n\n";
        text += "  Renderer: " + (rendererName.empty() ? std::string{"unknown"} : rendererName) + "\n";
        text += "  Platform: " + (platformName.empty() ? std::string{"unknown"} : platformName) + "\n";
        text += "  Profile:  " + std::string{studioHostProfileName(profile)} + "\n\n";
        text += "Unmet requirements:\n\n";

        for (const StudioRequirementOutcome& outcome : unmet)
        {
            text += "  " + outcome.subject + " -- "
                  + std::string{studioRequirementStatusName(outcome.status)} + "\n";
            text += "      " + outcome.reason + "\n";
            if (!outcome.detail.empty()) { text += "      " + outcome.detail + "\n"; }
            if (outcome.status == StudioRequirementStatus::Unclassified)
            {
                // Said explicitly, because it is the actionable case: the renderer may well be able
                // to do this and simply has not been audited, which is a CNA question rather than a
                // reason to buy hardware.
                text += "      This renderer has not classified this capability. It may be "
                        "available; nothing has measured it.\n";
            }
            text += "\n";
        }

        text += "Build CNA Studio against a renderer that satisfies these, or ask CNA to classify "
                "the capabilities marked unclassified.\n";
        return text;
    }

    std::string StudioHostEvaluation::report() const
    {
        std::string text = "CNA Studio host capability report\n\n";
        text += "  Platform:   " + (platformName.empty() ? std::string{"unknown"} : platformName) + "\n";
        text += "  Renderer:   " + (rendererName.empty() ? std::string{"unknown"} : rendererName) + "\n";
        text += std::string{"  Modern API: "} + (modernApiAvailable ? "available" : "unavailable") + "\n";
        text += std::string{"  Profile:    "} + std::string{studioHostProfileName(profile)} + "\n";
        text += std::string{"  Can host:   "} + (canHostStudio ? "yes" : "no") + "\n\n";

        for (const StudioRequirementOutcome& outcome : outcomes) { appendOutcome(text, outcome); }
        return text;
    }

    StudioHostEvaluation evaluateStudioHost(const StudioCapabilitySnapshot& snapshot,
                                            StudioHostProfile profile)
    {
        StudioHostEvaluation evaluation;
        evaluation.rendererName = std::string{snapshot.rendererName()};
        evaluation.platformName = std::string{snapshot.platformName()};
        evaluation.modernApiAvailable = snapshot.isModernApiAvailable();
        evaluation.profile = profile;

        // First. It is the headline of the whole contract and reporting it after eight renderer
        // capabilities would bury the one answer that decides whether the rest can even be
        // attempted -- a device cannot execute a ShaderEffect that this build has no type for.
        {
            StudioRequirementOutcome outcome;
            outcome.subject = std::string{kStudioModernApiSubject};
            outcome.reason = std::string{studioHostModernApiReason()};
            outcome.severity = StudioRequirementSeverity::Required;
            outcome.status = snapshot.isModernApiAvailable()
                                 ? StudioRequirementStatus::Satisfied
                                 : StudioRequirementStatus::Unsupported;
            // Never Unclassified: this is a fact about the build Studio is *in*, which is either
            // true or false and is never merely unaudited. Collapsing it into the renderer's
            // three-state vocabulary would invite a reader to go asking CNA to classify something
            // that is answered by a compiler flag.
            outcome.detail = snapshot.isModernApiAvailable()
                                 ? "the CNAEXT engine layer is compiled into this Studio"
                                 : "this Studio was built against a CNA without the CNAEXT engine "
                                   "layer (-DCNA_CNAEXT=OFF)";
            evaluation.outcomes.push_back(std::move(outcome));
        }

        for (const StudioHostFeatureRequirement& requirement : studioHostFeatureRequirements(profile))
        {
            StudioRequirementOutcome outcome;
            outcome.subject = requirement.feature;
            outcome.reason = requirement.reason;
            outcome.severity = requirement.severity;

            const StudioCapabilityAnswer answer = snapshot.feature(requirement.feature);
            outcome.status = statusOf(answer, requirement.restrictedIsEnough);
            outcome.detail = std::string{snapshot.featureNote(requirement.feature)};
            evaluation.outcomes.push_back(std::move(outcome));
        }

        for (const StudioHostLimitRequirement& requirement : studioHostLimitRequirements(profile))
        {
            StudioRequirementOutcome outcome;
            outcome.subject = requirement.limit;
            outcome.reason = requirement.reason;
            outcome.severity = requirement.severity;

            const std::optional<std::uint64_t> value = snapshot.limit(requirement.limit);
            if (!value.has_value())
            {
                outcome.status = StudioRequirementStatus::Unclassified;
            }
            else if (*value < requirement.minimum)
            {
                outcome.status = StudioRequirementStatus::BelowMinimum;
                outcome.detail = "reports " + std::to_string(*value) + ", Studio needs at least "
                               + std::to_string(requirement.minimum);
            }
            else
            {
                outcome.status = StudioRequirementStatus::Satisfied;
                outcome.detail = "reports " + std::to_string(*value);
            }
            evaluation.outcomes.push_back(std::move(outcome));
        }

        evaluation.canHostStudio = evaluation.unmetRequired().empty();
        return evaluation;
    }

    StudioUiBackendDecision resolveStudioUiBackend(const StudioHostEvaluation& modern)
    {
        StudioUiBackendDecision decision;

        if (modern.canHostStudio)
        {
            decision.choice = StudioUiBackendChoice::Modern;
            decision.reason = "This renderer meets the modern host profile.";
            return decision;
        }

        // Always named. "The modern renderer is unavailable" without the capability that made it
        // so sends a reader to the renderer's documentation rather than to the one line of it that
        // answers them.
        std::string missing;
        for (const StudioRequirementOutcome& outcome : modern.unmetRequired())
        {
            if (!missing.empty()) { missing += ", "; }
            missing += outcome.subject + " (" + std::string{studioRequirementStatusName(outcome.status)} + ")";
        }
        if (missing.empty()) { missing = "an unrecorded requirement"; }

        decision.choice = StudioUiBackendChoice::None;
        decision.reason = "This host does not meet the modern UI renderer's profile: " + missing
                        + ". STUDIO-02074 retired the classic UI renderer this used to fall back "
                          "to, so a host that cannot run the modern one no longer starts Studio at "
                          "all.";
        return decision;
    }
} // namespace CNA::Studio
