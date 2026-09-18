// SPDX-License-Identifier: MS-PL
/**
 * @file Sha256Tests.cpp
 * @brief The known answers, because everything else trusts this (`plan.md` STUDIO-09004).
 *
 * Two things in Studio stand on this hash: the third-party provenance gate, where a wrong answer
 * reports every vendored file as drifted and reads as a supply-chain scare, and the thumbnail
 * cache's content key, where a wrong answer is a thumbnail showing the wrong picture — which a user
 * believes. Neither failure looks like a hash bug from the outside, so the vectors matter more than
 * they would for most sixty lines.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Core/Sha256.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace CNA::Studio;

CNA_STUDIO_TEST(Sha256MatchesTheFipsVectors)
{
    // FIPS 180-4's own examples.
    CNA_STUDIO_EXPECT_EQ(
        studioSha256Hex(std::string_view{""}),
        std::string{"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"});
    CNA_STUDIO_EXPECT_EQ(
        studioSha256Hex(std::string_view{"abc"}),
        std::string{"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"});

    // Two blocks, so the loop over blocks runs rather than only the first one.
    CNA_STUDIO_EXPECT_EQ(
        studioSha256Hex(std::string_view{
            "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"}),
        std::string{"248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"});

    // A million 'a's: the one vector that exercises the length counter past a single block's worth
    // and would catch a padding length written in bytes where it should be bits.
    const std::string million(1000000, 'a');
    CNA_STUDIO_EXPECT_EQ(
        studioSha256Hex(std::string_view{million}),
        std::string{"cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"});

    // The byte and the string forms are the same function.
    const std::vector<unsigned char> bytes{'a', 'b', 'c'};
    CNA_STUDIO_EXPECT_EQ(studioSha256Hex(bytes), studioSha256Hex(std::string_view{"abc"}));
    CNA_STUDIO_EXPECT_EQ(studioSha256(bytes).size(), std::size_t{32});
}

CNA_STUDIO_TEST(HashingAFileAgreesWithHashingItsBytes)
{
    // The file form reads in chunks, because a cache key must be computable for a file bigger than
    // it is comfortable to hold. A chunk boundary landing mid-block is exactly where a streaming
    // hash goes wrong, so the sizes below straddle the 64 KB buffer.
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "cna-studio-sha256";
    std::error_code code;
    std::filesystem::create_directories(directory, code);

    for (const std::size_t size : {std::size_t{0}, std::size_t{1}, std::size_t{63},
                                   std::size_t{64}, std::size_t{65}, std::size_t{64 * 1024},
                                   std::size_t{64 * 1024 + 7}, std::size_t{200 * 1024}})
    {
        std::vector<unsigned char> bytes(size);
        for (std::size_t i = 0; i < size; ++i)
        {
            bytes[i] = static_cast<unsigned char>((i * 31u + 7u) & 0xFFu);
        }

        const std::filesystem::path file = directory / ("size-" + std::to_string(size) + ".bin");
        {
            std::ofstream stream{file, std::ios::binary};
            stream.write(reinterpret_cast<const char*>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
        }

        CNA_STUDIO_EXPECT_EQ(studioSha256HexOfFile(file.generic_string()),
                             studioSha256Hex(bytes));
    }

    std::filesystem::remove_all(directory, code);

    // A file that is not there is an empty answer rather than the hash of nothing, which would be
    // indistinguishable from a real empty file and would make a missing asset look cached.
    CNA_STUDIO_EXPECT_EQ(studioSha256HexOfFile("/nonexistent/path/nothing.bin"), std::string{});
}
