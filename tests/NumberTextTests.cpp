// SPDX-License-Identifier: MS-PL
/**
 * @file NumberTextTests.cpp
 * @brief The text a number is shown as, and the text it is saved as.
 *
 * `plan.md` STUDIO-35037 and STUDIO-02042 — one defect with two faces, found by putting a volume
 * of `0.6` on an audio source and looking at the panel: the field said `0.600000024`, and saving
 * the scene wrote `0.600000024` into it.
 *
 * `%.9g` is the precision that round-trips every binary32, which is why it was chosen, and nine
 * significant digits of a float are nine digits of its *binary representation* — the last three of
 * them noise. `%g`'s six digits are the same mistake the other way: `33.3333` does not name the
 * float `33.333332`, so typing the displayed text back would change the document.
 *
 * What is wanted is the shortest decimal that reads back as the same float. These cases are that
 * property, stated three ways: the text is short, the text is exact, and the two implementations
 * of the rule — the panel's and the JSON writer's — agree.
 */

#include "TestHarness.hpp"

#include "CNA/Studio/Core/Json.hpp"
#include "CNA/Studio/Core/NumberText.hpp"
#include "CNA/Studio/Scene/SceneDocument.hpp"

#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

using namespace CNA::Studio;

namespace
{
    /** @brief Floats worth asking about, including the ones that made this a task. */
    const std::vector<float>& interestingFloats()
    {
        static const std::vector<float> values = {
            0.0f, -0.0f, 1.0f, -1.0f, 0.5f, 0.25f, 1.5f, 2.0f,
            0.1f, 0.2f, 0.3f, 0.6f, 0.7f, 0.05f,
            200.0f, 200.5f, 400.0f, 600.0f, 1000000.0f, -4000.0f,
            100.0f / 3.0f, 1.0f / 7.0f, 3.14159265f, 2.718281828f,
            1e-8f, 1e-20f, 1e20f, 6.02214076e23f,
            -0.75f, -12345.678f, 16777216.0f, 16777217.0f,
            std::numeric_limits<float>::min(), std::numeric_limits<float>::max(),
            std::numeric_limits<float>::denorm_min(),
        };
        return values;
    }

    /** @brief The number a JSON document holds for one float, as text. */
    std::string jsonTextFor(float value)
    {
        JsonValue object = JsonValue::makeObject();
        object.set("x", JsonValue{static_cast<double>(value)});
        const std::string written = Json::write(object, /*pretty=*/false);

        const std::size_t colon = written.find(':');
        const std::size_t close = written.find('}');
        if (colon == std::string::npos || close == std::string::npos) { return written; }
        return written.substr(colon + 1, close - colon - 1);
    }
}

CNA_STUDIO_TEST(AFloatFieldShowsTheNumberSomebodyTyped)
{
    // The defect, named directly. A Volume set to 0.6 read as 0.600000024 and a Pitch set to 0.1
    // read as 0.100000001, in a field the user is invited to edit.
    CNA_STUDIO_EXPECT_EQ(studioFormatFloat(0.6f), std::string{"0.6"});
    CNA_STUDIO_EXPECT_EQ(studioFormatFloat(0.1f), std::string{"0.1"});
    CNA_STUDIO_EXPECT_EQ(studioFormatFloat(-0.25f), std::string{"-0.25"});
    CNA_STUDIO_EXPECT_EQ(studioFormatFloat(1.5f), std::string{"1.5"});
    CNA_STUDIO_EXPECT_EQ(studioFormatFloat(0.0f), std::string{"0"});

    // Negative zero included: it is a real float, but `-0` in a Position field reads as a defect,
    // and the JSON writer has always written it as `0`.
    CNA_STUDIO_EXPECT_EQ(studioFormatFloat(-0.0f), std::string{"0"});
    CNA_STUDIO_EXPECT_EQ(studioFormatFloat(1.0f), std::string{"1"});

    // And the other half of the same mistake is not made instead: six digits would print 33.3333,
    // which is a different number from the one the document holds.
    CNA_STUDIO_EXPECT_EQ(studioFormatFloat(100.0f / 3.0f), std::string{"33.333332"});
}

CNA_STUDIO_TEST(AFormattedFloatAlwaysReadsBackAsItself)
{
    // The property that makes the shortening safe. A field the user never touches is re-parsed
    // from the text it was shown as, so any value whose text does not round-trip would drift
    // every time the panel was looked at.
    for (const float value : interestingFloats())
    {
        const std::string text = studioFormatFloat(value);
        const float parsed = std::strtof(text.c_str(), nullptr);
        if (parsed != value)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "studioFormatFloat produced '" + text + "', which reads back as a different float.");
        }
        CNA_STUDIO_EXPECT(parsed == value);
    }
}

CNA_STUDIO_TEST(AFormattedFloatIsNoLongerThanItHasToBe)
{
    // Round-tripping alone is satisfied by `%.9g`, which is what the defect was. The text must
    // also be the shortest that round-trips -- among plain decimals when any of them does, which
    // is the correction below.
    for (const float value : interestingFloats())
    {
        if (value == 0.0f) { continue; }

        const std::string text = studioFormatFloat(value);
        std::string shortestPlain;
        std::string shortestAny;
        for (int precision = 1; precision <= 9; ++precision)
        {
            char buffer[32] = {};
            std::snprintf(buffer, sizeof(buffer), "%.*g", precision, static_cast<double>(value));
            if (std::strtof(buffer, nullptr) != value) { continue; }

            const std::string candidate{buffer};
            if (shortestAny.empty() || candidate.size() < shortestAny.size())
            {
                shortestAny = candidate;
            }
            if (candidate.find('e') == std::string::npos
                && (shortestPlain.empty() || candidate.size() < shortestPlain.size()))
            {
                shortestPlain = candidate;
            }
        }

        const std::string& wanted = shortestPlain.empty() ? shortestAny : shortestPlain;
        if (text != wanted)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "studioFormatFloat produced '" + text + "' where '" + wanted + "' is the shortest "
                "exact form.");
        }
        CNA_STUDIO_EXPECT_EQ(text, wanted);
    }
}

CNA_STUDIO_TEST(APlainDecimalBeatsAShorterPieceOfScientificNotation)
{
    // The trap the first version of this fell into. `%.1g` of 200 is `2e+02`, which reads back as
    // exactly 200 and is two characters shorter -- and a Position field that had read `200` began
    // reading `2e+02`, which is how "shortest" turned out to be the wrong question.
    CNA_STUDIO_EXPECT_EQ(studioFormatFloat(200.0f), std::string{"200"});
    CNA_STUDIO_EXPECT_EQ(studioFormatFloat(200.5f), std::string{"200.5"});
    CNA_STUDIO_EXPECT_EQ(studioFormatFloat(-4000.0f), std::string{"-4000"});
    CNA_STUDIO_EXPECT_EQ(studioFormatFloat(1000000.0f), std::string{"1000000"});

    // Scientific notation is still what a value with no plain form gets, rather than twenty zeros.
    CNA_STUDIO_EXPECT_EQ(studioFormatFloat(1e20f), std::string{"1e+20"});
    CNA_STUDIO_EXPECT_EQ(studioFormatFloat(1e-8f), std::string{"1e-08"});
}

CNA_STUDIO_TEST(AuthoredFilesHoldTheNumberSomebodyTypedRatherThanItsBinaryNoise)
{
    // STUDIO-02042. The same defect in the file rather than on the screen, and the worse half of
    // it: a scene's diff is read by people, and `0.600000024` where `0.6` was authored is noise
    // in every later review of that file.
    CNA_STUDIO_EXPECT_EQ(jsonTextFor(0.6f), std::string{"0.6"});
    CNA_STUDIO_EXPECT_EQ(jsonTextFor(0.1f), std::string{"0.1"});
    CNA_STUDIO_EXPECT_EQ(jsonTextFor(0.3f), std::string{"0.3"});
}

CNA_STUDIO_TEST(JsonAndTheFieldsAgreeAboutEveryFloat)
{
    // Two implementations of one rule, because `src/core/Json.cpp` is embedded verbatim into an
    // exported game and may include nothing but the standard library. This is what stops them
    // drifting -- and it is also the invariant a user should be able to rely on: the number the
    // panel shows is the number a save writes.
    for (const float value : interestingFloats())
    {
        const std::string field = studioFormatFloat(value);
        const std::string file = jsonTextFor(value);
        if (field != file)
        {
            CnaStudioTest::reportFailure(__FILE__, __LINE__,
                "the panel shows '" + field + "' where a save writes '" + file + "'.");
        }
        CNA_STUDIO_EXPECT_EQ(field, file);
    }
}

CNA_STUDIO_TEST(WholeNumbersStayWholeAndDoublesKeepTheirPrecision)
{
    // Two behaviours the shortening must not break. An entity count or a pixel coordinate is
    // written without a decimal point so a diff stays readable, and a value that is genuinely a
    // double -- not a float widened on the way in -- keeps every digit it needs.
    JsonValue object = JsonValue::makeObject();
    object.set("count", JsonValue{12.0});
    object.set("stamp", JsonValue{-4648124113.0});
    object.set("precise", JsonValue{0.12345678901234567});
    const std::string written = Json::write(object, /*pretty=*/false);

    CNA_STUDIO_EXPECT(written.find("\"count\":12") != std::string::npos);
    CNA_STUDIO_EXPECT(written.find("\"stamp\":-4648124113") != std::string::npos);

    const JsonParseResult parsed = Json::parse(written);
    CNA_STUDIO_EXPECT(parsed.succeeded);
    CNA_STUDIO_EXPECT_EQ(parsed.value["precise"].asNumber(), 0.12345678901234567);
}

CNA_STUDIO_TEST(ASavedSceneHoldsThePropertyValueAsItWasAuthored)
{
    // End to end, because the two cases above are about a formatter and this is about the file a
    // user's version control will show them.
    SceneDocument scene;
    StudioEntity subject{Uuid::generate(), "Speaker"};
    StudioComponent source{"CNA.AudioSource"};
    source.setProperty("volume", PropertyValue{0.6f});
    source.setProperty("pan", PropertyValue{-0.25f});
    subject.getComponents().push_back(std::move(source));
    scene.addEntity(std::move(subject));

    const std::string written = Json::write(scene.toJson());
    CNA_STUDIO_EXPECT(written.find("0.600000024") == std::string::npos);
    CNA_STUDIO_EXPECT(written.find("0.6") != std::string::npos);
    CNA_STUDIO_EXPECT(written.find("-0.25") != std::string::npos);
}
