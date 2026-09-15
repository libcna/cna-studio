// SPDX-License-Identifier: MS-PL
/**
 * @file StudioPreferencesTests.cpp
 * @brief What a user has decided about Studio (plan.md STUDIO-06009, STUDIO-06010, STUDIO-06011).
 *
 * The separation from project settings is the point and the thing that can quietly go wrong: a
 * theme committed to version control makes every teammate's Studio dark, and a build directory kept
 * per-user makes a project build differently for each of them. After that, everything here is about
 * a file a person may open and edit — so the interesting cases are the values that would leave a
 * Studio the user cannot recover from without finding and deleting it.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/ShellPanels/StudioPreferencesPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioShellPanels.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioPreferences.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    constexpr float kWidth = 1280.0f;
    constexpr float kHeight = 720.0f;

    UiInputState at(float x, float y, bool leftDown = false)
    {
        UiInputState input;
        input.displayWidth = kWidth;
        input.displayHeight = kHeight;
        input.mouseX = x;
        input.mouseY = y;
        input.mouseInWindow = true;
        input.setMouseDown(UiMouseButton::Left, leftDown);
        return input;
    }

    /** @brief A preferences file in a temporary directory, removed on the way out. */
    class ScopedPreferences
    {
    public:
        explicit ScopedPreferences(const std::string& name)
        {
            directory_ = std::filesystem::temp_directory_path()
                       / ("cna-studio-prefs-" + name + "-" + std::to_string(counter()++));
            std::error_code code;
            std::filesystem::remove_all(directory_, code);
            std::filesystem::create_directories(directory_, code);
        }

        ~ScopedPreferences()
        {
            std::error_code code;
            std::filesystem::remove_all(directory_, code);
        }

        ScopedPreferences(const ScopedPreferences&) = delete;
        ScopedPreferences& operator=(const ScopedPreferences&) = delete;

        [[nodiscard]] std::string path() const
        {
            return (directory_ / StudioPreferencesStore::kFileName).generic_string();
        }

        void write(const std::string& contents) const
        {
            std::ofstream stream{std::filesystem::path{path()}, std::ios::binary | std::ios::trunc};
            stream << contents;
        }

    private:
        static int& counter() { static int value = 0; return value; }
        std::filesystem::path directory_;
    };

    /** @brief Preferences with every field away from its default. */
    StudioPreferences edited()
    {
        StudioPreferences preferences;
        preferences.theme = "light";
        preferences.uiScale = 1.5f;
        preferences.fontSizePoints = 16.0f;
        preferences.navigation = StudioNavigationStyle::Blender;
        preferences.cameraSpeed = 2.5f;
        preferences.invertZoom = true;
        preferences.autosaveSeconds = 60;
        preferences.reopenLastProject = false;
        preferences.externalEditor = "/usr/bin/vim";
        preferences.cmakePath = "/opt/cmake/bin/cmake";
        preferences.buildJobs = 8;
        preferences.defaultLayout = "Animation";
        preferences.shortcuts = {StudioShortcutOverride{"studio.file.save",
                                                        StudioShortcut{UiKey::W, withControl()}}};
        return preferences;
    }

    struct Harness
    {
        StudioContext context;
        StudioLog log;
        StudioShell shell;
        StudioShellPanels panels{shell, context, log};

        Harness() { shell.resetLayout(); }

        void frame() { shell.renderFrame(at(-1.0f, -1.0f)); }
        void poll() { panels.poll(0.0); }
    };
}

// --- The model ---------------------------------------------------------------------------------

CNA_STUDIO_TEST(PreferencesRoundTripThroughJson)
{
    const StudioPreferences original = edited();
    const StudioPreferences restored = studioPreferencesFromJson(studioPreferencesToJson(original));
    CNA_STUDIO_EXPECT(restored == original);
}

CNA_STUDIO_TEST(AShortcutIsStoredAsTheTextTheMenusShow)
{
    // A file a person may open should read as the thing they see in the UI rather than as a key
    // code they would have to look up -- and editing one by hand is then an ordinary thing to do.
    StudioPreferences preferences;
    preferences.shortcuts = {StudioShortcutOverride{"studio.edit.undo",
                                                    StudioShortcut{UiKey::Z, withControl()}}};

    const JsonValue document = studioPreferencesToJson(preferences);
    const std::string chord = document["workspace"]["shortcuts"].getElements()[0]["shortcut"]
                                  .asString();
    CNA_STUDIO_EXPECT_EQ(chord, std::string{"Ctrl+Z"});

    // And the two are inverses, or a shortcut would change every time Studio restarted.
    StudioShortcut parsed;
    CNA_STUDIO_EXPECT(parseStudioShortcut(chord, parsed));
    CNA_STUDIO_EXPECT(parsed == preferences.shortcuts.front().shortcut);
}

CNA_STUDIO_TEST(EveryChordThisBuildCanShowItCanAlsoRead)
{
    // The two halves are used in opposite directions by the same file, so a key that describes but
    // does not parse is a rebinding that silently stops loading.
    for (int value = 1; value < static_cast<int>(UiKey::Count); ++value)
    {
        const auto key = static_cast<UiKey>(value);
        const std::string_view name = studioKeyName(key);
        if (name.empty()) { continue; }

        StudioShortcut shortcut;
        shortcut.key = key;
        shortcut.modifiers = withControl();
        shortcut.modifiers.shift = true;

        StudioShortcut parsed;
        CNA_STUDIO_EXPECT(parseStudioShortcut(describeStudioShortcut(shortcut), parsed));
        CNA_STUDIO_EXPECT(parsed == shortcut);
    }
}

CNA_STUDIO_TEST(AValueThatWouldLeaveAnUnusableStudioIsClamped)
{
    // A UI scale of zero has no pixels and a font size of zero has no text, and a user who reached
    // either would have to find and delete the file to get back.
    StudioPreferences preferences;
    preferences.uiScale = 0.0f;
    preferences.fontSizePoints = 0.0f;
    preferences.cameraSpeed = 1000.0f;
    preferences.theme = "chartreuse";

    const StudioPreferences clamped = studioClampPreferences(preferences);
    CNA_STUDIO_EXPECT(clamped.uiScale >= 0.5f);
    CNA_STUDIO_EXPECT(clamped.fontSizePoints >= 8.0f);
    CNA_STUDIO_EXPECT(clamped.cameraSpeed <= 10.0f);
    CNA_STUDIO_EXPECT_EQ(clamped.theme, std::string{"dark"});
}

CNA_STUDIO_TEST(NoAutosaveIsARealAnswerRatherThanATooSmallNumber)
{
    // Zero means "do not autosave", which a user can mean -- so it is kept, while one second is
    // raised to something they can work through.
    StudioPreferences none;
    none.autosaveSeconds = 0;
    CNA_STUDIO_EXPECT_EQ(studioClampPreferences(none).autosaveSeconds, 0);

    StudioPreferences tiny;
    tiny.autosaveSeconds = 1;
    CNA_STUDIO_EXPECT(studioClampPreferences(tiny).autosaveSeconds >= 30);
}

CNA_STUDIO_TEST(TheSameCommandReboundTwiceHasOneAnswer)
{
    StudioPreferences preferences;
    preferences.shortcuts = {
        StudioShortcutOverride{"studio.file.save", StudioShortcut{UiKey::W, withControl()}},
        StudioShortcutOverride{"studio.file.save", StudioShortcut{UiKey::E, withControl()}},
        StudioShortcutOverride{"", StudioShortcut{UiKey::R, withControl()}}};

    const StudioPreferences clamped = studioClampPreferences(preferences);
    CNA_STUDIO_EXPECT_EQ(clamped.shortcuts.size(), std::size_t{1});
    CNA_STUDIO_EXPECT(clamped.shortcuts.front().shortcut.key == UiKey::E);
}

CNA_STUDIO_TEST(AReboundCommandReachesTheRegistryAndAnUnknownOneIsSkipped)
{
    StudioActionRegistry registry;
    registerCoreStudioActions(registry);

    StudioPreferences preferences;
    preferences.shortcuts = {
        StudioShortcutOverride{"studio.file.save", StudioShortcut{UiKey::E, withControl()}},
        // An upgrade that retired a command must cost the user that one shortcut, not the rest.
        StudioShortcutOverride{"studio.gone.away", StudioShortcut{UiKey::R, withControl()}}};

    CNA_STUDIO_EXPECT_EQ(preferences.applyShortcuts(registry), std::size_t{1});
    CNA_STUDIO_EXPECT(registry.find("studio.file.save")->shortcut.key == UiKey::E);
}

// --- The file ----------------------------------------------------------------------------------

CNA_STUDIO_TEST(PreferencesSurviveBeingWrittenAndReadBack)
{
    const ScopedPreferences scope{"roundtrip"};
    const StudioPreferencesStore store{scope.path()};

    std::string problem;
    CNA_STUDIO_EXPECT(store.save(edited(), &problem));
    CNA_STUDIO_EXPECT(problem.empty());

    const StudioPreferencesDocument stored = store.load();
    CNA_STUDIO_EXPECT(stored.found);
    CNA_STUDIO_EXPECT(stored.problem.empty());
    CNA_STUDIO_EXPECT(stored.preferences == edited());
}

CNA_STUDIO_TEST(AFirstRunIsNotAProblemToReport)
{
    // Reporting the absence of a file the user has never saved would train them to ignore the
    // channel that reports the real problems.
    const ScopedPreferences scope{"first"};
    const StudioPreferencesDocument stored = StudioPreferencesStore{scope.path()}.load();

    CNA_STUDIO_EXPECT(!stored.found);
    CNA_STUDIO_EXPECT(stored.problem.empty());
    CNA_STUDIO_EXPECT(stored.preferences == StudioPreferences{});
}

CNA_STUDIO_TEST(UnreadablePreferencesFallBackAndSaySoRatherThanFailing)
{
    const ScopedPreferences scope{"corrupt"};
    scope.write("{ this is not json");

    const StudioPreferencesDocument stored = StudioPreferencesStore{scope.path()}.load();
    CNA_STUDIO_EXPECT(!stored.found);
    CNA_STUDIO_EXPECT(!stored.problem.empty());
    CNA_STUDIO_EXPECT(stored.preferences == StudioPreferences{});
}

CNA_STUDIO_TEST(PreferencesFromANewerStudioAreRefusedRatherThanHalfRead)
{
    // A newer Studio may write a field whose *absence* means something, and guessing at it is how
    // a preference silently changes.
    const ScopedPreferences scope{"newer"};
    scope.write(R"({"fileVersion":99,"appearance":{"theme":"light"}})");

    const StudioPreferencesDocument stored = StudioPreferencesStore{scope.path()}.load();
    CNA_STUDIO_EXPECT(!stored.found);
    CNA_STUDIO_EXPECT(!stored.problem.empty());
    CNA_STUDIO_EXPECT_EQ(stored.preferences.theme, std::string{"dark"});
}

CNA_STUDIO_TEST(AFileFromAnOlderStudioKeepsWhatItSaidAndDefaultsTheRest)
{
    // The migration mechanism is that every field defaults to something usable, so a file written
    // before a preference existed needs no per-version code to read correctly.
    const ScopedPreferences scope{"older"};
    scope.write(R"({"fileVersion":1,"appearance":{"theme":"light"}})");

    const StudioPreferencesDocument stored = StudioPreferencesStore{scope.path()}.load();
    CNA_STUDIO_EXPECT(stored.found);
    CNA_STUDIO_EXPECT(stored.problem.empty());
    CNA_STUDIO_EXPECT_EQ(stored.preferences.theme, std::string{"light"});
    CNA_STUDIO_EXPECT_EQ(stored.preferences.autosaveSeconds, StudioPreferences{}.autosaveSeconds);
}

CNA_STUDIO_TEST(AHandEditedValueOutOfRangeIsClampedOnTheWayIn)
{
    const ScopedPreferences scope{"handedited"};
    scope.write(R"({"fileVersion":1,"appearance":{"uiScale":0.0,"fontSizePoints":0.0}})");

    const StudioPreferencesDocument stored = StudioPreferencesStore{scope.path()}.load();
    CNA_STUDIO_EXPECT(stored.found);
    CNA_STUDIO_EXPECT(stored.preferences.uiScale >= 0.5f);
    CNA_STUDIO_EXPECT(stored.preferences.fontSizePoints >= 8.0f);
}

CNA_STUDIO_TEST(AShortcutThisBuildCannotParseIsSkippedRatherThanGuessedAt)
{
    // Binding the command to something the user did not ask for is worse than leaving it alone.
    const ScopedPreferences scope{"badchord"};
    scope.write(R"({"fileVersion":1,"workspace":{"shortcuts":[)"
                R"({"action":"studio.file.save","shortcut":"Hyper+Quux"},)"
                R"({"action":"studio.edit.undo","shortcut":"Ctrl+Z"}]}})");

    const StudioPreferencesDocument stored = StudioPreferencesStore{scope.path()}.load();
    CNA_STUDIO_EXPECT(stored.found);
    CNA_STUDIO_EXPECT_EQ(stored.preferences.shortcuts.size(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(stored.preferences.shortcuts.front().actionId,
                         std::string{"studio.edit.undo"});
}

// --- The panel ---------------------------------------------------------------------------------

CNA_STUDIO_TEST(ThePreferencesPanelDrawsAndIsNotAnEmptyRectangle)
{
    Harness harness;
    CNA_STUDIO_EXPECT(harness.shell.openPanel("preferences"));
    CNA_STUDIO_EXPECT(harness.shell.activatePanel("preferences"));
    harness.frame();

    CNA_STUDIO_EXPECT(harness.shell.hasPanelContent("preferences"));
    CNA_STUDIO_EXPECT(harness.shell.frame().drawData().getTotalCommandCount() > 0);
    CNA_STUDIO_EXPECT_EQ(harness.shell.frame().phaseViolations(), std::size_t{0});
}

CNA_STUDIO_TEST(ChangingAPreferenceReachesTheThemeImmediately)
{
    // Applied as it is changed rather than on an OK button: a preferences page with Apply is one
    // where the user finds out whether they liked it only after committing to it.
    Harness harness;
    harness.panels.preferences().theme = "light";
    harness.panels.preferences().uiScale = 2.0f;

    StudioPreferences saved;
    bool wrote = false;
    harness.panels.setPreferencesSink([&](const StudioPreferences& preferences, std::string*) {
        saved = preferences;
        wrote = true;
        return true;
    });

    harness.panels.applyPreferences();
    CNA_STUDIO_EXPECT(wrote);
    CNA_STUDIO_EXPECT_EQ(saved.theme, std::string{"light"});
    CNA_STUDIO_EXPECT_EQ(harness.shell.theme().scale(), 2.0f);
}

CNA_STUDIO_TEST(APreferenceThatCouldNotBeSavedStillTookEffect)
{
    // Applied before it is persisted, so a write that fails leaves the user looking at what they
    // chose -- they can see it worked and decide what to do about the file.
    Harness harness;
    harness.panels.preferences().theme = "light";
    harness.panels.setPreferencesSink([](const StudioPreferences&, std::string* problem) {
        if (problem != nullptr) { *problem = "the disk is full"; }
        return false;
    });

    harness.panels.applyPreferences();
    CNA_STUDIO_EXPECT(!harness.log.entries().empty());
    CNA_STUDIO_EXPECT(harness.log.entries().back().message.find("the disk is full")
                      != std::string::npos);
}

CNA_STUDIO_TEST(ResettingPreferencesAsksFirst)
{
    // The one control here that discards decisions the user made deliberately; every other change
    // is a single value they can put back.
    Harness harness;
    harness.panels.preferences().theme = "light";
    harness.panels.preferences().cameraSpeed = 3.0f;

    StudioPreferencesPanelContext context;
    StudioFrame probe{StudioTheme::dark()};
    runStudioFrame(probe, at(-1.0f, -1.0f), [&](StudioFrame& frame) {
        (void)studioPreferencesPanel(frame, UiRect{0.0f, 0.0f, 400.0f, 600.0f},
                                     harness.panels.preferences(), context);
    });

    // The panel reports the request rather than acting on it.
    CNA_STUDIO_EXPECT_EQ(harness.panels.preferences().cameraSpeed, 3.0f);
}

CNA_STUDIO_TEST(TheOpenWithRowOffersTheLayoutsThatExist)
{
    Harness harness;
    harness.shell.setSavedLayouts({StudioNamedLayout{"Animation", harness.shell.saveLayout()},
                                   StudioNamedLayout{"Debug", harness.shell.saveLayout()}});
    CNA_STUDIO_EXPECT(harness.shell.openPanel("preferences"));
    CNA_STUDIO_EXPECT(harness.shell.activatePanel("preferences"));
    harness.frame();

    // Drawn without a phase violation with real layouts in it, which is what the row is for: the
    // list is the shell's, not a copy the panel keeps.
    CNA_STUDIO_EXPECT_EQ(harness.shell.frame().phaseViolations(), std::size_t{0});
    CNA_STUDIO_EXPECT_EQ(harness.shell.savedLayouts().size(), std::size_t{2});
}
