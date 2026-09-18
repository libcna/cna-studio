// SPDX-License-Identifier: MS-PL
/**
 * @file FontImportTests.cpp
 * @brief A `.ttf` is a typeface, not a font description (`plan.md` STUDIO-10006).
 *
 * The bug this closes is quiet: a font dropped in a project was typed as `SpriteFont` and handed
 * the sprite-font importer, which looks for `<Asset … FontDescription>` in what is a binary file,
 * found none, and left the inspector showing five empty read-only fields. Nothing failed. Nothing
 * was reported. The font simply did not import.
 *
 * So these cases are about the two halves of that: the tables really are read, and the *type* is
 * right -- including for a project that was scanned by a build which got it wrong.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/Assets/AssetImporters.hpp"
#include "CNA/Studio/Assets/FontImport.hpp"
#include "CNA/Studio/Core/Json.hpp"

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
                  / ("cna-studio-font-" + name + "-" + std::to_string(counter()++));
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
        for (int shift = 24; shift >= 0; shift -= 8)
        {
            out.push_back(static_cast<unsigned char>((value >> shift) & 0xFFu));
        }
    }

    void append16(std::vector<unsigned char>& out, std::uint16_t value)
    {
        out.push_back(static_cast<unsigned char>((value >> 8) & 0xFFu));
        out.push_back(static_cast<unsigned char>(value & 0xFFu));
    }

    /** @brief A `name` table carrying @p family as name ID 1 and @p style as ID 2, in UTF-16BE. */
    std::vector<unsigned char> makeNameTable(const std::u16string& family, const std::u16string& style,
                                             std::uint16_t platform = 3)
    {
        const auto encode = [&](const std::u16string& text) {
            std::vector<unsigned char> bytes;
            if (platform == 3)
            {
                for (const char16_t unit : text) { append16(bytes, static_cast<std::uint16_t>(unit)); }
            }
            else
            {
                // Macintosh, platform 1 encoding 0: one byte a character, ASCII in practice.
                for (const char16_t unit : text) { bytes.push_back(static_cast<unsigned char>(unit)); }
            }
            return bytes;
        };

        const std::vector<unsigned char> familyBytes = encode(family);
        const std::vector<unsigned char> styleBytes = encode(style);

        std::vector<unsigned char> table;
        append16(table, 0);  // format
        append16(table, 2);  // record count
        append16(table, static_cast<std::uint16_t>(6 + 2 * 12));  // storage offset

        const auto record = [&](std::uint16_t nameId, std::uint16_t length, std::uint16_t offset) {
            append16(table, platform);
            append16(table, platform == 3 ? 1 : 0);  // encoding: Unicode BMP, or MacRoman
            append16(table, 0);                      // language
            append16(table, nameId);
            append16(table, length);
            append16(table, offset);
        };

        record(1, static_cast<std::uint16_t>(familyBytes.size()), 0);
        record(2, static_cast<std::uint16_t>(styleBytes.size()),
               static_cast<std::uint16_t>(familyBytes.size()));

        table.insert(table.end(), familyBytes.begin(), familyBytes.end());
        table.insert(table.end(), styleBytes.begin(), styleBytes.end());
        return table;
    }

    struct FontShape
    {
        std::uint32_t sfntVersion = 0x00010000u;
        std::uint16_t glyphCount = 512;
        std::uint16_t unitsPerEm = 2048;
        std::u16string family = u"Test Sans";
        std::u16string style = u"Regular";
        std::uint16_t namePlatform = 3;
        bool withKern = false;

        /** @brief Emit the table directory and then stop, leaving the tables themselves missing. */
        bool truncateAfterDirectory = false;

        /** @brief Point `head` past the end of the file, which is what a hostile file does. */
        bool headOutOfBounds = false;

        /** @brief Claim a `name` table of this many bytes, however long it really is. */
        std::uint32_t nameLengthLie = 0;

        /**
         * @brief Where this font's offset table starts in the file.
         *
         * Zero for a standalone font. Inside a collection it is the font's own offset, because a
         * TTC's table records hold offsets from the start of the *file* rather than from the start
         * of the font -- which is the detail a fixture gets wrong first.
         */
        std::uint32_t baseOffset = 0;
    };

    /** @brief A minimal but real sfnt: an offset table, a directory, and `head`, `maxp`, `name`. */
    std::vector<unsigned char> makeFont(const FontShape& shape)
    {
        std::vector<unsigned char> head(54, 0);
        head[18] = static_cast<unsigned char>((shape.unitsPerEm >> 8) & 0xFFu);
        head[19] = static_cast<unsigned char>(shape.unitsPerEm & 0xFFu);

        std::vector<unsigned char> maxp(6, 0);
        maxp[0] = 0x00;
        maxp[1] = 0x01;
        maxp[4] = static_cast<unsigned char>((shape.glyphCount >> 8) & 0xFFu);
        maxp[5] = static_cast<unsigned char>(shape.glyphCount & 0xFFu);

        const std::vector<unsigned char> name = makeNameTable(shape.family, shape.style,
                                                              shape.namePlatform);
        const std::vector<unsigned char> kern(8, 0);

        struct Entry
        {
            std::uint32_t tag;
            const std::vector<unsigned char>* bytes;
        };
        std::vector<Entry> tables{{0x68656164u, &head}, {0x6D617870u, &maxp}, {0x6E616D65u, &name}};
        if (shape.withKern) { tables.push_back(Entry{0x6B65726Eu, &kern}); }

        std::vector<unsigned char> font;
        append32(font, shape.sfntVersion);
        append16(font, static_cast<std::uint16_t>(tables.size()));
        append16(font, 0);
        append16(font, 0);
        append16(font, 0);

        std::uint32_t offset =
            shape.baseOffset + static_cast<std::uint32_t>(12 + tables.size() * 16);
        std::vector<unsigned char> body;
        for (const Entry& entry : tables)
        {
            append32(font, entry.tag);
            append32(font, 0);  // checksum, which nothing here reads
            append32(font, shape.headOutOfBounds && entry.tag == 0x68656164u ? 0x7FFFFFFFu : offset);
            append32(font, shape.nameLengthLie > 0 && entry.tag == 0x6E616D65u
                               ? shape.nameLengthLie
                               : static_cast<std::uint32_t>(entry.bytes->size()));

            body.insert(body.end(), entry.bytes->begin(), entry.bytes->end());
            offset += static_cast<std::uint32_t>(entry.bytes->size());
        }

        if (!shape.truncateAfterDirectory) { font.insert(font.end(), body.begin(), body.end()); }
        return font;
    }
}

CNA_STUDIO_TEST(AFontReportsItsFamilyStyleAndDesignGridFromItsOwnTables)
{
    const ScopedDirectory directory{"tables"};

    FontShape shape;
    shape.withKern = true;
    const std::optional<StudioFontDescription> description =
        readFontDescription(directory.write("Test.ttf", makeFont(shape)));

    CNA_STUDIO_EXPECT(description.has_value());
    if (!description) { return; }

    // From the name table, not from the file name. A font reporting "Test" back at somebody because
    // that is what they called the file is an answer that helps nobody.
    CNA_STUDIO_EXPECT_EQ(description->family, std::string{"Test Sans"});
    CNA_STUDIO_EXPECT_EQ(description->style, std::string{"Regular"});

    CNA_STUDIO_EXPECT_EQ(description->format, std::string{"TrueType"});
    CNA_STUDIO_EXPECT_EQ(description->glyphCount, std::uint32_t{512});
    CNA_STUDIO_EXPECT_EQ(description->unitsPerEm, std::uint32_t{2048});
    CNA_STUDIO_EXPECT(description->hasKerning);

    // No kern or GPOS table means the Kerning setting beside it changes nothing, which a user
    // toggling it cannot otherwise tell from a bug.
    FontShape plain;
    CNA_STUDIO_EXPECT(!readFontDescription(directory.write("Plain.ttf", makeFont(plain)))->hasKerning);
}

CNA_STUDIO_TEST(TheSfntVersionSaysWhichKindOfFontItIs)
{
    const ScopedDirectory directory{"versions"};

    FontShape openType;
    openType.sfntVersion = 0x4F54544Fu;  // 'OTTO'
    CNA_STUDIO_EXPECT_EQ(readFontDescription(directory.write("Cff.otf", makeFont(openType)))->format,
                         std::string{"OpenType"});

    // Apple's older 'true' tag is TrueType by another name.
    FontShape appleTrue;
    appleTrue.sfntVersion = 0x74727565u;
    CNA_STUDIO_EXPECT_EQ(readFontDescription(directory.write("Old.ttf", makeFont(appleTrue)))->format,
                         std::string{"TrueType"});

    // A collection is a header pointing at several fonts. The first is reported, because a family
    // in one file is still one asset and choosing between them needs a picker nothing has yet.
    std::vector<unsigned char> collection;
    append32(collection, 0x74746366u);  // 'ttcf'
    append32(collection, 0x00010000u);
    append32(collection, 2);            // font count
    append32(collection, 20);           // first font's offset table
    append32(collection, 20);           // and the second, pointed at the same one

    FontShape innerShape;
    innerShape.baseOffset = 20;  // where the collection header leaves off
    const std::vector<unsigned char> inner = makeFont(innerShape);
    collection.insert(collection.end(), inner.begin(), inner.end());

    const std::optional<StudioFontDescription> read =
        readFontDescription(directory.write("Family.ttc", collection));
    CNA_STUDIO_EXPECT(read.has_value());
    if (read)
    {
        CNA_STUDIO_EXPECT_EQ(read->format, std::string{"TrueType Collection"});
        CNA_STUDIO_EXPECT_EQ(read->family, std::string{"Test Sans"});
    }
}

CNA_STUDIO_TEST(AFontsNameSurvivesNotBeingAscii)
{
    // The name table is UTF-16BE, and a reader that assumed one byte a character would turn a
    // family name into interleaved nulls -- a silent corruption of somebody's name, in the one
    // field of the inspector that is theirs rather than the editor's.
    const ScopedDirectory directory{"unicode"};

    FontShape shape;
    shape.family = u"Joséfin Про";  // Josefin Про
    shape.style = u"中等";                        // 中等
    const std::optional<StudioFontDescription> description =
        readFontDescription(directory.write("Wide.ttf", makeFont(shape)));

    CNA_STUDIO_EXPECT(description.has_value());
    if (!description) { return; }
    CNA_STUDIO_EXPECT_EQ(description->family, std::string{"Joséfin Про"});
    CNA_STUDIO_EXPECT_EQ(description->style, std::string{"中等"});

    // A font with only Macintosh names is read too, rather than reported as nameless: it is the
    // fallback every font carries and some older ones carry alone.
    FontShape mac;
    mac.namePlatform = 1;
    mac.family = u"Chicago";
    CNA_STUDIO_EXPECT_EQ(readFontDescription(directory.write("Mac.ttf", makeFont(mac)))->family,
                         std::string{"Chicago"});
}

CNA_STUDIO_TEST(ABrokenFontSaysWhyAndAFileThatIsNotOneSaysNothing)
{
    const ScopedDirectory directory{"broken"};

    // The sfnt version is right and the tables are not there. That is a font somebody truncated,
    // and it is exactly the case a reader returning a bare nothing makes indistinguishable from a
    // readme (`plan.md` STUDIO-10013).
    FontShape truncated;
    truncated.truncateAfterDirectory = true;
    std::string problem;
    CNA_STUDIO_EXPECT(!readFontDescription(directory.write("Cut.ttf", makeFont(truncated)), &problem));
    CNA_STUDIO_EXPECT(!problem.empty());

    // A table record pointing past the end of the file is refused by arithmetic rather than seeked
    // to. A font is a file somebody put in a project folder and may be hostile.
    FontShape hostile;
    hostile.headOutOfBounds = true;
    problem.clear();
    CNA_STUDIO_EXPECT(!readFontDescription(directory.write("Evil.ttf", makeFont(hostile)), &problem));
    CNA_STUDIO_EXPECT(!problem.empty());

    // A table claiming to be four gigabytes long is a dozen bytes to write. What this case pins is
    // the *outcome*: a font whose name table cannot be read is a font with no family, not a crash
    // and not a refusal of the whole file.
    //
    // It does not pin the bound itself, and saying so is better than implying otherwise: the read
    // would fail either way, because a short read is refused as well. What the arithmetic buys is
    // that four gigabytes are never *asked for* -- and making that observable would mean putting a
    // counter in a file that has no other reason for one.
    FontShape enormous;
    enormous.nameLengthLie = 0xFFFFFFF0u;
    problem.clear();
    const std::optional<StudioFontDescription> huge =
        readFontDescription(directory.write("Huge.ttf", makeFont(enormous)), &problem);

    // The head and maxp tables are still readable, so this is a *font with an unreadable name*
    // rather than a broken file -- reported as a font with no family, not refused outright.
    CNA_STUDIO_EXPECT(huge.has_value());
    if (huge) { CNA_STUDIO_EXPECT(huge->family.empty()); }

    // A file that never claimed to be a font is silent. A project is full of these.
    problem.clear();
    CNA_STUDIO_EXPECT(!readFontDescription(
        directory.write("notes.txt", {'h', 'e', 'l', 'l', 'o', ' ', 'y', 'o', 'u'}), &problem));
    CNA_STUDIO_EXPECT(problem.empty());

    problem.clear();
    CNA_STUDIO_EXPECT(!readFontDescription((directory.path() / "absent.ttf").generic_string(),
                                           &problem));
    CNA_STUDIO_EXPECT(problem.empty());
}

CNA_STUDIO_TEST(AFontIsItsOwnAssetTypeRatherThanASpriteFontDescription)
{
    const ScopedDirectory directory{"types"};
    directory.write("Assets/Body.ttf", makeFont(FontShape{}));
    directory.write("Assets/Ui.otf", makeFont(FontShape{}));

    {
        std::ofstream stream{directory.path() / "Assets" / "Title.spritefont", std::ios::binary};
        stream << "<?xml version=\"1.0\"?><XnaContent><Asset Type=\"Graphics:FontDescription\">"
                  "<FontName>Segoe UI</FontName><Size>14</Size></Asset></XnaContent>";
    }

    AssetDatabase assets;
    assets.setProjectRoot(directory.path().generic_string());
    CNA_STUDIO_EXPECT(assets.scan("Assets").succeeded);

    const AssetRecord* body = assets.findByPath("Assets/Body.ttf");
    const AssetRecord* ui = assets.findByPath("Assets/Ui.otf");
    const AssetRecord* title = assets.findByPath("Assets/Title.spritefont");
    CNA_STUDIO_EXPECT(body != nullptr && ui != nullptr && title != nullptr);
    if (body == nullptr || ui == nullptr || title == nullptr) { return; }

    CNA_STUDIO_EXPECT(body->type == AssetType::Font);
    CNA_STUDIO_EXPECT(ui->type == AssetType::Font);
    CNA_STUDIO_EXPECT_EQ(body->importerId, std::string{ImporterIds::kFont});

    // The description keeps its own type and its own importer. The two are not variants of each
    // other: one settles the size and the range, and the other settles nothing.
    CNA_STUDIO_EXPECT(title->type == AssetType::SpriteFont);
    CNA_STUDIO_EXPECT_EQ(title->importerId, std::string{ImporterIds::kSpriteFont});

    // And the facts come out, where before a `.ttf` got the sprite-font importer, was declined, and
    // showed five empty fields.
    CNA_STUDIO_EXPECT_EQ(applyImporterFacts(assets), std::size_t{3});
    CNA_STUDIO_EXPECT_EQ(assets.findByPath("Assets/Body.ttf")->importerSettings["family"].asString(),
                         std::string{"Test Sans"});
    CNA_STUDIO_EXPECT_EQ(assets.findByPath("Assets/Title.spritefont")
                             ->importerSettings["fontName"].asString(),
                         std::string{"Segoe UI"});

    // Idempotent, like every other facts pass.
    CNA_STUDIO_EXPECT_EQ(applyImporterFacts(assets), std::size_t{0});
}

CNA_STUDIO_TEST(AFontScannedByAnOlderBuildIsRetypedRatherThanLeftBroken)
{
    // A project scanned before `AssetType::Font` existed has a sidecar saying "SpriteFont" for its
    // `.ttf`. Left alone, that font would be handed the sprite-font importer for ever -- the exact
    // broken state this task exists to end, preserved by the fix for it.
    const ScopedDirectory directory{"retype"};
    directory.write("Assets/Body.ttf", makeFont(FontShape{}));

    const Uuid id = Uuid::generate();
    {
        std::ofstream stream{directory.path() / "Assets" / "Body.ttf.cnaasset", std::ios::binary};
        stream << "{\"formatVersion\":1,\"id\":\"" << id.toString()
               << "\",\"type\":\"SpriteFont\",\"importer\":\"CNA.SpriteFontImporter\"}";
    }

    AssetDatabase assets;
    assets.setProjectRoot(directory.path().generic_string());
    CNA_STUDIO_EXPECT(assets.scan("Assets").succeeded);

    const AssetRecord* record = assets.find(id);
    CNA_STUDIO_EXPECT(record != nullptr);
    if (record == nullptr) { return; }

    // The id survives, which is the thing scenes reference and the thing that must never change.
    CNA_STUDIO_EXPECT(record->type == AssetType::Font);
    CNA_STUDIO_EXPECT_EQ(record->importerId, std::string{ImporterIds::kFont});
    CNA_STUDIO_EXPECT(applyImporterFacts(assets, id));

    // An importer somebody pointed somewhere else on purpose is left where they pointed it.
    const ScopedDirectory chosen{"retype-chosen"};
    chosen.write("Assets/Body.ttf", makeFont(FontShape{}));
    {
        std::ofstream stream{chosen.path() / "Assets" / "Body.ttf.cnaasset", std::ios::binary};
        stream << "{\"formatVersion\":1,\"id\":\"" << Uuid::generate().toString()
               << "\",\"type\":\"SpriteFont\",\"importer\":\"Studio.CustomFontImporter\"}";
    }

    AssetDatabase custom;
    custom.setProjectRoot(chosen.path().generic_string());
    CNA_STUDIO_EXPECT(custom.scan("Assets").succeeded);
    CNA_STUDIO_EXPECT_EQ(custom.findByPath("Assets/Body.ttf")->importerId,
                         std::string{"Studio.CustomFontImporter"});
}

CNA_STUDIO_TEST(FontSettingsReadBackTheDeclaredDefaultsAndCountTheirRangeHonestly)
{
    const StudioFontImportSettings untouched =
        StudioFontImportSettings::fromJson(JsonValue::makeObject());

    // Absent is the declared default, not a zero: a point size of 0 is a font nobody can read, and
    // a first character of 0 rasterises thirty-two control codes nobody asked for.
    CNA_STUDIO_EXPECT(untouched.pointSize > 15.9f && untouched.pointSize < 16.1f);
    CNA_STUDIO_EXPECT_EQ(untouched.firstCharacter, 32);
    CNA_STUDIO_EXPECT_EQ(untouched.lastCharacter, 126);
    CNA_STUDIO_EXPECT(untouched.useKerning);
    CNA_STUDIO_EXPECT_EQ(untouched.characterCount(), std::size_t{95});
    CNA_STUDIO_EXPECT(StudioFontImportSettings::fromJson(JsonValue{}).useKerning);

    JsonValue chosen = JsonValue::makeObject();
    chosen.set("pointSize", JsonValue{32.0});
    chosen.set("firstCharacter", JsonValue{65.0});
    chosen.set("lastCharacter", JsonValue{90.0});
    chosen.set("spacing", JsonValue{-1.5});
    chosen.set("useKerning", JsonValue{false});

    const StudioFontImportSettings edited = StudioFontImportSettings::fromJson(chosen);
    CNA_STUDIO_EXPECT(edited.pointSize > 31.9f && edited.pointSize < 32.1f);
    CNA_STUDIO_EXPECT_EQ(edited.characterCount(), std::size_t{26});
    CNA_STUDIO_EXPECT(edited.spacing < -1.4f);
    CNA_STUDIO_EXPECT(!edited.useKerning);

    // An inverted range is empty rather than four billion. The two fields are edited separately, so
    // a user passes through this state on the way to any range that moves downwards.
    StudioFontImportSettings inverted;
    inverted.firstCharacter = 100;
    inverted.lastCharacter = 50;
    CNA_STUDIO_EXPECT_EQ(inverted.characterCount(), std::size_t{0});

    StudioFontImportSettings single;
    single.firstCharacter = 65;
    single.lastCharacter = 65;
    CNA_STUDIO_EXPECT_EQ(single.characterCount(), std::size_t{1});
}

CNA_STUDIO_TEST(EverySettingTheFontImporterDeclaresIsOneItsSettingsStructReads)
{
    // The same policy the model and audio importers are held to. These settings are a decision
    // recorded for the content build rather than something Studio acts on -- it rasterises nothing
    // -- but `fromJson` is the single reader of them, and a declared field it did not read would be
    // a control that goes nowhere at all.
    ComponentRegistry importers;
    registerBuiltinImporters(importers);

    const ComponentDescriptor* descriptor = importers.find(ImporterIds::kFont);
    CNA_STUDIO_EXPECT(descriptor != nullptr);
    if (descriptor == nullptr) { return; }

    const StudioFontImportSettings defaults;

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
                         JsonValue{static_cast<double>(property.defaultValue.get<float>()) + 3.0});
        }
        else if (property.type == PropertyType::Integer)
        {
            settings.set(property.name,
                         JsonValue{static_cast<double>(property.defaultValue.get<std::int64_t>()) + 1.0});
        }
        else
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                                         "the font importer declares a setting of a kind this case "
                                         "does not know how to change: " + property.name);
            continue;
        }

        const StudioFontImportSettings read = StudioFontImportSettings::fromJson(settings);
        const bool noticed = read.pointSize != defaults.pointSize
                             || read.firstCharacter != defaults.firstCharacter
                             || read.lastCharacter != defaults.lastCharacter
                             || read.spacing != defaults.spacing
                             || read.useKerning != defaults.useKerning;
        if (!noticed)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                                         "the font importer declares '" + property.name
                                             + "', and changing it changes nothing the settings "
                                               "read.");
        }
    }
}
