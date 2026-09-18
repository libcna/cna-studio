// SPDX-License-Identifier: MS-PL
#include "CNA/Studio/Assets/AudioImport.hpp"

#include "CNA/Studio/Core/Json.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace CNA::Studio
{
    namespace
    {
        std::uint32_t littleEndian32(const unsigned char* at)
        {
            return static_cast<std::uint32_t>(at[0]) | (static_cast<std::uint32_t>(at[1]) << 8)
                   | (static_cast<std::uint32_t>(at[2]) << 16)
                   | (static_cast<std::uint32_t>(at[3]) << 24);
        }

        std::uint32_t littleEndian16(const unsigned char* at)
        {
            return static_cast<std::uint32_t>(at[0]) | (static_cast<std::uint32_t>(at[1]) << 8);
        }

        bool matches(const unsigned char* at, const char* tag)
        {
            for (int index = 0; index < 4; ++index)
            {
                if (at[index] != static_cast<unsigned char>(tag[index])) { return false; }
            }
            return true;
        }

        /**
         * @brief Reads a RIFF/WAVE header, walking its chunks for `fmt ` and `data`.
         *
         * The two are not at fixed offsets and are not in a fixed order: a file written by a DAW
         * routinely carries `LIST`, `fact` or `bext` between them, and a reader that assumed
         * `fmt ` at 12 and `data` at 36 works on exactly the files a test would write by hand.
         *
         * @param stream Positioned anywhere; seeks for itself.
         */
        std::optional<StudioAudioDescription> readWave(std::istream& stream)
        {
            StudioAudioDescription description;
            description.format = "WAV";

            stream.clear();
            stream.seekg(12, std::ios::beg);  // past "RIFF", the size, and "WAVE"

            std::uint32_t dataBytes = 0;
            std::uint32_t byteRate = 0;
            bool haveFormat = false;

            // Bounded, because a file whose chunk sizes are nonsense would otherwise send this
            // round a loop that never advances.
            for (int chunk = 0; chunk < 64; ++chunk)
            {
                std::array<unsigned char, 8> header{};
                stream.read(reinterpret_cast<char*>(header.data()),
                            static_cast<std::streamsize>(header.size()));
                if (stream.gcount() < static_cast<std::streamsize>(header.size())) { break; }

                const std::uint32_t size = littleEndian32(header.data() + 4);

                if (matches(header.data(), "fmt "))
                {
                    // Sixteen bytes is the whole of the PCM format chunk and the start of every
                    // longer one. A shorter chunk is a malformed file, and reading it would leave
                    // the sample rate as whatever the uninitialised tail of the buffer held.
                    if (size < 16) { break; }

                    std::array<unsigned char, 16> format{};
                    const std::streamsize wanted = static_cast<std::streamsize>(
                        std::min<std::uint32_t>(size, static_cast<std::uint32_t>(format.size())));
                    stream.read(reinterpret_cast<char*>(format.data()), wanted);
                    if (stream.gcount() < wanted) { break; }

                    description.channels = littleEndian16(format.data() + 2);
                    description.sampleRate = littleEndian32(format.data() + 4);
                    byteRate = littleEndian32(format.data() + 8);
                    description.bitsPerSample = littleEndian16(format.data() + 14);
                    haveFormat = true;

                    // A `fmt ` chunk may be longer than the sixteen bytes read -- WAVE_FORMAT_-
                    // EXTENSIBLE adds twenty-two more -- and chunks are padded to even lengths.
                    stream.seekg(static_cast<std::streamoff>(size) - wanted + (size % 2), std::ios::cur);
                }
                else if (matches(header.data(), "data"))
                {
                    dataBytes = size;

                    // The data chunk is the last thing that matters and is usually the largest, so
                    // this stops here rather than seeking past several megabytes to find nothing.
                    break;
                }
                else
                {
                    stream.seekg(static_cast<std::streamoff>(size) + (size % 2), std::ios::cur);
                }

                if (!stream) { break; }
            }

            if (!haveFormat || !description.isMeasured()) { return std::nullopt; }

            // From the byte rate when the header declares one, because that is what a
            // non-PCM WAV's length actually depends on; from the arithmetic otherwise.
            const std::uint32_t bytesPerSecond =
                byteRate > 0 ? byteRate
                             : description.sampleRate * description.channels
                                   * (description.bitsPerSample / 8u);
            if (bytesPerSecond > 0 && dataBytes > 0)
            {
                description.durationSeconds =
                    static_cast<double>(dataBytes) / static_cast<double>(bytesPerSecond);
            }
            return description;
        }

        /** @brief The header of an Ogg page at @p at, or nothing when that is not a page. */
        struct OggPage
        {
            std::uint64_t granulePosition = 0;
            std::uint32_t serialNumber = 0;
            std::size_t headerBytes = 0;
            std::size_t payloadBytes = 0;
        };

        std::optional<OggPage> readOggPage(const std::vector<unsigned char>& bytes, std::size_t at)
        {
            if (at + 27 > bytes.size() || !matches(bytes.data() + at, "OggS")) { return std::nullopt; }

            OggPage page;
            for (int index = 0; index < 8; ++index)
            {
                page.granulePosition |= static_cast<std::uint64_t>(bytes[at + 6 + static_cast<std::size_t>(index)])
                                        << (8 * index);
            }
            page.serialNumber = littleEndian32(bytes.data() + at + 14);

            const std::size_t segments = bytes[at + 26];
            if (at + 27 + segments > bytes.size()) { return std::nullopt; }

            page.headerBytes = 27 + segments;
            for (std::size_t index = 0; index < segments; ++index)
            {
                page.payloadBytes += bytes[at + 27 + index];
            }
            return page;
        }

        /**
         * @brief Reads an Ogg Vorbis stream's rate, channels and length.
         *
         * The rate and channel count are in the identification header, which the container
         * requires to be alone in the first page -- so they are a fixed offset into the front of
         * the file. The *length* is not stated anywhere: it is the granule position of the last
         * page, which is a sample count, and finding that means reading the end of the file and
         * scanning backwards for the last page capture pattern. That is why this takes the whole
         * front and back rather than a stream.
         */
        std::optional<StudioAudioDescription> readOggVorbis(std::istream& stream,
                                                            const std::vector<unsigned char>& front)
        {
            const std::optional<OggPage> first = readOggPage(front, 0);
            if (!first) { return std::nullopt; }

            const std::size_t payload = first->headerBytes;
            if (payload + 30 > front.size()) { return std::nullopt; }

            // The Vorbis identification packet: a type byte of 1, the string "vorbis", a four-byte
            // version, then the channel count and the sample rate. A page that starts with
            // anything else is some other codec in an Ogg container -- Opus, Theora, FLAC -- and
            // is left unmeasured rather than read as Vorbis.
            if (front[payload] != 0x01
                || std::string(reinterpret_cast<const char*>(front.data() + payload + 1), 6) != "vorbis")
            {
                return std::nullopt;
            }

            StudioAudioDescription description;
            description.format = "Ogg Vorbis";
            description.channels = front[payload + 11];
            description.sampleRate = littleEndian32(front.data() + payload + 12);
            if (!description.isMeasured()) { return std::nullopt; }

            // The last page's granule position is the stream's total sample count. Scanned for in
            // the final chunk of the file rather than by walking every page from the front, which
            // on a four-minute track is tens of thousands of seeks for one number.
            stream.clear();
            stream.seekg(0, std::ios::end);
            const std::streamoff length = stream.tellg();
            if (length <= 0) { return description; }

            constexpr std::streamoff kTailBytes = 65536;
            const std::streamoff from = length > kTailBytes ? length - kTailBytes : 0;
            std::vector<unsigned char> tail(static_cast<std::size_t>(length - from));
            stream.seekg(from, std::ios::beg);
            stream.read(reinterpret_cast<char*>(tail.data()),
                        static_cast<std::streamsize>(tail.size()));
            tail.resize(static_cast<std::size_t>(stream.gcount()));

            for (std::size_t at = tail.size(); at >= 27; --at)
            {
                const std::size_t start = at - 27;
                const std::optional<OggPage> page = readOggPage(tail, start);
                if (!page || page->serialNumber != first->serialNumber) { continue; }

                // 0xFFFFFFFFFFFFFFFF marks a page with no packet completed on it, which carries no
                // usable position; the scan continues past one rather than reporting nonsense.
                if (page->granulePosition == ~std::uint64_t{0}) { continue; }

                description.durationSeconds = static_cast<double>(page->granulePosition)
                                              / static_cast<double>(description.sampleRate);
                break;
            }
            return description;
        }
    }

    StudioAudioImportSettings StudioAudioImportSettings::fromJson(const JsonValue& importerSettings)
    {
        StudioAudioImportSettings settings;

        const JsonValue& volume = importerSettings["importVolume"];
        if (!volume.isNull())
        {
            settings.importVolume = static_cast<float>(volume.asNumber(settings.importVolume));
        }

        const JsonValue& memory = importerSettings["loadIntoMemory"];
        if (!memory.isNull()) { settings.loadIntoMemory = memory.asBoolean(settings.loadIntoMemory); }

        return settings;
    }

    std::optional<StudioAudioDescription> readAudioDescription(const std::string& absolutePath)
    {
        std::ifstream stream{absolutePath, std::ios::binary};
        if (!stream) { return std::nullopt; }

        std::vector<unsigned char> front(4096);
        stream.read(reinterpret_cast<char*>(front.data()),
                    static_cast<std::streamsize>(front.size()));
        front.resize(static_cast<std::size_t>(stream.gcount()));
        if (front.size() < 16) { return std::nullopt; }

        std::optional<StudioAudioDescription> description;

        if (matches(front.data(), "RIFF") && front.size() >= 12 && matches(front.data() + 8, "WAVE"))
        {
            description = readWave(stream);
        }
        else if (matches(front.data(), "OggS"))
        {
            description = readOggVorbis(stream, front);
        }

        if (!description) { return std::nullopt; }

        // Sixteen-bit PCM, because that is what a decoded clip is held as whatever it arrived as.
        // Computed here rather than in each reader: it is the same arithmetic for every format,
        // and a compressed one getting it wrong is exactly the case it exists to inform.
        description->decodedBytes = static_cast<std::uint64_t>(
            description->durationSeconds * description->sampleRate * description->channels * 2.0);
        return description;
    }

    std::string studioDescribeDuration(double seconds)
    {
        if (seconds <= 0.0) { return "0:00.0"; }

        const auto totalTenths = static_cast<std::uint64_t>(seconds * 10.0 + 0.5);
        const std::uint64_t minutes = totalTenths / 600u;
        const std::uint64_t remainingSeconds = (totalTenths / 10u) % 60u;
        const std::uint64_t tenths = totalTenths % 10u;

        std::array<char, 64> text{};
        std::snprintf(text.data(), text.size(), "%llu:%02llu.%llu",
                      static_cast<unsigned long long>(minutes),
                      static_cast<unsigned long long>(remainingSeconds),
                      static_cast<unsigned long long>(tenths));
        return std::string{text.data()};
    }
}
