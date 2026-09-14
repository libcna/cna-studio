// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Ui/UiInputState.hpp"

namespace CNA::Editor
{
    void UiInputState::clearEvents()
    {
        // Wheel and characters are per-frame events; mouse position, button and key state are
        // absolute and must survive into the next frame, or every held button would read as a
        // release the moment input stopped arriving.
        wheelX = 0.0f;
        wheelY = 0.0f;
        characters.clear();
    }

    void UiInputState::appendUtf8(std::string_view text)
    {
        std::size_t offset = 0;
        while (offset < text.size())
        {
            const auto lead = static_cast<unsigned char>(text[offset]);
            char32_t codePoint = 0;
            std::size_t length = 1;

            if (lead < 0x80u) { codePoint = lead; length = 1; }
            else if ((lead & 0xE0u) == 0xC0u) { codePoint = lead & 0x1Fu; length = 2; }
            else if ((lead & 0xF0u) == 0xE0u) { codePoint = lead & 0x0Fu; length = 3; }
            else if ((lead & 0xF8u) == 0xF0u) { codePoint = lead & 0x07u; length = 4; }
            else { ++offset; continue; }

            if (offset + length > text.size()) { break; }
            for (std::size_t index = 1; index < length; ++index)
            {
                codePoint = (codePoint << 6) | (static_cast<unsigned char>(text[offset + index]) & 0x3Fu);
            }
            offset += length;

            if (codePoint <= 0xFFFF)
            {
                characters.push_back(static_cast<char16_t>(codePoint));
            }
            else
            {
                // Above the basic multilingual plane: emit a surrogate pair, matching what
                // CNA's TextInputEXT delivers for the same code point.
                const char32_t adjusted = codePoint - 0x10000;
                characters.push_back(static_cast<char16_t>(0xD800 + (adjusted >> 10)));
                characters.push_back(static_cast<char16_t>(0xDC00 + (adjusted & 0x3FF)));
            }
        }
    }
}
