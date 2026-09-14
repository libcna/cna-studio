// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Assets/AssetWatcher.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <system_error>

namespace CNA::Editor
{
    namespace
    {
        /** @brief One file's size and modification time, or absent when it is not there. */
        struct FileStamp
        {
            bool exists = false;
            std::uint64_t size = 0;
            std::int64_t modifiedTime = 0;
        };

        FileStamp stampOf(const std::string& absolutePath)
        {
            std::error_code errorCode;
            const std::filesystem::path path{absolutePath};

            FileStamp stamp;
            stamp.exists = std::filesystem::exists(path, errorCode) && !errorCode;
            if (!stamp.exists) { return stamp; }

            stamp.size = static_cast<std::uint64_t>(std::filesystem::file_size(path, errorCode));
            if (errorCode) { stamp.size = 0; errorCode.clear(); }

            // Seconds, matching what a scan records. The clock's native ticks are around 4.6e18,
            // past the range a double holds exactly, and the sidecar's numbers are doubles -- so
            // storing ticks would make every asset look modified on every comparison.
            const auto writeTime = std::filesystem::last_write_time(path, errorCode);
            stamp.modifiedTime =
                errorCode
                    ? 0
                    : std::chrono::duration_cast<std::chrono::seconds>(writeTime.time_since_epoch()).count();

            return stamp;
        }
    }

    void AssetWatcher::setInterval(double seconds)
    {
        interval_ = std::max(0.0, seconds);
    }

    AssetWatchResult AssetWatcher::poll(AssetDatabase& assets, double deltaSeconds)
    {
        AssetWatchResult result;

        elapsed_ += std::max(0.0, deltaSeconds);
        if (elapsed_ < interval_) { return result; }
        elapsed_ = 0.0;
        result.polled = true;

        for (const AssetRecord* record : assets.getAll())
        {
            const FileStamp stamp = stampOf(assets.resolvePath(record->sourcePath));

            // A record that has never been stamped -- size and time both zero -- is one whose file
            // was already absent when it was scanned. Comparing against that would report it as
            // changed on the first poll of every session.
            const bool wasKnownPresent = record->sourceSize != 0 || record->sourceModifiedTime != 0;

            if (!stamp.exists)
            {
                if (wasKnownPresent)
                {
                    result.removed.push_back(record->id);

                    // Zeroed so the disappearance is reported once. Leaving the old stamp would
                    // make every subsequent poll report it again, and the console would fill up
                    // with the same line twice a second.
                    if (AssetRecord* mutableRecord = assets.findMutable(record->id))
                    {
                        mutableRecord->sourceSize = 0;
                        mutableRecord->sourceModifiedTime = 0;
                    }
                }
                continue;
            }

            if (stamp.size == record->sourceSize && stamp.modifiedTime == record->sourceModifiedTime)
            {
                continue;
            }

            AssetRecord* mutableRecord = assets.findMutable(record->id);
            if (mutableRecord == nullptr) { continue; }

            // A file coming back is worth telling apart from one being edited: the first fixes a
            // broken reference, the second means reloading something already on screen.
            (wasKnownPresent ? result.changed : result.restored).push_back(record->id);

            mutableRecord->sourceSize = stamp.size;
            mutableRecord->sourceModifiedTime = stamp.modifiedTime;
        }

        return result;
    }
}
