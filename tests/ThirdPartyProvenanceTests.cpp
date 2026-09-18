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
 * SHA-256 is written out here rather than reached for. Studio's core has no cryptographic
 * dependency and is not acquiring one to check its dependencies, which would be funny in the wrong
 * way; the algorithm is specified in FIPS 180-4, it is sixty lines, and the known-answer test below
 * is what makes it trustworthy.
 */

#include "TestHarness.hpp"

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
    /** @brief FIPS 180-4 SHA-256 of @p bytes, as sixty-four lowercase hexadecimal digits. */
    std::string sha256Hex(const std::vector<unsigned char>& bytes)
    {
        static constexpr std::array<std::uint32_t, 64> kRoundConstants{
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
            0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
            0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
            0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
            0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
            0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
            0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
            0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
            0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

        std::array<std::uint32_t, 8> hash{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                          0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};

        std::vector<unsigned char> message = bytes;
        const std::uint64_t bitLength = static_cast<std::uint64_t>(bytes.size()) * 8u;

        message.push_back(0x80u);
        while (message.size() % 64u != 56u) { message.push_back(0x00u); }
        for (int shift = 56; shift >= 0; shift -= 8)
        {
            message.push_back(static_cast<unsigned char>((bitLength >> shift) & 0xFFu));
        }

        const auto rotateRight = [](std::uint32_t value, int by) {
            return (value >> by) | (value << (32 - by));
        };

        for (std::size_t block = 0; block < message.size(); block += 64u)
        {
            std::array<std::uint32_t, 64> schedule{};
            for (std::size_t i = 0; i < 16u; ++i)
            {
                schedule[i] = (static_cast<std::uint32_t>(message[block + i * 4u]) << 24)
                            | (static_cast<std::uint32_t>(message[block + i * 4u + 1u]) << 16)
                            | (static_cast<std::uint32_t>(message[block + i * 4u + 2u]) << 8)
                            | static_cast<std::uint32_t>(message[block + i * 4u + 3u]);
            }
            for (std::size_t i = 16u; i < 64u; ++i)
            {
                const std::uint32_t s0 = rotateRight(schedule[i - 15], 7)
                                       ^ rotateRight(schedule[i - 15], 18) ^ (schedule[i - 15] >> 3);
                const std::uint32_t s1 = rotateRight(schedule[i - 2], 17)
                                       ^ rotateRight(schedule[i - 2], 19) ^ (schedule[i - 2] >> 10);
                schedule[i] = schedule[i - 16] + s0 + schedule[i - 7] + s1;
            }

            std::array<std::uint32_t, 8> working = hash;
            for (std::size_t i = 0; i < 64u; ++i)
            {
                const std::uint32_t s1 = rotateRight(working[4], 6) ^ rotateRight(working[4], 11)
                                       ^ rotateRight(working[4], 25);
                const std::uint32_t choose =
                    (working[4] & working[5]) ^ (~working[4] & working[6]);
                const std::uint32_t temp1 =
                    working[7] + s1 + choose + kRoundConstants[i] + schedule[i];
                const std::uint32_t s0 = rotateRight(working[0], 2) ^ rotateRight(working[0], 13)
                                       ^ rotateRight(working[0], 22);
                const std::uint32_t majority = (working[0] & working[1]) ^ (working[0] & working[2])
                                             ^ (working[1] & working[2]);
                const std::uint32_t temp2 = s0 + majority;

                working[7] = working[6];
                working[6] = working[5];
                working[5] = working[4];
                working[4] = working[3] + temp1;
                working[3] = working[2];
                working[2] = working[1];
                working[1] = working[0];
                working[0] = temp1 + temp2;
            }

            for (std::size_t i = 0; i < 8u; ++i) { hash[i] += working[i]; }
        }

        static constexpr char kDigits[] = "0123456789abcdef";
        std::string out;
        out.reserve(64);
        for (const std::uint32_t word : hash)
        {
            for (int shift = 28; shift >= 0; shift -= 4)
            {
                out.push_back(kDigits[(word >> shift) & 0xFu]);
            }
        }
        return out;
    }

    std::string sha256HexOf(std::string_view text)
    {
        return sha256Hex(std::vector<unsigned char>{text.begin(), text.end()});
    }

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

CNA_STUDIO_TEST(TheSha256InThisFileIsSha256)
{
    // Known answers from FIPS 180-4, because an implementation written here to check other files
    // has to be checked itself first -- a hash that is subtly wrong would agree with nothing and
    // report every vendored file as drifted, which reads as a supply-chain scare rather than a bug.
    CNA_STUDIO_EXPECT_EQ(
        sha256HexOf(""),
        std::string{"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"});
    CNA_STUDIO_EXPECT_EQ(
        sha256HexOf("abc"),
        std::string{"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"});

    // Two blocks, so the loop over blocks is exercised rather than only the first one.
    CNA_STUDIO_EXPECT_EQ(
        sha256HexOf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
        std::string{"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"});
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

        const std::string actual = sha256Hex(readBytes(entry.path()));
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
