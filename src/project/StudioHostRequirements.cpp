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

    const std::vector<StudioHostFeatureRequirement>& studioHostFeatureRequirements()
    {
        // Introduced as the UI needs them, never speculatively. A contract padded with everything
        // Studio might one day want would refuse to start on renderers it works perfectly well on,
        // and the pressure that creates is to ignore the contract rather than to fix it.
        static const std::vector<StudioHostFeatureRequirement> requirements = {
            {"ThreeDimensionalPipeline",
             "The Studio UI is drawn as indexed, vertex-coloured triangles through the 3D pipeline, "
             "and the scene viewport needs it outright.",
             StudioRequirementSeverity::Required, /*restrictedIsEnough=*/false},

            {"DepthStencilBuffer",
             "The scene viewport draws depth-sorted geometry; without a depth attachment it shows "
             "the last triangle submitted rather than the nearest one.",
             StudioRequirementSeverity::Required, /*restrictedIsEnough=*/true},

            {"ShaderEffects",
             "Material and shader-graph authoring compile the effects they preview. Without them "
             "Studio runs and those panels report that this host cannot preview.",
             StudioRequirementSeverity::Recommended, /*restrictedIsEnough=*/true},

            {"ShaderEffectSourceExecution",
             "A previewed shader must actually determine the pixels; a host that accepts the source "
             "and ignores it would show every material identically.",
             StudioRequirementSeverity::Recommended, /*restrictedIsEnough=*/true},

            {"MultiSampleAntiAliasing",
             "Viewport edge quality. Studio is usable without it; gizmo and wireframe edges alias.",
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
        return requirements;
    }

    const std::vector<StudioHostLimitRequirement>& studioHostLimitRequirements()
    {
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
        text += "  Platform: " + (platformName.empty() ? std::string{"unknown"} : platformName) + "\n\n";
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
        text += std::string{"  Can host:   "} + (canHostStudio ? "yes" : "no") + "\n\n";

        for (const StudioRequirementOutcome& outcome : outcomes) { appendOutcome(text, outcome); }
        return text;
    }

    StudioHostEvaluation evaluateStudioHost(const StudioCapabilitySnapshot& snapshot)
    {
        StudioHostEvaluation evaluation;
        evaluation.rendererName = std::string{snapshot.rendererName()};
        evaluation.platformName = std::string{snapshot.platformName()};
        evaluation.modernApiAvailable = snapshot.isModernApiAvailable();

        for (const StudioHostFeatureRequirement& requirement : studioHostFeatureRequirements())
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

        for (const StudioHostLimitRequirement& requirement : studioHostLimitRequirements())
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
} // namespace CNA::Studio
