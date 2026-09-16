// SPDX-License-Identifier: MS-PL
/**
 * @file StudioAssetReloadTests.cpp
 * @brief Noticing an asset edited outside Studio, and the guard that keeps both UIs noticing.
 *
 * `plan.md` STUDIO-07051.
 *
 * The change is not the interesting part — `AssetWatcher` had always reported it correctly, and its
 * own tests had always passed. What was missing was a *caller* on the native shell, so on the UI
 * that is the default a texture edited in another program was never noticed: the editor went on
 * drawing the art from before the edit, the mesh cache kept the old model, and a running game was
 * never told.
 *
 * That is the third time this exact shape has appeared in one phase (`--scene`, plugins, this), and
 * the last test in this file is about the shape rather than about assets.
 */

#include "TestHarness.hpp"
#include "SourceScan.hpp"

#include "CNA/Studio/Assets/AssetWatcher.hpp"
#include "CNA/Studio/Core/Uuid.hpp"
#include "CNA/Studio/StudioAssetReload.hpp"
#include "CNA/Studio/StudioContext.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

using namespace CNA::Studio;

namespace
{
    std::filesystem::path makeScratchDirectory(const std::string& name)
    {
        const std::filesystem::path directory =
            std::filesystem::temp_directory_path()
            / ("cna-studio-tests-" + name + "-" + Uuid::generate().toString());
        std::filesystem::create_directories(directory);
        return directory;
    }

    void writeFile(const std::filesystem::path& path, std::string_view contents)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream{path, std::ios::binary | std::ios::trunc};
        stream << contents;
    }

    /** @brief A context with one tracked asset, and the directory it lives in. */
    struct Fixture
    {
        std::filesystem::path directory;
        StudioContext context;
        AssetWatcher watcher;
        std::vector<std::string> messages;

        explicit Fixture(const std::string& name) : directory(makeScratchDirectory(name))
        {
            context.setLogSink([this](LogSeverity, const std::string& message) {
                messages.push_back(message);
            });

            writeFile(directory / "Assets" / "player.txt", "before");
            context.getAssets().setProjectRoot(directory.generic_string());
            (void)context.getAssets().scan("Assets");

            // Every poll below asks for one explicitly, so the interval never decides whether a
            // test sees the change it just made -- which would be a test that passes on a fast
            // machine and fails on a loaded one.
            watcher.setInterval(0.0);
        }

        ~Fixture() { std::filesystem::remove_all(directory); }

        Fixture(const Fixture&) = delete;
        Fixture& operator=(const Fixture&) = delete;

        [[nodiscard]] Uuid onlyAsset() const
        {
            const std::vector<const AssetRecord*> all = context.getAssets().getAll();
            return all.empty() ? Uuid{} : all.front()->id;
        }

        [[nodiscard]] bool said(std::string_view fragment) const
        {
            for (const std::string& message : messages)
            {
                if (message.find(fragment) != std::string::npos) { return true; }
            }
            return false;
        }
    };
}

CNA_STUDIO_TEST(AnEditedAssetDropsEveryCacheThatWasHoldingTheOldOne)
{
    // Four caches, and the reason they are all here rather than each in its own place: reporting
    // the change without dropping them is the worst outcome available. The editor would tell the
    // user it had noticed the edit and go on drawing the art from before it, which is a bug report
    // about the renderer rather than about the reload.
    Fixture fixture{"asset-reload-changed"};
    const Uuid asset = fixture.onlyAsset();
    CNA_STUDIO_EXPECT(asset.isValid());

    std::vector<Uuid> rendered;
    std::vector<Uuid> toPlayer;

    StudioAssetReloadSinks sinks;
    sinks.invalidateRendered = [&rendered](const Uuid& id) { rendered.push_back(id); };
    sinks.reloadInPlayer = [&toPlayer](const Uuid& id) { toPlayer.push_back(id); };

    // A first poll takes the stamps as they are, so the edit below is a change rather than a
    // discovery -- which is the difference between "this file moved" and "this file exists".
    (void)studioPollAssetChanges(fixture.watcher, fixture.context, sinks, 1.0);
    rendered.clear();
    toPlayer.clear();
    fixture.messages.clear();

    writeFile(fixture.directory / "Assets" / "player.txt", "after the edit");

    const StudioAssetReloadResult result =
        studioPollAssetChanges(fixture.watcher, fixture.context, sinks, 1.0);

    CNA_STUDIO_EXPECT_EQ(result.changed, std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(rendered.size(), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(toPlayer.size(), std::size_t{1});
    CNA_STUDIO_EXPECT(rendered.front() == asset);
    CNA_STUDIO_EXPECT(toPlayer.front() == asset);

    // Named by its path, not its id. "Something reloaded" is not a sentence a user can check
    // against the file they just saved.
    CNA_STUDIO_EXPECT(fixture.said("player.txt"));
    CNA_STUDIO_EXPECT(fixture.said("external change"));
}

CNA_STUDIO_TEST(AnAssetWhoseFileHasGoneIsReportedAndNotSentToThePlayer)
{
    // Deliberately *not* forwarded to a running game. The file is gone, so there is nothing to
    // reload with -- and a player told to reload a missing asset would drop the copy it is
    // successfully drawing in exchange for nothing.
    Fixture fixture{"asset-reload-removed"};
    CNA_STUDIO_EXPECT(fixture.onlyAsset().isValid());

    std::vector<Uuid> toPlayer;
    StudioAssetReloadSinks sinks;
    sinks.reloadInPlayer = [&toPlayer](const Uuid& id) { toPlayer.push_back(id); };

    (void)studioPollAssetChanges(fixture.watcher, fixture.context, sinks, 1.0);
    fixture.messages.clear();

    std::filesystem::remove(fixture.directory / "Assets" / "player.txt");

    const StudioAssetReloadResult result =
        studioPollAssetChanges(fixture.watcher, fixture.context, sinks, 1.0);

    CNA_STUDIO_EXPECT_EQ(result.removed, std::size_t{1});
    CNA_STUDIO_EXPECT(toPlayer.empty());
    CNA_STUDIO_EXPECT(fixture.said("gone missing"));
    // And it says where to look, because the useful question is what broke rather than what moved.
    CNA_STUDIO_EXPECT(fixture.said("Missing References"));
}

CNA_STUDIO_TEST(AnAssetThatComesBackIsReportedAndReloadedLikeAnEdit)
{
    // A file restored from a backup, or a folder moved back. Treated as a change rather than as a
    // discovery: everything that dropped it has to be told to look again, and the record is the
    // same record it always was -- a scene still references it by the same id.
    Fixture fixture{"asset-reload-restored"};

    std::vector<Uuid> rendered;
    StudioAssetReloadSinks sinks;
    sinks.invalidateRendered = [&rendered](const Uuid& id) { rendered.push_back(id); };

    (void)studioPollAssetChanges(fixture.watcher, fixture.context, sinks, 1.0);
    std::filesystem::remove(fixture.directory / "Assets" / "player.txt");
    (void)studioPollAssetChanges(fixture.watcher, fixture.context, sinks, 1.0);
    rendered.clear();
    fixture.messages.clear();

    writeFile(fixture.directory / "Assets" / "player.txt", "back again");

    const StudioAssetReloadResult result =
        studioPollAssetChanges(fixture.watcher, fixture.context, sinks, 1.0);

    CNA_STUDIO_EXPECT_EQ(result.restored, std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(rendered.size(), std::size_t{1});
    CNA_STUDIO_EXPECT(fixture.said("is back"));
}

CNA_STUDIO_TEST(AHostWithNoViewportAndNoPlayerStillReloadsWhatItCan)
{
    // Both sinks empty is the headless preview, and every test. The mesh cache and the importer
    // facts are the context's own and are dropped either way -- a reload that did nothing without
    // a viewport would make every headless caller silently stale.
    Fixture fixture{"asset-reload-headless"};

    (void)studioPollAssetChanges(fixture.watcher, fixture.context, {}, 1.0);
    fixture.messages.clear();

    writeFile(fixture.directory / "Assets" / "player.txt", "after the edit");

    const StudioAssetReloadResult result =
        studioPollAssetChanges(fixture.watcher, fixture.context, {}, 1.0);

    CNA_STUDIO_EXPECT_EQ(result.changed, std::size_t{1});
    CNA_STUDIO_EXPECT(result.any());
    CNA_STUDIO_EXPECT(fixture.said("external change"));
}

CNA_STUDIO_TEST(APollWithNothingToReportDoesNotSaySo)
{
    // The log is read by a person. An editor that wrote "nothing changed" twice a second would
    // have an Output Log in which nothing else can be found.
    Fixture fixture{"asset-reload-quiet"};

    (void)studioPollAssetChanges(fixture.watcher, fixture.context, {}, 1.0);
    fixture.messages.clear();

    const StudioAssetReloadResult result =
        studioPollAssetChanges(fixture.watcher, fixture.context, {}, 1.0);

    CNA_STUDIO_EXPECT(!result.any());
    CNA_STUDIO_EXPECT(fixture.messages.empty());
}

// ------------------------------------------------------------------------------------------------
// The shape, rather than the asset
// ------------------------------------------------------------------------------------------------

CNA_STUDIO_TEST(EveryStartUpRoutineBothUisNeedIsCalledByBothOfThem)
{
    // Three times in one phase, the same shape: a thing the prototype did, that the native shell --
    // the *default* UI -- did not, where nothing failed because not doing it produces exactly what
    // having nothing to do produces. `--scene` opened the project's own startup scene. The Plugins
    // menu was empty. An edited texture went on being drawn from the version before the edit.
    //
    // Each was fixed by extracting the decision into one routine and calling it from both. This
    // asserts the second half, which is the half that goes stale: an extraction that only one
    // caller uses is a refactor, not a fix, and it looks identical in a diff.
    //
    // A hand-written list, and it is worth being honest about why that is acceptable here where it
    // was not for the flag guard. This is not an inventory of everything the two UIs must share --
    // no such list can be complete. It is the set of routines that were extracted *because* they
    // had gone out of step, and its job is to keep those three in step. A fourth that is added and
    // not listed is exactly as guarded as it was before this test existed.
    const std::vector<std::string> shared = {
        "openStudioStartupDocument",  // STUDIO-07053
        "studioLoadPlugins",          // STUDIO-07052
        "studioPollAssetChanges",     // STUDIO-07051
    };

    const std::vector<std::string> callers = {
        "src/app/StudioApplication.cpp",        // the Dear ImGui prototype
        "src/viewport/CnaStudioShellHost.cpp",  // the native shell, which is the default
    };

    const std::vector<CnaStudioTest::Scan::SourceFile> sources =
        CnaStudioTest::Scan::collectSources({"src"});
    CNA_STUDIO_EXPECT(!sources.empty());

    std::size_t checked = 0;
    for (const std::string& caller : callers)
    {
        std::string code;
        bool found = false;
        for (const CnaStudioTest::Scan::SourceFile& file : sources)
        {
            if (file.relativePath != caller) { continue; }
            code = CnaStudioTest::Scan::stripCommentsAndStrings(file.text);
            found = true;
        }

        if (!found)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                caller + " is not in the source tree. If it was renamed, rename it here too -- a "
                         "caller this guard cannot find is a caller it stops guarding.");
            continue;
        }

        for (const std::string& routine : shared)
        {
            ++checked;
            if (code.find(routine + "(") == std::string::npos)
            {
                CnaStudioTest::reportFailure(__FILE__, __LINE__,
                    caller + " does not call `" + routine
                        + "`, so that UI silently does without it. Both UIs call it, or it is not "
                          "a shared start-up routine.");
            }
        }
    }

    CNA_STUDIO_EXPECT_EQ(checked, shared.size() * callers.size());
}
