// SPDX-License-Identifier: MS-PL
/**
 * @file Sha256.cpp
 * @brief FIPS 180-4 SHA-256.
 */

#include "CNA/Studio/Core/Sha256.hpp"

#include <algorithm>
#include <array>
#include <fstream>

namespace CNA::Studio
{
    namespace
    {
        constexpr std::array<std::uint32_t, 64> kRoundConstants{
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

        constexpr std::uint32_t rotateRight(std::uint32_t value, int by)
        {
            return (value >> by) | (value << (32 - by));
        }

        /** @brief Running state, so a file can be hashed without being held whole. */
        struct Hasher
        {
            std::array<std::uint32_t, 8> hash{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                              0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
            std::array<unsigned char, 64> block{};
            std::size_t filled = 0;
            std::uint64_t length = 0;

            void compress(const unsigned char* data)
            {
                std::array<std::uint32_t, 64> schedule{};
                for (std::size_t i = 0; i < 16u; ++i)
                {
                    schedule[i] = (static_cast<std::uint32_t>(data[i * 4u]) << 24)
                                | (static_cast<std::uint32_t>(data[i * 4u + 1u]) << 16)
                                | (static_cast<std::uint32_t>(data[i * 4u + 2u]) << 8)
                                | static_cast<std::uint32_t>(data[i * 4u + 3u]);
                }
                for (std::size_t i = 16u; i < 64u; ++i)
                {
                    const std::uint32_t s0 = rotateRight(schedule[i - 15], 7)
                                           ^ rotateRight(schedule[i - 15], 18)
                                           ^ (schedule[i - 15] >> 3);
                    const std::uint32_t s1 = rotateRight(schedule[i - 2], 17)
                                           ^ rotateRight(schedule[i - 2], 19)
                                           ^ (schedule[i - 2] >> 10);
                    schedule[i] = schedule[i - 16] + s0 + schedule[i - 7] + s1;
                }

                std::array<std::uint32_t, 8> working = hash;
                for (std::size_t i = 0; i < 64u; ++i)
                {
                    const std::uint32_t s1 = rotateRight(working[4], 6)
                                           ^ rotateRight(working[4], 11)
                                           ^ rotateRight(working[4], 25);
                    const std::uint32_t choose =
                        (working[4] & working[5]) ^ (~working[4] & working[6]);
                    const std::uint32_t temp1 =
                        working[7] + s1 + choose + kRoundConstants[i] + schedule[i];
                    const std::uint32_t s0 = rotateRight(working[0], 2)
                                           ^ rotateRight(working[0], 13)
                                           ^ rotateRight(working[0], 22);
                    const std::uint32_t majority = (working[0] & working[1])
                                                 ^ (working[0] & working[2])
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

            void update(const unsigned char* data, std::size_t count)
            {
                length += static_cast<std::uint64_t>(count);
                while (count > 0)
                {
                    const std::size_t take = std::min(count, block.size() - filled);
                    for (std::size_t i = 0; i < take; ++i) { block[filled + i] = data[i]; }
                    filled += take;
                    data += take;
                    count -= take;

                    if (filled == block.size())
                    {
                        compress(block.data());
                        filled = 0;
                    }
                }
            }

            std::vector<unsigned char> finish()
            {
                const std::uint64_t bits = length * 8u;

                unsigned char one = 0x80u;
                update(&one, 1);
                // `update` counted the padding byte; the length in the trailer is the message's.
                length -= 1;

                unsigned char zero = 0x00u;
                while (filled != 56u)
                {
                    update(&zero, 1);
                    length -= 1;
                }

                std::array<unsigned char, 8> trailer{};
                for (int i = 0; i < 8; ++i)
                {
                    trailer[static_cast<std::size_t>(i)] =
                        static_cast<unsigned char>((bits >> (56 - i * 8)) & 0xFFu);
                }
                update(trailer.data(), trailer.size());

                std::vector<unsigned char> out;
                out.reserve(32);
                for (const std::uint32_t word : hash)
                {
                    out.push_back(static_cast<unsigned char>((word >> 24) & 0xFFu));
                    out.push_back(static_cast<unsigned char>((word >> 16) & 0xFFu));
                    out.push_back(static_cast<unsigned char>((word >> 8) & 0xFFu));
                    out.push_back(static_cast<unsigned char>(word & 0xFFu));
                }
                return out;
            }
        };

        std::string toHex(const std::vector<unsigned char>& digest)
        {
            static constexpr char kDigits[] = "0123456789abcdef";
            std::string out;
            out.reserve(digest.size() * 2);
            for (const unsigned char byte : digest)
            {
                out.push_back(kDigits[(byte >> 4) & 0x0Fu]);
                out.push_back(kDigits[byte & 0x0Fu]);
            }
            return out;
        }
    }

    std::vector<unsigned char> studioSha256(const std::vector<unsigned char>& bytes)
    {
        Hasher hasher;
        if (!bytes.empty()) { hasher.update(bytes.data(), bytes.size()); }
        return hasher.finish();
    }

    std::string studioSha256Hex(const std::vector<unsigned char>& bytes)
    {
        return toHex(studioSha256(bytes));
    }

    std::string studioSha256Hex(std::string_view text)
    {
        Hasher hasher;
        if (!text.empty())
        {
            hasher.update(reinterpret_cast<const unsigned char*>(text.data()), text.size());
        }
        return toHex(hasher.finish());
    }

    std::string studioSha256HexOfFile(const std::string& absolutePath)
    {
        std::ifstream stream{absolutePath, std::ios::binary};
        if (!stream) { return {}; }

        // In chunks rather than whole: a cache key must be computable for a file bigger than it is
        // comfortable to hold, and a hash is the one thing that never needs all of it at once.
        Hasher hasher;
        std::array<char, 64 * 1024> buffer{};
        while (stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()))
               || stream.gcount() > 0)
        {
            hasher.update(reinterpret_cast<const unsigned char*>(buffer.data()),
                          static_cast<std::size_t>(stream.gcount()));
            if (!stream) { break; }
        }

        return toHex(hasher.finish());
    }
}
