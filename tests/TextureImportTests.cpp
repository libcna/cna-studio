// SPDX-License-Identifier: MS-PL
/**
 * @file TextureImportTests.cpp
 * @brief What a texture's import settings actually produce (`plan.md` STUDIO-10003).
 *
 * The settings themselves are not where this goes wrong. What goes wrong is the gap between what
 * was asked for and what can be given: DXT on an edge that is not a multiple of four, an opaque
 * texture that wants hardware sRGB when CNA has no sRGB DXT1, a file nothing has measured. Each of
 * those resolves to something the user did not choose, and the only thing standing between that and
 * a texture that is quietly the wrong format is that Studio says so.
 *
 * The other half is the facts the plan stands on. A source format read from a file's *name* would
 * make a renamed file report confident nonsense, and an alpha flag that guessed would turn every
 * opaque PNG into a DXT5 at twice the size.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/Assets/AssetImporters.hpp"
#include "CNA/Studio/Assets/TextureImport.hpp"
#include "CNA/Studio/Core/Json.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    /** @brief A temporary directory that removes itself. */
    class ScopedDirectory
    {
    public:
        explicit ScopedDirectory(const std::string& name)
        {
            path_ = std::filesystem::temp_directory_path()
                  / ("cna-studio-texture-" + name + "-" + std::to_string(counter()++));
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

    void appendBigEndian(std::vector<unsigned char>& out, std::uint32_t value)
    {
        out.push_back(static_cast<unsigned char>((value >> 24) & 0xFFu));
        out.push_back(static_cast<unsigned char>((value >> 16) & 0xFFu));
        out.push_back(static_cast<unsigned char>((value >> 8) & 0xFFu));
        out.push_back(static_cast<unsigned char>(value & 0xFFu));
    }

    /**
     * @brief A PNG's header chunks, with whatever colour type and transparency are asked for.
     *
     * Only the header matters here -- `readImageDescription` never decodes -- so the image data is
     * a single empty `IDAT`, and the chunk CRCs are zero. That is deliberate rather than lazy: it
     * keeps each case to the four bytes the case is *about*, and `ImageDecodeTests` already covers
     * a real, valid, decodable PNG.
     */
    std::vector<unsigned char> makePngHeader(std::uint32_t width, std::uint32_t height,
                                             unsigned char colorType, bool withTransparencyChunk)
    {
        std::vector<unsigned char> png{0x89u, 'P', 'N', 'G', 0x0Du, 0x0Au, 0x1Au, 0x0Au};

        const auto chunk = [&](const char (&type)[5], const std::vector<unsigned char>& data) {
            appendBigEndian(png, static_cast<std::uint32_t>(data.size()));
            for (int index = 0; index < 4; ++index)
            {
                png.push_back(static_cast<unsigned char>(type[index]));
            }
            png.insert(png.end(), data.begin(), data.end());
            appendBigEndian(png, 0);
        };

        std::vector<unsigned char> header;
        appendBigEndian(header, width);
        appendBigEndian(header, height);
        header.push_back(8);  // bit depth
        header.push_back(colorType);
        header.push_back(0);  // compression
        header.push_back(0);  // filter
        header.push_back(0);  // interlace
        chunk("IHDR", header);

        if (colorType == 3) { chunk("PLTE", {0xFFu, 0x00u, 0x00u}); }
        if (withTransparencyChunk) { chunk("tRNS", {0x00u}); }

        chunk("IDAT", {});
        chunk("IEND", {});
        return png;
    }

    /** @brief A BMP's `BITMAPINFOHEADER`, at whatever bit depth is asked for. */
    std::vector<unsigned char> makeBmpHeader(std::int32_t width, std::int32_t height,
                                             std::uint16_t bitCount)
    {
        std::vector<unsigned char> bmp;
        const auto little32 = [&](std::uint32_t value) {
            for (int shift = 0; shift < 32; shift += 8)
            {
                bmp.push_back(static_cast<unsigned char>((value >> shift) & 0xFFu));
            }
        };
        const auto little16 = [&](std::uint16_t value) {
            bmp.push_back(static_cast<unsigned char>(value & 0xFFu));
            bmp.push_back(static_cast<unsigned char>((value >> 8) & 0xFFu));
        };

        bmp.push_back('B');
        bmp.push_back('M');
        little32(54);  // file size
        little32(0);   // reserved
        little32(54);  // pixel data offset
        little32(40);  // DIB header size
        little32(static_cast<std::uint32_t>(width));
        little32(static_cast<std::uint32_t>(height));
        little16(1);   // planes
        little16(bitCount);
        little32(0);   // compression
        little32(0);   // image size
        little32(0);   // horizontal resolution
        little32(0);   // vertical resolution
        little32(0);   // palette entries
        little32(0);   // important colours
        return bmp;
    }

    StudioTextureSource sourceOf(int width, int height, bool alpha)
    {
        StudioTextureSource source;
        source.width = width;
        source.height = height;
        source.hasAlphaChannel = alpha;
        return source;
    }

    bool anyNoteMentions(const StudioTextureImportPlan& plan, const std::string& fragment)
    {
        for (const std::string& note : plan.notes)
        {
            if (note.find(fragment) != std::string::npos) { return true; }
        }
        return false;
    }
}

CNA_STUDIO_TEST(AMipChainIsCountedDownToASinglePixel)
{
    CNA_STUDIO_EXPECT_EQ(studioMipLevelCount(1024, 1024), std::uint32_t{11});
    CNA_STUDIO_EXPECT_EQ(studioMipLevelCount(1, 1), std::uint32_t{1});

    // The *longest* edge decides, not the shorter one: a 256x64 texture keeps halving after its
    // height has bottomed out at one pixel, and stopping at the short edge would under-count by
    // two levels and under-report the memory with it.
    CNA_STUDIO_EXPECT_EQ(studioMipLevelCount(256, 64), std::uint32_t{9});

    // Non-power-of-two halves by truncation, the way a GPU does: 100, 50, 25, 12, 6, 3, 1.
    CNA_STUDIO_EXPECT_EQ(studioMipLevelCount(100, 100), std::uint32_t{7});

    // Unmeasured is zero levels, which no texture has -- so a caller cannot mistake it for one.
    CNA_STUDIO_EXPECT_EQ(studioMipLevelCount(0, 512), std::uint32_t{0});
}

CNA_STUDIO_TEST(ASettingAbsentFromTheSidecarIsItsDeclaredDefaultRatherThanZero)
{
    // The failure this exists to prevent: reading a missing boolean as false would turn
    // premultiplied alpha off for every asset nobody has ever touched, which is most of them, and
    // would halo the edge of every sprite in a default project.
    const StudioTextureImportSettings settings =
        StudioTextureImportSettings::fromJson(JsonValue::makeObject());

    CNA_STUDIO_EXPECT(settings.premultiplyAlpha);
    CNA_STUDIO_EXPECT(!settings.generateMipmaps);
    CNA_STUDIO_EXPECT(!settings.srgbRead);
    CNA_STUDIO_EXPECT(settings.outputFormat == StudioTextureOutputFormat::Color);

    // A null sidecar -- an asset with no `importerSettings` object at all -- reads the same way.
    CNA_STUDIO_EXPECT(StudioTextureImportSettings::fromJson(JsonValue{}).premultiplyAlpha);

    JsonValue chosen = JsonValue::makeObject();
    chosen.set("outputFormat", JsonValue{std::string{"DxtCompressed"}});
    chosen.set("srgbRead", JsonValue{true});
    chosen.set("generateMipmaps", JsonValue{true});
    chosen.set("premultiplyAlpha", JsonValue{false});

    const StudioTextureImportSettings edited = StudioTextureImportSettings::fromJson(chosen);
    CNA_STUDIO_EXPECT(edited.outputFormat == StudioTextureOutputFormat::DxtCompressed);
    CNA_STUDIO_EXPECT(edited.srgbRead);
    CNA_STUDIO_EXPECT(edited.generateMipmaps);
    CNA_STUDIO_EXPECT(!edited.premultiplyAlpha);
}

CNA_STUDIO_TEST(CompressionPicksDxt1WithoutAlphaAndDxt5With)
{
    StudioTextureImportSettings settings;
    settings.outputFormat = StudioTextureOutputFormat::DxtCompressed;

    const StudioTextureImportPlan opaque = studioPlanTextureImport(settings, sourceOf(256, 256, false));
    CNA_STUDIO_EXPECT_EQ(opaque.surfaceFormat, std::string{"Dxt1"});
    CNA_STUDIO_EXPECT(opaque.isExactlyAsAsked());

    const StudioTextureImportPlan translucent =
        studioPlanTextureImport(settings, sourceOf(256, 256, true));
    CNA_STUDIO_EXPECT_EQ(translucent.surfaceFormat, std::string{"Dxt5"});
    CNA_STUDIO_EXPECT(translucent.isExactlyAsAsked());

    // Dxt5 is twice Dxt1, which is the whole reason the alpha *fact* is read from the file rather
    // than assumed: assuming would double every opaque texture in a compressed project.
    CNA_STUDIO_EXPECT_EQ(translucent.estimatedBytes, opaque.estimatedBytes * 2u);

    // DXT1 is four bits a pixel and DXT5 is eight, against an uncompressed thirty-two -- so the
    // saving is eight-fold and four-fold, not the "a quarter" both are loosely called.
    const StudioTextureImportPlan uncompressed =
        studioPlanTextureImport(StudioTextureImportSettings{}, sourceOf(256, 256, false));
    CNA_STUDIO_EXPECT_EQ(uncompressed.surfaceFormat, std::string{"Color"});
    CNA_STUDIO_EXPECT_EQ(uncompressed.estimatedBytes, opaque.estimatedBytes * 8u);
    CNA_STUDIO_EXPECT_EQ(uncompressed.estimatedBytes, translucent.estimatedBytes * 4u);
}

CNA_STUDIO_TEST(CompressionIsRefusedOnAnEdgeThatIsNotAMultipleOfFourAndSaidSo)
{
    StudioTextureImportSettings settings;
    settings.outputFormat = StudioTextureOutputFormat::DxtCompressed;

    const StudioTextureImportPlan plan = studioPlanTextureImport(settings, sourceOf(100, 62, false));

    // Resolved back to Color rather than padded. Padding invents a row of pixels the artist did
    // not draw, and it shows up as a seam when the texture is sampled -- a decision about somebody
    // else's art, made silently, is exactly what this refuses to do.
    CNA_STUDIO_EXPECT_EQ(plan.surfaceFormat, std::string{"Color"});
    CNA_STUDIO_EXPECT(!plan.isExactlyAsAsked());
    CNA_STUDIO_EXPECT(anyNoteMentions(plan, "multiple of 4"));

    // The note names the size, because "this texture" is not findable and "100 x 62" is.
    CNA_STUDIO_EXPECT(anyNoteMentions(plan, "100 x 62"));

    // One edge is enough to refuse it: 4x4 blocks need both.
    CNA_STUDIO_EXPECT_EQ(studioPlanTextureImport(settings, sourceOf(256, 254, false)).surfaceFormat,
                         std::string{"Color"});
    CNA_STUDIO_EXPECT(studioPlanTextureImport(settings, sourceOf(256, 256, false)).isExactlyAsAsked());
}

CNA_STUDIO_TEST(AnOpaqueSrgbTextureRunsIntoCnaHavingNoSrgbDxt1)
{
    // A CNA gap, recorded as one. `SurfaceFormat` has `Dxt5SrgbEXT` and `Bc7SrgbEXT` and no sRGB
    // DXT1, although D3D and OpenGL both define one. So an opaque texture that wants hardware sRGB
    // has to be given the format with an alpha channel it does not need, at twice the size.
    StudioTextureImportSettings settings;
    settings.outputFormat = StudioTextureOutputFormat::DxtCompressed;
    settings.srgbRead = true;

    const StudioTextureImportPlan opaque = studioPlanTextureImport(settings, sourceOf(256, 256, false));
    CNA_STUDIO_EXPECT_EQ(opaque.surfaceFormat, std::string{"Dxt5SrgbEXT"});
    CNA_STUDIO_EXPECT(!opaque.isExactlyAsAsked());
    CNA_STUDIO_EXPECT(anyNoteMentions(opaque, "no sRGB DXT1"));

    // The note says what to do about it, because a user reading "CNA has no sRGB DXT1" has no way
    // to know that the setting they can actually reach is the one next to it.
    CNA_STUDIO_EXPECT(anyNoteMentions(opaque, "sRGB Read off"));

    // A texture that *does* carry alpha asks for the same format and gets it with no complaint,
    // which is what makes the note above about the substitution rather than about sRGB.
    const StudioTextureImportPlan translucent =
        studioPlanTextureImport(settings, sourceOf(256, 256, true));
    CNA_STUDIO_EXPECT_EQ(translucent.surfaceFormat, std::string{"Dxt5SrgbEXT"});
    CNA_STUDIO_EXPECT(translucent.isExactlyAsAsked());

    // Uncompressed sRGB has a format of its own and needs no substitution at all.
    StudioTextureImportSettings uncompressed;
    uncompressed.srgbRead = true;
    const StudioTextureImportPlan plain = studioPlanTextureImport(uncompressed, sourceOf(100, 62, false));
    CNA_STUDIO_EXPECT_EQ(plain.surfaceFormat, std::string{"ColorSrgbEXT"});
    CNA_STUDIO_EXPECT(plain.isExactlyAsAsked());
}

CNA_STUDIO_TEST(MipmapsCostWhatThePlanSaysTheyCost)
{
    StudioTextureImportSettings settings;
    const StudioTextureImportPlan flat = studioPlanTextureImport(settings, sourceOf(1024, 1024, true));
    CNA_STUDIO_EXPECT_EQ(flat.mipLevels, std::uint32_t{1});
    CNA_STUDIO_EXPECT_EQ(flat.estimatedBytes, std::uint64_t{1024} * 1024u * 4u);

    settings.generateMipmaps = true;
    const StudioTextureImportPlan chained = studioPlanTextureImport(settings, sourceOf(1024, 1024, true));
    CNA_STUDIO_EXPECT_EQ(chained.mipLevels, std::uint32_t{11});
    CNA_STUDIO_EXPECT_EQ(chained.estimatedBytes, std::uint64_t{5592404});

    // The importer's tooltip says "a third more memory", and this is the arithmetic behind it.
    CNA_STUDIO_EXPECT(chained.estimatedBytes < flat.estimatedBytes * 4u / 3u);
    CNA_STUDIO_EXPECT(chained.estimatedBytes > flat.estimatedBytes);

    // A DXT chain does not bottom out at a byte: its unit is a 4x4 block, so the last three levels
    // cost a whole block each however few pixels are in them. That is the part a hand-rolled
    // estimate gets wrong, and it is why the chain is walked rather than multiplied by 4/3.
    settings.outputFormat = StudioTextureOutputFormat::DxtCompressed;
    const StudioTextureImportPlan compressed =
        studioPlanTextureImport(settings, sourceOf(1024, 1024, false));
    CNA_STUDIO_EXPECT_EQ(compressed.estimatedBytes, std::uint64_t{699064});
    CNA_STUDIO_EXPECT(compressed.estimatedBytes > chained.estimatedBytes / 8u);
}

CNA_STUDIO_TEST(AFileNothingHasMeasuredIsSaidToBeUnmeasuredRatherThanFree)
{
    StudioTextureImportSettings settings;
    settings.outputFormat = StudioTextureOutputFormat::DxtCompressed;
    settings.generateMipmaps = true;

    const StudioTextureImportPlan plan = studioPlanTextureImport(settings, StudioTextureSource{});

    // Zero bytes is the one number in an inspector that would be wrong rather than missing: it
    // reads as "this costs nothing", and the honest answer is "nobody knows".
    CNA_STUDIO_EXPECT_EQ(plan.mipLevels, std::uint32_t{0});
    CNA_STUDIO_EXPECT_EQ(plan.estimatedBytes, std::uint64_t{0});
    CNA_STUDIO_EXPECT(!plan.isExactlyAsAsked());
    CNA_STUDIO_EXPECT(anyNoteMentions(plan, "cannot read this file's header"));

    // And no format at all, which is the part it would be easy to get wrong. Resolving one anyway
    // looks harmless -- the format does not depend on the *size* -- but it depends on the alpha
    // channel, and an unmeasured source's `hasAlphaChannel` is a struct default rather than a
    // fact. "Dxt1" reads as a decision somebody can rely on, and this one would be a coin toss.
    CNA_STUDIO_EXPECT(plan.surfaceFormat.empty());
}

CNA_STUDIO_TEST(AHeaderSaysWhichFormatItIsAndWhetherItCarriesAlpha)
{
    const ScopedDirectory directory{"headers"};

    const std::string rgba = directory.write("rgba.png", makePngHeader(64, 32, 6, false));
    const std::optional<ImageDescription> rgbaDescription = readImageDescription(rgba);
    CNA_STUDIO_EXPECT(rgbaDescription.has_value());
    CNA_STUDIO_EXPECT_EQ(rgbaDescription->format, std::string{"PNG"});
    CNA_STUDIO_EXPECT_EQ(rgbaDescription->width, 64);
    CNA_STUDIO_EXPECT_EQ(rgbaDescription->height, 32);
    CNA_STUDIO_EXPECT(rgbaDescription->hasAlphaChannel);

    const std::string rgb = directory.write("rgb.png", makePngHeader(64, 32, 2, false));
    CNA_STUDIO_EXPECT(!readImageDescription(rgb)->hasAlphaChannel);

    // Grey with alpha is colour type 4, which is the one an "is it 6" check misses.
    const std::string grey = directory.write("grey.png", makePngHeader(8, 8, 4, false));
    CNA_STUDIO_EXPECT(readImageDescription(grey)->hasAlphaChannel);

    // A paletted PNG is the case the fixed header cannot answer: its palette entries are opaque
    // and its transparency arrives later, in an optional `tRNS` chunk.
    const std::string palette = directory.write("palette.png", makePngHeader(8, 8, 3, false));
    CNA_STUDIO_EXPECT(!readImageDescription(palette)->hasAlphaChannel);

    const std::string paletteAlpha = directory.write("palette-alpha.png", makePngHeader(8, 8, 3, true));
    CNA_STUDIO_EXPECT(readImageDescription(paletteAlpha)->hasAlphaChannel);

    // BMP: 32 bits is the only depth with a fourth channel.
    const std::string bmp32 = directory.write("deep.bmp", makeBmpHeader(20, 12, 32));
    CNA_STUDIO_EXPECT_EQ(readImageDescription(bmp32)->format, std::string{"BMP"});
    CNA_STUDIO_EXPECT_EQ(readImageDescription(bmp32)->width, 20);
    CNA_STUDIO_EXPECT(readImageDescription(bmp32)->hasAlphaChannel);
    CNA_STUDIO_EXPECT(!readImageDescription(directory.write("flat.bmp", makeBmpHeader(20, 12, 24)))
                           ->hasAlphaChannel);

    // A top-down BMP writes its height negative. The magnitude is the height; the sign is which
    // way up the rows are, which is the decoder's problem and not the inspector's.
    CNA_STUDIO_EXPECT_EQ(readImageDescription(directory.write("topdown.bmp",
                                                              makeBmpHeader(20, -12, 24)))->height,
                         12);

    // The format is read from the magic bytes, never from the name -- so a renamed file reports
    // what it actually is instead of what somebody called it.
    const std::string renamed = directory.write("actually-a-png.jpg", makePngHeader(4, 4, 6, false));
    CNA_STUDIO_EXPECT_EQ(readImageDescription(renamed)->format, std::string{"PNG"});

    // And something that is not an image at all is unknown rather than guessed at.
    CNA_STUDIO_EXPECT(!readImageDescription(directory.write("notes.txt", {'h', 'e', 'l', 'l', 'o'})));
    CNA_STUDIO_EXPECT(!readImageDescription((directory.path() / "absent.png").generic_string()));

    // readImageSize answers the same thing, since it is now a wrapper over this one. Two readers
    // that could disagree about a size is the bug this rules out.
    const std::optional<ImageSize> size = readImageSize(rgba);
    CNA_STUDIO_EXPECT(size.has_value());
    CNA_STUDIO_EXPECT_EQ(size->width, 64);
    CNA_STUDIO_EXPECT_EQ(size->height, 32);
}

CNA_STUDIO_TEST(AReimportRewritesTheTextureFactsAndLeavesEverySettingAlone)
{
    const ScopedDirectory directory{"reimport"};
    directory.write("Textures/Hero.png", makePngHeader(64, 64, 2, false));

    AssetDatabase assets;
    assets.setProjectRoot(directory.path().generic_string());
    assets.scan("Textures");
    CNA_STUDIO_EXPECT_EQ(assets.getCount(), std::size_t{1});

    const Uuid id = assets.getAll().front()->id;
    CNA_STUDIO_EXPECT(applyImporterFacts(assets, id));

    CNA_STUDIO_EXPECT_EQ(assets.find(id)->importerSettings["sourceFormat"].asString(),
                         std::string{"PNG"});
    CNA_STUDIO_EXPECT(!assets.find(id)->importerSettings["sourceAlpha"].asBoolean(true));

    // The facts pass is idempotent: running it again on a file that has not moved writes nothing,
    // which is what keeps opening a project twice from producing a repository full of diffs.
    CNA_STUDIO_EXPECT(!applyImporterFacts(assets, id));

    // A setting the user chose, sitting next to those facts.
    AssetRecord* record = assets.findMutable(id);
    CNA_STUDIO_EXPECT(record != nullptr);
    if (record->importerSettings.isNull()) { record->importerSettings = JsonValue::makeObject(); }
    record->importerSettings.set("outputFormat", JsonValue{std::string{"DxtCompressed"}});
    record->importerSettings.set("srgbRead", JsonValue{true});
    assets.writeSidecar(id);

    // The file is re-exported with an alpha channel and a different size.
    directory.write("Textures/Hero.png", makePngHeader(100, 62, 6, false));
    CNA_STUDIO_EXPECT(applyImporterFacts(assets, id));

    CNA_STUDIO_EXPECT(assets.find(id)->importerSettings["sourceAlpha"].asBoolean(false));

    // The settings survived, which is the whole point of the fact/setting split (STUDIO-10001):
    // re-exporting a texture must not revert a decision somebody made about it.
    const StudioTextureImportSettings settings =
        StudioTextureImportSettings::fromJson(assets.find(id)->importerSettings);
    CNA_STUDIO_EXPECT(settings.outputFormat == StudioTextureOutputFormat::DxtCompressed);
    CNA_STUDIO_EXPECT(settings.srgbRead);

    // And the plan built from both now reports what the new file can actually be given: 100x100
    // is not a multiple of four, so the compression the user asked for cannot be honoured, and the
    // substitution is said rather than silently made.
    StudioTextureSource source;
    const StudioVector2 pixels =
        PropertyValue::fromJson(assets.find(id)->importerSettings["pixelSize"], PropertyType::Vector2)
            .get<StudioVector2>();
    source.width = static_cast<int>(pixels.x);
    source.height = static_cast<int>(pixels.y);
    source.hasAlphaChannel = assets.find(id)->importerSettings["sourceAlpha"].asBoolean(false);

    CNA_STUDIO_EXPECT_EQ(source.width, 100);
    CNA_STUDIO_EXPECT_EQ(source.height, 62);
    const StudioTextureImportPlan plan = studioPlanTextureImport(settings, source);
    CNA_STUDIO_EXPECT_EQ(plan.surfaceFormat, std::string{"ColorSrgbEXT"});
    CNA_STUDIO_EXPECT(anyNoteMentions(plan, "multiple of 4"));
}
