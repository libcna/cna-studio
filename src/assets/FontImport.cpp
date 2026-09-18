// SPDX-License-Identifier: MS-PL
#include "CNA/Studio/Assets/FontImport.hpp"

#include "CNA/Studio/Core/Json.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <vector>

namespace CNA::Studio
{
    namespace
    {
        std::uint32_t bigEndian32(const unsigned char* at)
        {
            return (static_cast<std::uint32_t>(at[0]) << 24) | (static_cast<std::uint32_t>(at[1]) << 16)
                   | (static_cast<std::uint32_t>(at[2]) << 8) | static_cast<std::uint32_t>(at[3]);
        }

        std::uint32_t bigEndian16(const unsigned char* at)
        {
            return (static_cast<std::uint32_t>(at[0]) << 8) | static_cast<std::uint32_t>(at[1]);
        }

        /** @brief One entry of the sfnt table directory. */
        struct Table
        {
            std::uint32_t offset = 0;
            std::uint32_t length = 0;
        };

        /** @brief Reads @p length bytes at @p offset, or an empty vector when they are not there. */
        std::vector<unsigned char> readAt(std::istream& stream, std::uint64_t fileSize,
                                          std::uint64_t offset, std::uint64_t length)
        {
            // Checked against the file's real length rather than trusted. A table record claiming
            // to start four gigabytes into a two-kilobyte file is a few bytes to write, and a
            // reader that seeks there and reads whatever it finds is the bug that makes a font a
            // way into the editor.
            if (length == 0 || offset > fileSize || length > fileSize - offset) { return {}; }

            std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
            stream.clear();
            stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
            stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(length));
            if (stream.gcount() < static_cast<std::streamsize>(length)) { return {}; }
            return bytes;
        }

        /**
         * @brief Turns a UTF-16BE name string into UTF-8.
         *
         * Written out rather than reached for, because it is twenty lines and the alternative is a
         * dependency in a module that has none. Surrogate pairs are joined: a family name outside
         * the basic plane is rare and mangling it would be a silent corruption of somebody's name.
         */
        std::string utf16BeToUtf8(const unsigned char* at, std::size_t length)
        {
            std::string text;
            for (std::size_t index = 0; index + 1 < length; index += 2)
            {
                std::uint32_t code = bigEndian16(at + index);

                if (code >= 0xD800 && code <= 0xDBFF && index + 3 < length)
                {
                    const std::uint32_t low = bigEndian16(at + index + 2);
                    if (low >= 0xDC00 && low <= 0xDFFF)
                    {
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                        index += 2;
                    }
                }

                if (code < 0x80) { text.push_back(static_cast<char>(code)); }
                else if (code < 0x800)
                {
                    text.push_back(static_cast<char>(0xC0 | (code >> 6)));
                    text.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                }
                else if (code < 0x10000)
                {
                    text.push_back(static_cast<char>(0xE0 | (code >> 12)));
                    text.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                    text.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                }
                else
                {
                    text.push_back(static_cast<char>(0xF0 | (code >> 18)));
                    text.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
                    text.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                    text.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                }
            }
            return text;
        }

        /**
         * @brief Pulls name IDs 1 and 2 out of a `name` table.
         *
         * Windows Unicode entries are preferred and Macintosh ones are the fallback, which is the
         * order every font carries them in and the order that gets a readable answer from the
         * widest set of files. A font with neither leaves the names empty rather than guessing at
         * the file name, which would report what somebody called the file back at them.
         */
        void readNames(const std::vector<unsigned char>& table, StudioFontDescription& description)
        {
            if (table.size() < 6) { return; }

            const std::uint32_t count = bigEndian16(table.data() + 2);
            const std::uint32_t storage = bigEndian16(table.data() + 4);

            // Macintosh entries fill these only if no Windows entry turned up first.
            std::string fallbackFamily;
            std::string fallbackStyle;

            for (std::uint32_t index = 0; index < count; ++index)
            {
                const std::size_t record = 6 + static_cast<std::size_t>(index) * 12;
                if (record + 12 > table.size()) { break; }

                const std::uint32_t platform = bigEndian16(table.data() + record);
                const std::uint32_t encoding = bigEndian16(table.data() + record + 2);
                const std::uint32_t nameId = bigEndian16(table.data() + record + 6);
                const std::uint32_t length = bigEndian16(table.data() + record + 8);
                const std::uint32_t offset = bigEndian16(table.data() + record + 10);

                if (nameId != 1 && nameId != 2) { continue; }

                const std::size_t start = storage + static_cast<std::size_t>(offset);
                if (start > table.size() || length > table.size() - start) { continue; }

                const bool windows = platform == 3;
                const bool macintosh = platform == 1 && encoding == 0;
                if (!windows && !macintosh) { continue; }

                // Windows names are UTF-16BE; Macintosh ones are MacRoman, whose first 128 code
                // points are ASCII and whose rest is rare enough in a family name that copying the
                // bytes is better than carrying a 128-entry table for it.
                const std::string value =
                    windows ? utf16BeToUtf8(table.data() + start, length)
                            : std::string{reinterpret_cast<const char*>(table.data() + start), length};
                if (value.empty()) { continue; }

                std::string& target = nameId == 1 ? (windows ? description.family : fallbackFamily)
                                                  : (windows ? description.style : fallbackStyle);
                if (target.empty()) { target = value; }
            }

            if (description.family.empty()) { description.family = fallbackFamily; }
            if (description.style.empty()) { description.style = fallbackStyle; }
        }
    }

    StudioFontImportSettings StudioFontImportSettings::fromJson(const JsonValue& importerSettings)
    {
        StudioFontImportSettings settings;

        const JsonValue& pointSize = importerSettings["pointSize"];
        if (!pointSize.isNull())
        {
            settings.pointSize = static_cast<float>(pointSize.asNumber(settings.pointSize));
        }

        const JsonValue& first = importerSettings["firstCharacter"];
        if (!first.isNull())
        {
            settings.firstCharacter = static_cast<int>(first.asNumber(settings.firstCharacter));
        }

        const JsonValue& last = importerSettings["lastCharacter"];
        if (!last.isNull())
        {
            settings.lastCharacter = static_cast<int>(last.asNumber(settings.lastCharacter));
        }

        const JsonValue& spacing = importerSettings["spacing"];
        if (!spacing.isNull())
        {
            settings.spacing = static_cast<float>(spacing.asNumber(settings.spacing));
        }

        const JsonValue& useKerning = importerSettings["useKerning"];
        if (!useKerning.isNull()) { settings.useKerning = useKerning.asBoolean(settings.useKerning); }

        return settings;
    }

    std::size_t StudioFontImportSettings::characterCount() const
    {
        // An inverted range is empty rather than enormous. The two fields are edited separately, so
        // a user passes through "last is below first" on the way to any range that moves downwards,
        // and a count that wrapped there would report several billion characters mid-edit.
        if (lastCharacter < firstCharacter) { return 0; }
        return static_cast<std::size_t>(lastCharacter - firstCharacter) + 1u;
    }

    std::optional<StudioFontDescription> readFontDescription(const std::string& absolutePath,
                                                             std::string* outProblem)
    {
        const auto refuse = [&](const char* reason) -> std::optional<StudioFontDescription> {
            if (outProblem != nullptr) { *outProblem = reason; }
            return std::nullopt;
        };

        std::ifstream stream{absolutePath, std::ios::binary};
        if (!stream) { return std::nullopt; }

        stream.seekg(0, std::ios::end);
        const std::streamoff end = stream.tellg();
        if (end <= 0) { return std::nullopt; }
        const auto fileSize = static_cast<std::uint64_t>(end);

        std::vector<unsigned char> header = readAt(stream, fileSize, 0, 12);
        if (header.size() < 12) { return std::nullopt; }

        StudioFontDescription description;
        std::uint64_t directoryAt = 12;

        const std::uint32_t tag = bigEndian32(header.data());
        if (tag == 0x74746366u)  // 'ttcf'
        {
            // A collection is a header pointing at several fonts' offset tables. The first is the
            // one reported: a family in one file is still one asset, and answering "which of the
            // four" needs a picker this task has no consumer for.
            description.format = "TrueType Collection";

            const std::vector<unsigned char> collection = readAt(stream, fileSize, 0, 16);
            if (collection.size() < 16 || bigEndian32(collection.data() + 8) == 0)
            {
                return refuse("it is a TrueType collection with no fonts in it");
            }

            const std::vector<unsigned char> firstOffset = readAt(stream, fileSize, 12, 4);
            if (firstOffset.size() < 4) { return refuse("its collection header stops short"); }

            const std::uint64_t fontAt = bigEndian32(firstOffset.data());
            header = readAt(stream, fileSize, fontAt, 12);
            if (header.size() < 12) { return refuse("its first font's offset table is not there"); }
            directoryAt = fontAt + 12;
        }
        else if (tag == 0x00010000u || tag == 0x74727565u)  // 1.0, 'true'
        {
            description.format = "TrueType";
        }
        else if (tag == 0x4F54544Fu)  // 'OTTO'
        {
            description.format = "OpenType";
        }
        else
        {
            // Not a font at all, which is ordinary and silent: a project is full of files Studio
            // does not import (`plan.md` STUDIO-10013).
            return std::nullopt;
        }

        const std::uint32_t tableCount = bigEndian16(header.data() + 4);
        if (tableCount == 0) { return refuse("its table directory is empty"); }

        const std::vector<unsigned char> directory =
            readAt(stream, fileSize, directoryAt, static_cast<std::uint64_t>(tableCount) * 16u);
        if (directory.empty())
        {
            return refuse("its table directory runs past the end of the file, so the file is "
                          "truncated or corrupt");
        }

        Table head;
        Table maxp;
        Table name;

        for (std::uint32_t index = 0; index < tableCount; ++index)
        {
            const std::size_t record = static_cast<std::size_t>(index) * 16;
            const std::uint32_t recordTag = bigEndian32(directory.data() + record);
            const Table table{bigEndian32(directory.data() + record + 8),
                              bigEndian32(directory.data() + record + 12)};

            switch (recordTag)
            {
                case 0x68656164u: head = table; break;  // 'head'
                case 0x6D617870u: maxp = table; break;  // 'maxp'
                case 0x6E616D65u: name = table; break;  // 'name'
                // Either is enough to kern with, and which one a font uses is a decision of its
                // designer's that a user does not need to hear about.
                case 0x6B65726Eu:                       // 'kern'
                case 0x47504F53u:                       // 'GPOS'
                    description.hasKerning = true;
                    break;
                default: break;
            }
        }

        const std::vector<unsigned char> headBytes =
            readAt(stream, fileSize, head.offset, std::min<std::uint32_t>(head.length, 54u));
        if (headBytes.size() >= 20) { description.unitsPerEm = bigEndian16(headBytes.data() + 18); }

        const std::vector<unsigned char> maxpBytes =
            readAt(stream, fileSize, maxp.offset, std::min<std::uint32_t>(maxp.length, 6u));
        if (maxpBytes.size() >= 6) { description.glyphCount = bigEndian16(maxpBytes.data() + 4); }

        readNames(readAt(stream, fileSize, name.offset, name.length), description);

        if (!description.isMeasured())
        {
            // The sfnt version was right and the tables every font must have are not readable. That
            // is a broken font rather than a file that is not one, and it is exactly the case a
            // reader returning a bare nothing would make indistinguishable.
            return refuse("it starts as a font but its head or maxp table could not be read, so "
                          "the file is truncated or corrupt");
        }

        return description;
    }
}
