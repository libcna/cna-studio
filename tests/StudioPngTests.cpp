// SPDX-License-Identifier: MS-PL
/**
 * @file StudioPngTests.cpp
 * @brief The PNG encoder, decoded back (plan.md STUDIO-33016).
 *
 * The encoder was stored deflate: correct, tiny to read, and eight megabytes for a 1920x1080
 * capture. That is enough artifact traffic that CI would eventually be asked to stop keeping the
 * screenshots, and a visual test whose evidence is thrown away is a visual test nobody can review.
 *
 * Writing a compressor without a decompressor to check it against is how a subtly wrong bitstream
 * ships: an encoder cannot tell that it has packed a Huffman code the wrong way round, because
 * every bit it wrote is a bit it meant to write. So this file contains an inflate — only what the
 * encoder emits, which is one fixed-Huffman block — and the tests decode what was written and
 * compare it to the pixels that went in.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/UiCore/UiSoftwareRasterizer.hpp"

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    /** @brief Reads bits LSB-first, which is deflate's convention. */
    class BitReader
    {
    public:
        explicit BitReader(const std::vector<std::uint8_t>& data) : data_(data) {}

        std::uint32_t bits(int count)
        {
            std::uint32_t value = 0;
            for (int i = 0; i < count; ++i)
            {
                if (at_ >= data_.size() * 8) { failed_ = true; return value; }
                const std::uint8_t byte = data_[at_ / 8];
                value |= static_cast<std::uint32_t>((byte >> (at_ % 8)) & 1u) << i;
                ++at_;
            }
            return value;
        }

        /** @brief Reads a Huffman code, most significant bit first. */
        std::uint32_t code(int count)
        {
            std::uint32_t value = 0;
            for (int i = 0; i < count; ++i) { value = (value << 1) | bits(1); }
            return value;
        }

        void alignToByte() { at_ = (at_ + 7) / 8 * 8; }
        [[nodiscard]] std::size_t byteOffset() const { return at_ / 8; }
        void seekBytes(std::size_t offset) { at_ = offset * 8; }
        [[nodiscard]] bool failed() const { return failed_; }

    private:
        const std::vector<std::uint8_t>& data_;
        std::size_t at_ = 0;
        bool failed_ = false;
    };

    struct Range { int extraBits; int base; };

    const Range& lengthRange(std::uint32_t symbol)
    {
        static const Range kLengths[] = {
            {0, 3},   {0, 4},   {0, 5},   {0, 6},   {0, 7},   {0, 8},   {0, 9},   {0, 10},
            {1, 11},  {1, 13},  {1, 15},  {1, 17},  {2, 19},  {2, 23},  {2, 27},  {2, 31},
            {3, 35},  {3, 43},  {3, 51},  {3, 59},  {4, 67},  {4, 83},  {4, 99},  {4, 115},
            {5, 131}, {5, 163}, {5, 195}, {5, 227}, {0, 258}};
        return kLengths[symbol - 257];
    }

    const Range& distanceRange(std::uint32_t symbol)
    {
        static const Range kDistances[] = {
            {0, 1},     {0, 2},     {0, 3},     {0, 4},     {1, 5},      {1, 7},
            {2, 9},     {2, 13},    {3, 17},    {3, 25},    {4, 33},     {4, 49},
            {5, 65},    {5, 97},    {6, 129},   {6, 193},   {7, 257},    {7, 385},
            {8, 513},   {8, 769},   {9, 1025},  {9, 1537},  {10, 2049},  {10, 3073},
            {11, 4097}, {11, 6145}, {12, 8193}, {12, 12289}, {13, 16385}, {13, 24577}};
        return kDistances[symbol];
    }

    /**
     * @brief Inflates a deflate stream of stored and fixed-Huffman blocks.
     *
     * Only what the encoder emits. A dynamic-Huffman block would be a decoder for something nothing
     * here writes, and a decoder nobody exercises is not evidence of anything.
     */
    std::vector<std::uint8_t> inflateFixed(const std::vector<std::uint8_t>& data, bool& ok)
    {
        std::vector<std::uint8_t> out;
        BitReader reader{data};
        ok = true;

        for (;;)
        {
            const std::uint32_t last = reader.bits(1);
            const std::uint32_t type = reader.bits(2);

            if (type == 0)
            {
                reader.alignToByte();
                const std::size_t at = reader.byteOffset();
                if (at + 4 > data.size()) { ok = false; return out; }
                const std::size_t length =
                    static_cast<std::size_t>(data[at]) | (static_cast<std::size_t>(data[at + 1]) << 8);
                if (at + 4 + length > data.size()) { ok = false; return out; }
                out.insert(out.end(), data.begin() + static_cast<std::ptrdiff_t>(at + 4),
                           data.begin() + static_cast<std::ptrdiff_t>(at + 4 + length));
                reader.seekBytes(at + 4 + length);
            }
            else if (type == 1)
            {
                for (;;)
                {
                    // The fixed tree, decoded by its prefix lengths: 7 bits covers 256-279, 8 bits
                    // covers 0-143 and 280-287, 9 bits covers 144-255.
                    std::uint32_t symbol = 0;
                    std::uint32_t seven = reader.code(7);
                    if (seven <= 0x17) { symbol = seven + 256; }
                    else
                    {
                        const std::uint32_t eight = (seven << 1) | reader.bits(1);
                        if (eight >= 0x30 && eight <= 0xBF) { symbol = eight - 0x30; }
                        else if (eight >= 0xC0 && eight <= 0xC7) { symbol = eight - 0xC0 + 280; }
                        else
                        {
                            const std::uint32_t nine = (eight << 1) | reader.bits(1);
                            if (nine < 0x190 || nine > 0x1FF) { ok = false; return out; }
                            symbol = nine - 0x190 + 144;
                        }
                    }

                    if (reader.failed()) { ok = false; return out; }
                    if (symbol == 256) { break; }

                    if (symbol < 256)
                    {
                        out.push_back(static_cast<std::uint8_t>(symbol));
                        continue;
                    }
                    if (symbol > 285) { ok = false; return out; }

                    const Range& length = lengthRange(symbol);
                    const auto count = static_cast<std::size_t>(
                        length.base + static_cast<int>(reader.bits(length.extraBits)));

                    const std::uint32_t distanceSymbol = reader.code(5);
                    if (distanceSymbol > 29) { ok = false; return out; }
                    const Range& distance = distanceRange(distanceSymbol);
                    const auto back = static_cast<std::size_t>(
                        distance.base + static_cast<int>(reader.bits(distance.extraBits)));

                    if (back == 0 || back > out.size()) { ok = false; return out; }
                    for (std::size_t i = 0; i < count; ++i)
                    {
                        out.push_back(out[out.size() - back]);
                    }
                }
            }
            else { ok = false; return out; }

            if (last == 1) { break; }
            if (reader.failed()) { ok = false; return out; }
        }

        ok = ok && !reader.failed();
        return out;
    }

    int paethOf(int a, int b, int c)
    {
        const int p = a + b - c;
        const int pa = std::abs(p - a);
        const int pb = std::abs(p - b);
        const int pc = std::abs(p - c);
        if (pa <= pb && pa <= pc) { return a; }
        return pb <= pc ? b : c;
    }

    /** @brief What a decoder gets: the PNG's pixels, or an empty buffer when it would not read. */
    ImageBuffer decodePng(const std::vector<std::uint8_t>& png, std::string& problem)
    {
        ImageBuffer image;

        const std::uint8_t signature[] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        if (png.size() < 8 || !std::equal(std::begin(signature), std::end(signature), png.begin()))
        {
            problem = "not a PNG";
            return image;
        }

        const auto read32 = [&](std::size_t at) {
            return (static_cast<std::uint32_t>(png[at]) << 24)
                 | (static_cast<std::uint32_t>(png[at + 1]) << 16)
                 | (static_cast<std::uint32_t>(png[at + 2]) << 8)
                 | static_cast<std::uint32_t>(png[at + 3]);
        };

        std::vector<std::uint8_t> idat;
        std::size_t at = 8;
        while (at + 12 <= png.size())
        {
            const auto length = static_cast<std::size_t>(read32(at));
            const std::string type(png.begin() + static_cast<std::ptrdiff_t>(at + 4),
                                   png.begin() + static_cast<std::ptrdiff_t>(at + 8));
            if (at + 12 + length > png.size()) { problem = "truncated chunk " + type; return {}; }

            if (type == "IHDR")
            {
                image.width = static_cast<int>(read32(at + 8));
                image.height = static_cast<int>(read32(at + 12));
                if (png[at + 16] != 8 || png[at + 17] != 6)
                {
                    problem = "not 8-bit RGBA";
                    return {};
                }
            }
            else if (type == "IDAT")
            {
                idat.insert(idat.end(), png.begin() + static_cast<std::ptrdiff_t>(at + 8),
                            png.begin() + static_cast<std::ptrdiff_t>(at + 8 + length));
            }
            at += 12 + length;
        }

        if (idat.size() < 6) { problem = "no image data"; return {}; }

        // Past the two-byte zlib header, and short of the four-byte Adler-32.
        const std::vector<std::uint8_t> stream(idat.begin() + 2, idat.end() - 4);
        bool ok = false;
        const std::vector<std::uint8_t> raw = inflateFixed(stream, ok);
        if (!ok) { problem = "the deflate stream did not decode"; return {}; }

        const auto stride = static_cast<std::size_t>(image.width) * 4;
        if (raw.size() != static_cast<std::size_t>(image.height) * (stride + 1))
        {
            problem = "inflated " + std::to_string(raw.size()) + " bytes, expected "
                    + std::to_string(static_cast<std::size_t>(image.height) * (stride + 1));
            return {};
        }

        image.pixels.assign(static_cast<std::size_t>(image.height) * stride, 0);
        for (int y = 0; y < image.height; ++y)
        {
            const std::size_t rowStart = static_cast<std::size_t>(y) * (stride + 1);
            const std::uint8_t filter = raw[rowStart];
            std::uint8_t* row = image.pixels.data() + static_cast<std::size_t>(y) * stride;
            const std::uint8_t* above =
                y > 0 ? image.pixels.data() + static_cast<std::size_t>(y - 1) * stride : nullptr;

            for (std::size_t i = 0; i < stride; ++i)
            {
                const int left = i >= 4 ? row[i - 4] : 0;
                const int up = above != nullptr ? above[i] : 0;
                const int upLeft = (above != nullptr && i >= 4) ? above[i - 4] : 0;

                int predicted = 0;
                switch (filter)
                {
                    case 1: predicted = left; break;
                    case 2: predicted = up; break;
                    case 3: predicted = (left + up) / 2; break;
                    case 4: predicted = paethOf(left, up, upLeft); break;
                    case 0: predicted = 0; break;
                    default: problem = "unknown filter"; return {};
                }
                row[i] = static_cast<std::uint8_t>((raw[rowStart + 1 + i] + predicted) & 0xFF);
            }
        }
        return image;
    }

    /** @brief An image whose pixels are deliberately awkward for a compressor. */
    ImageBuffer noisyImage(int width, int height)
    {
        ImageBuffer image;
        image.width = width;
        image.height = height;
        image.pixels.resize(static_cast<std::size_t>(width) * height * 4);

        std::uint32_t seed = 0x12345678u;
        for (std::uint8_t& byte : image.pixels)
        {
            seed = seed * 1664525u + 1013904223u;
            byte = static_cast<std::uint8_t>((seed >> 16) & 0xFFu);
        }
        return image;
    }

    /** @brief An image a compressor should do very well on: flat, with one band. */
    ImageBuffer flatImage(int width, int height)
    {
        ImageBuffer image;
        image.width = width;
        image.height = height;
        image.pixels.assign(static_cast<std::size_t>(width) * height * 4, 0x20);

        for (int y = height / 3; y < height / 2; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const std::size_t at = (static_cast<std::size_t>(y) * width + x) * 4;
                image.pixels[at] = 0xC0;
                image.pixels[at + 3] = 0xFF;
            }
        }
        return image;
    }

    bool samePixels(const ImageBuffer& a, const ImageBuffer& b)
    {
        return a.width == b.width && a.height == b.height && a.pixels == b.pixels;
    }
}

CNA_STUDIO_TEST(AnEncodedImageDecodesBackToTheSamePixels)
{
    // The check that makes the rest of this worth anything. A compressor cannot tell that it has
    // packed a Huffman code the wrong way round: every bit it wrote is a bit it meant to write.
    for (const ImageBuffer& original : {flatImage(97, 61), noisyImage(64, 40), flatImage(1, 1),
                                        noisyImage(1, 300), noisyImage(300, 1)})
    {
        const std::vector<std::uint8_t> png = encodeImageAsPng(original);
        CNA_STUDIO_EXPECT(!png.empty());

        std::string problem;
        const ImageBuffer decoded = decodePng(png, problem);
        if (!samePixels(original, decoded))
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "a " + std::to_string(original.width) + "x" + std::to_string(original.height)
                + " image did not survive the round trip"
                + (problem.empty() ? std::string{": the pixels differ"} : ": " + problem));
        }
    }
}

CNA_STUDIO_TEST(ACaptureIsAnOrderOfMagnitudeSmallerThanItsPixels)
{
    // The acceptance criterion, and a real one: a screenshot of the shell is mostly flat panels and
    // repeated rows, which is exactly what filtering and LZ77 are for. Ten to one is the promise;
    // the real figure for a 1920x1080 shell capture is closer to ninety.
    const ImageBuffer image = flatImage(640, 480);
    const std::vector<std::uint8_t> png = encodeImageAsPng(image);

    CNA_STUDIO_EXPECT(!png.empty());
    CNA_STUDIO_EXPECT(png.size() * 10 < image.pixels.size());
}

CNA_STUDIO_TEST(EvenIncompressibleNoiseStaysAReadablePng)
{
    // The other end. Fixed Huffman spends nine bits on many literals, so random bytes come out
    // *larger* than they went in -- which is correct and worth pinning, because the failure it
    // could hide is a stream that silently truncates once it stops helping.
    const ImageBuffer image = noisyImage(128, 128);
    const std::vector<std::uint8_t> png = encodeImageAsPng(image);

    std::string problem;
    const ImageBuffer decoded = decodePng(png, problem);
    CNA_STUDIO_EXPECT(problem.empty());
    CNA_STUDIO_EXPECT(samePixels(image, decoded));

    // Within a factor of two of the raw pixels: nine bits for eight is 12.5% over, and anything
    // near that is the encoder behaving; anything far above it means a block is being rewritten.
    CNA_STUDIO_EXPECT(png.size() < image.pixels.size() * 2);
}

CNA_STUDIO_TEST(EveryChunksChecksumIsTheOneAStandardReaderWillCompute)
{
    // A CRC nobody verifies is a CRC that can be wrong for years. Recomputed here the way a reader
    // does -- over the type and the payload, not the length -- which is the detail encoders get
    // wrong and their own readers then agree with.
    const ImageBuffer image = flatImage(40, 30);
    const std::vector<std::uint8_t> png = encodeImageAsPng(image);
    CNA_STUDIO_EXPECT(png.size() > 8);

    const auto read32 = [&](std::size_t at) {
        return (static_cast<std::uint32_t>(png[at]) << 24)
             | (static_cast<std::uint32_t>(png[at + 1]) << 16)
             | (static_cast<std::uint32_t>(png[at + 2]) << 8)
             | static_cast<std::uint32_t>(png[at + 3]);
    };

    std::uint32_t table[256];
    for (std::uint32_t n = 0; n < 256; ++n)
    {
        std::uint32_t c = n;
        for (int k = 0; k < 8; ++k) { c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1); }
        table[n] = c;
    }

    std::size_t at = 8;
    std::size_t chunks = 0;
    while (at + 12 <= png.size())
    {
        const auto length = static_cast<std::size_t>(read32(at));
        std::uint32_t crc = 0xFFFFFFFFu;
        for (std::size_t i = at + 4; i < at + 8 + length; ++i)
        {
            crc = table[(crc ^ png[i]) & 0xFFu] ^ (crc >> 8);
        }
        crc ^= 0xFFFFFFFFu;

        CNA_STUDIO_EXPECT_EQ(crc, read32(at + 8 + length));
        ++chunks;
        at += 12 + length;
    }

    // IHDR, IDAT, IEND.
    CNA_STUDIO_EXPECT_EQ(chunks, std::size_t{3});
    CNA_STUDIO_EXPECT_EQ(at, png.size());
}

CNA_STUDIO_TEST(AnImageWithNoPixelsEncodesToNothingRatherThanAHeader)
{
    ImageBuffer empty;
    CNA_STUDIO_EXPECT(encodeImageAsPng(empty).empty());
}
