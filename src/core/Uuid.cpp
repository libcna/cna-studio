// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Core/Uuid.hpp"

#include <algorithm>
#include <random>

namespace CNA::Editor
{
    namespace
    {
        std::mt19937_64& threadLocalEngine()
        {
            // Seeded once per thread. std::random_device may be expensive (or, on some
            // platforms, may open /dev/urandom) so it is consulted exactly once rather than
            // per generate() call.
            static thread_local std::mt19937_64 engine{[] {
                std::random_device device;
                const auto high = static_cast<std::uint64_t>(device());
                const auto low = static_cast<std::uint64_t>(device());
                return (high << 32) ^ low;
            }()};
            return engine;
        }

        int hexValue(char character)
        {
            if (character >= '0' && character <= '9') { return character - '0'; }
            if (character >= 'a' && character <= 'f') { return character - 'a' + 10; }
            if (character >= 'A' && character <= 'F') { return character - 'A' + 10; }
            return -1;
        }
    }

    Uuid Uuid::generate()
    {
        std::uniform_int_distribution<std::uint64_t> distribution;
        const std::uint64_t high = distribution(threadLocalEngine());
        const std::uint64_t low = distribution(threadLocalEngine());

        std::array<std::uint8_t, 16> bytes{};
        for (int index = 0; index < 8; ++index)
        {
            bytes[static_cast<std::size_t>(index)] =
                static_cast<std::uint8_t>((high >> (56 - index * 8)) & 0xFFu);
            bytes[static_cast<std::size_t>(index + 8)] =
                static_cast<std::uint8_t>((low >> (56 - index * 8)) & 0xFFu);
        }

        // RFC 4122 section 4.4: set the version to 4 and the variant to the RFC 4122 form.
        bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0Fu) | 0x40u);
        bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3Fu) | 0x80u);
        return Uuid{bytes};
    }

    Uuid Uuid::parse(std::string_view text)
    {
        if (!text.empty() && text.front() == '{' && text.back() == '}')
        {
            text = text.substr(1, text.size() - 2);
        }

        std::array<std::uint8_t, 16> bytes{};
        std::size_t byteIndex = 0;
        int pendingNibble = -1;

        for (const char character : text)
        {
            if (character == '-') { continue; }

            const int nibble = hexValue(character);
            if (nibble < 0) { return Uuid{}; }

            if (pendingNibble < 0)
            {
                pendingNibble = nibble;
                continue;
            }

            if (byteIndex >= bytes.size()) { return Uuid{}; }
            bytes[byteIndex++] = static_cast<std::uint8_t>((pendingNibble << 4) | nibble);
            pendingNibble = -1;
        }

        if (byteIndex != bytes.size() || pendingNibble >= 0) { return Uuid{}; }
        return Uuid{bytes};
    }

    std::string Uuid::toString() const
    {
        static constexpr char kDigits[] = "0123456789abcdef";
        std::string result;
        result.reserve(36);

        for (std::size_t index = 0; index < bytes_.size(); ++index)
        {
            if (index == 4 || index == 6 || index == 8 || index == 10) { result.push_back('-'); }
            result.push_back(kDigits[(bytes_[index] >> 4) & 0x0Fu]);
            result.push_back(kDigits[bytes_[index] & 0x0Fu]);
        }
        return result;
    }

    bool Uuid::isValid() const
    {
        return std::any_of(bytes_.begin(), bytes_.end(), [](std::uint8_t byte) { return byte != 0; });
    }
}

namespace std
{
    std::size_t hash<CNA::Editor::Uuid>::operator()(const CNA::Editor::Uuid& value) const noexcept
    {
        // FNV-1a over the 16 raw bytes: cheap, and good enough for hash-bucket distribution of
        // values that are already uniformly random.
        std::size_t result = 1469598103934665603ULL;
        for (const std::uint8_t byte : value.getBytes())
        {
            result ^= static_cast<std::size_t>(byte);
            result *= 1099511628211ULL;
        }
        return result;
    }
}
