// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/UiCore/WidgetId.hpp
 * @brief Stable widget identity, derived from a scoped id stack.
 *
 * `plan.md` STUDIO-03002.
 *
 * An immediate-mode UI describes its widgets afresh every frame, so a widget's *identity* cannot
 * come from a pointer or an index -- both move. It comes from where the widget sits in the
 * description: a hash of its own label combined with the hash of the scope it was declared in.
 *
 * Getting this wrong is subtle and expensive. Two symptoms, both of which this design prevents:
 *
 * 1. **Identity derived from position.** If the third row's id is "row 3", inserting a row above it
 *    silently hands its scroll position, its expansion state and its half-typed text to a
 *    different object. Ids therefore come from stable keys -- a name, or an entity UUID -- and
 *    never from an iteration counter unless the caller explicitly asks for that.
 * 2. **Two widgets colliding on one id.** Two buttons both labelled "Delete" in the same scope
 *    become one widget: pressing either lights both, and only one of them works. That is a bug
 *    which reads as a rendering glitch, so this class detects it in debug builds and names both
 *    offenders rather than leaving it to be discovered visually.
 *
 * ### Why FNV-1a rather than std::hash
 *
 * `std::hash<std::string_view>` is permitted to differ between standard library implementations and
 * between runs of the same binary. Widget ids are written into saved workspace layouts and used as
 * keys for state that must survive a restart, so the hash has to be *stable across builds*. FNV-1a
 * is specified, tiny and adequate: these are short keys in a space of a few thousand live widgets,
 * not a cryptographic application.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace CNA::Studio
{
    /**
     * @brief A widget's identity: a stable 64-bit hash of its scope path and key.
     *
     * A distinct type rather than a bare `std::uint64_t`, so that an id cannot be silently
     * confused with a count, an index or a texture handle.
     */
    class WidgetId
    {
    public:
        /** @brief Constructs the invalid id, which no widget ever has. */
        constexpr WidgetId() = default;

        /**
         * @brief Constructs an id from a raw hash value.
         * @param value The hash. Zero means invalid.
         */
        explicit constexpr WidgetId(std::uint64_t value) : value_(value) {}

        /** @brief The raw hash value. */
        [[nodiscard]] constexpr std::uint64_t value() const { return value_; }

        /** @brief Reports whether this id refers to a widget at all. */
        [[nodiscard]] constexpr bool isValid() const { return value_ != 0; }

        friend constexpr bool operator==(WidgetId lhs, WidgetId rhs) { return lhs.value_ == rhs.value_; }
        friend constexpr bool operator!=(WidgetId lhs, WidgetId rhs) { return lhs.value_ != rhs.value_; }
        friend constexpr bool operator<(WidgetId lhs, WidgetId rhs) { return lhs.value_ < rhs.value_; }

    private:
        std::uint64_t value_ = 0;
    };

    /** @brief The id no widget has. Used as "nothing is focused", "nothing is hovered". */
    inline constexpr WidgetId kInvalidWidgetId{};

    /**
     * @brief Combines a seed with a key, FNV-1a style.
     *
     * Exposed so that callers can build a composite key from parts without allocating a string to
     * concatenate them.
     *
     * @param seed Value to mix into, typically the enclosing scope's hash.
     * @param key Text to mix.
     * @return The combined hash, never zero.
     */
    [[nodiscard]] std::uint64_t hashWidgetKey(std::uint64_t seed, std::string_view key);

    /**
     * @brief Combines a seed with an integer key.
     * @param seed Value to mix into.
     * @param key Integer to mix, e.g. a list index the caller has decided is genuinely stable.
     * @return The combined hash, never zero.
     */
    [[nodiscard]] std::uint64_t hashWidgetIndex(std::uint64_t seed, std::int64_t key);

    /**
     * @brief The scope stack that widget ids are derived from.
     *
     * Usage mirrors the shape of the UI: push a scope when entering a panel, a row or a repeated
     * group; pop on the way out. Because an id is built from the whole enclosing path, the same
     * label in two panels produces two different widgets -- which is what a caller expects and
     * what makes labels reusable.
     *
     * The collision check is deliberately *per frame*: a widget declared once per frame is normal,
     * so the set of ids seen is cleared by beginFrame(). Declaring the same id twice within one
     * frame is the bug.
     */
    class WidgetIdStack
    {
    public:
        WidgetIdStack();

        /**
         * @brief Starts a frame: clears the collision set and resets to the root scope.
         *
         * An unbalanced push/pop from the previous frame is dropped here rather than accumulating,
         * so one buggy panel cannot corrupt every later frame's ids.
         */
        void beginFrame();

        /**
         * @brief Enters a named scope.
         * @param key Scope name. Stable across frames.
         */
        void push(std::string_view key);

        /**
         * @brief Enters a scope identified by an integer.
         * @param key Scope index. The caller is asserting this is stable.
         */
        void pushIndex(std::int64_t key);

        /**
         * @brief Enters a scope identified by an existing id, e.g. an entity's own id.
         * @param id Identity to scope by.
         */
        void pushId(WidgetId id);

        /** @brief Leaves the current scope. Popping the root scope is ignored. */
        void pop();

        /** @brief The current scope's hash. */
        [[nodiscard]] std::uint64_t currentScope() const { return scopes_.back(); }

        /** @brief The current nesting depth; zero at the root. */
        [[nodiscard]] std::size_t depth() const { return scopes_.size() - 1; }

        /**
         * @brief Builds an id for a widget in the current scope.
         *
         * A label may carry a `"##"` suffix: everything after it contributes to the id but is not
         * displayed. That is how two buttons that must both read "Delete" get distinct identities
         * without inventing distinct visible text.
         *
         * @param key The widget's key, optionally with a `"##"` suffix.
         * @return The widget's id.
         */
        [[nodiscard]] WidgetId make(std::string_view key);

        /**
         * @brief Builds an id for an indexed widget in the current scope.
         * @param key Index. Only stable if the caller's collection is stable.
         * @return The widget's id.
         */
        [[nodiscard]] WidgetId makeIndex(std::int64_t key);

        /**
         * @brief Strips the non-displayed `"##"` suffix from a label.
         * @param label Label possibly containing `"##"`.
         * @return The portion that should be drawn.
         */
        [[nodiscard]] static std::string_view visibleLabel(std::string_view label);

        /**
         * @brief Number of ids issued twice in the current frame.
         *
         * Non-zero means two widgets share an identity, which presents as one widget responding
         * for both. Asserted on in debug builds and in the test suite.
         */
        [[nodiscard]] std::size_t collisionCount() const { return collisions_.size(); }

        /** @brief The ids issued more than once this frame, for diagnostics. */
        [[nodiscard]] const std::vector<WidgetId>& collisions() const { return collisions_; }

        /**
         * @brief Whether to track collisions at all.
         *
         * Tracking costs a hash-set insert per widget. It is on by default because the bug it
         * catches is otherwise diagnosed by staring at a window, and can be turned off for a
         * profiling run.
         *
         * @param enabled True to track.
         */
        void setCollisionTrackingEnabled(bool enabled) { trackCollisions_ = enabled; }

    private:
        void recordIssued(WidgetId id);

        std::vector<std::uint64_t> scopes_;
        std::vector<std::uint64_t> issued_;
        std::vector<WidgetId> collisions_;
        bool trackCollisions_ = true;
    };
} // namespace CNA::Studio
