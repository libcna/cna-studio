// SPDX-License-Identifier: MS-PL
/**
 * @file ThirdPartyProvenanceTests.cpp
 * @brief Everything vendored is accounted for, and still what was accounted for (`plan.md`
 *        STUDIO-10012).
 *
 * The rule is that a dependency arrives with its provenance, licence, version and reason recorded
 * *before* it is added. A rule like that decays the ordinary way: somebody drops a header into
 * `third_party/` to get a build working, means to write it up, and does not. By the time anybody
 * audits the tree the file has been there for months and nobody remembers where it came from.
 *
 * So the record is a gate. `third_party/PROVENANCE.tsv` lists every file with its origin and its
 * hash; this walks the directory and fails on anything not listed, on any listed file that has
 * gone, and on any hash that no longer matches. "Verbatim copy of upstream" stops being a sentence
 * in a notices file and becomes something that is checked.
 *
 * The hash is `CNA/Studio/Core/Sha256.hpp` — Studio's own, written out rather than depended on,
 * because a project whose rule is that every dependency arrives with a recorded reason should not
 * acquire one in order to check its dependencies. `Sha256Tests.cpp` holds it to the FIPS 180-4
 * vectors, which is what makes it trustworthy here.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Core/Sha256.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{

    /** @brief The repository root, baked in by CMake so the test does not guess at a working directory. */
    std::filesystem::path sourceRoot()
    {
        return std::filesystem::path{CNA_STUDIO_SOURCE_ROOT}.lexically_normal();
    }

    /** @brief One manifest row. */
    struct ProvenanceEntry
    {
        std::string sha256;
        std::string licence;
        std::string version;
        std::string origin;

        /** @brief Whether this file is this repository's own rather than a vendored copy. */
        [[nodiscard]] bool isOurs() const { return sha256 == "-"; }
    };

    std::map<std::string, ProvenanceEntry> readProvenance(std::string* outProblem)
    {
        std::map<std::string, ProvenanceEntry> entries;

        const std::filesystem::path path = sourceRoot() / "third_party" / "PROVENANCE.tsv";
        std::ifstream stream{path, std::ios::binary};
        if (!stream)
        {
            if (outProblem != nullptr) { *outProblem = "cannot read " + path.generic_string(); }
            return entries;
        }

        std::string line;
        while (std::getline(stream, line))
        {
            if (!line.empty() && line.back() == '\r') { line.pop_back(); }
            if (line.empty() || line.front() == '#') { continue; }

            std::vector<std::string> fields;
            std::istringstream row{line};
            std::string field;
            while (std::getline(row, field, '\t')) { fields.push_back(field); }

            if (fields.size() < 5)
            {
                if (outProblem != nullptr) { *outProblem = "malformed row: " + line; }
                continue;
            }

            entries[fields[0]] = ProvenanceEntry{fields[1], fields[2], fields[3], fields[4]};
        }
        return entries;
    }

    std::vector<unsigned char> readBytes(const std::filesystem::path& path)
    {
        std::ifstream stream{path, std::ios::binary};
        return std::vector<unsigned char>{std::istreambuf_iterator<char>{stream},
                                          std::istreambuf_iterator<char>{}};
    }
}

CNA_STUDIO_TEST(EveryVendoredFileIsAccountedForAndStillWhatWasAccountedFor)
{
    // The gate STUDIO-10012 is actually about. A dependency dropped into the tree to get a build
    // working, with the write-up meant for later, is how a project ends up unable to say what it
    // ships -- and the answer matters the day somebody asks whether a licence permits something.
    std::string problem;
    const std::map<std::string, ProvenanceEntry> recorded = readProvenance(&problem);

    if (!problem.empty()) { CnaStudioTest::reportFailure(__FILE__, __LINE__, problem); }
    CNA_STUDIO_EXPECT(!recorded.empty());

    const std::filesystem::path root = sourceRoot();
    const std::filesystem::path vendored = root / "third_party";
    CNA_STUDIO_EXPECT(std::filesystem::is_directory(vendored));

    std::set<std::string> seen;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::recursive_directory_iterator{vendored})
    {
        if (!entry.is_regular_file()) { continue; }

        const std::string relative =
            std::filesystem::relative(entry.path(), root).generic_string();

        // The manifest describes itself no more than a list describes the paper it is on.
        if (relative == "third_party/PROVENANCE.tsv") { continue; }

        seen.insert(relative);

        const auto found = recorded.find(relative);
        if (found == recorded.end())
        {
            CnaStudioTest::reportFailure(
                __FILE__, __LINE__,
                relative + " is vendored but not in third_party/PROVENANCE.tsv. A dependency is "
                           "recorded before it is added, not after somebody notices.");
            continue;
        }

        if (found->second.isOurs())
        {
            // Our own files change freely -- they are ours to change. What they must not do is
            // claim an upstream, because "this repository's own" is the licence statement.
            continue;
        }

        const std::string actual = CNA::Studio::studioSha256Hex(readBytes(entry.path()));
        if (actual != found->second.sha256)
        {
            CnaStudioTest::reportFailure(
                __FILE__, __LINE__,
                relative + " is recorded as " + found->second.sha256 + " and hashes to " + actual
                    + ". A vendored file that has drifted from its record is one nobody can say "
                      "the provenance of any more.");
        }
    }

    // And the other direction: a record for a file that has gone is a record nobody has read.
    for (const auto& [path, entry] : recorded)
    {
        (void)entry;
        if (seen.count(path) == 0)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                                         path + " is recorded in third_party/PROVENANCE.tsv and is "
                                                "not in the tree.");
        }
    }
}

CNA_STUDIO_TEST(EveryVendoredComponentCarriesALicenceAndIsWrittenUp)
{
    // The manifest is the machine-checkable half; THIRD_PARTY_NOTICES.md is where the *reasons*
    // live, and the two drift apart the moment only one of them is required. Reasons are what an
    // audit actually needs: "why is this here" is the question a licence file cannot answer.
    const std::filesystem::path root = sourceRoot();

    std::ifstream noticesStream{root / "THIRD_PARTY_NOTICES.md", std::ios::binary};
    CNA_STUDIO_EXPECT(noticesStream.good());
    std::ostringstream buffer;
    buffer << noticesStream.rdbuf();
    const std::string notices = buffer.str();

    const std::map<std::string, ProvenanceEntry> recorded = readProvenance(nullptr);

    std::set<std::string> components;
    for (const auto& [path, entry] : recorded)
    {
        (void)entry;
        const std::filesystem::path relative{path};
        components.insert(relative.parent_path().generic_string());
    }

    CNA_STUDIO_EXPECT(!components.empty());

    for (const std::string& component : components)
    {
        // A licence file beside the code, so a reader of the directory does not have to go and
        // find the notices to learn what they may do with it.
        bool hasLicence = false;
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator{root / component})
        {
            const std::string name = entry.path().filename().string();
            if (name.find("LICENSE") != std::string::npos
                || name.find("LICENCE") != std::string::npos
                || name.find("OFL") != std::string::npos)
            {
                hasLicence = true;
                break;
            }
        }

        if (!hasLicence)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                                         component + " vendors code with no licence file beside it.");
        }

        // And a section in the notices naming the directory, so the write-up and the tree cannot
        // drift into describing different sets of things.
        if (notices.find(component) == std::string::npos)
        {
            CnaStudioTest::reportFailure(
                __FILE__, __LINE__,
                component + " is vendored and THIRD_PARTY_NOTICES.md does not mention it.");
        }
    }
}
