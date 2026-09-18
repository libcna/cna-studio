// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/Core/Sha256.hpp
 * @brief SHA-256, written out rather than depended on.
 *
 * `plan.md` STUDIO-09004, STUDIO-10012.
 *
 * Studio's core has no cryptographic dependency and is not acquiring one to hash its own files —
 * which would be funny in the wrong way for a project whose rule is that every dependency arrives
 * with a recorded reason. The algorithm is specified in FIPS 180-4, it is sixty lines, and the
 * known-answer vectors in `tests/Sha256Tests.cpp` are what make it trustworthy.
 *
 * ### Why a cryptographic hash for a cache key
 *
 * The thumbnail cache keys on what a file *contains* rather than on its name and timestamp, so two
 * copies of one texture share a decode and a file rewritten to the same length within the same
 * second is still noticed. A collision there is not a crash: it is a thumbnail showing the wrong
 * picture, which a user believes. A 64-bit hash makes that about one chance in a hundred million
 * for a project of a hundred thousand assets — small, and not small enough for a wrong answer that
 * looks like a right one. This is cheap enough (the file is being read and decoded anyway, on a
 * worker) that the stronger hash costs nothing worth having.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace CNA::Studio
{
    /** @brief SHA-256 of @p bytes, as thirty-two bytes. */
    [[nodiscard]] std::vector<unsigned char> studioSha256(const std::vector<unsigned char>& bytes);

    /** @brief SHA-256 of @p bytes, as sixty-four lowercase hexadecimal digits. */
    [[nodiscard]] std::string studioSha256Hex(const std::vector<unsigned char>& bytes);

    /** @brief SHA-256 of @p text, as sixty-four lowercase hexadecimal digits. */
    [[nodiscard]] std::string studioSha256Hex(std::string_view text);

    /**
     * @brief SHA-256 of the file at @p absolutePath, or an empty string when it cannot be read.
     *
     * Reads in chunks rather than whole: a cache key must be computable for a file bigger than it
     * is comfortable to hold, and a hash is the one thing that never needs the whole of it at once.
     *
     * **Reads from disk.** Not for a draw path.
     */
    [[nodiscard]] std::string studioSha256HexOfFile(const std::string& absolutePath);
}
