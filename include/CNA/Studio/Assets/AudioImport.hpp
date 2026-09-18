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
 * WAV, FLAC, MP3 and Ogg Vorbis, which between them are `AssetType::SoundEffect` and
 * `AssetType::Song`. Each is read exactly or not at all -- nothing here estimates:
 *
 * - **WAV** states its format and data size in chunks, which are walked because they are not at
 *   fixed offsets.
 * - **FLAC** states the rate, the channels, the depth and the total sample count outright in
 *   `STREAMINFO`. The easiest of the four.
 * - **Ogg Vorbis** states the rate and channels in its identification header and its *length*
 *   nowhere -- that is the granule position of the last page, found by reading the end of the file.
 * - **MP3** has three cases and Studio can tell which it is in: a Xing or Info tag states the frame
 *   count exactly; no tag with an unchanging bitrate is constant-bitrate, where the length is
 *   arithmetic on the file size and likewise exact; and no tag with a bitrate that *does* change is
 *   declined, because the only exact answer costs a walk of the whole file. See `readMp3`.
 *
 * `STUDIO-10015`. An Ogg holding something other than Vorbis -- Opus, Theora, FLAC-in-Ogg -- is
 * still declined, with a reason that says so.
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
         * @brief Bits a sample in the file. Zero for a lossy format, which has no such number.
         *
         * Zero is not "unknown" here: a Vorbis or MP3 stream stores frequency coefficients rather
         * than samples, and reporting "16" for one because that is what it decodes to would be an
         * answer to a question nobody asked. FLAC is lossless, so its samples are samples and it
         * reports a real depth -- which is still not what it costs in memory, because the runtime
         * holds sixteen-bit PCM whatever the file had.
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
     * @param absolutePath The file.
     * @param outProblem When set, receives a reason *only* when the file announces itself as one of
     *        the four formats and then cannot be read anyway -- including the variable-bitrate MP3
     *        with no header, whose reason says exactly that. A file that is not audio at all leaves
     *        it empty, because that is not a problem (`plan.md` STUDIO-10013).
     * @return The description, or std::nullopt when the file cannot be read or is not a format
     *         this reads. A caller must treat "unknown" as unknown rather than as silence.
     */
    [[nodiscard]] std::optional<StudioAudioDescription> readAudioDescription(
        const std::string& absolutePath, std::string* outProblem = nullptr);

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
