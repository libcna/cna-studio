// SPDX-License-Identifier: MS-PL
/**
 * @file StudioTargetProfileTests.cpp
 * @brief The target-profile model: six axes, their validation, and their translation to CMake.
 *
 * `plan.md` STUDIO-02040, STUDIO-02041.
 *
 * The property most worth guarding is the one that is easiest to lose: **this is the game's target,
 * never Studio's host**. A renderer that cannot draw Studio's UI must still be offered as something
 * a game ships on, and the day those two decisions share a code path is the day a user is told they
 * cannot build for a platform because the tool could not run on it.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Project/BuildRunner.hpp"
#include "CNA/Studio/Project/Project.hpp"
#include "CNA/Studio/Project/RendererCatalog.hpp"
#include "CNA/Studio/Project/StudioHostRequirements.hpp"
#include "CNA/Studio/Project/TargetProfile.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    /** @brief Whether a validation reported anything about an axis. */
    const StudioProfileProblem* problemOn(const StudioProfileValidation& validation,
                                          std::string_view axis)
    {
        for (const StudioProfileProblem& problem : validation.problems)
        {
            if (problem.axis == axis) { return &problem; }
        }
        return nullptr;
    }

    /** @brief Whether a CMake argument list contains a `-DNAME=VALUE`. */
    bool has(const std::vector<std::string>& arguments, std::string_view assignment)
    {
        return std::any_of(arguments.begin(), arguments.end(),
                           [&](const std::string& argument) { return argument == assignment; });
    }
}

// ------------------------------------------------------------------------------------------------
// The model
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(EveryAxisHasAStableNameThatParsesBack)
{
    // The names are written into `.cnaproject` files, so a round trip that lost one would be a
    // project that silently changed what it builds.
    for (int i = 0; i < static_cast<int>(StudioTargetOs::Count); ++i)
    {
        const auto os = static_cast<StudioTargetOs>(i);
        StudioTargetOs parsed{};
        CNA_STUDIO_EXPECT(!studioTargetOsName(os).empty());
        CNA_STUDIO_EXPECT(!studioTargetOsDisplayName(os).empty());
        CNA_STUDIO_EXPECT(parseStudioTargetOs(studioTargetOsName(os), parsed));
        CNA_STUDIO_EXPECT(parsed == os);
    }
    for (int i = 0; i < static_cast<int>(StudioArchitecture::Count); ++i)
    {
        const auto architecture = static_cast<StudioArchitecture>(i);
        StudioArchitecture parsed{};
        CNA_STUDIO_EXPECT(!studioArchitectureName(architecture).empty());
        CNA_STUDIO_EXPECT(parseStudioArchitecture(studioArchitectureName(architecture), parsed));
        CNA_STUDIO_EXPECT(parsed == architecture);
    }
    for (int i = 0; i < static_cast<int>(StudioBuildConfiguration::Count); ++i)
    {
        const auto configuration = static_cast<StudioBuildConfiguration>(i);
        StudioBuildConfiguration parsed{};
        CNA_STUDIO_EXPECT(parseStudioBuildConfiguration(
            studioBuildConfigurationName(configuration), parsed));
        CNA_STUDIO_EXPECT(parsed == configuration);
    }

    StudioTargetOs ignored{};
    CNA_STUDIO_EXPECT(!parseStudioTargetOs("solaris", ignored));
}

CNA_STUDIO_TEST(EveryFeatureNamesTheCnaOptionItSwitches)
{
    for (const StudioFeatureOption& feature : getKnownStudioFeatures())
    {
        CNA_STUDIO_EXPECT(!feature.name.empty());
        CNA_STUDIO_EXPECT(feature.cnaOption.rfind("CNA_", 0) == 0);
        CNA_STUDIO_EXPECT(!feature.note.empty());
        CNA_STUDIO_EXPECT(findStudioFeature(feature.name) == &feature);
    }
    CNA_STUDIO_EXPECT(findStudioFeature("telepathy") == nullptr);
}

CNA_STUDIO_TEST(FeaturesStaySortedSoAProjectFileDoesNotChurn)
{
    StudioTargetProfile profile = StudioTargetProfile::defaults();
    profile.setFeature("net", true);
    profile.setFeature("draco", true);
    profile.setFeature("net", true);   // twice, deliberately

    CNA_STUDIO_EXPECT(std::is_sorted(profile.features.begin(), profile.features.end()));
    CNA_STUDIO_EXPECT_EQ(std::count(profile.features.begin(), profile.features.end(),
                                    std::string{"net"}), std::ptrdiff_t{1});

    profile.setFeature("net", false);
    CNA_STUDIO_EXPECT(!profile.hasFeature("net"));
    CNA_STUDIO_EXPECT(profile.hasFeature("draco"));
}

// ------------------------------------------------------------------------------------------------
// Validation
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(TheDefaultProfileIsBuildable)
{
    StudioTargetProfile profile = StudioTargetProfile::defaults();
    const StudioProfileValidation validation = validateStudioTargetProfile(profile);

    CNA_STUDIO_EXPECT(validation.isBuildable());
    CNA_STUDIO_EXPECT(validation.isClean());
    CNA_STUDIO_EXPECT(validation.report().empty());
}

CNA_STUDIO_TEST(ARendererCnaCannotBuildForTheTargetIsAnErrorNamingCnasReason)
{
    // Transcribed from CNA's own cmake/RendererSelection.cmake, where each of these is a hard
    // configure error. Studio says so before the build rather than after it.
    StudioTargetProfile profile = StudioTargetProfile::defaults();
    profile.os = StudioTargetOs::Linux;
    profile.renderer = "directx11";

    const StudioProfileValidation validation = validateStudioTargetProfile(profile);
    CNA_STUDIO_EXPECT(!validation.isBuildable());

    const StudioProfileProblem* problem = problemOn(validation, "renderer");
    CNA_STUDIO_EXPECT(problem != nullptr);
    CNA_STUDIO_EXPECT(problem != nullptr && problem->message.find("Linux") != std::string::npos);
    CNA_STUDIO_EXPECT(problem != nullptr && problem->message.find("Windows") != std::string::npos);

    profile.os = StudioTargetOs::Windows;
    CNA_STUDIO_EXPECT(validateStudioTargetProfile(profile).isBuildable());
}

CNA_STUDIO_TEST(BrowserOnlyAndNativeOnlyRenderersAreBothGated)
{
    std::string reason;
    CNA_STUDIO_EXPECT(!isRendererAvailableOn("webgl2", StudioTargetOs::Linux, &reason));
    CNA_STUDIO_EXPECT(!reason.empty());
    CNA_STUDIO_EXPECT(isRendererAvailableOn("webgl2", StudioTargetOs::Web));

    CNA_STUDIO_EXPECT(!isRendererAvailableOn("opengles3", StudioTargetOs::Web));
    CNA_STUDIO_EXPECT(isRendererAvailableOn("opengles3", StudioTargetOs::Linux));

    // A renderer CNA gates nowhere is available everywhere: Studio does not invent a restriction
    // CNA does not state.
    CNA_STUDIO_EXPECT(isRendererAvailableOn("software", StudioTargetOs::Linux));
    CNA_STUDIO_EXPECT(isRendererAvailableOn("software", StudioTargetOs::Web));
    CNA_STUDIO_EXPECT(isRendererAvailableOn("vulkan", StudioTargetOs::Android));
}

CNA_STUDIO_TEST(GlideIsGatedOnTheArchitectureCnaGatesItOn)
{
    StudioTargetProfile profile = StudioTargetProfile::defaults();
    profile.os = StudioTargetOs::Windows;
    profile.renderer = "glide";
    profile.architecture = StudioArchitecture::X86_64;

    const StudioProfileValidation validation = validateStudioTargetProfile(profile);
    CNA_STUDIO_EXPECT(!validation.isBuildable());
    CNA_STUDIO_EXPECT(problemOn(validation, "architecture") != nullptr);

    profile.architecture = StudioArchitecture::X86;
    CNA_STUDIO_EXPECT(validateStudioTargetProfile(profile).isBuildable());
}

CNA_STUDIO_TEST(WebAssemblyAndWebGoTogetherOrNotAtAll)
{
    StudioTargetProfile profile = StudioTargetProfile::defaults();
    profile.architecture = StudioArchitecture::Wasm32;
    CNA_STUDIO_EXPECT(!validateStudioTargetProfile(profile).isBuildable());

    profile.os = StudioTargetOs::Web;
    profile.renderer = "webgl2";
    CNA_STUDIO_EXPECT(validateStudioTargetProfile(profile).isBuildable());

    profile.architecture = StudioArchitecture::X86_64;
    CNA_STUDIO_EXPECT(!validateStudioTargetProfile(profile).isBuildable());
}

CNA_STUDIO_TEST(AReservedPlatformIsRefusedRatherThanQuietlyDefaulted)
{
    StudioTargetProfile profile = StudioTargetProfile::defaults();
    profile.platform = "win32";   // reserved in CNA, not implemented

    const StudioProfileValidation validation = validateStudioTargetProfile(profile);
    CNA_STUDIO_EXPECT(!validation.isBuildable());
    CNA_STUDIO_EXPECT(problemOn(validation, "platform") != nullptr);

    profile.platform = "not-a-platform";
    CNA_STUDIO_EXPECT(!validateStudioTargetProfile(profile).isBuildable());
}

CNA_STUDIO_TEST(ALegacyRendererIsMigratedInPlaceAndReported)
{
    StudioTargetProfile profile = StudioTargetProfile::defaults();
    profile.renderer = "easygl";

    const StudioProfileValidation validation = validateStudioTargetProfile(profile);
    CNA_STUDIO_EXPECT(validation.isBuildable());
    CNA_STUDIO_EXPECT(!validation.isClean());
    CNA_STUDIO_EXPECT(profile.renderer != "easygl");
    CNA_STUDIO_EXPECT(findRenderer(profile.renderer) != nullptr);

    const StudioProfileProblem* problem = problemOn(validation, "renderer");
    CNA_STUDIO_EXPECT(problem != nullptr);
    CNA_STUDIO_EXPECT(problem != nullptr
                      && problem->severity == StudioProfileSeverity::Migration);
}

CNA_STUDIO_TEST(AnUnknownFeatureWarnsRatherThanBlocks)
{
    // Studio is not the authority on what CNA options exist -- CNA is -- so a name this build does
    // not recognise is passed through and mentioned, not refused.
    StudioTargetProfile profile = StudioTargetProfile::defaults();
    profile.setFeature("some-future-cna-option", true);

    const StudioProfileValidation validation = validateStudioTargetProfile(profile);
    CNA_STUDIO_EXPECT(validation.isBuildable());
    CNA_STUDIO_EXPECT(problemOn(validation, "features") != nullptr);
}

// ------------------------------------------------------------------------------------------------
// The game's target is not Studio's host (STUDIO-02041)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(ARendererThatCannotHostStudioIsStillAValidGameTarget)
{
    // The property this whole separation exists for. A project using only classic XNA-compatible
    // functionality has every right to ship on a renderer Studio's own UI could never run on, and
    // the day those two decisions share a code path is the day a user is told they cannot build
    // for a platform because the tool could not run on it.
    std::size_t checked = 0;
    for (const RendererInfo& renderer : getKnownRenderers())
    {
        if (renderer.hostSupport == RendererHostSupport::StudioHost) { continue; }

        StudioTargetProfile profile = StudioTargetProfile::defaults();
        profile.renderer = std::string{renderer.commandLineName};

        // Pick an operating system CNA will actually build it for, so the test is about host
        // support and not about a gate that has nothing to do with it.
        for (int i = 0; i < static_cast<int>(StudioTargetOs::Count); ++i)
        {
            const auto os = static_cast<StudioTargetOs>(i);
            if (!isRendererAvailableOn(profile.renderer, os)) { continue; }

            profile.os = os;
            profile.architecture = os == StudioTargetOs::Web ? StudioArchitecture::Wasm32
                                                             : StudioArchitecture::X86_64;
            if (profile.renderer == "glide") { profile.architecture = StudioArchitecture::X86; }

            const StudioProfileValidation validation = validateStudioTargetProfile(profile);
            if (!validation.isBuildable())
            {
                CnaStudioTest::reportFailure(__FILE__, __LINE__,
                    "renderer '" + profile.renderer + "' was refused as a game target on "
                    + std::string{studioTargetOsDisplayName(os)} + ":\n" + validation.report());
            }
            CNA_STUDIO_EXPECT(validation.isBuildable());
            ++checked;
            break;
        }
    }
    CNA_STUDIO_EXPECT(checked > 20);
}

CNA_STUDIO_TEST(TheTargetProfileNeverConsultsTheHostCapabilityContract)
{
    // Stated as a test because it is the kind of coupling somebody adds in good faith: a profile
    // that "helpfully" refused a renderer Studio cannot host would be refusing a perfectly good
    // shipping target.
    StudioTargetProfile profile = StudioTargetProfile::defaults();
    profile.renderer = "stub";
    const StudioProfileValidation validation = validateStudioTargetProfile(profile);
    CNA_STUDIO_EXPECT(validation.isBuildable());

    // Meanwhile the host contract, asked about a device that classifies nothing, refuses.
    const StudioCapabilitySnapshot nothingClassified;
    CNA_STUDIO_EXPECT(!evaluateStudioHost(nothingClassified).canHostStudio);
}

// ------------------------------------------------------------------------------------------------
// CMake arguments and persistence
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(AProfileTranslatesToTheCMakeArgumentsStudioWouldRun)
{
    StudioTargetProfile profile = StudioTargetProfile::defaults();
    profile.renderer = "vulkan";
    profile.platform = "sdl3";
    profile.configuration = StudioBuildConfiguration::RelWithDebInfo;
    profile.setFeature("net", true);
    profile.setFeature("draco", false);

    const std::vector<std::string> arguments = studioTargetProfileCMakeArguments(profile);

    CNA_STUDIO_EXPECT(has(arguments, "-DCMAKE_BUILD_TYPE=RelWithDebInfo"));
    // Upper case, because that is how CNA spells its identities; Studio stores them lower case
    // because that is how a user types them, and converts in exactly one place.
    CNA_STUDIO_EXPECT(has(arguments, "-DCNA_GRAPHICS_RENDERER=VULKAN"));
    CNA_STUDIO_EXPECT(has(arguments, "-DCNA_PLATFORM=SDL3"));

    // Every known feature, on *or off*, explicitly. Passing only the enabled ones lets a stale
    // cache keep a feature the profile turned off -- a build that works for whoever configured it
    // and for nobody else.
    CNA_STUDIO_EXPECT(has(arguments, "-DCNA_ENABLE_NET=ON"));
    CNA_STUDIO_EXPECT(has(arguments, "-DCNA_ENABLE_DRACO=OFF"));
    for (const StudioFeatureOption& feature : getKnownStudioFeatures())
    {
        const std::string on = "-D" + std::string{feature.cnaOption} + "="
                             + std::string{feature.enabledValue};
        const std::string off = "-D" + std::string{feature.cnaOption} + "=OFF";
        CNA_STUDIO_EXPECT(has(arguments, on) || has(arguments, off));
    }
}

CNA_STUDIO_TEST(TurningVideoOnAsksCnaToUseFfmpegIfPresentRatherThanToRequireIt)
{
    // CNA_ENABLE_VIDEO is the one switch in this set that is not a boolean: OFF, AUTO or ON, where
    // ON *requires* FFmpeg and fails the configure without it. Studio's "video" feature means the
    // project would like video, not that it cannot be built without it -- so it maps to AUTO.
    //
    // This is not a hypothetical. Passing ON is what stopped an exported game configuring on a
    // machine with no FFmpeg, which the export guard found by building the result rather than only
    // reading it. A project that genuinely requires video sets CNA_ENABLE_VIDEO itself; the
    // tri-state is STUDIO-17012.
    StudioTargetProfile profile;
    profile.setFeature("video", true);

    const std::vector<std::string> arguments = studioTargetProfileCMakeArguments(profile);
    CNA_STUDIO_EXPECT(has(arguments, "-DCNA_ENABLE_VIDEO=AUTO"));
    CNA_STUDIO_EXPECT(!has(arguments, "-DCNA_ENABLE_VIDEO=ON"));

    profile.setFeature("video", false);
    const std::vector<std::string> withoutVideo = studioTargetProfileCMakeArguments(profile);
    CNA_STUDIO_EXPECT(has(withoutVideo, "-DCNA_ENABLE_VIDEO=OFF"));

    // Every other feature really is a boolean, and turning one on must still say ON.
    for (const StudioFeatureOption& feature : getKnownStudioFeatures())
    {
        if (feature.name == "video") { continue; }
        CNA_STUDIO_EXPECT_EQ(std::string{feature.enabledValue}, std::string{"ON"});
    }
}

CNA_STUDIO_TEST(AProfileRoundTripsThroughJson)
{
    StudioTargetProfile profile;
    profile.name = "Windows 32-bit";
    profile.os = StudioTargetOs::Windows;
    profile.architecture = StudioArchitecture::X86;
    profile.platform = "sdl2";
    profile.renderer = "directx9";
    profile.configuration = StudioBuildConfiguration::MinSizeRel;
    profile.setFeature("cnaext", true);

    const StudioTargetProfile restored = studioTargetProfileFromJson(
        studioTargetProfileToJson(profile));

    CNA_STUDIO_EXPECT_EQ(restored.name, profile.name);
    CNA_STUDIO_EXPECT(restored.os == profile.os);
    CNA_STUDIO_EXPECT(restored.architecture == profile.architecture);
    CNA_STUDIO_EXPECT_EQ(restored.platform, profile.platform);
    CNA_STUDIO_EXPECT_EQ(restored.renderer, profile.renderer);
    CNA_STUDIO_EXPECT(restored.configuration == profile.configuration);
    CNA_STUDIO_EXPECT(restored.hasFeature("cnaext"));
}

CNA_STUDIO_TEST(AnAxisThisBuildDoesNotRecogniseKeepsItsDefaultRatherThanFailingTheLoad)
{
    JsonValue value = JsonValue::makeObject();
    value.set("name", std::string{"From the future"});
    value.set("os", std::string{"plan9"});
    value.set("architecture", std::string{"risc-v"});
    value.set("configuration", std::string{"Fastest"});

    const StudioTargetProfile profile = studioTargetProfileFromJson(value);
    CNA_STUDIO_EXPECT_EQ(profile.name, std::string{"From the future"});
    CNA_STUDIO_EXPECT(profile.os == StudioTargetProfile::defaults().os);
    CNA_STUDIO_EXPECT(profile.architecture == StudioTargetProfile::defaults().architecture);
    CNA_STUDIO_EXPECT(profile.configuration == StudioTargetProfile::defaults().configuration);
}

// ------------------------------------------------------------------------------------------------
// The project carries them
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(AProjectWithNoProfilesMigratesItsOneRendererIntoOne)
{
    // Every `.cnaproject` the prototype wrote is this case.
    JsonValue json = JsonValue::makeObject();
    json.set("formatVersion", JsonValue{Project::kFormatVersion});
    json.set("name", JsonValue{"OldGame"});
    json.set("defaultGraphicsBackend", JsonValue{"vulkan"});

    Project project;
    const ProjectLoadResult result = project.loadFromJson(json);
    CNA_STUDIO_EXPECT(result.succeeded);
    CNA_STUDIO_EXPECT_EQ(project.getTargetProfiles().size(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(project.getActiveTargetProfile().renderer, std::string{"vulkan"});

    bool explained = false;
    for (const std::string& warning : result.warnings)
    {
        if (warning.find("target profiles") != std::string::npos) { explained = true; }
    }
    CNA_STUDIO_EXPECT(explained);
}

CNA_STUDIO_TEST(ProjectProfilesRoundTripAndKeepTheLegacyFieldInStep)
{
    Project project;
    StudioTargetProfile desktop = StudioTargetProfile::defaults();
    desktop.name = "Linux Desktop";
    desktop.renderer = "opengles3";

    StudioTargetProfile windows = StudioTargetProfile::defaults();
    windows.name = "Windows";
    windows.os = StudioTargetOs::Windows;
    windows.renderer = "directx11";

    CNA_STUDIO_EXPECT(project.setTargetProfiles({desktop, windows}));
    CNA_STUDIO_EXPECT(project.setActiveTargetProfileIndex(1));

    // `defaultGraphicsBackend` is a serialized contract the player's discovery depends on, so it
    // follows the active profile rather than becoming a second place the renderer is decided.
    CNA_STUDIO_EXPECT_EQ(project.getDefaultGraphicsBackend(), std::string{"directx11"});

    Project reloaded;
    const ProjectLoadResult result = reloaded.loadFromJson(project.toJson());
    CNA_STUDIO_EXPECT(result.succeeded);
    CNA_STUDIO_EXPECT_EQ(reloaded.getTargetProfiles().size(), std::size_t{2});
    CNA_STUDIO_EXPECT_EQ(reloaded.getActiveTargetProfileIndex(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(reloaded.getActiveTargetProfile().name, std::string{"Windows"});
}

CNA_STUDIO_TEST(AProjectRefusesToHaveNoBuildTargetAtAll)
{
    Project project;
    CNA_STUDIO_EXPECT(!project.setTargetProfiles({}));
    CNA_STUDIO_EXPECT(!project.getTargetProfiles().empty());

    CNA_STUDIO_EXPECT(!project.setActiveTargetProfileIndex(99));
    CNA_STUDIO_EXPECT_EQ(project.getActiveTargetProfileIndex(), std::size_t{0});
}

CNA_STUDIO_TEST(AnOutOfRangeActiveProfileFallsBackAndSaysSo)
{
    JsonValue json = JsonValue::makeObject();
    json.set("formatVersion", JsonValue{Project::kFormatVersion});
    json.set("name", JsonValue{"Game"});
    JsonValue profiles = JsonValue::makeArray();
    profiles.append(studioTargetProfileToJson(StudioTargetProfile::defaults()));
    json.set("targetProfiles", std::move(profiles));
    json.set("activeTargetProfile", 7);

    Project project;
    const ProjectLoadResult result = project.loadFromJson(json);
    CNA_STUDIO_EXPECT(result.succeeded);
    CNA_STUDIO_EXPECT_EQ(project.getActiveTargetProfileIndex(), std::size_t{0});

    bool reported = false;
    for (const std::string& warning : result.warnings)
    {
        if (warning.find("activeTargetProfile") != std::string::npos) { reported = true; }
    }
    CNA_STUDIO_EXPECT(reported);
}

// ------------------------------------------------------------------------------------------------
// The transcription, checked against CNA itself (STUDIO-29007)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(EveryGatedRendererNameIsARendererCnaActuallyHas)
{
    // The cheap half, and it runs everywhere: a typo in the gate table would silently gate nothing.
    for (const RendererInfo& renderer : getKnownRenderers())
    {
        // Every renderer must answer the question on every system, one way or the other.
        for (int i = 0; i < static_cast<int>(StudioTargetOs::Count); ++i)
        {
            (void) isRendererAvailableOn(renderer.commandLineName, static_cast<StudioTargetOs>(i));
        }
    }

    // And each name Studio gates must be one CNA has, or the gate is protecting nothing.
    for (const char* gated : {"directx11", "directx12", "directx9", "direct2d", "glide", "gdi",
                              "canvas", "html_dom", "svg_dom", "webgl1", "webgl2",
                              "opengles2", "opengles3", "opengl33", "magnum"})
    {
        if (findRenderer(gated) == nullptr)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                std::string{"the renderer gate table names '"} + gated
                + "', which is not a renderer Studio's catalogue has");
        }
        CNA_STUDIO_EXPECT(findRenderer(gated) != nullptr);
    }
}

#if defined(CNA_STUDIO_TEST_CNA_ROOT)
CNA_STUDIO_TEST(TheRendererGateTranscriptionStillMatchesCnasOwnConfigureRules)
{
    // CNA states these as CMake conditions and exposes them nowhere a program can ask (gap G-08),
    // so Studio holds a transcription -- and a transcription with no check is a copy that drifts.
    // This reads CNA's own file and fails when a renderer CNA gates is one Studio does not.
    const std::filesystem::path selection =
        std::filesystem::path{CNA_STUDIO_TEST_CNA_ROOT} / "cmake" / "RendererSelection.cmake";

    std::ifstream stream{selection};
    CNA_STUDIO_EXPECT(stream.is_open());
    if (!stream.is_open()) { return; }

    const std::string text{std::istreambuf_iterator<char>{stream},
                           std::istreambuf_iterator<char>{}};

    // The Windows gate: CNA lists its renderers in one condition ending in
    // `AND NOT CMAKE_SYSTEM_NAME STREQUAL "Windows"`.
    const std::size_t windowsGate = text.find("renderer only builds when targeting Windows");
    CNA_STUDIO_EXPECT(windowsGate != std::string::npos);

    const std::size_t conditionStart = text.rfind("if((CNA_GRAPHICS_RENDERER", windowsGate);
    CNA_STUDIO_EXPECT(conditionStart != std::string::npos);
    if (conditionStart == std::string::npos) { return; }

    const std::string condition = text.substr(conditionStart, windowsGate - conditionStart);

    // Every renderer named in that condition must be one Studio refuses outside Windows.
    std::size_t offset = 0;
    std::size_t checked = 0;
    while (true)
    {
        // Matched on the full `CNA_GRAPHICS_RENDERER STREQUAL "..."`, not on `STREQUAL "..."`
        // alone: the same condition ends with `CMAKE_SYSTEM_NAME STREQUAL "Windows"`, and a looser
        // parse reads that as a renderer called "windows" and reports a drift that is its own.
        constexpr std::string_view kNeedle = "CNA_GRAPHICS_RENDERER STREQUAL \"";
        const std::size_t quote = condition.find(kNeedle, offset);
        if (quote == std::string::npos) { break; }
        const std::size_t begin = quote + kNeedle.size();
        const std::size_t end = condition.find('"', begin);
        if (end == std::string::npos) { break; }

        std::string name = condition.substr(begin, end - begin);
        for (char& character : name)
        {
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        }
        offset = end + 1;

        if (isRendererAvailableOn(name, StudioTargetOs::Linux))
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "CNA refuses to build '" + name + "' anywhere but Windows, and Studio's target "
                "profile still offers it on Linux. The gate table in TargetProfile.cpp is out of "
                "date with " + selection.string());
        }
        CNA_STUDIO_EXPECT(!isRendererAvailableOn(name, StudioTargetOs::Linux));
        CNA_STUDIO_EXPECT(isRendererAvailableOn(name, StudioTargetOs::Windows));
        ++checked;
    }

    // If the parse found nothing, the test proved nothing -- which is the failure this whole file
    // is written to avoid.
    CNA_STUDIO_EXPECT(checked >= 10);
}
#endif

// ------------------------------------------------------------------------------------------------
// The build runner takes its orders from the profile (STUDIO-17003)
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(TheBuildRequestComesFromTheActiveProfileAndSetsTheVariablesCnaHasNow)
{
    Project project;
    StudioTargetProfile profile = StudioTargetProfile::defaults();
    profile.name = "Vulkan";
    profile.renderer = "vulkan";
    profile.platform = "sdl3";
    profile.configuration = StudioBuildConfiguration::Debug;
    profile.setFeature("net", true);
    CNA_STUDIO_EXPECT(project.setTargetProfiles({profile}));

    const BuildRequest request = makeBuildRequestFromActiveProfile(project);
    CNA_STUDIO_EXPECT_EQ(request.graphicsBackend, std::string{"VULKAN"});
    CNA_STUDIO_EXPECT_EQ(request.platform, std::string{"SDL3"});
    CNA_STUDIO_EXPECT_EQ(request.configuration, std::string{"Debug"});
    CNA_STUDIO_EXPECT(!request.extraDefinitions.empty());
    CNA_STUDIO_EXPECT(has(request.extraDefinitions, "-DCNA_ENABLE_NET=ON"));

    // The renderer reaches the configure command under the name current CNA actually defines.
    // Studio passed CNA_GRAPHICS_BACKEND, which CNA does not define at all, so every game it
    // configured silently took CNA's default renderer -- and the build succeeded, which is what
    // let it survive.
    // A real directory with a real CMakeLists.txt: planBuild refuses to plan a build for a project
    // that has neither, which is the right behaviour and means the test has to supply both.
    const std::filesystem::path root = std::filesystem::temp_directory_path()
                                     / "cna-studio-profile-build-test";
    std::filesystem::create_directories(root);
    {
        std::ofstream lists{root / "CMakeLists.txt"};
        lists << "cmake_minimum_required(VERSION 3.20)\nproject(Sample)\n";
    }

    BuildRequest planned = makeBuildRequestFromActiveProfile(project);
    planned.projectRoot = root.string();
    planned.cmakePath = findCMake();

    std::string configure;
    for (const BuildStep& step : planBuild(planned))
    {
        if (step.description != "Configure") { continue; }
        for (const std::string& argument : step.arguments) { configure += argument + " "; }
    }
    CNA_STUDIO_EXPECT(configure.find("-DCNA_GRAPHICS_RENDERER=VULKAN") != std::string::npos);
    CNA_STUDIO_EXPECT(configure.find("-DCNA_PLATFORM=SDL3") != std::string::npos);
    CNA_STUDIO_EXPECT(configure.find("-DCNA_GRAPHICS_BACKEND=") == std::string::npos);

    std::error_code errorCode;
    std::filesystem::remove_all(root, errorCode);
}

CNA_STUDIO_TEST(ARendererWrittenInCnasOwnSpellingIsNormalisedWithoutComplaint)
{
    // Every project that predates target profiles carries CNA's upper-case identity in
    // `defaultGraphicsBackend`, and that string becomes the migrated profile's renderer. Leaving
    // it as written made every renderer comparison in Studio a case-insensitive one -- a rule that
    // holds until the one place that forgets it, which is where it surfaced: a Build panel whose
    // renderer control was blank, because "OPENGLES3" matched nothing in a list of lower-case
    // names. Nothing about the target changes here, only how it is written down, so it is
    // normalised silently rather than reported as a migration.
    StudioTargetProfile profile = StudioTargetProfile::defaults();
    profile.os = StudioTargetOs::Linux;
    profile.renderer = "OPENGLES3";
    profile.platform = "SDL3";

    const StudioProfileValidation validation = validateStudioTargetProfile(profile);

    CNA_STUDIO_EXPECT_EQ(profile.renderer, std::string{"opengles3"});
    CNA_STUDIO_EXPECT_EQ(profile.platform, std::string{"sdl3"});
    CNA_STUDIO_EXPECT(validation.isClean());
}

CNA_STUDIO_TEST(AMigratedLegacyRendererComesBackInTheCatalogueSpelling)
{
    // Migration and normalisation compose: the alias table names its replacement in whatever case
    // it was written, and what lands on the profile has to be the one spelling everything else
    // compares against.
    StudioTargetProfile profile = StudioTargetProfile::defaults();
    profile.renderer = "EASYGL";

    const StudioProfileValidation validation = validateStudioTargetProfile(profile);

    CNA_STUDIO_EXPECT(!validation.isClean());
    CNA_STUDIO_EXPECT(findRenderer(profile.renderer) != nullptr);
    CNA_STUDIO_EXPECT_EQ(profile.renderer,
                         std::string{findRenderer(profile.renderer)->commandLineName});
}
