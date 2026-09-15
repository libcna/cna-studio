// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Project/StudioHostRequirements.hpp
 * @brief What a renderer must be able to do before it can host CNA Studio itself.
 *
 * `plan.md` STUDIO-02020, STUDIO-02021, STUDIO-02022. Architecture: `docs/ARCHITECTURE.md` §4.
 *
 * ### Capabilities, never renderer names
 *
 * The obvious implementation of "can this renderer host Studio" is a list of renderer names. It is
 * also the wrong one, and wrong in a way that gets worse over time: adding a renderer to CNA would
 * then require editing Studio, a renderer that *gains* a capability would stay ineligible until
 * somebody noticed, and the list would rot silently because nothing ever fails when it is stale.
 *
 * So the contract is a set of **capabilities**, stated once, as data, and evaluated against what
 * the live device reports. A renderer becomes eligible the moment it can do what Studio needs, and
 * no Studio source file changes. `STUDIO-02034`'s guard test rejects a renderer-name comparison
 * anywhere outside the catalogue, which is what keeps this from being quietly bypassed.
 *
 * ### Why this module knows CNA's vocabulary without linking CNA
 *
 * Requirements name CNA's `RendererFeature` and `RendererLimit` entries by their stable English
 * identifiers — the strings `CNA::GetRendererFeatureName` returns. That keeps the contract, and
 * every test over it, in a module with no CNA dependency and no GPU, while still being expressed
 * in exactly the terms the device answers in. The one place that touches a real
 * `RendererCapabilityProfile` is the adapter in `cna-studio-viewport`, which fills a
 * @ref StudioCapabilitySnapshot and hands it here.
 *
 * A name Studio asks about that CNA does not have reads as @ref StudioCapabilityAnswer::Unknown,
 * which is treated as unmet — so a typo or a retired feature fails loudly at the first evaluation
 * rather than silently passing.
 *
 * ### `Unknown` is not `Supported`
 *
 * CNA answers each feature `Supported`, `Restricted`, `Unsupported` or `Unknown`, and `Unknown` is
 * common: it means *this renderer has not classified it*, not *no*. Studio treats it as unmet for
 * a **required** capability and says which of the two it was, because a tool that starts and then
 * cannot draw is worse than one that refuses with a reason. For a **recommended** capability it is
 * reported and nothing is blocked.
 *
 * ### The set is deliberately small
 *
 * Requirements are added when the UI actually needs them, never speculatively. A contract padded
 * with everything Studio might one day want would refuse to start on renderers it works perfectly
 * well on, and the resulting pressure would be to ignore the contract rather than to fix it.
 *
 * ### Two profiles, because Studio has two UI render backends
 *
 * `STUDIO-02070`. The contract's headline has always been that hosting Studio needs a renderer
 * capable of CNA's modern graphics API, and until that task the evaluation *carried* modern-API
 * availability into its report and **consulted it nowhere**. A renderer with no modern API at all
 * passed `canHostStudio` on the strength of `ThreeDimensionalPipeline`, `DepthStencilBuffer` and a
 * 2048-pixel texture limit — which is exactly the classic XNA capability set a renderer that
 * cannot execute a shader has. That was an accident, not a decision, and it is closed.
 *
 * What replaces it is not one stricter list but two named ones, because Studio genuinely has two
 * UI GPU backends during the migration recorded in `docs/UI-RENDER-PATH.md`:
 *
 * | Profile | What it is the contract for | Extra required capabilities |
 * |---------|-----------------------------|-----------------------------|
 * | @ref StudioHostProfile::Modern | `StudioModernUiRenderer` — `ShaderEffect` and GPU buffers | the modern API compiled in, `ShaderEffects`, `ShaderEffectSourceExecution` |
 * | @ref StudioHostProfile::Compatibility | `CnaUiRenderer` — `BasicEffect` and user-pointer draws | none beyond the classic set |
 *
 * **`Modern` is the default argument**, deliberately. The bug being fixed was a permissive
 * default, so the strict answer is what a caller gets for writing `evaluateStudioHost(snapshot)`.
 * Asking for `Compatibility` is an explicit act and is visible in the diagnostic, the log, the
 * Diagnostics panel and the status bar.
 *
 * **The compatibility profile is a migration-era allowance with a reason.** CNA's `SOFTWARE`
 * renderer — the only one this project's CI can build, because it needs no display and no GPU
 * (`docs/CNA-GAPS.md` G-10) — reports `ShaderEffects` and `ShaderEffectSourceExecution` as
 * unsupported. Making the modern profile the only profile would therefore make Studio refuse to
 * start in every automated configuration it has. The allowance is written down, named, tested and
 * announced at run time rather than being the silent default it used to be.
 *
 * ### The profile does not change what a *game* may ship on
 *
 * `docs/ARCHITECTURE.md` §3: the Studio host renderer and the game target renderer are independent
 * axes. A renderer that fails the modern profile outright — a 2D-only one, a fixed-function one,
 * one from a feature era below the UI's — remains a perfectly good game target for Play, Build and
 * Package. Nothing in this file is consulted when validating a target profile, and
 * `STUDIO-02073` is the test that keeps it that way.
 */

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace CNA::Studio
{
    /**
     * @brief Which of Studio's two UI render backends the contract is being evaluated for.
     *
     * See the file comment. `Modern` is what a shipping Studio requires; `Compatibility` is the
     * classic path retained for the duration of the migration in `docs/UI-RENDER-PATH.md`.
     */
    enum class StudioHostProfile : std::uint8_t
    {
        /** @brief The intended contract: `ShaderEffect`, GPU buffers, the CNAEXT engine layer. */
        Modern,
        /** @brief The inherited classic XNA path: `BasicEffect` and user-pointer draws. */
        Compatibility
    };

    /** @brief Returns a stable English name for a profile. */
    [[nodiscard]] std::string_view studioHostProfileName(StudioHostProfile profile);

    /**
     * @brief The subject the modern-API requirement reports under.
     *
     * Named rather than spelled out at each use, because a diagnostic, a log line, a panel row and
     * three tests all have to agree on it, and a typo in any one of them would read as the
     * requirement being absent.
     */
    inline constexpr std::string_view kStudioModernApiSubject = "ModernGraphicsApi";

    /**
     * @brief A renderer's classified answer about one capability.
     *
     * Mirrors `CNA::RendererFeatureSupport` without depending on it, so that this module and its
     * tests need no CNA checkout and no GPU.
     */
    enum class StudioCapabilityAnswer : std::uint8_t
    {
        /** @brief The renderer has not classified this. Not the same as no. */
        Unknown,
        /** @brief The capability is unavailable and its operation refuses deterministically. */
        Unsupported,
        /** @brief The complete documented contract is available. */
        Supported,
        /** @brief Only an explicitly described subset is available. */
        Restricted
    };

    /** @brief Returns a stable English name for an answer. */
    [[nodiscard]] std::string_view studioCapabilityAnswerName(StudioCapabilityAnswer answer);

    /** @brief How badly Studio needs a capability. */
    enum class StudioRequirementSeverity : std::uint8_t
    {
        /** @brief Studio cannot draw its own UI without it and refuses to start. */
        Required,
        /** @brief Studio runs; something is degraded or a panel is unavailable. */
        Recommended
    };

    /** @brief One capability Studio's host renderer is asked for. */
    struct StudioHostFeatureRequirement
    {
        /** @brief CNA `RendererFeature` identifier, e.g. `"ThreeDimensionalPipeline"`. */
        std::string feature;
        /** @brief Why Studio needs it, in one sentence, for the diagnostic. */
        std::string reason;
        /** @brief Whether Studio refuses to start without it. */
        StudioRequirementSeverity severity = StudioRequirementSeverity::Required;
        /**
         * @brief Whether a `Restricted` answer is good enough.
         *
         * Some capabilities Studio uses in a way any documented subset covers; others it uses to
         * the edge of the contract. Stating which per requirement is the difference between a
         * contract that is accurate and one that is merely strict.
         */
        bool restrictedIsEnough = true;
    };

    /** @brief One numeric limit Studio's host renderer must meet. */
    struct StudioHostLimitRequirement
    {
        /** @brief CNA `RendererLimit` identifier, e.g. `"MaxTextureDimension"`. */
        std::string limit;
        /** @brief Smallest acceptable value. */
        std::uint64_t minimum = 0;
        /** @brief Why Studio needs it, in one sentence. */
        std::string reason;
        /** @brief Whether Studio refuses to start without it. */
        StudioRequirementSeverity severity = StudioRequirementSeverity::Required;
    };

    /**
     * @brief What a device reports, expressed without any CNA type.
     *
     * Filled by the adapter in `cna-studio-viewport` from a real `RendererCapabilityProfile`, and
     * by tests from nothing at all — which is the point: every branch of the evaluation, including
     * the ones that only a renderer nobody has can reach, is exercised in CI with no GPU.
     */
    class StudioCapabilitySnapshot
    {
    public:
        /** @brief Sets the renderer's stable name, for the report. */
        void setRendererName(std::string name) { rendererName_ = std::move(name); }
        /** @brief The renderer's stable name. */
        [[nodiscard]] std::string_view rendererName() const { return rendererName_; }

        /** @brief Sets the platform implementation's name, for the report. */
        void setPlatformName(std::string name) { platformName_ = std::move(name); }
        /** @brief The platform implementation's name. */
        [[nodiscard]] std::string_view platformName() const { return platformName_; }

        /** @brief Records whether the modern CNAEXT graphics API is available. */
        void setModernApiAvailable(bool available) { modernApi_ = available; }
        /** @brief Whether the modern CNAEXT graphics API is available. */
        [[nodiscard]] bool isModernApiAvailable() const { return modernApi_; }

        /**
         * @brief Records one capability answer.
         * @param feature CNA `RendererFeature` identifier.
         * @param answer The renderer's classified answer.
         * @param note The renderer's English qualification, if it gave one.
         */
        void setFeature(std::string feature, StudioCapabilityAnswer answer, std::string note = {});

        /**
         * @brief Returns a capability answer.
         * @param feature CNA `RendererFeature` identifier.
         * @return The answer, or `Unknown` when the renderer never mentioned it.
         */
        [[nodiscard]] StudioCapabilityAnswer feature(std::string_view feature) const;

        /**
         * @brief Returns the renderer's qualification of a capability.
         * @param feature CNA `RendererFeature` identifier.
         * @return The note, or empty.
         */
        [[nodiscard]] std::string_view featureNote(std::string_view feature) const;

        /**
         * @brief Records one numeric limit. Only call it for a limit the device reports as known.
         * @param limit CNA `RendererLimit` identifier.
         * @param value The reported value.
         */
        void setLimit(std::string limit, std::uint64_t value);

        /**
         * @brief Returns a numeric limit.
         * @param limit CNA `RendererLimit` identifier.
         * @return The value, or nothing when the renderer did not classify it.
         */
        [[nodiscard]] std::optional<std::uint64_t> limit(std::string_view limit) const;

        /** @brief Number of capabilities the renderer classified. */
        [[nodiscard]] std::size_t featureCount() const { return features_.size(); }

    private:
        struct FeatureEntry
        {
            std::string name;
            StudioCapabilityAnswer answer = StudioCapabilityAnswer::Unknown;
            std::string note;
        };

        std::string rendererName_;
        std::string platformName_;
        bool modernApi_ = false;
        std::vector<FeatureEntry> features_;
        std::vector<std::pair<std::string, std::uint64_t>> limits_;
    };

    /** @brief How one requirement came out against a device. */
    enum class StudioRequirementStatus : std::uint8_t
    {
        /** @brief The device provides the complete contract. */
        Satisfied,
        /** @brief The device provides a described subset, and that is enough here. */
        SatisfiedWithRestriction,
        /** @brief The device provides only a subset, and that is not enough here. */
        Restricted,
        /** @brief The device says no. */
        Unsupported,
        /** @brief The device has not classified this. Not the same as no, and still not a yes. */
        Unclassified,
        /** @brief A numeric limit the device reports is below what Studio needs. */
        BelowMinimum
    };

    /** @brief Returns a stable English name for a status. */
    [[nodiscard]] std::string_view studioRequirementStatusName(StudioRequirementStatus status);

    /** @brief One requirement's outcome. */
    struct StudioRequirementOutcome
    {
        /** @brief The CNA feature or limit identifier this is about. */
        std::string subject;
        /** @brief Why Studio wanted it. */
        std::string reason;
        /** @brief Whether Studio refuses to start without it. */
        StudioRequirementSeverity severity = StudioRequirementSeverity::Required;
        /** @brief How it came out. */
        StudioRequirementStatus status = StudioRequirementStatus::Unclassified;
        /** @brief The renderer's own words, or the numbers, when there are any. */
        std::string detail;

        /** @brief Whether this requirement is met. */
        [[nodiscard]] bool isMet() const
        {
            return status == StudioRequirementStatus::Satisfied
                || status == StudioRequirementStatus::SatisfiedWithRestriction;
        }
    };

    /** @brief The verdict on one device. */
    struct StudioHostEvaluation
    {
        /** @brief Whether every **required** capability is met. */
        bool canHostStudio = false;
        /** @brief The renderer this was evaluated against. */
        std::string rendererName;
        /** @brief The platform implementation. */
        std::string platformName;
        /** @brief Whether the modern CNAEXT graphics API is available. */
        bool modernApiAvailable = false;
        /** @brief Which backend's contract this verdict is about. */
        StudioHostProfile profile = StudioHostProfile::Modern;
        /** @brief Every requirement's outcome, in declaration order. */
        std::vector<StudioRequirementOutcome> outcomes;

        /** @brief The required requirements that are not met. */
        [[nodiscard]] std::vector<StudioRequirementOutcome> unmetRequired() const;
        /** @brief The recommended requirements that are not met. */
        [[nodiscard]] std::vector<StudioRequirementOutcome> unmetRecommended() const;

        /**
         * @brief The message Studio prints when it will not start (`STUDIO-02022`).
         *
         * Names each unmet requirement, says whether the renderer refused it or never classified
         * it, and repeats the renderer's own qualification where it gave one. Empty when Studio
         * can host.
         *
         * @return The diagnostic.
         */
        [[nodiscard]] std::string diagnostic() const;

        /**
         * @brief The start-up report (`STUDIO-02021`): platform, renderer, modern API, every
         *        requirement and its outcome.
         * @return The report.
         */
        [[nodiscard]] std::string report() const;
    };

    /**
     * @brief The capabilities Studio's own UI needs. The living list of `STUDIO-02020`.
     * @param profile Which backend's contract to return. See the file comment.
     * @return The requirements, in the order they are reported.
     */
    [[nodiscard]] const std::vector<StudioHostFeatureRequirement>& studioHostFeatureRequirements(
        StudioHostProfile profile = StudioHostProfile::Modern);

    /**
     * @brief The numeric limits Studio's own UI needs.
     * @param profile Which backend's contract to return.
     * @return The limits, in the order they are reported.
     */
    [[nodiscard]] const std::vector<StudioHostLimitRequirement>& studioHostLimitRequirements(
        StudioHostProfile profile = StudioHostProfile::Modern);

    /**
     * @brief Whether this profile requires the modern CNAEXT graphics API to be compiled in.
     * @param profile The profile to ask about.
     * @return True for @ref StudioHostProfile::Modern.
     */
    [[nodiscard]] bool studioHostProfileRequiresModernApi(StudioHostProfile profile);

    /**
     * @brief Why the modern profile needs the engine layer, for the diagnostic.
     * @return One sentence.
     */
    [[nodiscard]] std::string_view studioHostModernApiReason();

    /**
     * @brief Evaluates the contract against what a device reports.
     * @param snapshot What the device said.
     * @param profile Which backend's contract to evaluate. Strict by default, because the defect
     *        this parameter exists to fix was a permissive default.
     * @return The verdict.
     */
    [[nodiscard]] StudioHostEvaluation evaluateStudioHost(
        const StudioCapabilitySnapshot& snapshot,
        StudioHostProfile profile = StudioHostProfile::Modern);

    /** @brief Which UI render backend a host should use, and why. */
    enum class StudioUiBackendChoice : std::uint8_t
    {
        /** @brief Neither profile is satisfied. Studio refuses to start. */
        None,
        /** @brief The modern CNAEXT path. */
        Modern,
        /** @brief The classic path, because the modern profile is unmet. */
        Compatibility
    };

    /** @brief Returns a stable English name for a backend choice. */
    [[nodiscard]] std::string_view studioUiBackendChoiceName(StudioUiBackendChoice choice);

    /** @brief A backend decision with the sentence that explains it. */
    struct StudioUiBackendDecision
    {
        /** @brief What to draw with. */
        StudioUiBackendChoice choice = StudioUiBackendChoice::None;
        /**
         * @brief One line, always populated, naming what decided it.
         *
         * Populated even when the modern path is chosen: "why is this host on the classic
         * renderer" and "why is this host on the modern one" are the same question asked by
         * somebody reading a bug report, and an empty string answers neither.
         */
        std::string reason;
    };

    /**
     * @brief Chooses a UI render backend from the two profile verdicts for one device.
     *
     * CNA-free and pure, so the fallback — the one path that only a renderer without shaders
     * reaches — is exercised in CI on a machine that has no renderer at all.
     *
     * @param modern The verdict under @ref StudioHostProfile::Modern.
     * @param compatibility The verdict under @ref StudioHostProfile::Compatibility.
     * @param allowCompatibilityFallback Whether falling back is permitted. False makes the modern
     *        profile a hard requirement, which is what a shipping Studio and `--ui-renderer=modern`
     *        both ask for.
     * @return The decision.
     */
    [[nodiscard]] StudioUiBackendDecision resolveStudioUiBackend(
        const StudioHostEvaluation& modern, const StudioHostEvaluation& compatibility,
        bool allowCompatibilityFallback = true);
} // namespace CNA::Studio
