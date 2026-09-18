// SPDX-License-Identifier: MS-PL
/**
 * @file AudioImportTests.cpp
 * @brief What an audio file says about itself, read without a sound device (`plan.md` STUDIO-10005).
 *
 * The number that matters is the one nothing else in the editor could answer: what a clip costs in
 * memory. A file size does not say — a three-minute Ogg is four megabytes on disk and forty once
 * decoded — and "Load Into Memory" is a decision somebody makes without it today.
 *
 * The rest is the usual discipline for a header reader: the chunks are not at fixed offsets, a
 * renamed file reports what it is rather than what it is called, and a format this cannot measure
 * says so instead of guessing.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/Assets/AssetImporters.hpp"
#include "CNA/Studio/Assets/AudioImport.hpp"
#include "CNA/Studio/Core/Json.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    class ScopedDirectory
    {
    public:
        explicit ScopedDirectory(const std::string& name)
        {
            path_ = std::filesystem::temp_directory_path()
                  / ("cna-studio-audio-" + name + "-" + std::to_string(counter()++));
            std::error_code code;
            std::filesystem::remove_all(path_, code);
            std::filesystem::create_directories(path_, code);
        }

        ~ScopedDirectory()
        {
            std::error_code code;
            std::filesystem::remove_all(path_, code);
        }

        ScopedDirectory(const ScopedDirectory&) = delete;
        ScopedDirectory& operator=(const ScopedDirectory&) = delete;

        [[nodiscard]] std::filesystem::path path() const { return path_; }

        std::string write(const std::string& name, const std::vector<unsigned char>& bytes) const
        {
            const std::filesystem::path file = path_ / name;
            std::filesystem::create_directories(file.parent_path());
            std::ofstream stream{file, std::ios::binary};
            stream.write(reinterpret_cast<const char*>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
            return file.generic_string();
        }

    private:
        static int& counter()
        {
            static int value = 0;
            return value;
        }

        std::filesystem::path path_;
    };

    void append32(std::vector<unsigned char>& out, std::uint32_t value)
    {
        for (int shift = 0; shift < 32; shift += 8)
        {
            out.push_back(static_cast<unsigned char>((value >> shift) & 0xFFu));
        }
    }

    void append16(std::vector<unsigned char>& out, std::uint16_t value)
    {
        out.push_back(static_cast<unsigned char>(value & 0xFFu));
        out.push_back(static_cast<unsigned char>((value >> 8) & 0xFFu));
    }

    void appendTag(std::vector<unsigned char>& out, const char* tag)
    {
        for (int index = 0; index < 4; ++index)
        {
            out.push_back(static_cast<unsigned char>(tag[index]));
        }
    }

    /**
     * @brief A RIFF/WAVE file of @p sampleCount silent frames.
     *
     * @param junkChunkBytes A chunk of that many bytes inserted between `fmt ` and `data`, which
     *        is what a DAW's `LIST` or `bext` looks like to a reader. Zero for none.
     */
    std::vector<unsigned char> makeWave(std::uint32_t sampleRate, std::uint16_t channels,
                                        std::uint16_t bitsPerSample, std::uint32_t sampleCount,
                                        std::uint32_t junkChunkBytes = 0)
    {
        const std::uint32_t blockAlign = static_cast<std::uint32_t>(channels) * (bitsPerSample / 8u);
        const std::uint32_t dataBytes = sampleCount * blockAlign;

        std::vector<unsigned char> body;
        appendTag(body, "WAVE");

        appendTag(body, "fmt ");
        append32(body, 16);
        append16(body, 1);  // PCM
        append16(body, channels);
        append32(body, sampleRate);
        append32(body, sampleRate * blockAlign);
        append16(body, static_cast<std::uint16_t>(blockAlign));
        append16(body, bitsPerSample);

        if (junkChunkBytes > 0)
        {
            appendTag(body, "LIST");
            append32(body, junkChunkBytes);
            body.insert(body.end(), junkChunkBytes, 0);
            if (junkChunkBytes % 2 != 0) { body.push_back(0); }
        }

        appendTag(body, "data");
        append32(body, dataBytes);
        body.insert(body.end(), dataBytes, 0);

        std::vector<unsigned char> wave;
        appendTag(wave, "RIFF");
        append32(wave, static_cast<std::uint32_t>(body.size()));
        wave.insert(wave.end(), body.begin(), body.end());
        return wave;
    }

    /** @brief One Ogg page: header, segment table and payload. CRC is left zero -- nothing reads it. */
    void appendOggPage(std::vector<unsigned char>& out, std::uint32_t serial,
                       std::uint32_t sequence, std::uint64_t granule,
                       const std::vector<unsigned char>& payload)
    {
        appendTag(out, "OggS");
        out.push_back(0);  // version
        out.push_back(0);  // header type
        for (int index = 0; index < 8; ++index)
        {
            out.push_back(static_cast<unsigned char>((granule >> (8 * index)) & 0xFFu));
        }
        append32(out, serial);
        append32(out, sequence);
        append32(out, 0);  // checksum

        // Segments are 255 bytes each until the last, which is what tells a reader the packet ended.
        std::vector<unsigned char> segments;
        std::size_t remaining = payload.size();
        while (remaining >= 255)
        {
            segments.push_back(255);
            remaining -= 255;
        }
        segments.push_back(static_cast<unsigned char>(remaining));

        out.push_back(static_cast<unsigned char>(segments.size()));
        out.insert(out.end(), segments.begin(), segments.end());
        out.insert(out.end(), payload.begin(), payload.end());
    }

    /**
     * @brief An Ogg Vorbis stream: an identification page, then @p trailingPages carrying data.
     *
     * The last page's granule position is the stream's sample count, which is the only place a
     * Vorbis stream's length is written down.
     */
    std::vector<unsigned char> makeOggVorbis(std::uint32_t sampleRate, unsigned char channels,
                                             std::uint64_t totalSamples, int trailingPages = 4,
                                             const char* codec = "vorbis")
    {
        std::vector<unsigned char> identification;
        identification.push_back(0x01);
        for (int index = 0; index < 6; ++index)
        {
            identification.push_back(static_cast<unsigned char>(codec[index]));
        }
        append32(identification, 0);  // version
        identification.push_back(channels);
        append32(identification, sampleRate);
        identification.insert(identification.end(), 16, 0);  // bitrates, blocksizes, framing

        std::vector<unsigned char> ogg;
        appendOggPage(ogg, 0xC0FFEEu, 0, 0, identification);

        for (int page = 1; page <= trailingPages; ++page)
        {
            const bool last = page == trailingPages;
            const std::uint64_t granule =
                last ? totalSamples
                     : totalSamples * static_cast<std::uint64_t>(page)
                           / static_cast<std::uint64_t>(trailingPages + 1);
            appendOggPage(ogg, 0xC0FFEEu, static_cast<std::uint32_t>(page), granule,
                          std::vector<unsigned char>(600, 0x55u));
        }
        return ogg;
    }

    bool nearly(double value, double expected, double tolerance = 1e-3)
    {
        return std::fabs(value - expected) <= tolerance;
    }
}

CNA_STUDIO_TEST(AWaveReportsItsRateChannelsAndLengthFromItsHeader)
{
    const ScopedDirectory directory{"wave"};

    // Two seconds of 44.1 kHz stereo.
    const std::string path = directory.write("shot.wav", makeWave(44100, 2, 16, 88200));
    const std::optional<StudioAudioDescription> description = readAudioDescription(path);

    CNA_STUDIO_EXPECT(description.has_value());
    if (!description) { return; }

    CNA_STUDIO_EXPECT_EQ(description->format, std::string{"WAV"});
    CNA_STUDIO_EXPECT_EQ(description->sampleRate, std::uint32_t{44100});
    CNA_STUDIO_EXPECT_EQ(description->channels, std::uint32_t{2});
    CNA_STUDIO_EXPECT_EQ(description->bitsPerSample, std::uint32_t{16});
    CNA_STUDIO_EXPECT(nearly(description->durationSeconds, 2.0));

    // The number nothing else in the editor could answer.
    CNA_STUDIO_EXPECT_EQ(description->decodedBytes, std::uint64_t{88200} * 2u * 2u);
}

CNA_STUDIO_TEST(AWavesChunksAreWalkedRatherThanAssumedToBeAtFixedOffsets)
{
    const ScopedDirectory directory{"wavechunks"};

    // The same clip with a 2 KB chunk between `fmt ` and `data`, which is what a DAW's LIST or
    // bext looks like. A reader that assumed `data` at offset 36 reads its length out of the
    // middle of that chunk and reports a duration off by any factor at all.
    const std::string plain = directory.write("plain.wav", makeWave(22050, 1, 8, 22050));
    const std::string padded = directory.write("padded.wav", makeWave(22050, 1, 8, 22050, 2048));

    const std::optional<StudioAudioDescription> first = readAudioDescription(plain);
    const std::optional<StudioAudioDescription> second = readAudioDescription(padded);
    CNA_STUDIO_EXPECT(first.has_value() && second.has_value());
    if (!first || !second) { return; }

    CNA_STUDIO_EXPECT(nearly(first->durationSeconds, 1.0));
    CNA_STUDIO_EXPECT(nearly(second->durationSeconds, first->durationSeconds));
    CNA_STUDIO_EXPECT_EQ(second->sampleRate, std::uint32_t{22050});
    CNA_STUDIO_EXPECT_EQ(second->bitsPerSample, std::uint32_t{8});

    // Eight-bit mono on disk is still sixteen-bit in memory, which is the whole point of reporting
    // the decoded size rather than the file size.
    CNA_STUDIO_EXPECT_EQ(second->decodedBytes, std::uint64_t{22050} * 2u);
}

CNA_STUDIO_TEST(AnOggVorbisLengthComesFromTheLastPagesGranulePosition)
{
    const ScopedDirectory directory{"ogg"};

    // Three minutes of 48 kHz stereo, which is a music track rather than an effect -- exactly the
    // case the Load Into Memory setting is about.
    const std::uint64_t samples = 48000ull * 180ull;
    const std::string path = directory.write("theme.ogg", makeOggVorbis(48000, 2, samples));

    const std::optional<StudioAudioDescription> description = readAudioDescription(path);
    CNA_STUDIO_EXPECT(description.has_value());
    if (!description) { return; }

    CNA_STUDIO_EXPECT_EQ(description->format, std::string{"Ogg Vorbis"});
    CNA_STUDIO_EXPECT_EQ(description->sampleRate, std::uint32_t{48000});
    CNA_STUDIO_EXPECT_EQ(description->channels, std::uint32_t{2});

    // Compressed, so there is no "bits per sample" to report and zero is the honest answer rather
    // than the sixteen it happens to decode to.
    CNA_STUDIO_EXPECT_EQ(description->bitsPerSample, std::uint32_t{0});
    CNA_STUDIO_EXPECT(nearly(description->durationSeconds, 180.0, 0.01));
    CNA_STUDIO_EXPECT_EQ(description->decodedBytes, samples * 2u * 2u);

    // Not the first page's granule, which is zero, and not an intermediate page's -- the scan has
    // to find the *last* one, which is why it reads the end of the file rather than the start.
    CNA_STUDIO_EXPECT(description->durationSeconds > 100.0);
}

CNA_STUDIO_TEST(AFormatStudioCannotMeasureSaysSoRatherThanGuessing)
{
    const ScopedDirectory directory{"unmeasured"};

    // An MP3, which is the format whose length genuinely is not in its header when it is
    // variable-bitrate. A duration wrong by a factor of two is worse than one that is absent.
    CNA_STUDIO_EXPECT(!readAudioDescription(
        directory.write("track.mp3", std::vector<unsigned char>{0xFFu, 0xFBu, 0x90u, 0x44u, 0, 0,
                                                                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0})));

    // An Ogg container holding something that is not Vorbis -- Opus, say -- is left alone rather
    // than read as Vorbis, whose identification header it does not have.
    const std::string opus =
        directory.write("voice.ogg", makeOggVorbis(48000, 2, 48000, 2, "OpusHe"));
    CNA_STUDIO_EXPECT(!readAudioDescription(opus));

    CNA_STUDIO_EXPECT(!readAudioDescription(directory.write("notes.txt", {'h', 'i'})));
    CNA_STUDIO_EXPECT(!readAudioDescription((directory.path() / "absent.wav").generic_string()));

    // And the format is read from the magic bytes rather than the name, so a renamed file reports
    // what it actually is.
    const std::string renamed = directory.write("actually-a-wave.ogg", makeWave(8000, 1, 16, 8000));
    CNA_STUDIO_EXPECT(readAudioDescription(renamed).has_value());
    CNA_STUDIO_EXPECT_EQ(readAudioDescription(renamed)->format, std::string{"WAV"});
}

CNA_STUDIO_TEST(ADurationIsWrittenTheWayAClipsLengthIsWritten)
{
    CNA_STUDIO_EXPECT_EQ(studioDescribeDuration(0.0), std::string{"0:00.0"});
    CNA_STUDIO_EXPECT_EQ(studioDescribeDuration(4.25), std::string{"0:04.3"});
    CNA_STUDIO_EXPECT_EQ(studioDescribeDuration(83.4), std::string{"1:23.4"});
    CNA_STUDIO_EXPECT_EQ(studioDescribeDuration(600.0), std::string{"10:00.0"});

    // The seconds are padded and the minutes are not, which is how a clock is written and how a
    // column of them stays readable.
    CNA_STUDIO_EXPECT_EQ(studioDescribeDuration(65.0), std::string{"1:05.0"});
}

CNA_STUDIO_TEST(AudioSettingsReadBackTheDeclaredDefaultsRatherThanZeroes)
{
    const StudioAudioImportSettings untouched =
        StudioAudioImportSettings::fromJson(JsonValue::makeObject());

    // A missing Import Volume that read back as zero would make every clip nobody had touched
    // preview as silence, which is indistinguishable from a broken audio device.
    CNA_STUDIO_EXPECT(untouched.importVolume > 0.999f);
    CNA_STUDIO_EXPECT(untouched.loadIntoMemory);
    CNA_STUDIO_EXPECT(StudioAudioImportSettings::fromJson(JsonValue{}).loadIntoMemory);

    JsonValue chosen = JsonValue::makeObject();
    chosen.set("importVolume", JsonValue{0.25});
    chosen.set("loadIntoMemory", JsonValue{false});

    const StudioAudioImportSettings edited = StudioAudioImportSettings::fromJson(chosen);
    CNA_STUDIO_EXPECT(nearly(edited.importVolume, 0.25, 1e-5));
    CNA_STUDIO_EXPECT(!edited.loadIntoMemory);
}

CNA_STUDIO_TEST(BothAudioTypesHaveAnImporterAndReportTheirFactsIntoTheirSidecars)
{
    const ScopedDirectory directory{"audiofacts"};
    directory.write("Audio/step.wav", makeWave(44100, 1, 16, 22050));
    directory.write("Audio/theme.ogg", makeOggVorbis(44100, 2, 44100ull * 90ull));

    AssetDatabase assets;
    assets.setProjectRoot(directory.path().generic_string());
    CNA_STUDIO_EXPECT(assets.scan("Audio").succeeded);
    CNA_STUDIO_EXPECT_EQ(assets.getCount(), std::size_t{2});

    // Both types, not just the one. `AssetDatabase` has assigned `CNA.SongImporter` to every .ogg
    // since it was written and nothing registered one, so the inspector told a user their music's
    // importer "is not registered in this build" -- which reads as a broken installation.
    ComponentRegistry importers;
    registerBuiltinImporters(importers);
    CNA_STUDIO_EXPECT(importers.find(ImporterIds::kSoundEffect) != nullptr);
    CNA_STUDIO_EXPECT(importers.find(ImporterIds::kSong) != nullptr);

    CNA_STUDIO_EXPECT_EQ(applyImporterFacts(assets), std::size_t{2});

    const AssetRecord* effect = assets.findByPath("Audio/step.wav");
    const AssetRecord* song = assets.findByPath("Audio/theme.ogg");
    CNA_STUDIO_EXPECT(effect != nullptr && song != nullptr);
    if (effect == nullptr || song == nullptr) { return; }

    CNA_STUDIO_EXPECT(effect->type == AssetType::SoundEffect);
    CNA_STUDIO_EXPECT(song->type == AssetType::Song);

    CNA_STUDIO_EXPECT_EQ(effect->importerSettings["duration"].asString(), std::string{"0:00.5"});
    CNA_STUDIO_EXPECT_EQ(effect->importerSettings["sourceFormat"].asString(), std::string{"WAV"});
    CNA_STUDIO_EXPECT(nearly(effect->importerSettings["channels"].asNumber(), 1.0));

    CNA_STUDIO_EXPECT_EQ(song->importerSettings["duration"].asString(), std::string{"1:30.0"});
    CNA_STUDIO_EXPECT_EQ(song->importerSettings["sourceFormat"].asString(), std::string{"Ogg Vorbis"});
    CNA_STUDIO_EXPECT(nearly(song->importerSettings["decodedBytes"].asNumber(),
                             static_cast<double>(44100ull * 90ull * 2ull * 2ull)));

    // Nothing changed, so nothing is written -- the rule that keeps opening a project twice from
    // producing a diff.
    CNA_STUDIO_EXPECT_EQ(applyImporterFacts(assets), std::size_t{0});
}

CNA_STUDIO_TEST(EverySettingAnAudioImporterDeclaresIsOneSomethingReads)
{
    // The same policy the model importer is held to. `importVolume` reaches the inspector's
    // preview and `loadIntoMemory` is recorded for the content build; a third setting appearing
    // here that `fromJson` does not read would be a control that does nothing.
    ComponentRegistry importers;
    registerBuiltinImporters(importers);

    for (const char* id : {ImporterIds::kSoundEffect, ImporterIds::kSong})
    {
        const ComponentDescriptor* descriptor = importers.find(id);
        CNA_STUDIO_EXPECT(descriptor != nullptr);
        if (descriptor == nullptr) { continue; }

        for (const PropertyDescriptor& property : descriptor->properties)
        {
            if (property.readOnly) { continue; }

            JsonValue settings = JsonValue::makeObject();
            if (property.type == PropertyType::Boolean)
            {
                settings.set(property.name, JsonValue{!property.defaultValue.get<bool>()});
            }
            else if (property.type == PropertyType::Float)
            {
                settings.set(property.name,
                             JsonValue{static_cast<double>(property.defaultValue.get<float>()) / 2.0});
            }
            else
            {
                CnaStudioTest::reportFailure(__FILE__, __LINE__,
                                             "an audio importer declares a setting of a kind this "
                                             "case does not know how to change: " + property.name);
                continue;
            }

            const StudioAudioImportSettings read = StudioAudioImportSettings::fromJson(settings);
            const StudioAudioImportSettings defaults;
            if (nearly(read.importVolume, defaults.importVolume, 1e-5)
                && read.loadIntoMemory == defaults.loadIntoMemory)
            {
                CnaStudioTest::reportFailure(__FILE__, __LINE__,
                                             std::string{id} + " declares '" + property.name
                                                 + "', and changing it changes nothing anything "
                                                   "reads.");
            }
        }
    }
}
