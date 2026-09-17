// SPDX-License-Identifier: MS-PL
/**
 * @file StudioHostCapabilityTests.cpp
 * @brief The Studio host capability contract, evaluated against synthetic devices.
 *
 * The point of stating the contract as data in a CNA-free module is that every branch of it can be
 * reached here — including the ones only a renderer nobody owns could produce. A contract that
 * could only be tested on hardware would be tested on one machine, once.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Project/RendererCatalog.hpp"
#include "CNA/Studio/Project/StudioHostRequirements.hpp"
#include "CNA/Studio/Project/TargetProfile.hpp"

#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    /** @brief A device that answers `Supported` to everything Studio requires. */
    StudioCapabilitySnapshot capableDevice()
    {
        StudioCapabilitySnapshot snapshot;
        snapshot.setRendererName("SYNTHETIC");
        snapshot.setPlatformName("SDL3");
        snapshot.setModernApiAvailable(true);

        for (const StudioHostFeatureRequirement& requirement : studioHostFeatureRequirements())
        {
            snapshot.setFeature(requirement.feature, StudioCapabilityAnswer::Supported);
        }
        for (const StudioHostLimitRequirement& requirement : studioHostLimitRequirements())
        {
            snapshot.setLimit(requirement.limit, requirement.minimum);
        }
        return snapshot;
    }

    /** @brief The first required feature in the contract. */
    std::string firstRequiredFeature()
    {
        for (const StudioHostFeatureRequirement& requirement : studioHostFeatureRequirements())
        {
            if (requirement.severity == StudioRequirementSeverity::Required)
            {
                return requirement.feature;
            }
        }
        return {};
    }

    /** @brief Finds one requirement's outcome in an evaluation. */
    const StudioRequirementOutcome* outcomeFor(const StudioHostEvaluation& evaluation,
                                               std::string_view subject)
    {
        for (const StudioRequirementOutcome& outcome : evaluation.outcomes)
        {
            if (outcome.subject == subject) { return &outcome; }
        }
        return nullptr;
    }
}

// ------------------------------------------------------------------------------------------------
// The contract itself (STUDIO-02020)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(TheContractNamesCapabilitiesAndNeverRenderers)
{
    // The whole reason the contract is capability-driven: adding a renderer to CNA must not
    // require editing Studio, and a renderer that gains a capability must become eligible without
    // anyone noticing it had.
    for (const StudioHostFeatureRequirement& requirement : studioHostFeatureRequirements())
    {
        CNA_STUDIO_EXPECT(!requirement.feature.empty());
        CNA_STUDIO_EXPECT(!requirement.reason.empty());

        for (const char* rendererish : {"OPENGL", "VULKAN", "DIRECTX", "METAL", "SOFTWARE",
                                        "WEBGPU", "GLIDE"})
        {
            CNA_STUDIO_EXPECT(requirement.feature.find(rendererish) == std::string::npos);
        }
    }
    for (const StudioHostLimitRequirement& requirement : studioHostLimitRequirements())
    {
        CNA_STUDIO_EXPECT(!requirement.limit.empty());
        CNA_STUDIO_EXPECT(!requirement.reason.empty());
        CNA_STUDIO_EXPECT(requirement.minimum > 0);
    }
}

CNA_STUDIO_TEST(EveryRequirementSaysWhyStudioWantsIt)
{
    // A diagnostic that names a capability and not a reason tells a user what to search for and
    // nothing about whether they care.
    const StudioHostEvaluation evaluation = evaluateStudioHost(capableDevice());
    for (const StudioRequirementOutcome& outcome : evaluation.outcomes)
    {
        CNA_STUDIO_EXPECT(outcome.reason.size() > 20);
    }
}

CNA_STUDIO_TEST(TheContractIsSmallEnoughToBeHonest)
{
    // Not a style rule. A contract padded with everything Studio might one day want refuses to
    // start on renderers it works perfectly well on, and the pressure that creates is to ignore
    // the contract rather than to fix it.
    std::size_t required = 0;
    for (const StudioHostFeatureRequirement& requirement : studioHostFeatureRequirements())
    {
        if (requirement.severity == StudioRequirementSeverity::Required) { ++required; }
    }
    for (const StudioHostLimitRequirement& requirement : studioHostLimitRequirements())
    {
        if (requirement.severity == StudioRequirementSeverity::Required) { ++required; }
    }
    CNA_STUDIO_EXPECT(required > 0);
    CNA_STUDIO_EXPECT(required <= 6);
}

// ------------------------------------------------------------------------------------------------
// Evaluation (STUDIO-02021)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(ADeviceThatSupportsEverythingCanHostStudio)
{
    const StudioHostEvaluation evaluation = evaluateStudioHost(capableDevice());

    CNA_STUDIO_EXPECT(evaluation.canHostStudio);
    CNA_STUDIO_EXPECT(evaluation.unmetRequired().empty());
    CNA_STUDIO_EXPECT(evaluation.diagnostic().empty());
    CNA_STUDIO_EXPECT_EQ(evaluation.rendererName, std::string{"SYNTHETIC"});
    CNA_STUDIO_EXPECT_EQ(evaluation.platformName, std::string{"SDL3"});
    CNA_STUDIO_EXPECT(evaluation.modernApiAvailable);
}

CNA_STUDIO_TEST(ADeviceMissingOneRequiredCapabilityCannotHostStudio)
{
    StudioCapabilitySnapshot snapshot = capableDevice();
    const std::string feature = firstRequiredFeature();
    snapshot.setFeature(feature, StudioCapabilityAnswer::Unsupported, "no 3D path on this renderer");

    const StudioHostEvaluation evaluation = evaluateStudioHost(snapshot);
    const std::vector<StudioRequirementOutcome> unmet = evaluation.unmetRequired();

    CNA_STUDIO_EXPECT(!evaluation.canHostStudio);
    CNA_STUDIO_EXPECT_EQ(unmet.size(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(unmet.front().subject, feature);
    CNA_STUDIO_EXPECT(unmet.front().status == StudioRequirementStatus::Unsupported);

    // The renderer's own words survive into the diagnostic: they are usually the most specific
    // thing anybody will ever be told about the failure.
    const std::string diagnostic = evaluation.diagnostic();
    CNA_STUDIO_EXPECT(diagnostic.find(feature) != std::string::npos);
    CNA_STUDIO_EXPECT(diagnostic.find("no 3D path on this renderer") != std::string::npos);
}

CNA_STUDIO_TEST(AnUnclassifiedRequiredCapabilityCountsAsUnmetAndSaysSo)
{
    // `Unknown` means the renderer has not classified it, which is not `no` -- and is still not
    // `yes`. A tool that starts and then cannot draw is worse than one that refuses with a reason.
    StudioCapabilitySnapshot snapshot = capableDevice();
    const std::string feature = firstRequiredFeature();
    snapshot.setFeature(feature, StudioCapabilityAnswer::Unknown);

    const StudioHostEvaluation evaluation = evaluateStudioHost(snapshot);
    const std::vector<StudioRequirementOutcome> unmet = evaluation.unmetRequired();

    CNA_STUDIO_EXPECT(!evaluation.canHostStudio);
    CNA_STUDIO_EXPECT(unmet.front().status == StudioRequirementStatus::Unclassified);

    const std::string diagnostic = evaluation.diagnostic();
    CNA_STUDIO_EXPECT(diagnostic.find("unclassified") != std::string::npos);
    CNA_STUDIO_EXPECT(diagnostic.find("has not classified") != std::string::npos);
    CNA_STUDIO_EXPECT(diagnostic.find("unsupported") == std::string::npos);
}

CNA_STUDIO_TEST(ACapabilityTheRendererNeverMentionedIsUnclassifiedRatherThanAssumed)
{
    // The default snapshot says nothing at all, which is exactly what a renderer that has audited
    // nothing reports. Assuming the best here is how a tool ships that opens a blank window.
    StudioCapabilitySnapshot empty;
    // The modern API is a fact about this build rather than about the renderer, so it is never
    // unclassified -- see evaluateStudioHost. Granting it here keeps this test about the renderer's
    // silence, which is what it is for.
    empty.setModernApiAvailable(true);
    const StudioHostEvaluation evaluation = evaluateStudioHost(empty);

    CNA_STUDIO_EXPECT(!evaluation.canHostStudio);
    for (const StudioRequirementOutcome& outcome : evaluation.unmetRequired())
    {
        CNA_STUDIO_EXPECT(outcome.status == StudioRequirementStatus::Unclassified);
    }
}

CNA_STUDIO_TEST(AMissingRecommendedCapabilityDoesNotStopStudioStarting)
{
    StudioCapabilitySnapshot snapshot = capableDevice();
    std::string recommended;
    for (const StudioHostFeatureRequirement& requirement : studioHostFeatureRequirements())
    {
        if (requirement.severity == StudioRequirementSeverity::Recommended)
        {
            recommended = requirement.feature;
            break;
        }
    }
    CNA_STUDIO_EXPECT(!recommended.empty());
    snapshot.setFeature(recommended, StudioCapabilityAnswer::Unsupported);

    const StudioHostEvaluation evaluation = evaluateStudioHost(snapshot);
    CNA_STUDIO_EXPECT(evaluation.canHostStudio);
    CNA_STUDIO_EXPECT(evaluation.diagnostic().empty());
    CNA_STUDIO_EXPECT_EQ(evaluation.unmetRecommended().size(), std::size_t{1});
    CNA_STUDIO_EXPECT(evaluation.report().find(recommended) != std::string::npos);
}

CNA_STUDIO_TEST(RestrictedIsEnoughOnlyWhereTheRequirementSaysItIs)
{
    for (const StudioHostFeatureRequirement& requirement : studioHostFeatureRequirements())
    {
        if (requirement.severity != StudioRequirementSeverity::Required) { continue; }

        StudioCapabilitySnapshot snapshot = capableDevice();
        snapshot.setFeature(requirement.feature, StudioCapabilityAnswer::Restricted,
                            "a documented subset");

        const StudioHostEvaluation evaluation = evaluateStudioHost(snapshot);
        const StudioRequirementOutcome* outcome = outcomeFor(evaluation, requirement.feature);
        CNA_STUDIO_EXPECT(outcome != nullptr);

        if (requirement.restrictedIsEnough)
        {
            CNA_STUDIO_EXPECT(outcome->status == StudioRequirementStatus::SatisfiedWithRestriction);
            CNA_STUDIO_EXPECT(evaluation.canHostStudio);
        }
        else
        {
            CNA_STUDIO_EXPECT(outcome->status == StudioRequirementStatus::Restricted);
            CNA_STUDIO_EXPECT(!evaluation.canHostStudio);
        }
    }
}

CNA_STUDIO_TEST(ALimitBelowTheMinimumIsReportedWithBothNumbers)
{
    StudioCapabilitySnapshot snapshot = capableDevice();
    const StudioHostLimitRequirement& requirement = studioHostLimitRequirements().front();
    snapshot.setLimit(requirement.limit, requirement.minimum / 2);

    const StudioHostEvaluation evaluation = evaluateStudioHost(snapshot);
    CNA_STUDIO_EXPECT(!evaluation.canHostStudio);

    const StudioRequirementOutcome* outcome = outcomeFor(evaluation, requirement.limit);
    CNA_STUDIO_EXPECT(outcome != nullptr);
    CNA_STUDIO_EXPECT(outcome->status == StudioRequirementStatus::BelowMinimum);
    CNA_STUDIO_EXPECT(outcome->detail.find(std::to_string(requirement.minimum / 2))
                      != std::string::npos);
    CNA_STUDIO_EXPECT(outcome->detail.find(std::to_string(requirement.minimum))
                      != std::string::npos);
}

CNA_STUDIO_TEST(AnUnreportedLimitIsUnclassifiedRatherThanZero)
{
    // Zero is a value. A requirement comparing against it would read "reports 0" for a renderer
    // that in fact reported nothing at all, which sends the reader looking for hardware that does
    // not exist.
    StudioCapabilitySnapshot snapshot = capableDevice();
    StudioCapabilitySnapshot without;
    without.setRendererName(std::string{snapshot.rendererName()});
    for (const StudioHostFeatureRequirement& requirement : studioHostFeatureRequirements())
    {
        without.setFeature(requirement.feature, StudioCapabilityAnswer::Supported);
    }

    const StudioHostEvaluation evaluation = evaluateStudioHost(without);
    const StudioRequirementOutcome* outcome =
        outcomeFor(evaluation, studioHostLimitRequirements().front().limit);

    CNA_STUDIO_EXPECT(outcome != nullptr);
    CNA_STUDIO_EXPECT(outcome->status == StudioRequirementStatus::Unclassified);
    CNA_STUDIO_EXPECT(outcome->detail.empty());
}

// ------------------------------------------------------------------------------------------------
// The diagnostic and the report (STUDIO-02021, STUDIO-02022)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(TheStartUpReportAnswersEveryQuestionTheArchitectureSaysItMust)
{
    const StudioHostEvaluation evaluation = evaluateStudioHost(capableDevice());
    const std::string report = evaluation.report();

    CNA_STUDIO_EXPECT(report.find("SDL3") != std::string::npos);       // the platform
    CNA_STUDIO_EXPECT(report.find("SYNTHETIC") != std::string::npos);  // the renderer
    CNA_STUDIO_EXPECT(report.find("Modern API") != std::string::npos); // CNAEXT availability

    for (const StudioHostFeatureRequirement& requirement : studioHostFeatureRequirements())
    {
        CNA_STUDIO_EXPECT(report.find(requirement.feature) != std::string::npos);
    }
    CNA_STUDIO_EXPECT(report.find("[required]") != std::string::npos);
    CNA_STUDIO_EXPECT(report.find("[recommended]") != std::string::npos);
}

CNA_STUDIO_TEST(TheDiagnosticNamesEveryUnmetRequirementAndTheRenderer)
{
    StudioCapabilitySnapshot snapshot;
    snapshot.setRendererName("ASCII-ART");
    snapshot.setPlatformName("TERMINAL");

    const StudioHostEvaluation evaluation = evaluateStudioHost(snapshot);
    const std::string diagnostic = evaluation.diagnostic();

    CNA_STUDIO_EXPECT(!diagnostic.empty());
    CNA_STUDIO_EXPECT(diagnostic.find("ASCII-ART") != std::string::npos);
    CNA_STUDIO_EXPECT(diagnostic.find("TERMINAL") != std::string::npos);

    for (const StudioRequirementOutcome& outcome : evaluation.unmetRequired())
    {
        CNA_STUDIO_EXPECT(diagnostic.find(outcome.subject) != std::string::npos);
        CNA_STUDIO_EXPECT(diagnostic.find(outcome.reason) != std::string::npos);
    }

    // A recommended capability the device also lacks must not appear: the message exists to say
    // why Studio will not start, and padding it with things that are not the reason buries the
    // things that are.
    for (const StudioRequirementOutcome& outcome : evaluation.unmetRecommended())
    {
        CNA_STUDIO_EXPECT(diagnostic.find(outcome.reason) == std::string::npos);
    }
}

CNA_STUDIO_TEST(AnEvaluationIsDeterministicAndOrderedAsDeclared)
{
    const StudioCapabilitySnapshot snapshot = capableDevice();
    const StudioHostEvaluation first = evaluateStudioHost(snapshot);
    const StudioHostEvaluation second = evaluateStudioHost(snapshot);

    CNA_STUDIO_EXPECT_EQ(first.outcomes.size(), second.outcomes.size());
    CNA_STUDIO_EXPECT_EQ(first.report(), second.report());

    // One outcome ahead of the feature list in the modern profile: the modern API itself, which is
    // reported first because it decides whether the rest can be attempted at all.
    CNA_STUDIO_EXPECT_EQ(first.outcomes.size(),
                         1 + studioHostFeatureRequirements().size()
                             + studioHostLimitRequirements().size());
    CNA_STUDIO_EXPECT_EQ(first.outcomes.front().subject, std::string{kStudioModernApiSubject});
    for (std::size_t i = 0; i < studioHostFeatureRequirements().size(); ++i)
    {
        CNA_STUDIO_EXPECT_EQ(first.outcomes[i + 1].subject,
                             studioHostFeatureRequirements()[i].feature);
    }
}

CNA_STUDIO_TEST(EveryAnswerAndStatusHasAName)
{
    for (const StudioCapabilityAnswer answer : {StudioCapabilityAnswer::Unknown,
                                                StudioCapabilityAnswer::Unsupported,
                                                StudioCapabilityAnswer::Supported,
                                                StudioCapabilityAnswer::Restricted})
    {
        CNA_STUDIO_EXPECT(!studioCapabilityAnswerName(answer).empty());
    }
    for (const StudioRequirementStatus status : {StudioRequirementStatus::Satisfied,
                                                 StudioRequirementStatus::SatisfiedWithRestriction,
                                                 StudioRequirementStatus::Restricted,
                                                 StudioRequirementStatus::Unsupported,
                                                 StudioRequirementStatus::Unclassified,
                                                 StudioRequirementStatus::BelowMinimum})
    {
        CNA_STUDIO_EXPECT(!studioRequirementStatusName(status).empty());
    }
}

// ------------------------------------------------------------------------------------------------
// The modern API is an enforced requirement, not a field on the report (STUDIO-02070)
//
// Four cases, written as four tests rather than as one parameterised one, because each names a
// different failure and a shared body would report all four as the same line number. They are the
// contract's negative space: what the evaluation must *refuse*, which is the half that was missing.
// ------------------------------------------------------------------------------------------------

namespace
{
    /**
     * @brief A renderer with the complete classic XNA capability set and no shaders at all.
     *
     * Not hypothetical. This is CNA's `SOFTWARE` renderer, which reports `ShaderEffects` and
     * `ShaderEffectSourceExecution` unsupported and everything the classic UI path needs supported.
     */
    StudioCapabilitySnapshot classicOnlyDevice()
    {
        StudioCapabilitySnapshot snapshot;
        snapshot.setRendererName("CLASSIC-ONLY");
        snapshot.setPlatformName("SDL3");
        snapshot.setModernApiAvailable(false);

        snapshot.setFeature("ThreeDimensionalPipeline", StudioCapabilityAnswer::Supported);
        snapshot.setFeature("DepthStencilBuffer", StudioCapabilityAnswer::Supported);
        snapshot.setFeature("MultiSampleAntiAliasing", StudioCapabilityAnswer::Supported);
        snapshot.setFeature("AnisotropicFiltering", StudioCapabilityAnswer::Supported);
        snapshot.setFeature("WireFrameRasterization", StudioCapabilityAnswer::Supported);
        snapshot.setFeature("ShaderEffects", StudioCapabilityAnswer::Unsupported);
        snapshot.setFeature("ShaderEffectSourceExecution", StudioCapabilityAnswer::Unsupported);
        snapshot.setLimit("MaxTextureDimension", 16384);
        return snapshot;
    }
}

CNA_STUDIO_TEST(CaseAAClassicOnlyRendererCannotHostStudioAndTheDiagnosticSaysWhy)
{
    // The defect this closes: the evaluation carried modernApiAvailable into its report and
    // consulted it nowhere, so this device passed on ThreeDimensionalPipeline, DepthStencilBuffer
    // and a texture limit -- the exact capability set a renderer that cannot execute a shader has.
    const StudioHostEvaluation evaluation = evaluateStudioHost(classicOnlyDevice());

    CNA_STUDIO_EXPECT(!evaluation.canHostStudio);

    const std::string diagnostic = evaluation.diagnostic();
    CNA_STUDIO_EXPECT(!diagnostic.empty());
    // Named, not merely implied by a count of unmet requirements. A user reading "3 requirements
    // unmet" has to go and find out which; a user reading the name can search for it.
    CNA_STUDIO_EXPECT(diagnostic.find(std::string{kStudioModernApiSubject}) != std::string::npos);
    CNA_STUDIO_EXPECT(diagnostic.find("CNAEXT") != std::string::npos);

    const StudioRequirementOutcome* modern = outcomeFor(evaluation, kStudioModernApiSubject);
    CNA_STUDIO_EXPECT(modern != nullptr);
    CNA_STUDIO_EXPECT(modern->severity == StudioRequirementSeverity::Required);
    CNA_STUDIO_EXPECT(modern->status == StudioRequirementStatus::Unsupported);
}

CNA_STUDIO_TEST(CaseAAClassicOnlyRendererWithTheEngineLayerStillFailsOnTheShaders)
{
    // Half of Case A on its own: a Studio built *with* CNAEXT, on a renderer that cannot execute a
    // shader. The engine layer being compiled in is necessary and is not sufficient -- a type
    // Studio can name is not a shader the device will run.
    StudioCapabilitySnapshot snapshot = classicOnlyDevice();
    snapshot.setModernApiAvailable(true);
    const StudioHostEvaluation evaluation = evaluateStudioHost(snapshot);

    CNA_STUDIO_EXPECT(!evaluation.canHostStudio);
    CNA_STUDIO_EXPECT(outcomeFor(evaluation, kStudioModernApiSubject)->isMet());

    const std::string diagnostic = evaluation.diagnostic();
    CNA_STUDIO_EXPECT(diagnostic.find("ShaderEffects") != std::string::npos);
    CNA_STUDIO_EXPECT(diagnostic.find("ShaderEffectSourceExecution") != std::string::npos);
}

CNA_STUDIO_TEST(CaseBAModernHostMissingOneRequiredCapabilityFailsWithItNamed)
{
    for (const StudioHostFeatureRequirement& requirement : studioHostFeatureRequirements())
    {
        if (requirement.severity != StudioRequirementSeverity::Required) { continue; }

        StudioCapabilitySnapshot snapshot = capableDevice();
        snapshot.setFeature(requirement.feature, StudioCapabilityAnswer::Unsupported);
        const StudioHostEvaluation evaluation = evaluateStudioHost(snapshot);

        CNA_STUDIO_EXPECT(!evaluation.canHostStudio);
        CNA_STUDIO_EXPECT_EQ(evaluation.unmetRequired().size(), std::size_t{1});
        CNA_STUDIO_EXPECT_EQ(evaluation.unmetRequired().front().subject, requirement.feature);
        CNA_STUDIO_EXPECT(evaluation.diagnostic().find(requirement.feature) != std::string::npos);
    }
}

CNA_STUDIO_TEST(CaseCTheFullModernProfilePasses)
{
    const StudioHostEvaluation evaluation = evaluateStudioHost(capableDevice());

    CNA_STUDIO_EXPECT(evaluation.canHostStudio);
    CNA_STUDIO_EXPECT(evaluation.modernApiAvailable);
    CNA_STUDIO_EXPECT(evaluation.profile == StudioHostProfile::Modern);
    CNA_STUDIO_EXPECT(evaluation.diagnostic().empty());
    CNA_STUDIO_EXPECT(evaluation.unmetRequired().empty());
}

CNA_STUDIO_TEST(CaseDARendererThatCannotHostStudioIsStillAValidGameTarget)
{
    // ARCHITECTURE.md §3, and the single easiest way to get this product's architecture wrong. The
    // classic-only device above is CNA's SOFTWARE renderer, which cannot host the modern Studio UI
    // and ships a perfectly good XNA-compatible game.
    CNA_STUDIO_EXPECT(!evaluateStudioHost(classicOnlyDevice()).canHostStudio);

    StudioTargetProfile profile;
    profile.renderer = "software";
    profile.platform = "sdl3";
    profile.os = StudioTargetOs::Linux;
    profile.architecture = StudioArchitecture::X86_64;
    profile.configuration = StudioBuildConfiguration::Release;

    const StudioProfileValidation validation = validateStudioTargetProfile(profile);
    CNA_STUDIO_EXPECT(validation.isBuildable());

    for (const StudioProfileProblem& problem : validation.problems)
    {
        // Not "no errors", which a profile could satisfy by failing silently: no problem on any
        // axis may cite hosting Studio as a reason a *game* cannot ship on this renderer.
        CNA_STUDIO_EXPECT(problem.message.find("host") == std::string::npos);
        CNA_STUDIO_EXPECT(problem.message.find("Studio UI") == std::string::npos);
    }

    // And the static catalogue agrees with the runtime evaluation about this renderer, which it did
    // not before STUDIO-02070: the catalogue has called SOFTWARE preview-only since it was written,
    // while the runtime contract said it could host Studio.
    const RendererInfo* software = findRenderer("software");
    CNA_STUDIO_EXPECT(software != nullptr);
    CNA_STUDIO_EXPECT(software->hostSupport != RendererHostSupport::StudioHost);
}

// ------------------------------------------------------------------------------------------------
// Choosing a backend from the verdict (STUDIO-02072, STUDIO-02074)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(AHostMeetingTheModernProfileGetsTheModernRenderer)
{
    const StudioCapabilitySnapshot snapshot = capableDevice();
    const StudioUiBackendDecision decision = resolveStudioUiBackend(evaluateStudioHost(snapshot));

    CNA_STUDIO_EXPECT(decision.choice == StudioUiBackendChoice::Modern);
    CNA_STUDIO_EXPECT(!decision.reason.empty());
}

CNA_STUDIO_TEST(AClassicOnlyHostGetsNothingAndTheReasonNamesWhatIsMissing)
{
    // STUDIO-02074 retired the classic UI renderer this used to fall back to: a host that cannot
    // meet the modern profile refuses to start rather than degrading to a second UI GPU stack.
    const StudioCapabilitySnapshot snapshot = classicOnlyDevice();
    const StudioUiBackendDecision decision = resolveStudioUiBackend(evaluateStudioHost(snapshot));

    CNA_STUDIO_EXPECT(decision.choice == StudioUiBackendChoice::None);
    // The whole point of naming what is missing: a bug report that says "Studio will not start"
    // is answered by this sentence and by nothing else in the product.
    CNA_STUDIO_EXPECT(decision.reason.find(std::string{kStudioModernApiSubject}) != std::string::npos);
    CNA_STUDIO_EXPECT(decision.reason.find("ShaderEffects") != std::string::npos);
}

CNA_STUDIO_TEST(EveryProfileAndBackendChoiceHasAName)
{
    CNA_STUDIO_EXPECT(!studioHostProfileName(StudioHostProfile::Modern).empty());
    CNA_STUDIO_EXPECT(!studioHostFeatureRequirements().empty());
    CNA_STUDIO_EXPECT(!studioHostLimitRequirements().empty());

    for (const StudioUiBackendChoice choice : {StudioUiBackendChoice::None,
                                               StudioUiBackendChoice::Modern})
    {
        CNA_STUDIO_EXPECT(!studioUiBackendChoiceName(choice).empty());
    }
}
