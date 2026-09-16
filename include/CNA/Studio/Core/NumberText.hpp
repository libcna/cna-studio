// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Core/NumberText.hpp
 * @brief Turning a number into the text a person would have typed for it.
 *
 * `plan.md` STUDIO-35037, STUDIO-02042.
 *
 * ### The problem this exists for
 *
 * `%.9g` is the precision that round-trips every IEEE binary32, and it is what Studio used. It is
 * also what put `0.600000024` in a Volume field the user had set to `0.6`, and `0.100000001` in a
 * Pitch field set to `0.1` — because nine significant digits of a float are nine digits of its
 * *binary representation*, and the last three of them are noise. A property grid that answers
 * "0.6" with "0.600000024" reads as a tool that has lost the number.
 *
 * `%g` — six digits — is the other half of the same mistake in the other direction: it prints
 * `33.3333` for a float that is `33.333332`, so the text no longer names the value, and typing it
 * back would change the document.
 *
 * The answer is neither: it is the **shortest** decimal that reads back as the same float. For
 * `0.6f` that is `0.6`, for `100.0f/3.0f` it is `33.333332`, and in both cases parsing the text
 * returns the exact float it came from, so a field the user does not touch cannot drift.
 */

#include <cstdint>
#include <string>
#include <string_view>

namespace CNA::Studio
{
    /**
     * @brief Formats @p value as the shortest text that parses back to the same float.
     *
     * Found by trying precisions rather than by computing one, because the shortest round-tripping
     * representation is a property of the value and not of its magnitude: `0.5f` needs one digit
     * and `0.1f` needs one, while `100.0f/3.0f` needs eight. Nine is the ceiling — `%.9g`
     * round-trips every binary32 — so the search is bounded and always terminates with an exact
     * answer.
     *
     * Infinities and NaN come back as `inf`, `-inf` and `nan`, which is what `%g` produces: they
     * are not valid in an authored file, and a field showing one is showing a real defect that
     * hiding behind an empty string would not help anybody find.
     *
     * @param value The float.
     * @return Its shortest exact decimal text.
     */
    [[nodiscard]] std::string studioFormatFloat(float value);

    /**
     * @brief Parses a float from @p text, refusing anything with characters left over.
     *
     * "3abc" is not three. Accepting a prefix is how a typo silently becomes a value the user did
     * not enter and cannot see is wrong — which is worse than a rejected edit, because the field
     * then shows a number they did not type and have no reason to doubt.
     *
     * Trailing whitespace is allowed, because it is what a paste brings with it and is not a typo.
     *
     * @param text The text to read.
     * @param out Receives the value; untouched when the text is not a number.
     * @return Whether the whole of @p text was one number.
     */
    [[nodiscard]] bool studioParseFloat(std::string_view text, float& out);

    /**
     * @brief Parses an integer from @p text, refusing anything with characters left over.
     *
     * The same contract as @ref studioParseFloat and for the same reason. Refuses `"3.5"`
     * outright rather than truncating it: a field that silently turns a typed 3.5 into 3 is a
     * field the user has to check after every edit.
     *
     * @param text The text to read.
     * @param out Receives the value; untouched when the text is not an integer.
     * @return Whether the whole of @p text was one integer.
     */
    [[nodiscard]] bool studioParseInteger(std::string_view text, std::int64_t& out);
}
