// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Assets/AssetImporters.hpp"

#include "CNA/Editor/Assets/ModelImport.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <istream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace CNA::Editor
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

            descriptor.properties = {
                std::move(wrap),
                std::move(filter),
                makeProperty("generateMipmaps", "Generate Mipmaps", PropertyType::Boolean,
                             PropertyValue{false},
                             "Costs a third more memory and is wrong for most 2D art, which is "
                             "drawn at its native size."),
                makeProperty("premultiplyAlpha", "Premultiply Alpha", PropertyType::Boolean,
                             PropertyValue{true},
                             "XNA's SpriteBatch defaults to premultiplied blending, so this is on "
                             "by default; turning it off means also passing NonPremultiplied when "
                             "drawing, or the edges of every sprite will halo."),

                // Read-only, because it is a fact about the file rather than a choice about it.
                // Shown because "why is this sprite blurry" is usually answered by its size.
                [] {
                    PropertyDescriptor size = makeProperty("pixelSize", "Pixel Size",
                                                           PropertyType::Vector2,
                                                           PropertyValue{EditorVector2{}},
                                                           "Read from the file's header. Zero means the format is one the editor cannot measure yet.");
                    size.readOnly = true;
                    return size;
                }(),
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

        ComponentDescriptor makeSoundEffectImporter()
        {
            ComponentDescriptor descriptor;
            descriptor.typeId = ImporterIds::kSoundEffect;
            descriptor.displayName = "Sound Effect Importer";
            descriptor.category = "Import";

            PropertyDescriptor volume = makeProperty("importVolume", "Import Volume",
                                                     PropertyType::Float, PropertyValue{1.0f},
                                                     "Applied once at import rather than at every "
                                                     "play, so a clip that is simply too loud is "
                                                     "fixed in one place.");
            volume.minimum = 0.0;
            volume.maximum = 1.0;

            descriptor.properties = {
                std::move(volume),
                makeProperty("loadIntoMemory", "Load Into Memory", PropertyType::Boolean,
                             PropertyValue{true},
                             "Off streams the clip from disk instead, which is right for music-"
                             "length audio and wrong for a footstep."),
            };
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

            descriptor.properties = {
                std::move(scale),
                makeProperty("importMaterials", "Import Materials", PropertyType::Boolean,
                             PropertyValue{true}),
                makeProperty("importAnimations", "Import Animations", PropertyType::Boolean,
                             PropertyValue{true},
                             "Declared but not yet read: animation needs a skeleton to drive, and "
                             "the imported mesh has no node hierarchy to be one. See ED-405."),
                fact("meshCount", "Meshes", PropertyType::Integer, PropertyValue{0},
                     "Drawable parts -- one per glTF primitive, since a primitive is the largest "
                     "span with a single material."),
                fact("vertexCount", "Vertices", PropertyType::Integer, PropertyValue{0}, ""),
                fact("triangleCount", "Triangles", PropertyType::Integer, PropertyValue{0}, ""),
                fact("materialCount", "Materials", PropertyType::Integer, PropertyValue{0}, ""),
                fact("modelSize", "Size", PropertyType::Vector3, PropertyValue{EditorVector3{}},
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

    std::optional<ImageSize> readImageSize(const std::string& absolutePath)
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
        if (read >= 24)
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
                return ImageSize{bigEndian(16), bigEndian(20)};
            }
        }

        // JPEG: no fixed offsets at all. The size lives in a "start of frame" segment somewhere
        // after a chain of others whose lengths have to be walked -- which is why this needs the
        // stream rather than the header block above.
        if (read >= 2 && byteAt(0) == 0xFF && byteAt(1) == 0xD8)
        {
            return readJpegSize(stream);
        }

        // BMP: "BM", then a DIB header whose width and height are little-endian at fixed offsets.
        // The height is signed and negative for a top-down image, so its magnitude is what counts.
        if (read >= 26 && byteAt(0) == 'B' && byteAt(1) == 'M')
        {
            const auto littleEndian = [&](std::size_t at) {
                return static_cast<std::int32_t>(byteAt(at) | (byteAt(at + 1) << 8)
                                                 | (byteAt(at + 2) << 16) | (byteAt(at + 3) << 24));
            };
            const std::int32_t height = littleEndian(22);
            return ImageSize{static_cast<int>(littleEndian(18)),
                             static_cast<int>(height < 0 ? -height : height)};
        }

        return std::nullopt;
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
        // The model's own `scaleFactor` decides what its size *means*, so the facts are gathered
        // with the setting the sidecar already holds. Reporting a size measured at 1.0 next to a
        // scale factor of 100 would be two answers to one question, which is the thing the
        // fact/setting split exists to prevent.
        ModelImportSettings settings;
        const JsonValue& storedScale = record.importerSettings["scaleFactor"];
        if (!storedScale.isNull())
        {
            settings.scaleFactor =
                PropertyValue::fromJson(storedScale, PropertyType::Float).get<float>();
        }

        const std::optional<ModelDescription> description =
            readModelDescription(assets.resolvePath(record.sourcePath), settings);
        if (!description) { return false; }

        JsonValue facts = JsonValue::makeObject();
        facts.set("meshCount", JsonValue{static_cast<double>(description->partCount)});
        facts.set("vertexCount", JsonValue{static_cast<double>(description->vertexCount)});
        facts.set("triangleCount", JsonValue{static_cast<double>(description->triangleCount)});
        facts.set("materialCount", JsonValue{static_cast<double>(description->materialCount)});
        facts.set("modelSize", PropertyValue{description->size}.toJson());

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

    std::size_t applyImporterFacts(AssetDatabase& assets)
    {
        std::size_t changed = 0;

        for (const AssetRecord* record : assets.getAll())
        {
            if (record->type == AssetType::SpriteFont)
            {
                changed += Detail::applySpriteFontFacts(assets, *record) ? 1 : 0;
                continue;
            }
            if (record->type == AssetType::Model)
            {
                changed += Detail::applyModelFacts(assets, *record) ? 1 : 0;
                continue;
            }
            if (record->type != AssetType::Texture2D) { continue; }

            const std::optional<ImageSize> size = readImageSize(assets.resolvePath(record->sourcePath));
            if (!size) { continue; }

            const EditorVector2 measured{static_cast<float>(size->width),
                                         static_cast<float>(size->height)};

            const JsonValue& stored = record->importerSettings["pixelSize"];
            if (!stored.isNull()
                && PropertyValue::fromJson(stored, PropertyType::Vector2).get<EditorVector2>() == measured)
            {
                continue;
            }

            AssetRecord* mutableRecord = assets.findMutable(record->id);
            if (mutableRecord == nullptr) { continue; }

            if (mutableRecord->importerSettings.isNull())
            {
                mutableRecord->importerSettings = JsonValue::makeObject();
            }
            mutableRecord->importerSettings.set("pixelSize", PropertyValue{measured}.toJson());
            assets.writeSidecar(record->id);
            ++changed;
        }

        return changed;
    }

    void registerBuiltinImporters(ComponentRegistry& registry)
    {
        registry.registerComponent(makeTextureImporter());
        registry.registerComponent(makeSpriteFontImporter());
        registry.registerComponent(makeSoundEffectImporter());
        registry.registerComponent(makeModelImporter());
    }
}
