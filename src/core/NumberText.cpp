// SPDX-License-Identifier: MS-PL
#include "CNA/Studio/Core/NumberText.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <stdexcept>

namespace CNA::Studio
{
    std::string studioFormatFloat(float value)
    {
        // Negative zero reads as "0" rather than "-0". It is a real float and the distinction is
        // real, but it is not one a property grid has any business showing: `-0` in a Position
        // field looks like a defect, and the JSON writer has always written it as `0` anyway, so
        // showing `-0` would be the one number where the field and the file disagreed.
        if (value == 0.0f) { return "0"; }

        // Nine significant digits round-trip every binary32, so this always ends with an exact
        // answer; the earlier ones are what make `0.6f` read as `0.6`.
        //
        // **Shortest is not the same as fewest digits**, which is the trap this fell into first:
        // `%.1g` of 200 is `2e+02`, which reads back as exactly 200 and is therefore a correct
        // answer to the wrong question -- a Position field showed `2e+02` where it had shown
        // `200`. A plain decimal is preferred whenever one round-trips, however many digits it
        // takes, and scientific notation is left to the values that have no other form.
        //
        // Deliberately the same rule the JSON writer uses on a number that is exactly a float, so
        // what a field shows is what a save would write. The two are separate implementations
        // because `src/core/Json.cpp` is embedded verbatim into exported games and may include
        // nothing but the standard library; `JsonAndTheFieldsAgreeAboutEveryFloat` is what keeps
        // them from drifting.
        std::string best;
        bool bestIsScientific = true;
        for (int precision = 1; precision <= 9; ++precision)
        {
            char buffer[32] = {};
            std::snprintf(buffer, sizeof(buffer), "%.*g", precision, static_cast<double>(value));
            if (std::strtof(buffer, nullptr) != value) { continue; }

            const bool scientific = std::strchr(buffer, 'e') != nullptr;
            const std::string candidate{buffer};
            if (best.empty() || (bestIsScientific && !scientific)
                || (bestIsScientific == scientific && candidate.size() < best.size()))
            {
                best = candidate;
                bestIsScientific = scientific;
            }
        }

        if (best.empty())
        {
            // Not reachable for a finite float, and reached for an infinity or a NaN, which never
            // compare equal to the text they print. Shown as `inf`, `-inf` or `nan` rather than
            // hidden: neither is valid in an authored file, and a field showing one is showing a
            // real defect somebody needs to find.
            char buffer[32] = {};
            std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(value));
            best = buffer;
        }
        return best;
    }

    bool studioParseFloat(std::string_view text, float& out)
    {
        // `std::stof` over a string rather than `from_chars` over the view, because libstdc++ 13's
        // floating-point `from_chars` is there but this keeps one implementation of "is the whole
        // of this a number" rather than two that disagree about `1e400` at the edges.
        try
        {
            const std::string owned{text};
            std::size_t consumed = 0;
            const float parsed = std::stof(owned, &consumed);
            while (consumed < owned.size()
                   && std::isspace(static_cast<unsigned char>(owned[consumed])) != 0)
            {
                ++consumed;
            }
            if (consumed != owned.size()) { return false; }
            out = parsed;
            return true;
        }
        catch (const std::exception&)
        {
            // Out of range and not-a-number both land here, and both mean the same thing to a
            // caller: the field does not hold a value it can use.
            return false;
        }
    }

    bool studioParseInteger(std::string_view text, std::int64_t& out)
    {
        try
        {
            const std::string owned{text};
            std::size_t consumed = 0;
            const long long parsed = std::stoll(owned, &consumed);
            while (consumed < owned.size()
                   && std::isspace(static_cast<unsigned char>(owned[consumed])) != 0)
            {
                ++consumed;
            }
            // `"3.5"` stops at the dot, so `consumed` is short and the whole text was not an
            // integer. Refused rather than truncated: a field that silently turns a typed 3.5 into
            // 3 is one the user has to check after every edit.
            if (consumed != owned.size()) { return false; }
            out = static_cast<std::int64_t>(parsed);
            return true;
        }
        catch (const std::exception&)
        {
            return false;
        }
    }
}
