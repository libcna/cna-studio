// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Project/TargetProfile.hpp
 * @brief What a game is built *for*: operating system, architecture, platform, renderer,
 *        configuration and features.
 *
 * `plan.md` STUDIO-02040, STUDIO-02041, STUDIO-17003.
 *
 * ### Six axes, because CNA has six
 *
 * The prototype modelled one: a "backend" string. Current CNA separates renderer from platform,
 * builds for several operating systems and architectures, takes a build configuration, and gates
 * whole subsystems behind feature options. Collapsing that into one string cannot express "SDL3
 * windowing with a Vulkan renderer", cannot express a 32-bit Windows build, and cannot express a
 * game that does not want the networking layer compiled in.
 *
 * ### This is the *game's* target, never Studio's host
 *
 * `docs/ARCHITECTURE.md` §3: the renderer that draws CNA Studio and the renderer that draws the
 * user's game are independent, and conflating them is the easiest way to get this product wrong. A
 * renderer that cannot host Studio's UI must still be offered as a game target — a project using
 * only classic XNA-compatible functionality has every right to ship on one. Nothing in this file
 * consults the Studio host capability contract, and nothing in that contract consults this.
 *
 * ### Validation knows what CNA declares, and admits what it does not
 *
 * Studio can check a profile against CNA's registered renderer identities, its implemented
 * platforms, and the per-renderer operating-system gates CNA's own `cmake/RendererSelection.cmake`
 * enforces as hard configure errors. Those gates are facts, not guesses, and each one below cites
 * where it was read from.
 *
 * What Studio *cannot* do is ask CNA for them: they exist only as CMake conditions, so the only way
 * to learn that a renderer cannot be built for a target is to attempt a configure and read the
 * error. That is CNA gap G-08, and this validation is the workaround — which means the table here
 * is a transcription with a known expiry date, not a second source of truth. `STUDIO-29007` is the
 * guard test that re-reads CNA and fails when the transcription drifts.
 */

#include "CNA/Studio/Core/Json.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace CNA::Studio
{
    /** @brief The operating system a build targets. */
    enum class StudioTargetOs : std::uint8_t
    {
        Linux,
        Windows,
        MacOs,
        Android,
        IOs,
        /** @brief A browser, through Emscripten. */
        Web,
        /** @brief Number of declared systems; not itself one. */
        Count
    };

    /** @brief The processor architecture a build targets. */
    enum class StudioArchitecture : std::uint8_t
    {
        X86_64,
        X86,
        Arm64,
        Arm32,
        /** @brief WebAssembly, 32-bit. */
        Wasm32,
        /** @brief Number of declared architectures; not itself one. */
        Count
    };

    /** @brief The CMake build configuration. */
    enum class StudioBuildConfiguration : std::uint8_t
    {
        Debug,
        Release,
        RelWithDebInfo,
        MinSizeRel,
        /** @brief Number of declared configurations; not itself one. */
        Count
    };

    /** @brief Returns the stable lower-case name of an operating system, as written to disk. */
    [[nodiscard]] std::string_view studioTargetOsName(StudioTargetOs os);
    /** @brief Returns the name a user sees for an operating system. */
    [[nodiscard]] std::string_view studioTargetOsDisplayName(StudioTargetOs os);
    /** @brief Parses an operating system name; returns false for an unknown one. */
    [[nodiscard]] bool parseStudioTargetOs(std::string_view name, StudioTargetOs& out);

    /** @brief Returns the stable lower-case name of an architecture, as written to disk. */
    [[nodiscard]] std::string_view studioArchitectureName(StudioArchitecture architecture);
    /** @brief Parses an architecture name; returns false for an unknown one. */
    [[nodiscard]] bool parseStudioArchitecture(std::string_view name, StudioArchitecture& out);

    /** @brief Returns the CMake spelling of a configuration, e.g. `"RelWithDebInfo"`. */
    [[nodiscard]] std::string_view studioBuildConfigurationName(StudioBuildConfiguration value);
    /** @brief Parses a configuration name, case-insensitively; returns false for an unknown one. */
    [[nodiscard]] bool parseStudioBuildConfiguration(std::string_view name,
                                                     StudioBuildConfiguration& out);

    /** @brief An optional CNA subsystem a target can compile in or leave out. */
    struct StudioFeatureOption
    {
        /** @brief Stable lower-case name, as written to disk, e.g. `"net"`. */
        std::string_view name;
        /** @brief The CNA CMake option it sets, e.g. `"CNA_ENABLE_NET"`. */
        std::string_view cnaOption;
        /** @brief Name shown to a user. */
        std::string_view displayName;
        /** @brief What leaving it out costs. */
        std::string_view note;
        /** @brief Whether a new project has it on. */
        bool defaultEnabled = false;

        /**
         * @brief What "on" is spelled as in CNA's configure.
         *
         * Usually `"ON"`, because most of CNA's switches are booleans. `CNA_ENABLE_VIDEO` is not:
         * it takes `OFF`, `AUTO` or `ON`, where `ON` *requires* FFmpeg and fails the configure
         * without it, and `AUTO` uses FFmpeg when the machine has it. Studio passing `ON` for a
         * feature the project merely wants is how an exported game stopped building on a machine
         * with no FFmpeg -- found by the export guard (`STUDIO-02051`), which is the whole reason
         * that guard builds the exported project rather than only reading it.
         *
         * Studio's model is still a boolean, so a project that requires video unconditionally has
         * to say so by overriding `CNA_ENABLE_VIDEO` itself; `STUDIO-17012` is the tri-state.
         */
        std::string_view enabledValue = "ON";
    };

    /** @brief Every feature a target profile can carry. */
    [[nodiscard]] const std::vector<StudioFeatureOption>& getKnownStudioFeatures();

    /**
     * @brief Finds a feature by its stable name.
     * @param name Feature name.
     * @return The feature, or nullptr.
     */
    [[nodiscard]] const StudioFeatureOption* findStudioFeature(std::string_view name);

    /**
     * @brief One complete build target for a game.
     *
     * A value: copy it, change an axis, and you have a variant. That is how a project holds several
     * — "Linux desktop", "Windows 32-bit", "Web" — without any of them being privileged.
     */
    struct StudioTargetProfile
    {
        /** @brief What the user calls it, e.g. `"Linux Desktop"`. */
        std::string name = "Default";

        /** @brief Operating system. */
        StudioTargetOs os = StudioTargetOs::Linux;
        /** @brief Processor architecture. */
        StudioArchitecture architecture = StudioArchitecture::X86_64;

        /** @brief CNA platform implementation, by its lower-case name, e.g. `"sdl3"`. */
        std::string platform = "sdl3";
        /** @brief CNA renderer, by its lower-case name, e.g. `"opengles3"`. */
        std::string renderer = "opengles3";

        /** @brief Build configuration. */
        StudioBuildConfiguration configuration = StudioBuildConfiguration::Release;

        /** @brief Feature names that are on. Anything absent is off. */
        std::vector<std::string> features;

        /**
         * @brief Whether a feature is on.
         * @param feature Feature name.
         * @return True when it is in @ref features.
         */
        [[nodiscard]] bool hasFeature(std::string_view feature) const;

        /**
         * @brief Turns a feature on or off.
         * @param feature Feature name.
         * @param enabled Whether it should be on.
         */
        void setFeature(std::string_view feature, bool enabled);

        /** @brief Returns the default profile for a new project on the host system. */
        [[nodiscard]] static StudioTargetProfile defaults();
    };

    /** @brief How serious a validation finding is. */
    enum class StudioProfileSeverity : std::uint8_t
    {
        /** @brief The build will not configure. */
        Error,
        /** @brief The build configures; something about it is worth saying. */
        Warning,
        /** @brief Studio changed the profile to keep it buildable, and is saying so. */
        Migration
    };

    /** @brief One thing validation found. */
    struct StudioProfileProblem
    {
        StudioProfileSeverity severity = StudioProfileSeverity::Error;
        /** @brief Which axis it concerns: `"renderer"`, `"platform"`, `"architecture"`, ... */
        std::string axis;
        /** @brief What is wrong, in one sentence a user can act on. */
        std::string message;
    };

    /** @brief The outcome of validating a profile. */
    struct StudioProfileValidation
    {
        /** @brief Everything found, errors first. */
        std::vector<StudioProfileProblem> problems;

        /** @brief Whether the profile can be built at all. */
        [[nodiscard]] bool isBuildable() const;

        /** @brief Whether anything at all was found. */
        [[nodiscard]] bool isClean() const { return problems.empty(); }

        /** @brief A multi-line report, for the Build panel and for diagnostics. */
        [[nodiscard]] std::string report() const;
    };

    /**
     * @brief Checks a profile, migrating a legacy renderer name in place.
     *
     * Migration rather than rejection is deliberate: a `.cnaproject` the prototype wrote names
     * renderers that no longer exist, and failing on it would make an old project look corrupt
     * while silently substituting one would change what the user's game ships on. It is changed
     * *and reported*.
     *
     * @param profile Profile to check; its renderer may be rewritten.
     * @return What was found.
     */
    [[nodiscard]] StudioProfileValidation validateStudioTargetProfile(StudioTargetProfile& profile);

    /**
     * @brief Reports whether CNA can build @p renderer for @p os.
     *
     * Transcribed from CNA's `cmake/RendererSelection.cmake`, where these are hard configure
     * errors. See the file comment for why Studio has to hold this at all (CNA gap G-08).
     *
     * @param renderer Renderer name, lower case.
     * @param os Target operating system.
     * @param outReason Receives CNA's reason when the answer is no.
     * @return True when CNA will configure that combination.
     */
    [[nodiscard]] bool isRendererAvailableOn(std::string_view renderer, StudioTargetOs os,
                                             std::string* outReason = nullptr);

    /**
     * @brief The CMake arguments this profile configures a game's build with.
     *
     * Exactly what Studio passes, in a stable order, so that the Build panel can show the command
     * it is about to run. A tool that runs a build a user cannot see is a tool they cannot debug.
     *
     * @param profile Profile to translate.
     * @return The arguments, e.g. `-DCNA_GRAPHICS_RENDERER=OPENGLES3`.
     */
    /**
     * @brief CNA's own spelling of a renderer Studio stores lower case.
     *
     * Studio stores `"opengles3"`; CNA's configure wants `"OPENGLES3"`. One function knows that,
     * and everything that has to speak to CNA's build -- the build runner, the project exporter --
     * asks it rather than upper-casing a string and hoping the two conventions never diverge.
     *
     * @param renderer Studio's lower-case renderer name.
     * @return CNA's identity for it, or the name upper-cased when it is not one Studio knows.
     */
    /**
     * @brief A profile as one readable line, e.g. `"Linux x86_64 - opengles3 - Release"`.
     *
     * One place decides the wording, because it is shown in the status bar, in the Build panel's
     * target list and in a build log, and three spellings of the same profile read as three
     * different targets.
     *
     * @param profile The profile.
     * @return Its description.
     */
    [[nodiscard]] std::string studioTargetProfileSummary(const StudioTargetProfile& profile);

    [[nodiscard]] std::string studioRendererCnaIdentity(std::string_view renderer);

    /**
     * @brief CNA's own spelling of a platform Studio stores lower case.
     * @param platform Studio's lower-case platform name.
     * @return CNA's identity for it, or the name upper-cased when it is not one Studio knows.
     */
    [[nodiscard]] std::string studioPlatformCnaIdentity(std::string_view platform);

    /**
     * @brief Serializes a profile.
     * @param profile Profile to write.
     * @return The JSON object.
     */
    [[nodiscard]] JsonValue studioTargetProfileToJson(const StudioTargetProfile& profile);

    /**
     * @brief Reads a profile, filling anything absent from the defaults.
     *
     * Never fails: a project file missing an axis, or carrying one this build does not recognise,
     * yields a usable profile and a validation finding rather than an unopenable project.
     *
     * @param value The JSON object.
     * @return The profile.
     */
    [[nodiscard]] StudioTargetProfile studioTargetProfileFromJson(const JsonValue& value);
} // namespace CNA::Studio
