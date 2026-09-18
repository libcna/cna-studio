// SPDX-License-Identifier: MS-PL
#include "CNA/Studio/Assets/AssetImporters.hpp"

#include "CNA/Studio/Assets/AssetImporter.hpp"

#include "CNA/Studio/Assets/AudioImport.hpp"
#include "CNA/Studio/Assets/ModelImport.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <istream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace CNA::Studio
{
    /** @brief Helpers shared by the sprite-font reader and the facts pass. */
    namespace Detail
    {
        /** @brief Returns the text between `<tag>` and `</tag>`, or an empty string. */
        std::string readTag(const std::string& text, std::string_view tag)
        {
            const std::string open = "<" + std::string{tag} + ">";
            const std::string close = "</" + std::string{tag} + ">";

            const std::size_t start = text.find(open);
            if (start == std::string::npos) { return {}; }

            const std::size_t from = start + open.size();
            const std::size_t end = text.find(close, from);
            if (end == std::string::npos) { return {}; }

            std::string value = text.substr(from, end - from);
            const std::size_t first = value.find_first_not_of(" \t\r\n");
            const std::size_t last = value.find_last_not_of(" \t\r\n");
            if (first == std::string::npos) { return {}; }
            return value.substr(first, last - first + 1);
        }

        /** @brief Parses a number, answering zero rather than throwing on anything else. */
        float toFloat(const std::string& text)
        {
            try { return std::stof(text); }
            catch (const std::exception&) { return 0.0f; }
        }

        /**
         * @brief Turns a `CharacterRegion` bound into a character code.
         *
         * XNA writes these either as the character itself or as an XML entity, and a file written
         * by hand may use either -- so both are read. The space at the start of the usual region
         * is exactly the case that makes the entity form common.
         */
        int toCharacterCode(const std::string& text)
        {
            if (text.empty()) { return 0; }

            if (text.size() > 3 && text.rfind("&#", 0) == 0 && text.back() == ';')
            {
                const bool hexadecimal = text[2] == 'x' || text[2] == 'X';
                try
                {
                    return std::stoi(text.substr(hexadecimal ? 3 : 2, text.size() - (hexadecimal ? 4 : 3)),
                                     nullptr, hexadecimal ? 16 : 10);
                }
                catch (const std::exception&) { return 0; }
            }
            return static_cast<unsigned char>(text.front());
        }

        /**
         * @brief Merges @p facts into @p record's sidecar, but only where a value would change.
         *
         * @return True when something was written.
         */
        bool writeFactsIfChanged(AssetDatabase& assets, const AssetRecord& record,
                                 const JsonValue& facts);

        /**
         * @brief Writes what a `.spritefont` says about itself into its sidecar.
         *
         * @return True when something changed, so that opening a project twice produces no diff.
         */
        bool applySpriteFontFacts(AssetDatabase& assets, const AssetRecord& record);

        /**
         * @brief Writes an image's dimensions into its sidecar.
         *
         * @return True when something changed, so that opening a project twice produces no diff.
         */
        bool applyTextureFacts(AssetDatabase& assets, const AssetRecord& record);

        /**
         * @brief Writes what a model file says about itself into its sidecar.
         *
         * The most expensive facts pass the editor has, because glTF states none of these in a
         * header -- a triangle count is the sum of every primitive's, so the whole file has to be
         * read to find it. `writeFactsIfChanged` is what keeps that cost from turning into sidecar
         * churn on every open.
         *
         * @return True when something changed.
         */
        bool applyModelFacts(AssetDatabase& assets, const AssetRecord& record);

        /**
         * @brief Writes what an audio file says about itself into its sidecar.
         *
         * One reader for both audio types, because a `.wav` and an `.ogg` answer the same
         * questions and the split between `SoundEffect` and `Song` is about how a *game* uses
         * them, not about what is in the file.
         *
         * @return True when something changed, so that opening a project twice produces no diff.
         */
        bool applyAudioFacts(AssetDatabase& assets, const AssetRecord& record);
    }

    namespace
    {
        PropertyDescriptor makeProperty(std::string name,
                                        std::string displayName,
                                        PropertyType type,
                                        PropertyValue defaultValue,
                                        std::string tooltip = {})
        {
            PropertyDescriptor property;
            property.name = std::move(name);
            property.displayName = std::move(displayName);
            property.type = type;
            property.defaultValue = std::move(defaultValue);
            property.tooltip = std::move(tooltip);
            return property;
        }

        ComponentDescriptor makeTextureImporter()
        {
            ComponentDescriptor descriptor;
            descriptor.typeId = ImporterIds::kTexture;
            descriptor.displayName = "Texture Importer";
            descriptor.category = "Import";

            PropertyDescriptor wrap = makeProperty("wrapMode", "Wrap Mode", PropertyType::Enum,
                                                   PropertyValue{PropertyValue::EnumValue{"Clamp"}},
                                                   "How coordinates outside 0..1 are sampled. "
                                                   "Matches XNA's TextureAddressMode.");
            wrap.enumOptions = {"Clamp", "Wrap", "Mirror"};

            PropertyDescriptor filter = makeProperty("filterMode", "Filter Mode", PropertyType::Enum,
                                                     PropertyValue{PropertyValue::EnumValue{"Linear"}},
                                                     "Point keeps pixel art crisp; Linear smooths. "
                                                     "Matches XNA's SamplerState presets.");
            filter.enumOptions = {"Point", "Linear", "Anisotropic"};

            // Spelled as XNA's TextureProcessorOutputFormat spells it. XNA's third option,
            // NoChange, is deliberately missing: it means "keep the source bitmap's own format",
            // and Studio's only decoder produces eight-bit RGBA whatever it is handed -- so it
            // would be a choice with one outcome, which is a place for a user to look for
            // behaviour that is not there (`plan.md` STUDIO-10003).
            PropertyDescriptor output = makeProperty("outputFormat", "Output Format",
                                                     PropertyType::Enum,
                                                     PropertyValue{PropertyValue::EnumValue{"Color"}},
                                                     "Color is eight bits a channel. DxtCompressed "
                                                     "is block compression -- DXT1 without alpha at "
                                                     "an eighth the size, DXT5 with it at a quarter "
                                                     "-- and needs both edges to be a multiple of 4.");
            output.enumOptions = {"Color", "DxtCompressed"};

            const auto fact = [](std::string name, std::string display, PropertyType type,
                                 PropertyValue defaultValue, std::string tooltip) {
                PropertyDescriptor property = makeProperty(std::move(name), std::move(display), type,
                                                           std::move(defaultValue), std::move(tooltip));
                property.readOnly = true;
                return property;
            };

            descriptor.properties = {
                std::move(wrap),
                std::move(filter),
                std::move(output),
                makeProperty("srgbRead", "sRGB Read", PropertyType::Boolean,
                             PropertyValue{false},
                             "Whether the hardware converts this texture from sRGB to linear as it "
                             "samples it. Off is XNA's own behaviour -- it renders in gamma space, "
                             "so the bytes are sampled as they stand -- and off is also the only "
                             "correct answer for a normal map, a mask or a lookup table."),
                makeProperty("generateMipmaps", "Generate Mipmaps", PropertyType::Boolean,
                             PropertyValue{false},
                             "Costs a third more memory and is wrong for most 2D art, which is "
                             "drawn at its native size."),
                makeProperty("premultiplyAlpha", "Premultiply Alpha", PropertyType::Boolean,
                             PropertyValue{true},
                             "XNA's SpriteBatch defaults to premultiplied blending, so this is on "
                             "by default; turning it off means also passing NonPremultiplied when "
                             "drawing, or the edges of every sprite will halo."),

                // Read-only, because these are facts about the file rather than choices about it.
                // Shown because "why is this sprite blurry" is usually answered by its size, and
                // because the other two are what decide whether the format above can be given.
                fact("pixelSize", "Pixel Size", PropertyType::Vector2, PropertyValue{StudioVector2{}},
                     "Read from the file's header. Zero means the format is one Studio cannot "
                     "measure yet."),
                fact("sourceFormat", "Source Format", PropertyType::String, PropertyValue{std::string{}},
                     "Read from the file's magic bytes rather than its name, so a renamed file "
                     "reports what it actually is."),
                fact("sourceAlpha", "Source Alpha", PropertyType::Boolean, PropertyValue{false},
                     "Whether the file's encoding carries an alpha channel -- not whether any "
                     "pixel uses it. This is what decides DXT1 against DXT5."),
            };

            descriptor.unique = true;
            return descriptor;
        }

        ComponentDescriptor makeSpriteFontImporter()
        {
            ComponentDescriptor descriptor;
            descriptor.typeId = ImporterIds::kSpriteFont;
            descriptor.displayName = "Sprite Font Importer";
            descriptor.category = "Import";

            // Every field here is read-only, and that is the design rather than an omission. A
            // `.spritefont` is the content pipeline's own input: it already declares the font, the
            // size, the spacing and the character range, and an editable copy in the sidecar would
            // be a second answer to a question the build asks the file. So the editor reports what
            // the file says instead -- which is the part a person actually wants, since "what font
            // is this and does it cover the characters I need" is otherwise an XML file away.
            const auto fact = [](std::string name, std::string display, PropertyType type,
                                 PropertyValue defaultValue, std::string tooltip) {
                PropertyDescriptor property = makeProperty(std::move(name), std::move(display), type,
                                                           std::move(defaultValue), std::move(tooltip));
                property.readOnly = true;
                return property;
            };

            descriptor.properties = {
                fact("fontName", "Font", PropertyType::String, PropertyValue{std::string{}},
                     "The typeface the .spritefont asks the content pipeline to rasterise."),
                fact("pointSize", "Size", PropertyType::Float, PropertyValue{0.0f}, "In points."),
                fact("spacing", "Spacing", PropertyType::Float, PropertyValue{0.0f},
                     "Extra horizontal space between glyphs, in pixels."),
                fact("useKerning", "Kerning", PropertyType::Boolean, PropertyValue{true},
                     "Whether the pipeline applies the font's own kerning pairs."),
                fact("characterRange", "Characters", PropertyType::String, PropertyValue{std::string{}},
                     "The first CharacterRegion the file declares, as a code range. Answers "
                     "\"why does this text render as boxes\" without opening the XML."),
            };
            return descriptor;
        }

        /**
         * @brief The facts both audio importers report, which are the same facts.
         *
         * Shared rather than written twice: a `.wav` and an `.ogg` answer identical questions, and
         * two copies of one list is how they come to disagree about a name.
         */
        std::vector<PropertyDescriptor> audioFacts()
        {
            const auto fact = [](std::string name, std::string display, PropertyType type,
                                 PropertyValue defaultValue, std::string tooltip) {
                PropertyDescriptor property = makeProperty(std::move(name), std::move(display), type,
                                                           std::move(defaultValue), std::move(tooltip));
                property.readOnly = true;
                return property;
            };

            return {
                fact("sourceFormat", "Source Format", PropertyType::String, PropertyValue{std::string{}},
                     "Read from the file's magic bytes rather than its name. Empty means a format "
                     "Studio cannot measure yet -- MP3 and FLAC, today."),
                fact("duration", "Duration", PropertyType::String, PropertyValue{std::string{}}, ""),
                fact("sampleRate", "Sample Rate", PropertyType::Integer, PropertyValue{0}, "In hertz."),
                fact("channels", "Channels", PropertyType::Integer, PropertyValue{0},
                     "One is mono, which is what XNA needs for a sound to be positioned in 3D; a "
                     "stereo clip plays as-is wherever the listener is."),
                fact("decodedBytes", "In Memory", PropertyType::Integer, PropertyValue{0},
                     "What this occupies once decoded, which is what Load Into Memory costs. The "
                     "file size does not say: a three-minute Ogg is four megabytes on disk and "
                     "forty in memory."),
            };
        }

        PropertyDescriptor importVolumeProperty()
        {
            PropertyDescriptor volume = makeProperty("importVolume", "Import Volume",
                                                     PropertyType::Float, PropertyValue{1.0f},
                                                     "Applied once at import rather than at every "
                                                     "play, so a clip that is simply too loud is "
                                                     "fixed in one place. The inspector's preview "
                                                     "plays at this volume, so it can be heard.");
            volume.minimum = 0.0;
            volume.maximum = 1.0;
            return volume;
        }

        PropertyDescriptor loadIntoMemoryProperty()
        {
            return makeProperty("loadIntoMemory", "Load Into Memory", PropertyType::Boolean,
                                PropertyValue{true},
                                "Off streams the clip from disk instead, which is right for music-"
                                "length audio and wrong for a footstep. Recorded for the content "
                                "build; Studio streams nothing itself. \"In Memory\" below is what "
                                "the decision costs.");
        }

        ComponentDescriptor makeSoundEffectImporter()
        {
            ComponentDescriptor descriptor;
            descriptor.typeId = ImporterIds::kSoundEffect;
            descriptor.displayName = "Sound Effect Importer";
            descriptor.category = "Import";

            descriptor.properties = {importVolumeProperty(), loadIntoMemoryProperty()};
            for (PropertyDescriptor& property : audioFacts())
            {
                descriptor.properties.push_back(std::move(property));
            }
            return descriptor;
        }

        /**
         * @brief The Song importer, which had a type id and no descriptor at all.
         *
         * `AssetDatabase` has assigned `CNA.SongImporter` to every `.ogg`, `.mp3` and `.flac` since
         * it was written, and nothing registered one -- so the inspector told a user that the
         * importer for their music "is not registered in this build", which is true and reads as a
         * broken installation (`plan.md` STUDIO-10005).
         *
         * Its settings are the sound effect's. The split between the two types is about how a
         * *game* uses a clip -- `SoundEffect` is loaded and fired, `Song` is streamed through the
         * media player -- and not about what is in the file, so the questions are the same ones.
         */
        ComponentDescriptor makeSongImporter()
        {
            ComponentDescriptor descriptor;
            descriptor.typeId = ImporterIds::kSong;
            descriptor.displayName = "Song Importer";
            descriptor.category = "Import";

            descriptor.properties = {importVolumeProperty(), loadIntoMemoryProperty()};
            for (PropertyDescriptor& property : audioFacts())
            {
                descriptor.properties.push_back(std::move(property));
            }
            return descriptor;
        }

        ComponentDescriptor makeModelImporter()
        {
            ComponentDescriptor descriptor;
            descriptor.typeId = ImporterIds::kModel;
            descriptor.displayName = "Model Importer";
            descriptor.category = "Import";

            PropertyDescriptor scale = makeProperty("scaleFactor", "Scale Factor",
                                                    PropertyType::Float, PropertyValue{1.0f},
                                                    "Applied at import, so a model authored in "
                                                    "centimetres does not need scaling on every "
                                                    "entity that uses it.");
            scale.minimum = 0.0001;
            scale.maximum = 10000.0;

            // What the file says about itself, read by `Detail::applyModelFacts`. Read-only for
            // the reason the sprite font's are: these are answers taken from the file, and an
            // editable copy would be a second answer to a settled question. They are also the only
            // way to tell a model that imported cleanly from one whose geometry was skipped --
            // "0 triangles" beside a 4 MB file is the whole diagnosis.
            const auto fact = [](std::string name, std::string display, PropertyType type,
                                 PropertyValue defaultValue, std::string tooltip) {
                PropertyDescriptor property = makeProperty(std::move(name), std::move(display), type,
                                                           std::move(defaultValue), std::move(tooltip));
                property.readOnly = true;
                return property;
            };

            PropertyDescriptor normals = makeProperty("normals", "Normals", PropertyType::Enum,
                                                      PropertyValue{PropertyValue::EnumValue{"Import"}},
                                                      "Import keeps the file's own, which are "
                                                      "decisions the artist made. Calculate throws "
                                                      "them away and computes flat ones, which is "
                                                      "for a file whose normals are wrong -- it "
                                                      "duplicates every vertex per face, so the "
                                                      "mesh gets larger and looks faceted.");
            normals.enumOptions = {"Import", "Calculate"};

            descriptor.properties = {
                std::move(scale),
                makeProperty("importMaterials", "Import Materials", PropertyType::Boolean,
                             PropertyValue{true},
                             "Off brings the shape in without the file's materials, for a model "
                             "that is going to be materialled in the editor anyway."),
                std::move(normals),
                fact("meshCount", "Meshes", PropertyType::Integer, PropertyValue{0},
                     "Drawable parts -- one per glTF primitive, since a primitive is the largest "
                     "span with a single material."),
                fact("vertexCount", "Vertices", PropertyType::Integer, PropertyValue{0}, ""),
                fact("triangleCount", "Triangles", PropertyType::Integer, PropertyValue{0}, ""),
                fact("materialCount", "Materials", PropertyType::Integer, PropertyValue{0}, ""),
                // Was an "Import Animations" checkbox that nothing read. A count is the same
                // information told truthfully: a control that does nothing teaches a user that the
                // editor lies, which is worse than the gap it stands in for (STUDIO-10004).
                fact("animationCount", "Animations", PropertyType::Integer, PropertyValue{0},
                     "How many the file carries. Studio does not import animations yet, so this "
                     "is what would be lost by baking this model into a build today."),
                // The number the importer has always counted and never shown. "0 triangles" beside
                // a 4 MB file diagnoses one kind of silent loss; this diagnoses the other.
                fact("skippedPrimitives", "Skipped", PropertyType::Integer, PropertyValue{0},
                     "Primitives left out because they are lines or points rather than triangles. "
                     "Anything but zero means this model is not all here."),
                fact("modelSize", "Size", PropertyType::Vector3, PropertyValue{StudioVector3{}},
                     "The model's extent in world units, after Scale Factor. Answers \"why is this "
                     "thing the size of a building\" without placing it in a scene first."),
            };
            return descriptor;
        }
    }

    namespace
    {
        /**
         * @brief Reads a JPEG's size by walking its segments, @p stream positioned after the header.
         *
         * A JPEG has no fixed offset to walk to: the dimensions live in a "start of frame" segment
         * that sits after an arbitrary chain of others -- quantisation tables, application data,
         * comments -- each carrying its own length. Walking them is the only way, which is why this
         * takes the stream rather than the fixed header block every other format is read from.
         *
         * Returns nothing on anything unexpected. A file that turns out not to be a JPEG after all
         * is a size the editor does not know, not an error worth stopping an import for.
         */
        std::optional<ImageSize> readJpegSize(std::istream& stream)
        {
            const auto readByte = [&]() -> int {
                const int value = stream.get();
                return stream ? value : -1;
            };

            // Back to just after the two-byte start-of-image marker, since the caller read a whole
            // header block looking for signatures.
            stream.clear();
            stream.seekg(2, std::ios::beg);

            // Bounded rather than "until it works": a corrupt file can otherwise send this walking
            // for as long as the file is long, and a stuck importer is worse than an unknown size.
            constexpr int kMaximumSegments = 256;

            for (int segment = 0; segment < kMaximumSegments; ++segment)
            {
                // Segments start with 0xFF, and any number of 0xFF bytes is legal padding before
                // the marker itself.
                int marker = readByte();
                while (marker == 0xFF) { marker = readByte(); }
                if (marker < 0) { return std::nullopt; }

                // Standalone markers carry no length: restart intervals and the start of image.
                if ((marker >= 0xD0 && marker <= 0xD9) || marker == 0x01) { continue; }

                const int lengthHigh = readByte();
                const int lengthLow = readByte();
                if (lengthHigh < 0 || lengthLow < 0) { return std::nullopt; }

                const int length = (lengthHigh << 8) | lengthLow;
                if (length < 2) { return std::nullopt; }

                // The start-of-frame markers, of which there are many -- baseline, progressive,
                // lossless, arithmetic-coded -- and every one of them carries the size in the same
                // place. 0xC4, 0xC8 and 0xCC are not frames: they are Huffman tables, JPEG-LS
                // extensions and arithmetic-coding tables.
                const bool isStartOfFrame = marker >= 0xC0 && marker <= 0xCF && marker != 0xC4
                                            && marker != 0xC8 && marker != 0xCC;

                if (isStartOfFrame)
                {
                    std::array<char, 5> frame{};
                    stream.read(frame.data(), static_cast<std::streamsize>(frame.size()));
                    if (stream.gcount() < static_cast<std::streamsize>(frame.size()))
                    {
                        return std::nullopt;
                    }

                    const auto at = [&](std::size_t index) {
                        return static_cast<int>(static_cast<unsigned char>(frame[index]));
                    };

                    // One byte of sample precision, then height and width, big-endian and in that
                    // order -- the reverse of PNG's, which is the classic way to get this wrong.
                    const int height = (at(1) << 8) | at(2);
                    const int width = (at(3) << 8) | at(4);
                    if (width <= 0 || height <= 0) { return std::nullopt; }
                    return ImageSize{width, height};
                }

                stream.seekg(length - 2, std::ios::cur);
                if (!stream) { return std::nullopt; }
            }
            return std::nullopt;
        }
    }

    namespace
    {
        /**
         * @brief Whether a paletted PNG carries a `tRNS` chunk, which is where its alpha lives.
         *
         * Colour type 3 is the one case a PNG's fixed header cannot answer: the palette entries
         * themselves are opaque, and transparency arrives later in an optional chunk. That chunk is
         * required to appear before the first `IDAT`, so the walk is bounded by the image data
         * rather than by the end of the file -- a hundred-megabyte PNG costs the same few reads as
         * a small one.
         */
        bool pngPaletteHasTransparency(std::istream& stream)
        {
            stream.clear();
            stream.seekg(8, std::ios::beg);

            // Bounded twice over: by `IDAT` and by a chunk count, because a file whose lengths are
            // nonsense can otherwise send this round a loop that seeks nowhere.
            for (int chunk = 0; chunk < 64; ++chunk)
            {
                std::array<char, 8> header{};
                stream.read(header.data(), static_cast<std::streamsize>(header.size()));
                if (stream.gcount() < static_cast<std::streamsize>(header.size())) { return false; }

                const auto byteAt = [&](std::size_t index) {
                    return static_cast<std::uint32_t>(static_cast<unsigned char>(header[index]));
                };
                const std::uint32_t length = (byteAt(0) << 24) | (byteAt(1) << 16)
                                             | (byteAt(2) << 8) | byteAt(3);
                const std::string type{header.data() + 4, 4};

                if (type == "tRNS") { return true; }
                if (type == "IDAT" || type == "IEND") { return false; }

                // The length, then the four-byte CRC that follows every chunk.
                stream.seekg(static_cast<std::streamoff>(length) + 4, std::ios::cur);
                if (!stream) { return false; }
            }
            return false;
        }
    }

    std::optional<ImageDescription> readImageDescription(const std::string& absolutePath)
    {
        std::ifstream stream{absolutePath, std::ios::binary};
        if (!stream) { return std::nullopt; }

        std::array<char, 32> header{};
        stream.read(header.data(), static_cast<std::streamsize>(header.size()));
        const std::size_t read = static_cast<std::size_t>(stream.gcount());

        const auto byteAt = [&](std::size_t index) {
            return static_cast<std::uint32_t>(static_cast<unsigned char>(header[index]));
        };

        // PNG: an 8-byte signature, then an IHDR chunk whose first two fields are the width and
        // the height, big-endian. Fixed offsets, so no parsing is needed.
        static const std::array<unsigned char, 8> kPngSignature{0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        if (read >= 26)
        {
            bool isPng = true;
            for (std::size_t index = 0; index < kPngSignature.size(); ++index)
            {
                if (byteAt(index) != kPngSignature[index]) { isPng = false; break; }
            }

            if (isPng)
            {
                const auto bigEndian = [&](std::size_t at) {
                    return static_cast<int>((byteAt(at) << 24) | (byteAt(at + 1) << 16)
                                            | (byteAt(at + 2) << 8) | byteAt(at + 3));
                };

                ImageDescription description;
                description.format = "PNG";
                description.width = bigEndian(16);
                description.height = bigEndian(20);

                // IHDR byte 9, which is offset 25: 0 grey, 2 RGB, 3 palette, 4 grey+alpha,
                // 6 RGBA. Only the palette needs looking further.
                const std::uint32_t colorType = byteAt(25);
                description.hasAlphaChannel =
                    colorType == 4 || colorType == 6
                    || (colorType == 3 && pngPaletteHasTransparency(stream));
                return description;
            }
        }

        // JPEG: no fixed offsets at all. The size lives in a "start of frame" segment somewhere
        // after a chain of others whose lengths have to be walked -- which is why this needs the
        // stream rather than the header block above. JPEG has no alpha channel in any of its
        // forms, so there is nothing to look for.
        if (read >= 2 && byteAt(0) == 0xFF && byteAt(1) == 0xD8)
        {
            const std::optional<ImageSize> size = readJpegSize(stream);
            if (!size) { return std::nullopt; }
            return ImageDescription{size->width, size->height, "JPEG", false};
        }

        // BMP: "BM", then a DIB header whose width and height are little-endian at fixed offsets.
        // The height is signed and negative for a top-down image, so its magnitude is what counts.
        if (read >= 26 && byteAt(0) == 'B' && byteAt(1) == 'M')
        {
            const auto littleEndian = [&](std::size_t at) {
                return static_cast<std::int32_t>(byteAt(at) | (byteAt(at + 1) << 8)
                                                 | (byteAt(at + 2) << 16) | (byteAt(at + 3) << 24));
            };

            ImageDescription description;
            description.format = "BMP";
            description.width = static_cast<int>(littleEndian(18));
            const std::int32_t height = littleEndian(22);
            description.height = static_cast<int>(height < 0 ? -height : height);

            // Bit count, at offset 28. Thirty-two bits is the only BMP depth with a fourth
            // channel; a `BITMAPINFOHEADER` leaves it undefined and a `BITMAPV4HEADER` gives it a
            // mask, and neither distinction changes the answer to "may this be DXT1".
            if (read >= 30)
            {
                const std::uint32_t bitCount = byteAt(28) | (byteAt(29) << 8);
                description.hasAlphaChannel = bitCount == 32;
            }
            return description;
        }

        return std::nullopt;
    }

    std::optional<ImageSize> readImageSize(const std::string& absolutePath)
    {
        // The size is the part of the description that had a caller first, and most of them still
        // want only that. Kept as its own name rather than making every one of them reach past a
        // format and an alpha flag they have no use for.
        const std::optional<ImageDescription> description = readImageDescription(absolutePath);
        if (!description) { return std::nullopt; }
        return ImageSize{description->width, description->height};
    }

    bool Detail::writeFactsIfChanged(AssetDatabase& assets, const AssetRecord& record,
                                     const JsonValue& facts)
    {
        // Compared before writing, so that opening a project twice produces no diff. That is the
        // rule every facts pass follows, and the one that keeps `--headless` safe to run against a
        // repository you want left alone -- which is why it is one function rather than a passage
        // copied into each of them.
        bool unchanged = true;
        for (const auto& [name, value] : facts.getMembers())
        {
            if (Json::write(record.importerSettings[name], false) != Json::write(value, false))
            {
                unchanged = false;
                break;
            }
        }
        if (unchanged) { return false; }

        AssetRecord* mutableRecord = assets.findMutable(record.id);
        if (mutableRecord == nullptr) { return false; }

        if (mutableRecord->importerSettings.isNull())
        {
            mutableRecord->importerSettings = JsonValue::makeObject();
        }
        for (const auto& [name, value] : facts.getMembers())
        {
            mutableRecord->importerSettings.set(name, value);
        }
        assets.writeSidecar(record.id);
        return true;
    }

    bool Detail::applySpriteFontFacts(AssetDatabase& assets, const AssetRecord& record)
    {
        const std::optional<SpriteFontDescription> description =
            readSpriteFontDescription(assets.resolvePath(record.sourcePath));
        if (!description) { return false; }

        JsonValue facts = JsonValue::makeObject();
        facts.set("fontName", JsonValue{description->fontName});
        facts.set("pointSize", JsonValue{static_cast<double>(description->pointSize)});
        facts.set("spacing", JsonValue{static_cast<double>(description->spacing)});
        facts.set("useKerning", JsonValue{description->useKerning});
        facts.set("characterRange", JsonValue{std::to_string(description->firstCharacter) + "-"
                                              + std::to_string(description->lastCharacter)});

        return writeFactsIfChanged(assets, record, facts);
    }

    bool Detail::applyModelFacts(AssetDatabase& assets, const AssetRecord& record)
    {
        // Gathered with the settings the sidecar already holds, because every one of them changes
        // what the answers *are*: a size measured at scale 1.0 beside a scale factor of 100, or a
        // vertex count taken with the file's normals beside a "Normals: Calculate" that triples
        // it, would each be two answers to one question -- which is the thing the fact/setting
        // split exists to prevent. The facts describe what this asset imports as, not what the
        // file would yield to somebody else's settings.
        const ModelImportSettings settings = ModelImportSettings::fromJson(record.importerSettings);

        const std::optional<ModelDescription> description =
            readModelDescription(assets.resolvePath(record.sourcePath), settings);
        if (!description) { return false; }

        JsonValue facts = JsonValue::makeObject();
        facts.set("meshCount", JsonValue{static_cast<double>(description->partCount)});
        facts.set("vertexCount", JsonValue{static_cast<double>(description->vertexCount)});
        facts.set("triangleCount", JsonValue{static_cast<double>(description->triangleCount)});
        facts.set("materialCount", JsonValue{static_cast<double>(description->materialCount)});
        facts.set("animationCount", JsonValue{static_cast<double>(description->animationCount)});
        facts.set("skippedPrimitives",
                  JsonValue{static_cast<double>(description->skippedPrimitives)});
        facts.set("modelSize", PropertyValue{description->size}.toJson());

        return writeFactsIfChanged(assets, record, facts);
    }

    bool Detail::applyAudioFacts(AssetDatabase& assets, const AssetRecord& record)
    {
        const std::optional<StudioAudioDescription> description =
            readAudioDescription(assets.resolvePath(record.sourcePath));
        if (!description) { return false; }

        JsonValue facts = JsonValue::makeObject();
        facts.set("sourceFormat", JsonValue{description->format});
        facts.set("duration", JsonValue{studioDescribeDuration(description->durationSeconds)});
        facts.set("sampleRate", JsonValue{static_cast<double>(description->sampleRate)});
        facts.set("channels", JsonValue{static_cast<double>(description->channels)});
        facts.set("decodedBytes", JsonValue{static_cast<double>(description->decodedBytes)});

        return writeFactsIfChanged(assets, record, facts);
    }

    std::optional<SpriteFontDescription> readSpriteFontDescription(const std::string& path)
    {
        std::ifstream stream{path, std::ios::binary};
        if (!stream) { return std::nullopt; }

        std::ostringstream buffer;
        buffer << stream.rdbuf();
        const std::string text = buffer.str();

        // The one structural check. Without it any XML file with a <Size> element would be read as
        // a sprite font, and the inspector would report confident nonsense about it. Matched on the
        // suffix because XNA writes `Graphics:FontDescription` while hand-written and
        // MonoGame-flavoured files often say `SpriteFontDescription`; both are the same asset.
        if (text.find("<Asset") == std::string::npos
            || text.find("FontDescription") == std::string::npos)
        {
            return std::nullopt;
        }

        SpriteFontDescription description;
        description.fontName = Detail::readTag(text, "FontName");

        const std::string size = Detail::readTag(text, "Size");
        if (!size.empty()) { description.pointSize = Detail::toFloat(size); }

        const std::string spacing = Detail::readTag(text, "Spacing");
        if (!spacing.empty()) { description.spacing = Detail::toFloat(spacing); }

        const std::string kerning = Detail::readTag(text, "UseKerning");
        if (!kerning.empty()) { description.useKerning = kerning != "false" && kerning != "False"; }

        description.firstCharacter = Detail::toCharacterCode(Detail::readTag(text, "Start"));
        description.lastCharacter = Detail::toCharacterCode(Detail::readTag(text, "End"));
        return description;
    }

    namespace
    {
        /**
         * @brief A built-in importer: an id, the type it claims, and the reader it delegates to.
         *
         * The readers are unchanged and stay here, beside the libraries they need. What moved is
         * *who decides which one runs* (`plan.md` STUDIO-10002) -- rewriting the readers in the
         * same change would have made a refactor and a behaviour change one diff.
         */
        class BuiltinImporter final : public StudioAssetImporter
        {
        public:
            using Reader = bool (*)(AssetDatabase&, const AssetRecord&);

            BuiltinImporter(std::string_view importerId, AssetType type, Reader reader)
                : id_(importerId), type_(type), reader_(reader)
            {
            }

            [[nodiscard]] std::string_view id() const override { return id_; }

            [[nodiscard]] bool handles(AssetType type) const override { return type == type_; }

            [[nodiscard]] bool readFacts(AssetDatabase& assets,
                                         const AssetRecord& record) const override
            {
                return reader_ != nullptr && reader_(assets, record);
            }

        private:
            std::string id_;
            AssetType type_;
            Reader reader_;
        };
    }

    void registerBuiltinAssetImporters(StudioImporterRegistry& registry)
    {
        (void)registry.add(std::make_unique<BuiltinImporter>(
            ImporterIds::kTexture, AssetType::Texture2D, &Detail::applyTextureFacts));
        (void)registry.add(std::make_unique<BuiltinImporter>(
            ImporterIds::kSpriteFont, AssetType::SpriteFont, &Detail::applySpriteFontFacts));
        (void)registry.add(std::make_unique<BuiltinImporter>(
            ImporterIds::kModel, AssetType::Model, &Detail::applyModelFacts));
        (void)registry.add(std::make_unique<BuiltinImporter>(
            ImporterIds::kSoundEffect, AssetType::SoundEffect, &Detail::applyAudioFacts));
        (void)registry.add(std::make_unique<BuiltinImporter>(
            ImporterIds::kSong, AssetType::Song, &Detail::applyAudioFacts));
    }

    const StudioImporterRegistry& getBuiltinAssetImporters()
    {
        // Built once. The built-ins are fixed for a build, and a registry rebuilt per call would
        // allocate three importers per asset.
        static const StudioImporterRegistry registry = [] {
            StudioImporterRegistry built;
            registerBuiltinAssetImporters(built);
            return built;
        }();
        return registry;
    }

    bool applyImporterFacts(AssetDatabase& assets, const Uuid& id)
    {
        return applyImporterFacts(assets, id, getBuiltinAssetImporters());
    }

    bool applyImporterFacts(AssetDatabase& assets, const Uuid& id,
                            const StudioImporterRegistry& importers)
    {
        const AssetRecord* record = assets.find(id);
        if (record == nullptr) { return false; }

        // One lookup where there was a chain of type comparisons. An asset whose type nothing
        // claims is left alone rather than treated as a failure: a project holds files Studio does
        // not import, and a scan that reported each of them as a problem would be a scan nobody
        // reads (`plan.md` STUDIO-10002).
        const StudioAssetImporter* importer = importers.forType(record->type);
        if (importer == nullptr) { return false; }

        return importer->readFacts(assets, *record);
    }

    std::size_t applyImporterFacts(AssetDatabase& assets)
    {
        return applyImporterFacts(assets, getBuiltinAssetImporters());
    }

    std::size_t applyImporterFacts(AssetDatabase& assets, const StudioImporterRegistry& importers)
    {
        std::size_t changed = 0;

        // Ids first, because applying a fact writes a sidecar and may reallocate the record store
        // underneath a walk that is holding pointers into it.
        std::vector<Uuid> ids;
        ids.reserve(assets.getCount());
        for (const AssetRecord* record : assets.getAll()) { ids.push_back(record->id); }

        for (const Uuid& assetId : ids)
        {
            changed += applyImporterFacts(assets, assetId, importers) ? 1u : 0u;
        }
        return changed;
    }

    bool Detail::applyTextureFacts(AssetDatabase& assets, const AssetRecord& record)
    {
        const std::optional<ImageDescription> description =
            readImageDescription(assets.resolvePath(record.sourcePath));
        if (!description) { return false; }

        // Through the shared writer like every other facts pass, rather than a comparison written
        // out here. Three facts instead of one is where a hand-rolled "did anything change" starts
        // getting one of them wrong, and getting it wrong means a sidecar rewritten on every open.
        JsonValue facts = JsonValue::makeObject();
        facts.set("pixelSize", PropertyValue{StudioVector2{static_cast<float>(description->width),
                                                           static_cast<float>(description->height)}}
                                   .toJson());
        facts.set("sourceFormat", JsonValue{description->format});
        facts.set("sourceAlpha", JsonValue{description->hasAlphaChannel});

        return writeFactsIfChanged(assets, record, facts);
    }

    void registerBuiltinImporters(ComponentRegistry& registry)
    {
        registry.registerComponent(makeTextureImporter());
        registry.registerComponent(makeSpriteFontImporter());
        registry.registerComponent(makeSoundEffectImporter());
        registry.registerComponent(makeSongImporter());
        registry.registerComponent(makeModelImporter());
    }
}
