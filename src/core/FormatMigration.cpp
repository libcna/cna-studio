// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Core/FormatMigration.hpp"

#include <algorithm>

namespace CNA::Editor
{
    bool FormatMigrator::addMigration(int fromVersion, std::string description,
                                      std::function<bool(JsonValue&, std::string&)> apply)
    {
        if (!apply) { return false; }
        if (fromVersion <= 0 || fromVersion >= currentVersion_) { return false; }

        const bool duplicate = std::any_of(migrations_.begin(), migrations_.end(),
                                           [fromVersion](const FormatMigration& migration)
                                           { return migration.fromVersion == fromVersion; });
        if (duplicate) { return false; }

        migrations_.push_back(FormatMigration{fromVersion, std::move(description), std::move(apply)});
        return true;
    }

    FormatMigrationResult FormatMigrator::migrate(JsonValue& document) const
    {
        FormatMigrationResult result;

        const auto fail = [&result](std::string reason) {
            result.succeeded = false;
            result.errorMessage = std::move(reason);
            return result;
        };

        if (!document.isObject()) { return fail(formatName_ + " is not a JSON object"); }

        const int version = document["formatVersion"].asInt(0);
        result.fromVersion = version;
        result.toVersion = version;

        if (version <= 0)
        {
            return fail(formatName_ + " has no usable 'formatVersion'");
        }
        if (version > currentVersion_)
        {
            // Refused rather than read on a best-effort basis. A newer file may carry fields whose
            // absence this build would read as a default and then write back, which turns opening
            // the file into losing part of it.
            return fail(formatName_ + " formatVersion " + std::to_string(version)
                        + " is newer than this build supports (" + std::to_string(currentVersion_) + ")");
        }

        int current = version;
        while (current < currentVersion_)
        {
            const auto step = std::find_if(migrations_.begin(), migrations_.end(),
                                           [current](const FormatMigration& migration)
                                           { return migration.fromVersion == current; });
            if (step == migrations_.end())
            {
                return fail(formatName_ + " is at version " + std::to_string(current)
                            + " and this build has no migration to upgrade it");
            }

            std::string errorMessage;
            if (!step->apply(document, errorMessage))
            {
                return fail(formatName_ + " could not be upgraded from version "
                            + std::to_string(current) + ": "
                            + (errorMessage.empty() ? "the migration refused it" : errorMessage));
            }

            ++current;

            // Stamped here rather than by the step. A step that forgot would leave the document
            // claiming a version it is no longer in, and the next step -- or the next run of the
            // migrator -- would read it as the wrong shape.
            document.set("formatVersion", JsonValue{current});
            result.applied.push_back(step->description);
        }

        result.succeeded = true;
        result.toVersion = current;
        return result;
    }
}
