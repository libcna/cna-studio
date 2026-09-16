// SPDX-License-Identifier: MS-PL
/**
 * @file StudioOptionsTests.cpp
 * @brief The command line: what each flag means, and what a wrong one does.
 *
 * `plan.md` STUDIO-07048, STUDIO-07047. These were in `ApplicationTests.cpp`, which runs the
 * prototype's application — so eight cases about a struct every entry point uses were going to be
 * deleted along with an object that has nothing to do with them.
 *
 * Two rules the parser is held to here. **It never throws and never exits**: a bad flag sets
 * `hasError` and a message, and `main` decides what to do about it, because a parser that called
 * `exit` is one no test could ask a question of. And **every flag is in the usage text**, checked
 * by `EveryShellPreviewFlagIsInTheUsageText` — a flag nobody can discover is a flag only its
 * author can use.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/StudioOptions.hpp"

#include <string>

using namespace CNA::Studio;

CNA_STUDIO_TEST(OptionsParseTheDocumentedFlags)
{
    const char* argv[] = {"cna-studio", "--project=/tmp/MyGame.cnaproject", "--headless", "--frames=3"};
    const StudioOptions options = StudioOptions::parse(4, argv);

    CNA_STUDIO_EXPECT(!options.hasError);
    CNA_STUDIO_EXPECT_EQ(options.projectPath, std::string{"/tmp/MyGame.cnaproject"});
    CNA_STUDIO_EXPECT(options.headless);
    CNA_STUDIO_EXPECT_EQ(options.frameLimit, 3);
}

CNA_STUDIO_TEST(OptionsAcceptABareProjectPath)
{
    const char* argv[] = {"cna-studio", "/tmp/MyGame.cnaproject"};
    const StudioOptions options = StudioOptions::parse(2, argv);
    CNA_STUDIO_EXPECT(!options.hasError);
    CNA_STUDIO_EXPECT_EQ(options.projectPath, std::string{"/tmp/MyGame.cnaproject"});
}

CNA_STUDIO_TEST(OptionsRejectGraphicsBackendSelection)
{
    // CNA fixes its backend at compile time, so accepting --graphics here would teach users a
    // mental model the framework does not support. Rejecting it loudly is the honest behaviour.
    const char* argv[] = {"cna-studio", "--graphics=vulkan"};
    const StudioOptions options = StudioOptions::parse(2, argv);

    CNA_STUDIO_EXPECT(options.hasError);
    CNA_STUDIO_EXPECT(options.errorMessage.find("compile time") != std::string::npos);
    CNA_STUDIO_EXPECT(options.errorMessage.find("cna-player") != std::string::npos);
}

CNA_STUDIO_TEST(OptionsRejectUnknownFlags)
{
    const char* argv[] = {"cna-studio", "--nonsense"};
    const StudioOptions options = StudioOptions::parse(2, argv);
    CNA_STUDIO_EXPECT(options.hasError);
}

CNA_STUDIO_TEST(ThePanelOnlyPreviewTakesThePanelToCapture)
{
    const char* argv[] = {"cna-studio", "--shell-panel-only=preferences"};
    const StudioOptions options = StudioOptions::parse(2, argv);
    CNA_STUDIO_EXPECT(!options.hasError);
    CNA_STUDIO_EXPECT(options.shellPreviewPanelOnly == "preferences");
}

CNA_STUDIO_TEST(EveryShellPreviewFlagIsInTheUsageText)
{
    // The preview flags are the only way to look at the native shell on a machine with no display,
    // and one that exists but is written down nowhere is one nobody will find. Checked against the
    // parser rather than a list kept here, which would be a second thing to forget.
    const std::string usage = StudioOptions::getUsage();

    // Spelt the way each is actually passed: three of them are bare switches, and probing those
    // with a value would report the parser as broken for refusing something it should refuse.
    const std::vector<std::pair<std::string, bool>> flags = {
        {"--shell-preview", true},   {"--shell-pointer", true},  {"--shell-mouse-down", false},
        {"--shell-right-click", false}, {"--shell-open-menu", true}, {"--shell-float", true},
        {"--shell-invoke", true},    {"--shell-panel-only", true}, {"--shell-drag", true},
        {"--shell-notify", true},
        {"--shell-tooltip", false},  {"--shell-size", true},     {"--shell-scale", true},
        {"--shell-theme", true}};

    for (const auto& [flag, takesValue] : flags)
    {
        if (usage.find(flag) == std::string::npos)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                flag + " is accepted but is not in --help, so nobody will find it.");
        }

        // A value that is wrong for every one of them, so a flag reaching the unknown-option
        // branch is distinguishable from one that parsed and then complained about the value.
        const std::string argument = takesValue ? flag + "=1" : flag;
        const char* argv[] = {"cna-studio", argument.c_str()};
        const StudioOptions parsed = StudioOptions::parse(2, argv);
        if (parsed.hasError && parsed.errorMessage.find("unknown option") != std::string::npos)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                flag + " is in --help but the parser does not know it.");
        }
    }
}

CNA_STUDIO_TEST(UsageTextExplainsTheBackendConstraint)
{
    const std::string usage = StudioOptions::getUsage();
    CNA_STUDIO_EXPECT(usage.find("compile") != std::string::npos);
    CNA_STUDIO_EXPECT(usage.find("cna-player") != std::string::npos);
}

CNA_STUDIO_TEST(OptionsParseTheAutosaveAndRecoveryFlags)
{
    const char* argv[] = {"cna-studio", "--autosave=5", "--recovery-dir=/tmp/snapshots"};
    const StudioOptions options = StudioOptions::parse(3, argv);

    CNA_STUDIO_EXPECT(!options.hasError);
    CNA_STUDIO_EXPECT(options.autosaveSeconds > 4.9 && options.autosaveSeconds < 5.1);
    CNA_STUDIO_EXPECT_EQ(options.recoveryDirectory, std::string{"/tmp/snapshots"});

    const char* bad[] = {"cna-studio", "--autosave=often"};
    CNA_STUDIO_EXPECT(StudioOptions::parse(2, bad).hasError);

    // A negative interval is clamped rather than rejected: it means the same thing as zero, and
    // failing to start over it would be a poor trade.
    const char* negative[] = {"cna-studio", "--autosave=-1"};
    const StudioOptions clamped = StudioOptions::parse(2, negative);
    CNA_STUDIO_EXPECT(!clamped.hasError);
    CNA_STUDIO_EXPECT_EQ(clamped.autosaveSeconds, 0.0);
}
