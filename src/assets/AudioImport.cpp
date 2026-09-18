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

        /**
         * @brief Reads a FLAC's `STREAMINFO`, which states everything outright.
         *
         * The easiest of the four formats and the one that needs no arithmetic at all: the rate,
         * the channel count, the bit depth and the *total sample count* are all in the first
         * metadata block, which the format requires to come first. The only work is that they are
         * bit-packed rather than byte-aligned.
         */
        std::optional<StudioAudioDescription> readFlac(std::istream& stream, std::uint64_t fileSize)
        {
            // "fLaC", then a metadata block header of one type byte and three length bytes, then
            // the 34-byte STREAMINFO itself.
            if (fileSize < 42) { return std::nullopt; }

            std::array<unsigned char, 42> block{};
            stream.clear();
            stream.seekg(0, std::ios::beg);
            stream.read(reinterpret_cast<char*>(block.data()),
                        static_cast<std::streamsize>(block.size()));
            if (stream.gcount() < static_cast<std::streamsize>(block.size())) { return std::nullopt; }

            // Block type 0 is STREAMINFO, and the spec requires it to be the first block. A file
            // whose first block is something else is not a FLAC this can read, and guessing which
            // of the later blocks to trust instead would be reading a format that does not exist.
            if ((block[4] & 0x7Fu) != 0) { return std::nullopt; }

            const unsigned char* at = block.data() + 8;

            // 20 bits of sample rate, 3 of channels-1, 5 of bits-per-sample-1, 36 of total samples:
            // 64 bits starting at byte 10 of STREAMINFO, which is byte 18 of the file.
            const unsigned char* packed = at + 10;
            const std::uint32_t sampleRate = (static_cast<std::uint32_t>(packed[0]) << 12)
                                             | (static_cast<std::uint32_t>(packed[1]) << 4)
                                             | (static_cast<std::uint32_t>(packed[2]) >> 4);
            const std::uint32_t channels = ((packed[2] >> 1) & 0x07u) + 1u;
            const std::uint32_t bitsPerSample =
                ((static_cast<std::uint32_t>(packed[2] & 0x01u) << 4)
                 | (static_cast<std::uint32_t>(packed[3]) >> 4))
                + 1u;

            std::uint64_t totalSamples = static_cast<std::uint64_t>(packed[3] & 0x0Fu) << 32;
            for (int index = 0; index < 4; ++index)
            {
                totalSamples |= static_cast<std::uint64_t>(packed[4 + index])
                                << (8 * (3 - index));
            }

            StudioAudioDescription description;
            description.format = "FLAC";
            description.sampleRate = sampleRate;
            description.channels = channels;

            // Reported as the file stores it, which for FLAC is a real number rather than the zero
            // a Vorbis stream gets: FLAC is lossless, so its samples are samples. A 24-bit file
            // still costs 16-bit PCM in memory, because that is what the runtime holds it as --
            // which is why `decodedBytes` is worked out from the duration rather than from this.
            description.bitsPerSample = bitsPerSample;

            if (!description.isMeasured()) { return std::nullopt; }

            // Zero is legal and means "unknown", which is what a stream written before its length
            // was known says. Left as a zero duration rather than invented.
            if (totalSamples > 0)
            {
                description.durationSeconds =
                    static_cast<double>(totalSamples) / static_cast<double>(sampleRate);
            }
            return description;
        }

        /** @brief MPEG audio frame header fields, as far as a duration needs them. */
        struct MpegFrame
        {
            std::uint32_t sampleRate = 0;
            std::uint32_t channels = 0;
            std::uint32_t bitrateBitsPerSecond = 0;
            std::uint32_t samplesPerFrame = 0;
            std::uint32_t frameBytes = 0;

            /** @brief Where the side information ends and a Xing or Info tag would begin. */
            std::uint32_t sideInfoBytes = 0;

            [[nodiscard]] bool isValid() const { return sampleRate > 0 && frameBytes > 0; }
        };

        /** @brief Decodes a four-byte MPEG audio frame header, or reports it unusable. */
        MpegFrame readMpegFrame(const unsigned char* at)
        {
            MpegFrame frame;
            if (at[0] != 0xFFu || (at[1] & 0xE0u) != 0xE0u) { return frame; }

            const std::uint32_t versionId = (at[1] >> 3) & 0x03u;   // 0=2.5, 2=2, 3=1
            const std::uint32_t layer = (at[1] >> 1) & 0x03u;       // 1=III, 2=II, 3=I
            const std::uint32_t bitrateIndex = (at[2] >> 4) & 0x0Fu;
            const std::uint32_t rateIndex = (at[2] >> 2) & 0x03u;
            const std::uint32_t padding = (at[2] >> 1) & 0x01u;
            const std::uint32_t channelMode = (at[3] >> 6) & 0x03u;

            // Layer III only. Studio reads what a project holds, and a Layer I or II file in a
            // game is rare enough that supporting it would be code with no user -- and getting it
            // wrong silently is worse than declining it.
            if (versionId == 1 || layer != 1) { return frame; }
            if (bitrateIndex == 0 || bitrateIndex == 15 || rateIndex == 3) { return frame; }

            static constexpr std::array<std::uint32_t, 3> kSampleRates{44100, 48000, 32000};
            const std::uint32_t base = kSampleRates[rateIndex];
            frame.sampleRate = versionId == 3 ? base : (versionId == 2 ? base / 2u : base / 4u);

            static constexpr std::array<std::uint32_t, 15> kMpeg1Bitrates{
                0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
            static constexpr std::array<std::uint32_t, 15> kMpeg2Bitrates{
                0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160};
            frame.bitrateBitsPerSecond =
                (versionId == 3 ? kMpeg1Bitrates[bitrateIndex] : kMpeg2Bitrates[bitrateIndex]) * 1000u;

            frame.channels = channelMode == 3 ? 1u : 2u;
            frame.samplesPerFrame = versionId == 3 ? 1152u : 576u;
            frame.frameBytes = (frame.samplesPerFrame / 8u) * frame.bitrateBitsPerSecond
                                   / frame.sampleRate
                               + padding;

            // Where a Xing or Info tag sits, which depends on the version and whether the frame is
            // mono. These are the four sizes the format defines; nothing is being estimated.
            if (versionId == 3) { frame.sideInfoBytes = frame.channels == 1 ? 17u : 32u; }
            else { frame.sideInfoBytes = frame.channels == 1 ? 9u : 17u; }

            return frame;
        }

        /**
         * @brief Reads an MP3's rate, channels and length.
         *
         * The length is the part `STUDIO-10015` was filed to decide, and the answer is that there
         * are three cases rather than one, and Studio can tell which it is in:
         *
         * - **A Xing or Info tag** states the frame count outright, which is exact. Every encoder
         *   that produces variable-bitrate audio writes one -- that is what the tag is for.
         * - **No tag, and the bitrate does not change** is constant-bitrate audio, where the length
         *   is arithmetic on the file size and is likewise exact. Checked rather than assumed: a
         *   run of consecutive frame headers has to agree about the bitrate.
         * - **No tag and a bitrate that does change** is the one case with no cheap answer. It is
         *   *declined* rather than estimated, because an estimate from the first frame is wrong by
         *   whatever the file's dynamics are, and a duration wrong by a factor of two is worse than
         *   one that is absent.
         *
         * Walking every frame would answer the third case exactly and costs the whole file, on a
         * pass that runs over every asset in a project. That trade is not worth making for a case
         * encoders do not produce.
         */
        std::optional<StudioAudioDescription> readMp3(std::istream& stream, std::uint64_t fileSize,
                                                      const std::vector<unsigned char>& front)
        {
            // An ID3v2 tag sits in front of the audio and says how long it is, in a "synchsafe"
            // integer: seven bits per byte, because a set top bit would look like a frame sync.
            std::uint64_t audioAt = 0;
            if (front.size() >= 10 && front[0] == 'I' && front[1] == 'D' && front[2] == '3')
            {
                const std::uint64_t tagSize = (static_cast<std::uint64_t>(front[6] & 0x7Fu) << 21)
                                              | (static_cast<std::uint64_t>(front[7] & 0x7Fu) << 14)
                                              | (static_cast<std::uint64_t>(front[8] & 0x7Fu) << 7)
                                              | static_cast<std::uint64_t>(front[9] & 0x7Fu);
                audioAt = 10 + tagSize;
                if ((front[5] & 0x10u) != 0) { audioAt += 10; }  // a footer, when the flag says so
            }

            if (audioAt >= fileSize) { return std::nullopt; }

            // The first frame is looked for rather than assumed to be at `audioAt`: a tag whose
            // declared size is slightly off is common enough that every player scans, and the scan
            // is bounded so a file of noise cannot turn it into a walk of the whole thing.
            constexpr std::uint64_t kScanBytes = 8192;
            const std::uint64_t scanLength = std::min(kScanBytes, fileSize - audioAt);
            std::vector<unsigned char> scan(static_cast<std::size_t>(scanLength));
            stream.clear();
            stream.seekg(static_cast<std::streamoff>(audioAt), std::ios::beg);
            stream.read(reinterpret_cast<char*>(scan.data()),
                        static_cast<std::streamsize>(scan.size()));
            scan.resize(static_cast<std::size_t>(stream.gcount()));

            MpegFrame first;
            std::size_t firstAt = 0;
            for (std::size_t index = 0; index + 4 <= scan.size(); ++index)
            {
                const MpegFrame candidate = readMpegFrame(scan.data() + index);
                if (candidate.isValid())
                {
                    first = candidate;
                    firstAt = index;
                    break;
                }
            }
            if (!first.isValid()) { return std::nullopt; }

            StudioAudioDescription description;
            description.format = "MP3";
            description.sampleRate = first.sampleRate;
            description.channels = first.channels;

            // Zero, like Vorbis: an MP3 stores frequency coefficients rather than samples, so it
            // has no bits-per-sample to report and sixteen would answer a question nobody asked.
            description.bitsPerSample = 0;

            // A Xing or Info tag lives inside the first frame, after its side information. "Xing"
            // is written by variable-bitrate encoders and "Info" by constant-bitrate ones; both
            // carry the frame count in the same place.
            const std::size_t tagAt = firstAt + 4 + first.sideInfoBytes;
            if (tagAt + 12 <= scan.size()
                && (matches(scan.data() + tagAt, "Xing") || matches(scan.data() + tagAt, "Info")))
            {
                const std::uint32_t flags = static_cast<std::uint32_t>(
                    (scan[tagAt + 4] << 24) | (scan[tagAt + 5] << 16) | (scan[tagAt + 6] << 8)
                    | scan[tagAt + 7]);
                if ((flags & 0x01u) != 0)
                {
                    const std::uint64_t frames = (static_cast<std::uint64_t>(scan[tagAt + 8]) << 24)
                                                 | (static_cast<std::uint64_t>(scan[tagAt + 9]) << 16)
                                                 | (static_cast<std::uint64_t>(scan[tagAt + 10]) << 8)
                                                 | static_cast<std::uint64_t>(scan[tagAt + 11]);
                    if (frames > 0)
                    {
                        description.durationSeconds =
                            static_cast<double>(frames * first.samplesPerFrame)
                            / static_cast<double>(first.sampleRate);
                        return description;
                    }
                }
            }

            // No tag. The file is constant-bitrate or it is not, and which of those it is decides
            // whether the arithmetic below is exact -- so it is checked rather than assumed. A run
            // of consecutive frames that all declare the same bitrate is what constant means.
            std::uint64_t at = audioAt + firstAt;
            for (int checked = 0; checked < 8; ++checked)
            {
                std::array<unsigned char, 4> header{};
                stream.clear();
                stream.seekg(static_cast<std::streamoff>(at), std::ios::beg);
                stream.read(reinterpret_cast<char*>(header.data()),
                            static_cast<std::streamsize>(header.size()));
                if (stream.gcount() < static_cast<std::streamsize>(header.size())) { break; }

                const MpegFrame frame = readMpegFrame(header.data());
                if (!frame.isValid()) { break; }
                if (frame.bitrateBitsPerSecond != first.bitrateBitsPerSecond)
                {
                    // Variable bitrate with no header to say so. Declined rather than estimated:
                    // an estimate from the first frame is wrong by whatever the file's dynamics
                    // are, and a duration wrong by a factor of two is worse than one that is
                    // absent. Walking every frame would answer it and costs the whole file on a
                    // pass that runs over every asset in a project.
                    return std::nullopt;
                }
                at += frame.frameBytes;
            }

            // An ID3v1 tag is exactly 128 bytes at the end and is not audio. Subtracted so the
            // length of a short clip is not wrong by the fraction of a second it represents.
            std::uint64_t audioBytes = fileSize - (audioAt + firstAt);
            if (fileSize >= 128)
            {
                std::array<unsigned char, 3> tail{};
                stream.clear();
                stream.seekg(static_cast<std::streamoff>(fileSize - 128), std::ios::beg);
                stream.read(reinterpret_cast<char*>(tail.data()),
                            static_cast<std::streamsize>(tail.size()));
                if (stream.gcount() == 3 && tail[0] == 'T' && tail[1] == 'A' && tail[2] == 'G'
                    && audioBytes >= 128)
                {
                    audioBytes -= 128;
                }
            }

            if (first.bitrateBitsPerSecond == 0) { return std::nullopt; }
            description.durationSeconds = static_cast<double>(audioBytes * 8u)
                                          / static_cast<double>(first.bitrateBitsPerSecond);
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

    std::optional<StudioAudioDescription> readAudioDescription(const std::string& absolutePath,
                                                               std::string* outProblem)
    {
        std::ifstream stream{absolutePath, std::ios::binary};
        if (!stream) { return std::nullopt; }

        std::vector<unsigned char> front(4096);
        stream.read(reinterpret_cast<char*>(front.data()),
                    static_cast<std::streamsize>(front.size()));
        front.resize(static_cast<std::size_t>(stream.gcount()));
        if (front.size() < 16) { return std::nullopt; }

        stream.clear();
        stream.seekg(0, std::ios::end);
        const std::streamoff end = stream.tellg();
        if (end <= 0) { return std::nullopt; }
        const auto fileSize = static_cast<std::uint64_t>(end);

        std::optional<StudioAudioDescription> description;

        // The reason is set only when the file *announced itself* as one of these and then could
        // not be read anyway. A file that is not audio Studio reads at all is declined in silence,
        // because that is not a problem with the file (`plan.md` STUDIO-10013).
        const char* claimed = nullptr;

        if (matches(front.data(), "RIFF") && front.size() >= 12 && matches(front.data() + 8, "WAVE"))
        {
            claimed = "it starts as a WAV but no readable format and data chunk could be found in "
                      "it, so the file is truncated or corrupt";
            description = readWave(stream);
        }
        else if (matches(front.data(), "fLaC"))
        {
            claimed = "it starts as a FLAC but its STREAMINFO block could not be read, so the file "
                      "is truncated or corrupt";
            description = readFlac(stream, fileSize);
        }
        else if (front[0] == 'I' && front[1] == 'D' && front[2] == '3')
        {
            claimed = "it carries an ID3 tag but no readable MPEG audio after it, or its bitrate "
                      "varies with no Xing or Info header to say by how much";
            description = readMp3(stream, fileSize, front);
        }
        else if (front[0] == 0xFFu && (front[1] & 0xE0u) == 0xE0u)
        {
            claimed = "it starts as MPEG audio but is not Layer III, or its bitrate varies with no "
                      "Xing or Info header to say by how much";
            description = readMp3(stream, fileSize, front);
        }
        else if (matches(front.data(), "OggS"))
        {
            // An Ogg that is not Vorbis -- Opus, Theora, FLAC-in-Ogg -- is a container Studio can
            // open and a codec it cannot read, which is a gap rather than a broken file.
            claimed = "it is an Ogg file, but the stream in it is not Vorbis, which is the only "
                      "Ogg codec Studio reads";
            description = readOggVorbis(stream, front);
        }

        if (!description)
        {
            if (outProblem != nullptr && claimed != nullptr) { *outProblem = claimed; }
            return std::nullopt;
        }

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
