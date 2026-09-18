// SPDX-License-Identifier: MS-PL
/**
 * @file AssetImporterTests.cpp
 * @brief Adding a format is implementing an importer, not editing one (`plan.md` STUDIO-10002).
 *
 * The acceptance is "importers are isolated; a third-party library lives behind one, not in Studio
 * core", and both halves need a test rather than a paragraph. The first half is behavioural: a
 * caller can register an importer and it runs, without Studio knowing anything about it. The second
 * is structural, and it is the one that decays quietly — somebody needs a parser for one thing,
 * includes it where it is convenient, and a module that was CNA-free and dependency-free is not any
 * more. Nobody notices until the build breaks somewhere else.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Assets/AssetImporter.hpp"
#include "CNA/Studio/Assets/AssetImporters.hpp"

#include <filesystem>
#include "CNA/Studio/Core/Json.hpp"

#include <fstream>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    /** @brief An importer Studio has never heard of, which is the point. */
    class CountingImporter final : public StudioAssetImporter
    {
    public:
        CountingImporter(std::string importerId, AssetType type, int* calls)
            : id_(std::move(importerId)), type_(type), calls_(calls)
        {
        }

        [[nodiscard]] std::string_view id() const override { return id_; }
        [[nodiscard]] bool handles(AssetType type) const override { return type == type_; }

        [[nodiscard]] JsonValue gatherFacts(const std::string& absolutePath,
                                            const JsonValue& settings) const override
        {
            (void)absolutePath;
            (void)settings;
            if (calls_ != nullptr) { ++(*calls_); }

            // A different value every call, so that `applyImporterFacts` -- which writes only what
            // would actually change -- reports a change every time and the call count and the
            // return value stay the same assertion.
            JsonValue facts = JsonValue::makeObject();
            facts.set("calls", JsonValue{static_cast<double>(calls_ != nullptr ? *calls_ : 0)});
            return facts;
        }

    private:
        std::string id_;
        AssetType type_;
        int* calls_;
    };

    Uuid track(AssetDatabase& assets, const std::string& path, AssetType type)
    {
        AssetRecord record;
        record.id = Uuid::generate();
        record.sourcePath = path;
        record.type = type;
        const Uuid id = record.id;
        assets.add(std::move(record));
        return id;
    }
}

CNA_STUDIO_TEST(AnImporterStudioHasNeverHeardOfRunsLikeABuiltInOne)
{
    // The whole of the interface's job. Before this, importing was a chain of type comparisons in
    // the middle of the asset system: a new format meant editing that function, and a plugin could
    // not edit it at all -- which is why STUDIO-28003 waits on this task.
    AssetDatabase assets;
    const Uuid scene = track(assets, "Assets/Level.cnascene", AssetType::Scene);

    int calls = 0;
    StudioImporterRegistry registry;
    CNA_STUDIO_EXPECT(registry.add(
        std::make_unique<CountingImporter>("Test.SceneImporter", AssetType::Scene, &calls)));

    CNA_STUDIO_EXPECT_EQ(registry.getCount(), std::size_t{1});
    CNA_STUDIO_EXPECT(applyImporterFacts(assets, scene, registry));
    CNA_STUDIO_EXPECT_EQ(calls, 1);

    // And a type nothing claims is left alone rather than treated as a failure: a project holds
    // files Studio does not import, and a scan that reported each of them as a problem is a scan
    // nobody reads.
    const Uuid text = track(assets, "Assets/Notes.txt", AssetType::Unknown);
    CNA_STUDIO_EXPECT(!applyImporterFacts(assets, text, registry));
    CNA_STUDIO_EXPECT_EQ(calls, 1);

    // The whole-project pass goes through the same registry.
    CNA_STUDIO_EXPECT_EQ(applyImporterFacts(assets, registry), std::size_t{1});
    CNA_STUDIO_EXPECT_EQ(calls, 2);
}

CNA_STUDIO_TEST(TheRegistryIsOrderedAndRefusesADuplicateRatherThanPickingOne)
{
    // Two importers answering to one id is a build that behaves differently depending on which was
    // registered last, and a plugin colliding with a built-in should be told rather than quietly
    // winning. First claim wins for a *type*, in registration order, because an ambiguity resolved
    // by hash order behaves differently on another machine.
    int first = 0;
    int second = 0;

    StudioImporterRegistry registry;
    CNA_STUDIO_EXPECT(registry.add(
        std::make_unique<CountingImporter>("Test.First", AssetType::Texture2D, &first)));
    CNA_STUDIO_EXPECT(registry.add(
        std::make_unique<CountingImporter>("Test.Second", AssetType::Texture2D, &second)));

    // Same id twice is refused.
    CNA_STUDIO_EXPECT(!registry.add(
        std::make_unique<CountingImporter>("Test.First", AssetType::Model, &first)));
    CNA_STUDIO_EXPECT(!registry.add(nullptr));
    CNA_STUDIO_EXPECT_EQ(registry.getCount(), std::size_t{2});

    AssetDatabase assets;
    const Uuid texture = track(assets, "Assets/Crate.png", AssetType::Texture2D);
    CNA_STUDIO_EXPECT(applyImporterFacts(assets, texture, registry));
    CNA_STUDIO_EXPECT_EQ(first, 1);
    CNA_STUDIO_EXPECT_EQ(second, 0);

    CNA_STUDIO_EXPECT(registry.find("Test.Second") != nullptr);
    CNA_STUDIO_EXPECT(registry.find("Test.Missing") == nullptr);
    CNA_STUDIO_EXPECT_EQ(registry.getIds().size(), std::size_t{2});
    CNA_STUDIO_EXPECT_EQ(registry.getIds().front(), std::string{"Test.First"});
}

CNA_STUDIO_TEST(TheBuiltInImportersAreRegisteredRatherThanBranchedOn)
{
    // The built-ins went through the interface too, rather than staying a special case beside it --
    // a dispatch with one path for "ours" and another for "theirs" is two dispatches, and the
    // second one is the one that rots.
    const StudioImporterRegistry& builtin = getBuiltinAssetImporters();

    CNA_STUDIO_EXPECT(builtin.find(ImporterIds::kTexture) != nullptr);
    CNA_STUDIO_EXPECT(builtin.find(ImporterIds::kSpriteFont) != nullptr);
    CNA_STUDIO_EXPECT(builtin.find(ImporterIds::kModel) != nullptr);

    CNA_STUDIO_EXPECT(builtin.forType(AssetType::Texture2D) != nullptr);
    CNA_STUDIO_EXPECT(builtin.forType(AssetType::SpriteFont) != nullptr);
    CNA_STUDIO_EXPECT(builtin.forType(AssetType::Model) != nullptr);

    // Each claims exactly the type it is named for -- an importer that claimed two would make the
    // registration order decide behaviour nobody wrote down.
    const StudioAssetImporter* texture = builtin.find(ImporterIds::kTexture);
    if (texture != nullptr)
    {
        CNA_STUDIO_EXPECT(texture->handles(AssetType::Texture2D));
        CNA_STUDIO_EXPECT(!texture->handles(AssetType::Model));
        CNA_STUDIO_EXPECT(!texture->handles(AssetType::Scene));
    }

    // And the same registry is what the no-argument overload uses, so a test that exercises one is
    // exercising what the editor runs.
    CNA_STUDIO_EXPECT(&getBuiltinAssetImporters() == &builtin);
}

CNA_STUDIO_TEST(NoThirdPartyLibraryIsReachedFromOutsideTheFilesAllowedToReachIt)
{
    // The structural half of the acceptance, and the half that decays quietly: somebody needs a
    // parser for one thing, includes it where it is convenient, and a module that was
    // dependency-free is not any more. Nobody notices until it breaks somewhere else.
    //
    // So the allowlist is the rule, written down once. Each entry is a translation unit that owns
    // one library, which is the arrangement THIRD_PARTY_NOTICES.md describes: one include site, one
    // set of symbols, internal linkage.
    static const std::set<std::string> kMayInclude{
        "src/assets/ImageDecode.cpp",       // stb_image
        "src/assets/ModelImport.cpp",       // cgltf, through cgltf_prefixed.h
        "src/ui-core/StudioFontAtlas.cpp",  // stb_truetype
        "third_party/cgltf/cgltf_impl.cpp", // the one translation unit cgltf requires
    };

    static const std::vector<std::string> kThirdPartyHeaders{
        "stb_image.h", "stb_truetype.h", "cgltf.h", "cgltf_prefixed.h"};

    const std::filesystem::path root =
        std::filesystem::path{CNA_STUDIO_SOURCE_ROOT}.lexically_normal();

    for (const char* directory : {"src", "include"})
    {
        if (!std::filesystem::is_directory(root / directory)) { continue; }

        for (const std::filesystem::directory_entry& entry :
             std::filesystem::recursive_directory_iterator{root / directory})
        {
            if (!entry.is_regular_file()) { continue; }

            const std::string extension = entry.path().extension().string();
            if (extension != ".cpp" && extension != ".hpp") { continue; }

            const std::string relative =
                std::filesystem::relative(entry.path(), root).generic_string();
            if (kMayInclude.count(relative) != 0) { continue; }

            std::ifstream stream{entry.path(), std::ios::binary};
            std::string line;
            while (std::getline(stream, line))
            {
                if (line.find("#include") == std::string::npos) { continue; }

                for (const std::string& header : kThirdPartyHeaders)
                {
                    if (line.find(header) == std::string::npos) { continue; }

                    CnaStudioTest::reportFailure(
                        __FILE__, __LINE__,
                        relative + " includes '" + header
                            + "'. A third-party library lives behind one importer, not in Studio "
                              "core (plan.md STUDIO-10002); if this file genuinely owns that "
                              "library, say so in the allowlist in this test.");
                }
            }
        }
    }

    // The allowlist describes files that exist, or it is a rule about nothing.
    for (const std::string& allowed : kMayInclude)
    {
        CNA_STUDIO_EXPECT(std::filesystem::exists(root / allowed));
    }
}
