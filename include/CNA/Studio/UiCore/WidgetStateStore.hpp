// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/UiCore/WidgetStateStore.hpp
 * @brief Per-widget state that outlives the frame that describes the widget.
 *
 * `plan.md` STUDIO-03003.
 *
 * An immediate-mode widget is described anew each frame, but some of what it owns must not be:
 * a scroll offset, whether a tree node is expanded, where a text cursor sits, how far through a
 * drag the user is. That state is keyed by WidgetId and lives here.
 *
 * ### The leak this design exists to prevent
 *
 * The naive version is a map from id to state that is only ever inserted into. It works, and it
 * grows forever: every tree node the user ever expanded, every row of every asset folder they ever
 * scrolled past, retained for the lifetime of the process. In a session spent browsing a large
 * project that is a real leak, and it is invisible because nothing ever goes wrong.
 *
 * So entries carry the frame they were last touched, and entries untouched for
 * `retentionFrames()` are reclaimed. The retention window is not one frame: a widget inside a
 * collapsed section, on a hidden tab or scrolled out of a virtualised list is not described this
 * frame and must not lose its state for it. The default is generous for that reason -- the cost of
 * over-retaining is a few bytes, and the cost of under-retaining is a user watching their tree
 * collapse itself.
 *
 * ### Why the value type is a variant rather than a template
 *
 * A `get<T>` templated store would need its type registry threaded through every call site and
 * would make the reclamation sweep type-erased anyway. The state a widget needs is a small, closed
 * set of shapes, so it is enumerated: flags, a scalar, a point, and a string for text editing.
 * A widget needing something outside that set is a sign that it should own a document-side model
 * instead, which is a conversation worth having rather than a template to instantiate.
 */

#include "CNA/Studio/UiCore/WidgetId.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace CNA::Studio
{
    /**
     * @brief The retained state of one widget.
     *
     * Every field has a neutral default, so a widget seen for the first time behaves as though it
     * had always existed with sensible values rather than needing an explicit initialisation step.
     */
    struct WidgetState
    {
        /** @brief Expanded, as for a tree node or a collapsing section. */
        bool expanded = false;
        /** @brief Checked or toggled on. */
        bool checked = false;
        /** @brief Currently being dragged or otherwise actively manipulated. */
        bool active = false;
        /** @brief Horizontal scroll offset in pixels. */
        float scrollX = 0.0f;
        /** @brief Vertical scroll offset in pixels. */
        float scrollY = 0.0f;
        /** @brief A general-purpose scalar: a slider's in-progress value, a splitter's fraction. */
        float scalar = 0.0f;
        /** @brief A general-purpose integer: a selected tab, a sort column. */
        std::int64_t integer = 0;
        /** @brief Text being edited, which must survive the frames between keystrokes. */
        std::string text;
        /** @brief Caret position within @ref text, as a byte offset. */
        std::size_t caret = 0;
        /** @brief Selection anchor within @ref text, as a byte offset. */
        std::size_t selectionAnchor = 0;
    };

    /**
     * @brief Widget state keyed by identity, with reclamation of state nobody is using.
     */
    class WidgetStateStore
    {
    public:
        /** @brief Frames a widget may go undescribed before its state is reclaimed. */
        static constexpr std::uint64_t kDefaultRetentionFrames = 600;

        WidgetStateStore() = default;

        /**
         * @brief Advances to the next frame and reclaims state nobody has touched recently.
         *
         * The sweep is not run every frame: it walks the whole map, and doing that at frame rate
         * to reclaim a handful of entries would be a cost paid constantly for a benefit collected
         * rarely. It runs every `kSweepInterval` frames instead.
         */
        void beginFrame();

        /**
         * @brief Returns a widget's state, creating it if this is the first time it is seen.
         *
         * Touching the entry marks it live for the retention window, so merely asking for a
         * widget's state is enough to keep it.
         *
         * @param id The widget's identity.
         * @return A mutable reference, valid until the next call that inserts or reclaims.
         */
        [[nodiscard]] WidgetState& get(WidgetId id);

        /**
         * @brief Marks a widget live for the retention window without reading its state.
         *
         * A widget that is described but whose retained state it does not need this frame still
         * has to say it exists, or the sweep reclaims it. Spelling that `get(id)` and discarding
         * the result reads as a mistake; this says what it means.
         *
         * @param id The widget's identity.
         */
        void touch(WidgetId id);

        /**
         * @brief Returns a widget's state without creating it, and without marking it live.
         * @param id The widget's identity.
         * @return A pointer to the state, or nullptr when the widget has none.
         */
        [[nodiscard]] const WidgetState* find(WidgetId id) const;

        /**
         * @brief Forgets one widget's state.
         * @param id The widget's identity.
         * @return True when there was state to forget.
         */
        bool forget(WidgetId id);

        /** @brief Forgets everything. */
        void clear();

        /** @brief Number of widgets with retained state. */
        [[nodiscard]] std::size_t size() const { return entries_.size(); }

        /** @brief The current frame number. */
        [[nodiscard]] std::uint64_t frame() const { return frame_; }

        /** @brief Frames a widget may go undescribed before reclamation. */
        [[nodiscard]] std::uint64_t retentionFrames() const { return retentionFrames_; }

        /**
         * @brief Sets the retention window.
         *
         * Raise it for a UI with deeply nested collapsed content; lower it only with a measured
         * reason. Zero is rejected: it would reclaim the state of every widget that is merely
         * scrolled out of view.
         *
         * @param frames Frames to retain. Values below 2 are clamped to 2.
         */
        void setRetentionFrames(std::uint64_t frames);

        /**
         * @brief Runs the reclamation sweep now, regardless of the sweep interval.
         * @return The number of entries reclaimed.
         */
        std::size_t sweep();

    private:
        /** @brief Frames between automatic sweeps. See beginFrame() for why this is not 1. */
        static constexpr std::uint64_t kSweepInterval = 120;

        struct Entry
        {
            WidgetState state;
            std::uint64_t lastTouchedFrame = 0;
        };

        std::unordered_map<std::uint64_t, Entry> entries_;
        std::uint64_t frame_ = 1;
        std::uint64_t retentionFrames_ = kDefaultRetentionFrames;
        std::uint64_t lastSweepFrame_ = 1;
    };
} // namespace CNA::Studio
