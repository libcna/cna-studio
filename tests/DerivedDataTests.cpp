// SPDX-License-Identifier: MS-PL
/**
 * @file DerivedDataTests.cpp
 * @brief A project holds only what a person authored (`plan.md` STUDIO-09015).
 *
 * The rule is easy to state and easy to break by accident: everything Studio can regenerate —
 * thumbnails, decoded pixels, scan indexes, logs — lives under the user's Studio state directory,
 * and never inside the project. Writing a cache beside the source is a one-line convenience at the
 * moment somebody needs somewhere to put it, and the cost arrives months later as a repository full
 * of generated files that conflict on every merge.
 *
 * So this drives a real project through a real session and fails on any file that turns up in it.
 * Prose in a header cannot do that; a `.gitignore` cannot either, because a rule enforced by a file
 * somebody can delete is one that will eventually be deleted by somebody who did not know it
 * existed.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/Assets/ThumbnailCache.hpp"
#include "CNA/Studio/Core/StudioJobs.hpp"
#include "CNA/Studio/Core/UserPaths.hpp"
#include "CNA/Studio/Project/DerivedData.hpp"

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    class ScopedProject
    {
    public:
        explicit ScopedProject(const std::string& name)
        {
            path_ = std::filesystem::temp_directory_path()
                  / ("cna-studio-derived-" + name + "-" + std::to_string(counter()++));
            std::error_code code;
            std::filesystem::remove_all(path_, code);
            std::filesystem::create_directories(path_ / "Assets", code);
        }

        ~ScopedProject()
        {
            std::error_code code;
            std::filesystem::remove_all(path_, code);
        }

        ScopedProject(const ScopedProject&) = delete;
        ScopedProject& operator=(const ScopedProject&) = delete;

        [[nodiscard]] std::string root() const { return path_.generic_string(); }

        [[nodiscard]] std::string projectFile() const
        {
            return (path_ / "Game.cnaproject").generic_string();
        }

        void write(const std::string& relative, const std::string& text) const
        {
            const std::filesystem::path file = path_ / relative;
            std::error_code code;
            std::filesystem::create_directories(file.parent_path(), code);
            std::ofstream stream{file, std::ios::binary};
            stream << text;
        }

        /** @brief Every file in the project, project-relative, in a stable order. */
        [[nodiscard]] std::set<std::string> files() const
        {
            std::set<std::string> found;
            for (const std::filesystem::directory_entry& entry :
                 std::filesystem::recursive_directory_iterator{path_})
            {
                if (!entry.is_regular_file()) { continue; }
                found.insert(std::filesystem::relative(entry.path(), path_).generic_string());
            }
            return found;
        }

    private:
        static int& counter() { static int value = 0; return value; }
        std::filesystem::path path_;
    };
}

CNA_STUDIO_TEST(DerivedDataLivesOutsideTheProjectByConstruction)
{
    // Not "is gitignored": *outside*. A rule that depends on a `.gitignore` entry is one a new
    // contributor breaks by cloning, opening the project and committing a cache directory -- the
    // diff is enormous, nobody reads it, and from then on the repository carries generated files.
    // There is nothing to ignore here, which is a stronger property than ignoring it well.
    ScopedProject project{"outside"};

    const std::string derived = studioDerivedDataDirectory(project.projectFile());
    CNA_STUDIO_EXPECT(!derived.empty());
    CNA_STUDIO_EXPECT(!studioPathIsInsideProject(derived, project.root()));

    // Under the user's Studio state, beside the other things that are the user's rather than the
    // project's -- the recovery snapshots and the asset shortcuts already live there.
    CNA_STUDIO_EXPECT(derived.find(getStudioStateDirectory()) == 0);

    // Two projects of the same name in different places do not share a directory, or opening one
    // would show the other's thumbnails.
    ScopedProject other{"outside"};
    CNA_STUDIO_EXPECT(studioDerivedDataDirectory(other.projectFile()) != derived);

    // And the same project asked twice is the same answer, or nothing would ever be found again.
    CNA_STUDIO_EXPECT_EQ(studioDerivedDataDirectory(project.projectFile()), derived);

    // Nowhere to put it is an empty answer rather than a guess at the current directory.
    CNA_STUDIO_EXPECT(studioDerivedDataDirectory("").empty());
}

CNA_STUDIO_TEST(BeingInsideAProjectIsAPathQuestionRatherThanAStringOne)
{
    // `/project-backup` is not inside `/project`, and a prefix comparison says it is. That is the
    // bug this helper exists to not have, and it is the kind that passes every test written with
    // tidy paths.
    CNA_STUDIO_EXPECT(studioPathIsInsideProject("/project/Assets/a.png", "/project"));
    CNA_STUDIO_EXPECT(studioPathIsInsideProject("/project", "/project"));
    CNA_STUDIO_EXPECT(studioPathIsInsideProject("/project/Assets/../Assets/a.png", "/project"));

    CNA_STUDIO_EXPECT(!studioPathIsInsideProject("/project-backup/a.png", "/project"));
    CNA_STUDIO_EXPECT(!studioPathIsInsideProject("/elsewhere/a.png", "/project"));
    CNA_STUDIO_EXPECT(!studioPathIsInsideProject("", "/project"));
    CNA_STUDIO_EXPECT(!studioPathIsInsideProject("/project/a.png", ""));
}

CNA_STUDIO_TEST(ASessionLeavesNothingInTheProjectButSidecars)
{
    // The gate. A scan, a thumbnail pass and a reimport all run against a real project directory,
    // and afterwards the only files in it are the ones a person put there plus the `.cnaasset`
    // sidecars -- which are authored data that happens to be written by a tool: they hold an
    // asset's stable id and its import settings, a scene references assets by that id, and losing
    // one breaks scenes.
    ScopedProject project{"session"};
    project.write("Game.cnaproject", "{\"version\":1}");
    project.write("Assets/Notes.txt", "hand written");
    project.write("Assets/Sub/More.txt", "also hand written");

    const std::set<std::string> authored = project.files();

    AssetDatabase assets;
    assets.setProjectRoot(project.root());
    CNA_STUDIO_EXPECT(assets.scan("Assets").succeeded);

    StudioJobSystem jobs{StudioJobMode::Immediate};
    StudioThumbnailCache thumbnails;

    std::vector<Uuid> everything;
    for (const AssetRecord* record : assets.getAll()) { everything.push_back(record->id); }
    thumbnails.setWanted(everything);
    thumbnails.pump(jobs, assets);
    jobs.waitForIdle();
    jobs.drain();
    jobs.drain();

    // Whatever appeared is either something the test authored, or a sidecar for one of them.
    for (const std::string& file : project.files())
    {
        if (authored.count(file) != 0) { continue; }

        const bool isSidecar = file.size() > 9 && file.rfind(".cnaasset") == file.size() - 9;
        if (!isSidecar)
        {
            CnaStudioTest::reportFailure(
                __FILE__, __LINE__,
                "'" + file
                    + "' appeared in the project directory. Everything Studio can regenerate "
                      "belongs under the user's Studio state (plan.md STUDIO-09015); only authored "
                      "data and its sidecars live beside the source.");
        }
        else
        {
            // A sidecar describes a file that is there, rather than lingering for one that is not.
            const std::string described = file.substr(0, file.size() - 9);
            CNA_STUDIO_EXPECT(authored.count(described) != 0);
        }
    }
}
