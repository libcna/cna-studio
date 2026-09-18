// SPDX-License-Identifier: MS-PL
/**
 * @file ImageDecodeTests.cpp
 * @brief Pixels without a graphics device, on any thread (`plan.md` STUDIO-10014).
 *
 * The property that matters is not "it decodes a PNG" — a decoder that did not would be obvious.
 * It is that a *bad* file is an error message rather than a crash, and that the decoder can be run
 * from several threads at once, because both are what `STUDIO-09003`'s background thumbnails stand
 * on. The input is a file somebody dropped in a project folder: it may be truncated, it may be a
 * rename of something else entirely, and it may be hostile.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Assets/AssetImporters.hpp"
#include "CNA/Studio/Assets/ImageDecode.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
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
                  / ("cna-studio-image-" + name + "-" + std::to_string(counter()++));
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

        [[nodiscard]] std::string file(const std::string& name) const
        {
            return (path_ / name).generic_string();
        }

        void write(const std::string& name, const std::vector<unsigned char>& bytes) const
        {
            std::ofstream stream{path_ / name, std::ios::binary};
            stream.write(reinterpret_cast<const char*>(bytes.data()),
                         static_cast<std::streamsize>(bytes.size()));
        }

    private:
        static int& counter() { static int value = 0; return value; }
        std::filesystem::path path_;
    };

    /** @brief Big-endian 32-bit, which is what every length and CRC in a PNG is. */
    void appendBigEndian(std::vector<unsigned char>& out, std::uint32_t value)
    {
        out.push_back(static_cast<unsigned char>((value >> 24) & 0xFFu));
        out.push_back(static_cast<unsigned char>((value >> 16) & 0xFFu));
        out.push_back(static_cast<unsigned char>((value >> 8) & 0xFFu));
        out.push_back(static_cast<unsigned char>(value & 0xFFu));
    }

    std::uint32_t crc32Of(const std::vector<unsigned char>& bytes, std::size_t from)
    {
        static std::uint32_t table[256];
        static bool built = false;
        if (!built)
        {
            for (std::uint32_t i = 0; i < 256u; ++i)
            {
                std::uint32_t value = i;
                for (int bit = 0; bit < 8; ++bit)
                {
                    value = (value & 1u) != 0u ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
                }
                table[i] = value;
            }
            built = true;
        }

        std::uint32_t crc = 0xFFFFFFFFu;
        for (std::size_t i = from; i < bytes.size(); ++i)
        {
            crc = table[(crc ^ bytes[i]) & 0xFFu] ^ (crc >> 8);
        }
        return crc ^ 0xFFFFFFFFu;
    }

    void appendChunk(std::vector<unsigned char>& out, const char (&type)[5],
                     const std::vector<unsigned char>& data)
    {
        appendBigEndian(out, static_cast<std::uint32_t>(data.size()));
        const std::size_t crcFrom = out.size();
        for (int i = 0; i < 4; ++i) { out.push_back(static_cast<unsigned char>(type[i])); }
        out.insert(out.end(), data.begin(), data.end());
        appendBigEndian(out, crc32Of(out, crcFrom));
    }

    /**
     * @brief A real @p width by @p height RGBA PNG, built here rather than committed.
     *
     * Written out because a binary fixture is a file nobody can read in a review and nobody can
     * adjust without a tool. The image data uses zlib's stored (uncompressed) blocks, which is a
     * legal deflate stream and keeps this to arithmetic rather than a compressor.
     */
    std::vector<unsigned char> makePng(std::uint32_t width, std::uint32_t height,
                                       unsigned char red, unsigned char green, unsigned char blue)
    {
        std::vector<unsigned char> raw;
        for (std::uint32_t y = 0; y < height; ++y)
        {
            raw.push_back(0);  // filter: none
            for (std::uint32_t x = 0; x < width; ++x)
            {
                raw.push_back(red);
                raw.push_back(green);
                raw.push_back(blue);
                raw.push_back(0xFFu);
            }
        }

        // zlib container around stored deflate blocks.
        std::vector<unsigned char> zlib{0x78u, 0x01u};
        std::size_t offset = 0;
        while (offset < raw.size())
        {
            const std::size_t block = std::min<std::size_t>(65535u, raw.size() - offset);
            const bool last = offset + block >= raw.size();
            zlib.push_back(last ? 1u : 0u);
            zlib.push_back(static_cast<unsigned char>(block & 0xFFu));
            zlib.push_back(static_cast<unsigned char>((block >> 8) & 0xFFu));
            zlib.push_back(static_cast<unsigned char>(~block & 0xFFu));
            zlib.push_back(static_cast<unsigned char>((~block >> 8) & 0xFFu));
            zlib.insert(zlib.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                        raw.begin() + static_cast<std::ptrdiff_t>(offset + block));
            offset += block;
        }

        std::uint32_t s1 = 1;
        std::uint32_t s2 = 0;
        for (const unsigned char byte : raw)
        {
            s1 = (s1 + byte) % 65521u;
            s2 = (s2 + s1) % 65521u;
        }
        appendBigEndian(zlib, (s2 << 16) | s1);

        std::vector<unsigned char> png{0x89u, 'P', 'N', 'G', 0x0Du, 0x0Au, 0x1Au, 0x0Au};

        std::vector<unsigned char> header;
        appendBigEndian(header, width);
        appendBigEndian(header, height);
        header.push_back(8);  // bit depth
        header.push_back(6);  // colour type: RGBA
        header.push_back(0);
        header.push_back(0);
        header.push_back(0);
        appendChunk(png, "IHDR", header);
        appendChunk(png, "IDAT", zlib);
        appendChunk(png, "IEND", {});
        return png;
    }
}

CNA_STUDIO_TEST(APngBecomesPixelsWithNoGraphicsDevice)
{
    // The capability STUDIO-10014 is about, asserted at its simplest: bytes in, pixels out, no
    // device anywhere near it. This test binary has no graphics device at all, which is the point.
    const std::vector<unsigned char> png = makePng(4, 3, 0x20u, 0x40u, 0x60u);

    const StudioImageDecodeResult result = studioDecodeImage(png, "four-by-three.png");
    CNA_STUDIO_EXPECT(result.succeeded());
    CNA_STUDIO_EXPECT_EQ(result.error, std::string{});
    CNA_STUDIO_EXPECT_EQ(result.image.width, std::uint32_t{4});
    CNA_STUDIO_EXPECT_EQ(result.image.height, std::uint32_t{3});
    CNA_STUDIO_EXPECT_EQ(result.image.pixels.size(), std::size_t{4 * 3 * 4});

    // RGBA, in that order, whatever the file held -- a caller never grows a code path for a
    // channel count it does not care about.
    CNA_STUDIO_EXPECT_EQ(result.image.pixels[0], static_cast<unsigned char>(0x20u));
    CNA_STUDIO_EXPECT_EQ(result.image.pixels[1], static_cast<unsigned char>(0x40u));
    CNA_STUDIO_EXPECT_EQ(result.image.pixels[2], static_cast<unsigned char>(0x60u));
    CNA_STUDIO_EXPECT_EQ(result.image.pixels[3], static_cast<unsigned char>(0xFFu));

    // Every pixel, not just the first: a decoder that got the stride wrong would pass a one-pixel
    // check and hand back a sheared image.
    for (std::size_t i = 0; i < result.image.pixels.size(); i += 4)
    {
        CNA_STUDIO_EXPECT_EQ(result.image.pixels[i + 3], static_cast<unsigned char>(0xFFu));
    }
}

CNA_STUDIO_TEST(TheDecoderAndTheHeaderReaderAgreeAboutSize)
{
    // They have to. `readImageSize` is what fills in an asset's facts for the inspector, and the
    // decoder is what draws it; an asset that reported one size and drew at another would be a
    // worse bug than not reading the format at all.
    ScopedDirectory directory{"agree"};
    directory.write("wide.png", makePng(9, 2, 0xFFu, 0x00u, 0x00u));

    const std::string path = directory.file("wide.png");

    const StudioImageDecodeResult decoded = studioDecodeImageFile(path);
    CNA_STUDIO_EXPECT(decoded.succeeded());

    const std::optional<ImageSize> header = readImageSize(path);
    CNA_STUDIO_EXPECT(header.has_value());
    if (header.has_value() && decoded.succeeded())
    {
        CNA_STUDIO_EXPECT_EQ(static_cast<std::uint32_t>(header->width), decoded.image.width);
        CNA_STUDIO_EXPECT_EQ(static_cast<std::uint32_t>(header->height), decoded.image.height);
    }
}

CNA_STUDIO_TEST(ABadImageIsAnErrorMessageRatherThanACrash)
{
    // The input is a file somebody dropped in a project folder. It may be truncated, it may be a
    // rename of something that is not an image, and it may be hostile. A decoder that aborted the
    // editor on one would make a single broken asset cost the user their session.
    CNA_STUDIO_EXPECT(!studioDecodeImage({}, "empty.png").succeeded());
    CNA_STUDIO_EXPECT(!studioDecodeImage({'n', 'o', 't', ' ', 'a', 'n', ' ', 'i', 'm', 'a', 'g', 'e'},
                                         "prose.png")
                           .succeeded());

    // Truncated part-way through the pixel data, which is the common real case: a copy that was
    // interrupted, or a file the watcher caught mid-write.
    std::vector<unsigned char> truncated = makePng(8, 8, 0x10u, 0x20u, 0x30u);
    truncated.resize(truncated.size() / 2);
    const StudioImageDecodeResult cut = studioDecodeImage(truncated, "half.png");
    CNA_STUDIO_EXPECT(!cut.succeeded());

    // The error names the file, because an error that does not is one the user cannot act on.
    CNA_STUDIO_EXPECT(cut.error.find("half.png") != std::string::npos);

    // A header claiming more pixels than anybody has memory for is refused by arithmetic, before
    // any allocation is attempted.
    std::vector<unsigned char> huge = makePng(1, 1, 0, 0, 0);
    huge[16] = 0x7Fu;  // IHDR width, first byte
    const StudioImageDecodeResult enormous = studioDecodeImage(huge, "enormous.png");
    CNA_STUDIO_EXPECT(!enormous.succeeded());

    // And a missing file is reported rather than throwing out of the call.
    CNA_STUDIO_EXPECT(!studioDecodeImageFile("/nonexistent/path/to/nothing.png").succeeded());
}

CNA_STUDIO_TEST(TheDecoderRunsOnSeveralThreadsAtOnce)
{
    // The whole reason for a CPU decoder: STUDIO-09003's thumbnails are background jobs, and a
    // decoder that needed the main thread would leave them with nothing to do off it. Asserted
    // rather than assumed -- this is also the test ThreadSanitizer has something to say about.
    const std::vector<unsigned char> png = makePng(16, 16, 0x11u, 0x22u, 0x33u);

    std::atomic<int> decoded{0};
    std::atomic<int> failed{0};

    std::vector<std::thread> threads;
    threads.reserve(8);
    for (int i = 0; i < 8; ++i)
    {
        threads.emplace_back([&png, &decoded, &failed] {
            for (int pass = 0; pass < 16; ++pass)
            {
                const StudioImageDecodeResult result = studioDecodeImage(png, "shared.png");
                if (result.succeeded() && result.image.width == 16 && result.image.height == 16)
                {
                    ++decoded;
                }
                else
                {
                    ++failed;
                }
            }
        });
    }

    for (std::thread& thread : threads) { thread.join(); }

    CNA_STUDIO_EXPECT_EQ(decoded.load(), 8 * 16);
    CNA_STUDIO_EXPECT_EQ(failed.load(), 0);
}

CNA_STUDIO_TEST(OnlyTheFormatsStudioImportsAreOfferedAtAll)
{
    // Enabled one at a time rather than by taking everything the decoder offers: each format is a
    // parser reading files Studio did not write, and one nobody imports is attack surface with no
    // user. This asserts the *policy*, which is the part that would erode quietly.
    CNA_STUDIO_EXPECT(studioCanDecodeImageExtension("Assets/Crate.png"));
    CNA_STUDIO_EXPECT(studioCanDecodeImageExtension("Assets/Crate.PNG"));
    CNA_STUDIO_EXPECT(studioCanDecodeImageExtension("Assets/Photo.jpg"));
    CNA_STUDIO_EXPECT(studioCanDecodeImageExtension("Assets/Photo.jpeg"));
    CNA_STUDIO_EXPECT(studioCanDecodeImageExtension("Assets/Old.bmp"));

    CNA_STUDIO_EXPECT(!studioCanDecodeImageExtension("Assets/Sound.ogg"));
    CNA_STUDIO_EXPECT(!studioCanDecodeImageExtension("Assets/Vector.svg"));
    CNA_STUDIO_EXPECT(!studioCanDecodeImageExtension("Assets/NoExtension"));

    // A .gif is a format stb can read and this build does not enable, which is the assertion that
    // catches somebody "helpfully" turning the rest on.
    CNA_STUDIO_EXPECT(!studioCanDecodeImageExtension("Assets/Animation.gif"));

    // And the extension is a hint, not a promise: a file that claims .png and holds something else
    // still fails at the decode, which is where a wrong answer is cheap.
    CNA_STUDIO_EXPECT(!studioDecodeImage({'G', 'I', 'F', '8', '9', 'a'}, "claimed.png").succeeded());
}
