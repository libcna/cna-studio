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

        caret_ = studioUtf8Previous(text_, caret_);
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

        caret_ = studioUtf8Next(text_, caret_);
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

        // A whole code point, not a byte. Removing one byte of a three-byte character leaves a
        // string that is no longer UTF-8, which every later measurement and draw then has to cope
        // with -- and the user sees a character turn into two boxes.
        const std::size_t previous = studioUtf8Previous(text_, caret_);
        text_.erase(previous, caret_ - previous);
        caret_ = previous;
        anchor_ = previous;
        return true;
    }

    bool StudioTextEdit::deleteForward()
    {
        if (deleteSelection()) { return true; }
        if (caret_ >= text_.size()) { return false; }

        const std::size_t next = studioUtf8Next(text_, caret_);
        text_.erase(caret_, next - caret_);
        anchor_ = caret_;
        return true;
    }
}
