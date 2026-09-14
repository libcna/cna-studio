// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Core/Uuid.hpp
 * @brief Stable 128-bit identity for editor documents, entities and assets.
 *
 * Every persistent editor object is identified by a Uuid rather than by its name or by its
 * position in a file. That is what lets a scene reference an entity that was later renamed, and
 * an asset reference survive the source file being moved on disk (see AssetDatabase).
 */

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace CNA::Editor
{
    /**
     * @brief An RFC 4122 version 4 (random) UUID.
     *
     * Default-constructed instances are *nil* (all zero bytes) and compare false under
     * isValid(); a nil Uuid is the editor's "no reference" sentinel, so a missing parent or an
     * unassigned asset slot needs no separate optional wrapper.
     */
    class Uuid
    {
    public:
        /** @brief Constructs the nil UUID (all bytes zero). */
        constexpr Uuid() = default;

        /** @brief Constructs from raw bytes, most significant byte first. */
        explicit constexpr Uuid(const std::array<std::uint8_t, 16>& bytes) : bytes_(bytes) {}

        /**
         * @brief Generates a new random (version 4) UUID.
         *
         * Uses a thread-local PRNG seeded from std::random_device, so this is safe to call from
         * any thread and never blocks on entropy after the first call on that thread.
         */
        static Uuid generate();

        /**
         * @brief Parses the canonical 8-4-4-4-12 hexadecimal form.
         *
         * Accepts upper or lower case, with or without surrounding braces. Returns the nil UUID
         * when @p text is not a well-formed UUID -- callers that must distinguish "absent" from
         * "malformed" should validate the string length themselves before calling.
         *
         * @param text The textual form to parse.
         * @return The parsed value, or the nil UUID on failure.
         */
        static Uuid parse(std::string_view text);

        /** @brief Returns the canonical lower-case 8-4-4-4-12 form, without braces. */
        [[nodiscard]] std::string toString() const;

        /** @brief Returns true when this is not the nil UUID. */
        [[nodiscard]] bool isValid() const;

        /** @brief Returns the raw bytes, most significant byte first. */
        [[nodiscard]] constexpr const std::array<std::uint8_t, 16>& getBytes() const { return bytes_; }

        friend bool operator==(const Uuid& lhs, const Uuid& rhs) { return lhs.bytes_ == rhs.bytes_; }
        friend bool operator!=(const Uuid& lhs, const Uuid& rhs) { return !(lhs == rhs); }
        friend bool operator<(const Uuid& lhs, const Uuid& rhs) { return lhs.bytes_ < rhs.bytes_; }

    private:
        std::array<std::uint8_t, 16> bytes_{};
    };
}

namespace std
{
    /** @brief Enables CNA::Editor::Uuid as a key in unordered containers. */
    template <>
    struct hash<CNA::Editor::Uuid>
    {
        std::size_t operator()(const CNA::Editor::Uuid& value) const noexcept;
    };
}
