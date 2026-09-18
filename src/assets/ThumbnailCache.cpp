// SPDX-License-Identifier: MS-PL
/**
 * @file ThumbnailCache.cpp
 * @brief Thumbnails generated off the frame (`plan.md` STUDIO-09003).
 */

#include "CNA/Studio/Assets/ThumbnailCache.hpp"

#include <algorithm>
#include <memory>
#include <utility>

#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/Assets/ImageDecode.hpp"

namespace CNA::Studio
{
    StudioThumbnail studioDownscaleRgba(const std::vector<unsigned char>& source,
                                        std::uint32_t width, std::uint32_t height,
                                        std::uint32_t maximumEdge)
    {
        StudioThumbnail out;

        constexpr std::size_t kChannels = 4;
        const std::size_t expected = static_cast<std::size_t>(width)
                                   * static_cast<std::size_t>(height) * kChannels;
        if (width == 0 || height == 0 || maximumEdge == 0 || source.size() != expected)
        {
            return out;
        }

        // Already small enough: returned as it is rather than resampled, so a 16-pixel icon is not
        // softened for nothing.
        if (width <= maximumEdge && height <= maximumEdge)
        {
            out.width = width;
            out.height = height;
            out.pixels = source;
            return out;
        }

        const double scale = static_cast<double>(maximumEdge)
                           / static_cast<double>(std::max(width, height));
        out.width = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(
                                                   static_cast<double>(width) * scale));
        out.height = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(
                                                    static_cast<double>(height) * scale));
        out.pixels.assign(static_cast<std::size_t>(out.width)
                              * static_cast<std::size_t>(out.height) * kChannels,
                          0u);

        // A box filter: every source pixel that lands in a destination pixel, averaged. Nearest
        // neighbour is shorter and makes a downscaled texture look like a different texture --
        // thin detail either vanishes or turns into a moire, and a thumbnail that misrepresents
        // its asset is worse than none, because the user believes it.
        for (std::uint32_t y = 0; y < out.height; ++y)
        {
            const std::uint32_t sourceTop =
                static_cast<std::uint32_t>(static_cast<std::uint64_t>(y) * height / out.height);
            const std::uint32_t sourceBottom = std::max(
                sourceTop + 1,
                static_cast<std::uint32_t>(static_cast<std::uint64_t>(y + 1) * height / out.height));

            for (std::uint32_t x = 0; x < out.width; ++x)
            {
                const std::uint32_t sourceLeft =
                    static_cast<std::uint32_t>(static_cast<std::uint64_t>(x) * width / out.width);
                const std::uint32_t sourceRight = std::max(
                    sourceLeft + 1,
                    static_cast<std::uint32_t>(static_cast<std::uint64_t>(x + 1) * width
                                               / out.width));

                std::uint64_t totals[kChannels] = {0, 0, 0, 0};
                std::uint64_t counted = 0;

                for (std::uint32_t sy = sourceTop; sy < sourceBottom && sy < height; ++sy)
                {
                    for (std::uint32_t sx = sourceLeft; sx < sourceRight && sx < width; ++sx)
                    {
                        const std::size_t at = (static_cast<std::size_t>(sy)
                                                    * static_cast<std::size_t>(width)
                                                + static_cast<std::size_t>(sx))
                                             * kChannels;
                        for (std::size_t channel = 0; channel < kChannels; ++channel)
                        {
                            totals[channel] += source[at + channel];
                        }
                        ++counted;
                    }
                }

                if (counted == 0) { continue; }

                const std::size_t destination = (static_cast<std::size_t>(y)
                                                     * static_cast<std::size_t>(out.width)
                                                 + static_cast<std::size_t>(x))
                                              * kChannels;
                for (std::size_t channel = 0; channel < kChannels; ++channel)
                {
                    out.pixels[destination + channel] =
                        static_cast<unsigned char>(totals[channel] / counted);
                }
            }
        }

        return out;
    }

    bool StudioThumbnailCache::matches(const Entry& entry, std::uint64_t size,
                                       std::int64_t modifiedTime, const std::string& path)
    {
        // The stamp the last scan or watcher poll saw, compared without asking the filesystem
        // anything -- which is what makes a frame where nothing changed free. The path too: an
        // asset that moved is the same picture, but an entry keyed only on the stamp would survive
        // a move to a file that happens to be the same size.
        return entry.size == size && entry.modifiedTime == modifiedTime
            && entry.sourcePath == path;
    }

    const StudioThumbnail* StudioThumbnailCache::find(const Uuid& id) const
    {
        const auto found = entries_.find(id);
        if (found == entries_.end() || !found->second.decoded) { return nullptr; }
        return &found->second.thumbnail;
    }

    void StudioThumbnailCache::setWanted(std::vector<Uuid> ids)
    {
        ++clock_;
        wanted_ = std::move(ids);

        // Touched here rather than in `find`, so that the draw path stays const and an entry's
        // place in the eviction order is decided by what the browser is showing rather than by how
        // many times a frame happened to ask.
        for (const Uuid& id : wanted_)
        {
            const auto found = entries_.find(id);
            if (found != entries_.end()) { found->second.lastWanted = clock_; }
        }
    }

    std::size_t StudioThumbnailCache::pump(StudioJobSystem& jobs, const AssetDatabase& assets,
                                           std::size_t budget)
    {
        // Cancel first. Scrolling past an asset before its thumbnail is made is the common case,
        // not the exceptional one -- a user flicking through two thousand textures wants the forty
        // they stop on -- so this is the feature rather than tidiness. Cancelling before submitting
        // also frees queue slots for what is actually on screen.
        for (auto entry = pending_.begin(); entry != pending_.end();)
        {
            const bool stillWanted =
                std::find(wanted_.begin(), wanted_.end(), entry->first) != wanted_.end();
            if (stillWanted)
            {
                ++entry;
                continue;
            }

            // The completion still arrives, with the job cancelled, and files nothing.
            if (jobs.cancel(entry->second.id)) { ++cancelled_; }
            entry = pending_.erase(entry);
        }

        std::size_t submitted = 0;
        const std::size_t limit = budget > 0 ? budget : wanted_.size();

        for (const Uuid& id : wanted_)
        {
            if (submitted >= limit) { break; }
            if (pending_.count(id) != 0) { continue; }

            const AssetRecord* record = assets.find(id);
            if (record == nullptr || record->sourcePath.empty()) { continue; }
            if (!studioCanDecodeImageExtension(record->sourcePath)) { continue; }

            const auto existing = entries_.find(id);
            if (existing != entries_.end()
                && matches(existing->second, record->sourceSize, record->sourceModifiedTime,
                           record->sourcePath))
            {
                // Including a cached *failure*: a file that is not really a PNG would otherwise be
                // decoded again on every pump, for ever.
                continue;
            }

            // Asked before building anything: a refusal is "not now", and the next pump offers the
            // same work again (STUDIO-30002). Checking first keeps a full queue from costing a
            // string copy per asset per frame.
            if (!jobs.canAccept()) { break; }

            const std::string absolute = assets.resolvePath(record->sourcePath);
            const std::uint64_t size = record->sourceSize;
            const std::int64_t modifiedTime = record->sourceModifiedTime;
            const std::string relative = record->sourcePath;

            // The result travels through a shared_ptr rather than being captured by reference: the
            // body runs on a worker and the completion on the main thread, and nothing either of
            // them touches may be owned by a frame that has moved on.
            auto produced = std::make_shared<StudioThumbnail>();
            auto ok = std::make_shared<bool>(false);

            const StudioJobId job = jobs.submit(
                "Thumbnail " + relative,
                [absolute, produced, ok](StudioJobContext& context) {
                    if (context.isCancelled()) { return; }

                    const StudioImageDecodeResult decoded = studioDecodeImageFile(absolute);
                    if (!decoded.succeeded())
                    {
                        context.fail(decoded.error);
                        return;
                    }

                    // Checked again after the expensive half: a job cancelled while it was
                    // decoding should not go on to spend the downscale as well.
                    if (context.isCancelled()) { return; }

                    *produced = studioDownscaleRgba(decoded.image.pixels, decoded.image.width,
                                                    decoded.image.height, kThumbnailEdge);
                    *ok = !produced->isEmpty();
                },
                [this, id, size, modifiedTime, relative, produced,
                 ok](const StudioJobStatus& status) {
                    // Main thread, from drain(). The entry is filed here and nowhere else, which is
                    // what keeps the handoff a single crossing.
                    pending_.erase(id);

                    if (status.state == StudioJobState::Cancelled) { return; }

                    Entry& entry = entries_[id];
                    entry.size = size;
                    entry.modifiedTime = modifiedTime;
                    entry.sourcePath = relative;
                    entry.lastWanted = clock_;
                    entry.decoded = *ok;
                    entry.thumbnail = *ok ? std::move(*produced) : StudioThumbnail{};

                    if (*ok) { ++generated_; }
                    else { ++failed_; }

                    evict();
                });

            if (job == 0) { break; }

            pending_[id] = Pending{job, size, modifiedTime, relative};
            ++submitted;
        }

        return submitted;
    }

    void StudioThumbnailCache::invalidate(const Uuid& id)
    {
        if (!id.isValid())
        {
            entries_.clear();
            return;
        }
        entries_.erase(id);
    }

    std::size_t StudioThumbnailCache::getCount() const { return entries_.size(); }

    void StudioThumbnailCache::evict()
    {
        if (entries_.size() <= kMaximumEntries) { return; }

        // Least recently wanted first. Not least recently *drawn*: an asset the browser is showing
        // is wanted every poll, and one it scrolled past an hour ago is not, which is the
        // distinction that makes scrolling back up find its thumbnails still there.
        std::vector<std::pair<std::uint64_t, Uuid>> order;
        order.reserve(entries_.size());
        for (const auto& [id, entry] : entries_) { order.emplace_back(entry.lastWanted, id); }

        std::sort(order.begin(), order.end(),
                  [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

        const std::size_t excess = entries_.size() - kMaximumEntries;
        for (std::size_t i = 0; i < excess; ++i)
        {
            entries_.erase(order[i].second);
            ++evicted_;
        }
    }
}
