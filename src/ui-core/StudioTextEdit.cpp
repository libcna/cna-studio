// SPDX-License-Identifier: MS-PL
/**
 * @file StudioTextEdit.cpp
 * @brief The caret and selection model.
 */

#include "CNA/Studio/UiCore/StudioTextEdit.hpp"

#include <algorithm>

namespace CNA::Studio
{
    namespace
    {
        /** @brief Whether @p byte continues a multi-byte sequence rather than starting one. */
        bool isContinuation(char byte)
        {
            return (static_cast<unsigned char>(byte) & 0xC0U) == 0x80U;
        }
    }

    std::size_t studioUtf8BoundaryAt(std::string_view utf8, std::size_t offset)
    {
        offset = std::min(offset, utf8.size());
        while (offset > 0 && offset < utf8.size() && isContinuation(utf8[offset])) { --offset; }
        return offset;
    }

    std::size_t studioUtf8Previous(std::string_view utf8, std::size_t offset)
    {
        offset = std::min(offset, utf8.size());
        if (offset == 0) { return 0; }

        --offset;
        while (offset > 0 && isContinuation(utf8[offset])) { --offset; }
        return offset;
    }

    std::size_t studioUtf8Next(std::string_view utf8, std::size_t offset)
    {
        if (offset >= utf8.size()) { return utf8.size(); }

        ++offset;
        while (offset < utf8.size() && isContinuation(utf8[offset])) { ++offset; }
        return offset;
    }

    namespace
    {
        /**
         * @brief The grapheme-break class of a code point, for the rules below.
         *
         * Named for UAX #29's property values so the rules can be read against the standard. The
         * ranges are the ones that need no data tables; see `studioGraphemePrevious` for what that
         * leaves out and why.
         */
        enum class BreakClass
        {
            Other,
            CarriageReturn,
            LineFeed,
            Control,
            Extend,          //!< Combining marks, variation selectors, emoji modifiers.
            ZeroWidthJoiner,
            RegionalIndicator,
            Prepend,
            SpacingMark,
            HangulL,
            HangulV,
            HangulT,
            HangulLV,
            HangulLVT
        };

        bool inRange(char32_t c, char32_t low, char32_t high) { return c >= low && c <= high; }

        /** @brief Whether @p c is a combining mark or other zero-width attachment. */
        bool isExtend(char32_t c)
        {
            // The combining-mark blocks, the marks that live inside scripts, the variation
            // selectors and the emoji skin-tone modifiers. A range list rather than a table: these
            // are contiguous by design, and the ones that are not are the ones GB9c covers.
            return inRange(c, 0x0300, 0x036F)     // Combining Diacritical Marks
                || inRange(c, 0x0483, 0x0489)     // Cyrillic
                || inRange(c, 0x0591, 0x05BD) || c == 0x05BF
                || inRange(c, 0x05C1, 0x05C2) || inRange(c, 0x05C4, 0x05C5) || c == 0x05C7
                || inRange(c, 0x0610, 0x061A) || inRange(c, 0x064B, 0x065F) || c == 0x0670
                || inRange(c, 0x06D6, 0x06DC) || inRange(c, 0x06DF, 0x06E4)
                || inRange(c, 0x06E7, 0x06E8) || inRange(c, 0x06EA, 0x06ED)
                || inRange(c, 0x0711, 0x0711) || inRange(c, 0x0730, 0x074A)
                || inRange(c, 0x07A6, 0x07B0) || inRange(c, 0x07EB, 0x07F3)
                || inRange(c, 0x0816, 0x0819) || inRange(c, 0x081B, 0x0823)
                || inRange(c, 0x0825, 0x0827) || inRange(c, 0x0829, 0x082D)
                || inRange(c, 0x0900, 0x0902) || c == 0x093A || c == 0x093C
                || inRange(c, 0x0941, 0x0948) || c == 0x094D || inRange(c, 0x0951, 0x0957)
                || inRange(c, 0x0962, 0x0963)
                || inRange(c, 0x0E31, 0x0E31) || inRange(c, 0x0E34, 0x0E3A)
                || inRange(c, 0x0E47, 0x0E4E)
                || inRange(c, 0x1AB0, 0x1AFF)     // Combining Diacritical Marks Extended
                || inRange(c, 0x1DC0, 0x1DFF)     // Combining Diacritical Marks Supplement
                || inRange(c, 0x20D0, 0x20F0)     // Combining Diacritical Marks for Symbols
                || inRange(c, 0x302A, 0x302F)     // CJK tone marks
                || inRange(c, 0xFE00, 0xFE0F)     // Variation selectors
                || inRange(c, 0xFE20, 0xFE2F)     // Combining Half Marks
                || inRange(c, 0x1F3FB, 0x1F3FF)   // Emoji skin-tone modifiers
                || inRange(c, 0xE0100, 0xE01EF);  // Variation Selectors Supplement
        }

        /** @brief Whether @p c is a spacing combining mark: it takes width but joins leftwards. */
        bool isSpacingMark(char32_t c)
        {
            // Devanagari and the Indic scripts' visible vowel signs. They are `SpacingMark` rather
            // than `Extend` in UAX #29 -- a different property with, for a caret, the same answer.
            return inRange(c, 0x0903, 0x0903) || inRange(c, 0x093B, 0x093B)
                || inRange(c, 0x093E, 0x0940) || inRange(c, 0x0949, 0x094C)
                || inRange(c, 0x094E, 0x094F) || inRange(c, 0x0982, 0x0983)
                || inRange(c, 0x09BE, 0x09C0) || inRange(c, 0x09C7, 0x09C8)
                || inRange(c, 0x0A03, 0x0A03) || inRange(c, 0x0A3E, 0x0A40)
                || inRange(c, 0x0ABE, 0x0AC0) || inRange(c, 0x0B02, 0x0B03)
                || inRange(c, 0x0C01, 0x0C03) || inRange(c, 0x0C41, 0x0C44)
                || inRange(c, 0x0D02, 0x0D03) || inRange(c, 0x0D3E, 0x0D40);
        }

        BreakClass breakClass(char32_t c)
        {
            if (c == 0x000D) { return BreakClass::CarriageReturn; }
            if (c == 0x000A) { return BreakClass::LineFeed; }
            if (c == 0x200D) { return BreakClass::ZeroWidthJoiner; }
            if (c == 0x200C) { return BreakClass::Extend; }  // ZWNJ binds like a mark
            if (inRange(c, 0x1F1E6, 0x1F1FF)) { return BreakClass::RegionalIndicator; }

            // Hangul. The syllable block is LV where the trailing-jamo index is zero and LVT
            // otherwise, which is arithmetic rather than a table.
            if (inRange(c, 0x1100, 0x115F)) { return BreakClass::HangulL; }
            if (inRange(c, 0x1160, 0x11A7)) { return BreakClass::HangulV; }
            if (inRange(c, 0x11A8, 0x11FF)) { return BreakClass::HangulT; }
            if (inRange(c, 0xAC00, 0xD7A3))
            {
                return ((c - 0xAC00) % 28 == 0) ? BreakClass::HangulLV : BreakClass::HangulLVT;
            }

            if (isExtend(c)) { return BreakClass::Extend; }
            if (isSpacingMark(c)) { return BreakClass::SpacingMark; }
            if (c == 0x0600 || c == 0x0601 || c == 0x0602 || c == 0x0603 || c == 0x06DD
                || c == 0x070F || c == 0x110BD)
            {
                return BreakClass::Prepend;
            }

            // C0/C1 controls other than CR and LF, which break on both sides.
            if (c < 0x0020 || inRange(c, 0x007F, 0x009F)) { return BreakClass::Control; }
            return BreakClass::Other;
        }

        /**
         * @brief Whether a cluster boundary lies between @p left and @p right.
         *
         * `regionalRun` is how many regional indicators immediately precede @p right, so that
         * flags pair up rather than every indicator joining the one before it: `GB12`/`GB13` break
         * between an even-numbered run and the next indicator, which is what makes two flags in a
         * row two clusters rather than one long one.
         */
        bool breaksBetween(BreakClass left, BreakClass right, std::size_t regionalRun)
        {
            using B = BreakClass;

            if (left == B::CarriageReturn && right == B::LineFeed) { return false; }  // GB3
            if (left == B::Control || left == B::CarriageReturn || left == B::LineFeed)
            {
                return true;  // GB4
            }
            if (right == B::Control || right == B::CarriageReturn || right == B::LineFeed)
            {
                return true;  // GB5
            }

            // GB6-GB8: Hangul syllables compose L+V+T.
            if (left == B::HangulL
                && (right == B::HangulL || right == B::HangulV || right == B::HangulLV
                    || right == B::HangulLVT))
            {
                return false;
            }
            if ((left == B::HangulLV || left == B::HangulV)
                && (right == B::HangulV || right == B::HangulT))
            {
                return false;
            }
            if ((left == B::HangulLVT || left == B::HangulT) && right == B::HangulT)
            {
                return false;
            }

            // GB9: nothing breaks before a mark or a joiner. GB9a: nor before a spacing mark.
            // GB9b: nor after a prepend.
            if (right == B::Extend || right == B::ZeroWidthJoiner) { return false; }
            if (right == B::SpacingMark) { return false; }
            if (left == B::Prepend) { return false; }

            // GB11, in the form that needs no emoji table: a joiner binds to whatever follows it.
            // Full UAX #29 requires the right side to be extended-pictographic, so this also keeps
            // "a ZWJ b" together for letters -- which is a string nobody types and which renders
            // as one run anyway.
            if (left == B::ZeroWidthJoiner) { return false; }

            // GB12/GB13: regional indicators pair.
            if (left == B::RegionalIndicator && right == B::RegionalIndicator)
            {
                return regionalRun % 2 == 0;
            }

            return true;  // GB999
        }

        /** @brief Decodes the code point starting at @p offset, which must be a boundary. */
        char32_t codePointAt(std::string_view utf8, std::size_t offset)
        {
            if (offset >= utf8.size()) { return 0; }

            const auto byte = [&](std::size_t at) {
                return at < utf8.size() ? static_cast<unsigned char>(utf8[at]) : 0U;
            };

            const unsigned char first = byte(offset);
            if (first < 0x80U) { return first; }
            if ((first & 0xE0U) == 0xC0U)
            {
                return static_cast<char32_t>(((first & 0x1FU) << 6) | (byte(offset + 1) & 0x3FU));
            }
            if ((first & 0xF0U) == 0xE0U)
            {
                return static_cast<char32_t>(((first & 0x0FU) << 12)
                                             | ((byte(offset + 1) & 0x3FU) << 6)
                                             | (byte(offset + 2) & 0x3FU));
            }
            if ((first & 0xF8U) == 0xF0U)
            {
                return static_cast<char32_t>(((first & 0x07U) << 18)
                                             | ((byte(offset + 1) & 0x3FU) << 12)
                                             | ((byte(offset + 2) & 0x3FU) << 6)
                                             | (byte(offset + 3) & 0x3FU));
            }
            // A stray continuation or an invalid lead. U+FFFD keeps it a single `Other` cluster
            // rather than letting malformed input join its neighbours.
            return 0xFFFD;
        }

        /** @brief How many regional indicators end at @p offset, walking back from it. */
        std::size_t regionalRunBefore(std::string_view utf8, std::size_t offset)
        {
            std::size_t run = 0;
            std::size_t at = offset;
            while (at > 0)
            {
                const std::size_t previous = studioUtf8Previous(utf8, at);
                if (breakClass(codePointAt(utf8, previous)) != BreakClass::RegionalIndicator)
                {
                    break;
                }
                ++run;
                at = previous;
            }
            return run;
        }
    }

    std::size_t studioGraphemeNext(std::string_view utf8, std::size_t offset)
    {
        offset = studioUtf8BoundaryAt(utf8, offset);
        if (offset >= utf8.size()) { return utf8.size(); }

        std::size_t at = studioUtf8Next(utf8, offset);
        while (at < utf8.size())
        {
            const std::size_t previous = studioUtf8Previous(utf8, at);
            const BreakClass left = breakClass(codePointAt(utf8, previous));
            const BreakClass right = breakClass(codePointAt(utf8, at));
            if (breaksBetween(left, right, regionalRunBefore(utf8, at))) { break; }
            at = studioUtf8Next(utf8, at);
        }
        return at;
    }

    std::size_t studioGraphemePrevious(std::string_view utf8, std::size_t offset)
    {
        offset = studioUtf8BoundaryAt(utf8, std::min(offset, utf8.size()));
        if (offset == 0) { return 0; }

        // Walked forwards from the start of the text rather than backwards from `offset`, because
        // the regional-indicator rule counts a run from its beginning: deciding a boundary by
        // looking only leftwards would pair the flags from the wrong end and put the caret in the
        // middle of one.
        std::size_t begin = 0;
        while (true)
        {
            const std::size_t next = studioGraphemeNext(utf8, begin);
            if (next >= offset || next == begin) { return begin; }
            begin = next;
        }
    }

    StudioTextEdit::StudioTextEdit(std::string text) : text_(std::move(text))
    {
        caret_ = text_.size();
        anchor_ = caret_;
    }

    void StudioTextEdit::setText(std::string text)
    {
        text_ = std::move(text);
        // Clamped, not reset. A field refreshed underneath the user -- by an undo, or by something
        // else editing the same property -- should not also cost them their place.
        caret_ = studioUtf8BoundaryAt(text_, std::min(caret_, text_.size()));
        anchor_ = studioUtf8BoundaryAt(text_, std::min(anchor_, text_.size()));
    }

    std::string StudioTextEdit::selectedText() const
    {
        if (!hasSelection()) { return {}; }
        return text_.substr(selectionBegin(), selectionEnd() - selectionBegin());
    }

    void StudioTextEdit::moveTo(std::size_t offset, bool extend)
    {
        caret_ = studioUtf8BoundaryAt(text_, offset);
        if (!extend) { anchor_ = caret_; }
    }

    void StudioTextEdit::moveLeft(bool extend)
    {
        // With a selection and no Shift, Left goes to its *left edge* rather than one place left of
        // the caret. Every text field does this, and a field that did not would make deselecting
        // also move the caret somewhere the user did not ask for.
        if (!extend && hasSelection())
        {
            caret_ = selectionBegin();
            anchor_ = caret_;
            return;
        }

        caret_ = studioGraphemePrevious(text_, caret_);
        if (!extend) { anchor_ = caret_; }
    }

    void StudioTextEdit::moveRight(bool extend)
    {
        if (!extend && hasSelection())
        {
            caret_ = selectionEnd();
            anchor_ = caret_;
            return;
        }

        caret_ = studioGraphemeNext(text_, caret_);
        if (!extend) { anchor_ = caret_; }
    }

    void StudioTextEdit::moveHome(bool extend)
    {
        caret_ = 0;
        if (!extend) { anchor_ = 0; }
    }

    void StudioTextEdit::moveEnd(bool extend)
    {
        caret_ = text_.size();
        if (!extend) { anchor_ = caret_; }
    }

    void StudioTextEdit::selectAll()
    {
        anchor_ = 0;
        caret_ = text_.size();
    }

    bool StudioTextEdit::deleteSelection()
    {
        if (!hasSelection()) { return false; }

        const std::size_t begin = selectionBegin();
        text_.erase(begin, selectionEnd() - begin);
        caret_ = begin;
        anchor_ = begin;
        return true;
    }

    bool StudioTextEdit::insert(std::string_view utf8)
    {
        const bool removed = deleteSelection();
        if (utf8.empty()) { return removed; }

        text_.insert(caret_, utf8);
        caret_ += utf8.size();
        anchor_ = caret_;
        return true;
    }

    bool StudioTextEdit::deleteBackward()
    {
        if (deleteSelection()) { return true; }
        if (caret_ == 0) { return false; }

        // A whole cluster, not a byte and not a code point. A byte leaves a string that is no
        // longer UTF-8, which every later measurement and draw has to cope with; a code point
        // takes the accent off a letter and leaves the letter, which looks like Backspace having
        // missed.
        const std::size_t previous = studioGraphemePrevious(text_, caret_);
        text_.erase(previous, caret_ - previous);
        caret_ = previous;
        anchor_ = previous;
        return true;
    }

    bool StudioTextEdit::deleteForward()
    {
        if (deleteSelection()) { return true; }
        if (caret_ >= text_.size()) { return false; }

        const std::size_t next = studioGraphemeNext(text_, caret_);
        text_.erase(caret_, next - caret_);
        anchor_ = caret_;
        return true;
    }
}
