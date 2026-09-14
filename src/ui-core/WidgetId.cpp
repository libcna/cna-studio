// SPDX-License-Identifier: MS-PL
/**
 * @file WidgetId.cpp
 * @brief Widget identity derivation and per-frame collision detection.
 */

#include "CNA/Studio/UiCore/WidgetId.hpp"

#include <algorithm>

namespace CNA::Studio
{
    namespace
    {
        // FNV-1a, 64-bit. Chosen for being specified and therefore stable across standard library
        // implementations and builds -- see the header for why that matters here.
        constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
        constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

        /**
         * @brief Zero is the invalid id, so a real hash must never be zero.
         *
         * The probability of landing on it is about 1 in 2^64, which is small enough to ignore and
         * cheap enough not to. Folding to 1 loses nothing: the collision that creates is with a
         * single other value out of 2^64.
         */
        constexpr std::uint64_t avoidZero(std::uint64_t value)
        {
            return value == 0 ? 1ULL : value;
        }
    } // namespace

    std::uint64_t hashWidgetKey(std::uint64_t seed, std::string_view key)
    {
        std::uint64_t hash = seed == 0 ? kFnvOffsetBasis : seed;
        for (const char c : key)
        {
            hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
            hash *= kFnvPrime;
        }
        return avoidZero(hash);
    }

    std::uint64_t hashWidgetIndex(std::uint64_t seed, std::int64_t key)
    {
        std::uint64_t hash = seed == 0 ? kFnvOffsetBasis : seed;
        auto bits = static_cast<std::uint64_t>(key);
        for (int byte = 0; byte < 8; ++byte)
        {
            hash ^= (bits & 0xFFULL);
            hash *= kFnvPrime;
            bits >>= 8;
        }
        return avoidZero(hash);
    }

    WidgetIdStack::WidgetIdStack()
    {
        scopes_.push_back(kFnvOffsetBasis);
    }

    void WidgetIdStack::beginFrame()
    {
        // Reset rather than assert on an unbalanced stack from last frame. A panel that threw
        // between push and pop has already failed; making every subsequent frame's ids wrong as
        // well would turn one visible bug into an inexplicable one.
        scopes_.clear();
        scopes_.push_back(kFnvOffsetBasis);
        issued_.clear();
        collisions_.clear();
    }

    void WidgetIdStack::push(std::string_view key)
    {
        scopes_.push_back(hashWidgetKey(currentScope(), key));
    }

    void WidgetIdStack::pushIndex(std::int64_t key)
    {
        scopes_.push_back(hashWidgetIndex(currentScope(), key));
    }

    void WidgetIdStack::pushId(WidgetId id)
    {
        scopes_.push_back(hashWidgetIndex(currentScope(), static_cast<std::int64_t>(id.value())));
    }

    void WidgetIdStack::pop()
    {
        // The root scope is not poppable: an over-pop is a caller bug, but leaving the stack empty
        // would make currentScope() undefined for everything that follows.
        if (scopes_.size() > 1) { scopes_.pop_back(); }
    }

    std::string_view WidgetIdStack::visibleLabel(std::string_view label)
    {
        const std::size_t marker = label.find("##");
        return marker == std::string_view::npos ? label : label.substr(0, marker);
    }

    WidgetId WidgetIdStack::make(std::string_view key)
    {
        // The whole key hashes, including anything after "##" -- that suffix exists precisely to
        // separate two widgets whose visible text is identical.
        const WidgetId id{hashWidgetKey(currentScope(), key)};
        recordIssued(id);
        return id;
    }

    WidgetId WidgetIdStack::makeIndex(std::int64_t key)
    {
        const WidgetId id{hashWidgetIndex(currentScope(), key)};
        recordIssued(id);
        return id;
    }

    void WidgetIdStack::recordIssued(WidgetId id)
    {
        if (!trackCollisions_) { return; }

        // A sorted vector rather than a hash set: a frame issues a few thousand ids at most, they
        // are inserted once and never erased, and this keeps the allocation count at one growth
        // curve rather than a node per widget.
        const auto position = std::lower_bound(issued_.begin(), issued_.end(), id.value());
        if (position != issued_.end() && *position == id.value())
        {
            if (std::find(collisions_.begin(), collisions_.end(), id) == collisions_.end())
            {
                collisions_.push_back(id);
            }
            return;
        }
        issued_.insert(position, id.value());
    }
} // namespace CNA::Studio
