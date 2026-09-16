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

#include <string>

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
}
