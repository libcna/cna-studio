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

#include "CNA/Studio/Project/StudioHostRequirements.hpp"

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
    const StudioCapabilitySnapshot empty;
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

    CNA_STUDIO_EXPECT_EQ(first.outcomes.size(),
                         studioHostFeatureRequirements().size()
                             + studioHostLimitRequirements().size());
    for (std::size_t i = 0; i < studioHostFeatureRequirements().size(); ++i)
    {
        CNA_STUDIO_EXPECT_EQ(first.outcomes[i].subject,
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
