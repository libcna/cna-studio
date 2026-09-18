// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Assets/AudioImport.hpp
 * @brief What an audio file says about itself, read without a sound device.
 *
 * `plan.md` STUDIO-10005.
 *
 * ### Why a header read rather than a decode
 *
 * The same reason `readImageDescription` exists next to `studioDecodeImage`: the questions an
 * editor asks about a clip — how long is it, how many channels, what would it cost to hold in
 * memory — are all answered by a few dozen bytes at the front of the file, and decoding a
 * four-minute track to count its samples is a second of work for a number the header already
 * holds. This is in `cna-studio-assets`, one of the CNA-free modules, so it answers in a build with
 * no audio device and on any thread.
 *
 * ### What it reads, and what it admits it cannot
 *
 * WAV and Ogg Vorbis, which between them are `AssetType::SoundEffect` and most of
 * `AssetType::Song`. MP3 and FLAC are reported as unmeasured rather than guessed at: MP3's length
 * is genuinely not in its header when it is variable-bitrate, and a duration that is wrong by a
 * factor of two is worse than one that is absent. `STUDIO-10015` is the rest.
 */

#include <cstdint>
#include <optional>
#include <string>

namespace CNA::Studio
{
    class JsonValue;

    /** @brief What an audio file's header says about itself. */
    struct StudioAudioDescription
    {
        /**
         * @brief "WAV" or "Ogg Vorbis", read from the file's own magic bytes.
         *
         * Never from the extension, for the reason `ImageDescription::format` gives: a renamed
         * file should report what it is rather than what somebody called it.
         */
        std::string format;

        std::uint32_t sampleRate = 0;
        std::uint32_t channels = 0;

        /**
         * @brief Bits a sample in the file. Zero for a compressed format, which has no such number.
         *
         * Zero is not "unknown" here: a Vorbis stream stores coefficients rather than samples, and
         * reporting "16" for one because that is what it decodes to would be an answer to a
         * question nobody asked.
         */
        std::uint32_t bitsPerSample = 0;

        double durationSeconds = 0.0;

        /**
         * @brief What the clip occupies once decoded to sixteen-bit PCM, which is how it is held.
         *
         * The number behind the "Load Into Memory" decision. A footstep is kilobytes and a music
         * track is tens of megabytes, and the file size does not say which — a three-minute Ogg is
         * four megabytes on disk and forty in memory.
         */
        std::uint64_t decodedBytes = 0;

        /** @brief False when nothing could be read, which is different from a zero-length clip. */
        [[nodiscard]] bool isMeasured() const { return sampleRate > 0 && channels > 0; }
    };

    /**
     * @brief Reads @p absolutePath's audio header.
     *
     * @return The description, or std::nullopt when the file cannot be read or is not a format
     *         this reads. A caller must treat "unknown" as unknown rather than as silence.
     */
    [[nodiscard]] std::optional<StudioAudioDescription> readAudioDescription(
        const std::string& absolutePath);

    /** @brief What the user chose about an audio asset. */
    struct StudioAudioImportSettings
    {
        /**
         * @brief Scales the clip once at import rather than at every play. XNA's 0..1 range.
         *
         * Honoured by the editor's own preview, so that auditioning a clip in the inspector plays
         * it at the volume the setting claims. A number that changed nothing audible would be the
         * one place an editor is least able to get away with it.
         */
        float importVolume = 1.0f;

        /**
         * @brief Hold the decoded clip in memory, or stream it from disk.
         *
         * Recorded for the content build rather than acted on by Studio, which streams nothing and
         * says so: this is a decision about the *game*. What Studio contributes is the number that
         * makes it an informed one — see @ref StudioAudioDescription::decodedBytes.
         */
        bool loadIntoMemory = true;

        [[nodiscard]] static StudioAudioImportSettings fromJson(const JsonValue& importerSettings);
    };

    /** @brief @p seconds as `1:23.4`, or `0:04.2` — the form a clip's length is usually written in. */
    [[nodiscard]] std::string studioDescribeDuration(double seconds);
}
