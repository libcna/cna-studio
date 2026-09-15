// SPDX-License-Identifier: MS-PL
/**
 * @file CNA/Studio/UiCore/StudioTextEdit.hpp
 * @brief A caret and a selection over a UTF-8 string.
 *
 * `plan.md` STUDIO-03024.
 *
 * Separated from the widget that draws it, and tested on its own, because nearly everything that
 * goes wrong with a text field goes wrong here rather than in the drawing: a caret that lands in
 * the middle of a multi-byte character, Backspace that eats one byte of a three-byte character and
 * leaves a broken string, Shift+Left that collapses a selection instead of extending it, Home
 * clearing a selection when it should have kept the anchor. None of those need a frame, a font or a
 * window to reproduce, and none of them are visible in a screenshot until the damage is done.
 *
 * ### Offsets are bytes, always on a code-point boundary
 *
 * Bytes rather than code-point indices because every other part of the text path -- measurement,
 * truncation, the draw list -- speaks in `std::string_view`, and converting at each boundary would
 * be a conversion to get wrong. Every operation here moves the caret a whole code point at a time,
 * so an offset this class produced is always a valid place to split the string.
 *
 * ### Grapheme clusters are not code points, and this is not that
 *
 * A flag emoji is one thing a reader sees and several code points; so is `e` followed by a combining
 * acute. Moving by code point therefore steps *inside* some characters. Fixing that needs a grapheme
 * breaker, which is `STUDIO-03026`'s problem and a table this repository does not have yet. Code
 * points are a real improvement on bytes, and stopping here is a deliberate half-step rather than
 * an oversight.
 */

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace CNA::Studio
{
    /**
     * @brief The byte offset of the code point starting at or before @p offset.
     *
     * @param utf8 The text.
     * @param offset A byte offset into it.
     * @return The offset of the code-point boundary at or before @p offset.
     */
    [[nodiscard]] std::size_t studioUtf8BoundaryAt(std::string_view utf8, std::size_t offset);

    /**
     * @brief The byte offset one code point before @p offset.
     * @param utf8 The text.
     * @param offset A byte offset into it.
     * @return The previous boundary, or 0.
     */
    [[nodiscard]] std::size_t studioUtf8Previous(std::string_view utf8, std::size_t offset);

    /**
     * @brief The byte offset one code point after @p offset.
     * @param utf8 The text.
     * @param offset A byte offset into it.
     * @return The next boundary, or the length of @p utf8.
     */
    [[nodiscard]] std::size_t studioUtf8Next(std::string_view utf8, std::size_t offset);

    /**
     * @brief A caret and selection over an editable string.
     *
     * The selection runs between an *anchor* -- where the gesture started -- and the caret. Which
     * of the two is smaller is not fixed: dragging left from the middle of a word selects leftwards
     * and Shift+Right then shrinks that selection from its left edge, which is what every text
     * field does and what a model storing only a begin and an end cannot express.
     */
    class StudioTextEdit
    {
    public:
        StudioTextEdit() = default;

        /** @brief Constructs an editor over @p text with the caret at the end. */
        explicit StudioTextEdit(std::string text);

        /** @brief The text. */
        [[nodiscard]] const std::string& text() const { return text_; }

        /**
         * @brief Replaces the text, clamping the caret and anchor onto it.
         *
         * Clamped rather than reset: a field whose value is refreshed underneath the user -- by an
         * undo, or by something else editing the same property -- should not also lose their place.
         *
         * @param text The new text.
         */
        void setText(std::string text);

        /** @brief Where the caret is, as a byte offset. */
        [[nodiscard]] std::size_t caret() const { return caret_; }

        /** @brief Where the current gesture began, as a byte offset. */
        [[nodiscard]] std::size_t anchor() const { return anchor_; }

        /** @brief Whether anything is selected. */
        [[nodiscard]] bool hasSelection() const { return caret_ != anchor_; }

        /** @brief The lower end of the selection. */
        [[nodiscard]] std::size_t selectionBegin() const { return caret_ < anchor_ ? caret_ : anchor_; }

        /** @brief The upper end of the selection. */
        [[nodiscard]] std::size_t selectionEnd() const { return caret_ < anchor_ ? anchor_ : caret_; }

        /** @brief The selected text, or an empty string. */
        [[nodiscard]] std::string selectedText() const;

        /**
         * @brief Puts the caret at @p offset, snapped to a code-point boundary.
         * @param offset Byte offset.
         * @param extend True to keep the anchor, extending the selection.
         */
        void moveTo(std::size_t offset, bool extend);

        /** @brief Moves one code point left, or to the left edge of a selection when not extending. */
        void moveLeft(bool extend);

        /** @brief Moves one code point right, or to the right edge of a selection. */
        void moveRight(bool extend);

        /** @brief Moves to the start. */
        void moveHome(bool extend);

        /** @brief Moves to the end. */
        void moveEnd(bool extend);

        /** @brief Selects everything, with the caret at the end. */
        void selectAll();

        /** @brief Drops the selection, leaving the caret where it is. */
        void clearSelection() { anchor_ = caret_; }

        /**
         * @brief Replaces the selection with @p utf8, or inserts it at the caret.
         * @param utf8 Text to insert.
         * @return Whether the text changed.
         */
        bool insert(std::string_view utf8);

        /**
         * @brief Deletes the selection, or the code point before the caret.
         * @return Whether the text changed.
         */
        bool deleteBackward();

        /**
         * @brief Deletes the selection, or the code point after the caret.
         * @return Whether the text changed.
         */
        bool deleteForward();

        /**
         * @brief Deletes the selection, if there is one.
         * @return Whether the text changed.
         */
        bool deleteSelection();

    private:
        std::string text_;
        std::size_t caret_ = 0;
        std::size_t anchor_ = 0;
    };
}
