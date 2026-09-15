// SPDX-License-Identifier: MS-PL
/**
 * @file StudioWorkspaceStore.cpp
 * @brief Reads and writes the workspace layout file.
 */

#include "CNA/Studio/UiCore/StudioWorkspaceStore.hpp"

#include <algorithm>

#include "CNA/Studio/Core/UserPaths.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace CNA::Studio
{
    std::string StudioWorkspaceStore::defaultPath()
    {
        // Configuration, not state: a workspace arrangement is something the user made, and a
        // machine that swept it would have thrown away work rather than a cache.
        const std::string configDirectory = getStudioConfigDirectory();
        if (configDirectory.empty()) { return {}; }
        return (std::filesystem::path{configDirectory} / kFileName).generic_string();
    }

    std::string StudioWorkspaceStore::sanitizeName(std::string_view name)
    {
        std::size_t first = 0;
        while (first < name.size()
               && static_cast<unsigned char>(name[first]) <= static_cast<unsigned char>(' '))
        {
            ++first;
        }
        std::size_t last = name.size();
        while (last > first
               && static_cast<unsigned char>(name[last - 1]) <= static_cast<unsigned char>(' '))
        {
            --last;
        }

        std::string trimmed{name.substr(first, last - first)};
        if (trimmed.empty() || trimmed.size() > kMaximumNameLength) { return {}; }

        // Control characters would make the file unreadable to a person and the menu row a
        // surprise. Refused rather than stripped: a name the user did not type is not their name.
        for (const char character : trimmed)
        {
            if (static_cast<unsigned char>(character) < 0x20) { return {}; }
        }
        return trimmed;
    }

    bool StudioWorkspaceStore::writeDocument(const JsonValue& document,
                                             std::string* outProblem) const
    {
        const auto fail = [&](const std::string& reason) {
            if (outProblem != nullptr) { *outProblem = reason; }
            return false;
        };

        if (path_.empty()) { return fail("there is nowhere on this machine to store the layout"); }

        const std::filesystem::path path{path_};
        std::error_code code;
        if (!path.parent_path().empty())
        {
            std::filesystem::create_directories(path.parent_path(), code);
            if (code)
            {
                return fail("cannot create '" + path.parent_path().generic_string() + "': "
                            + code.message());
            }
        }

        // Written beside the original and renamed over it. A Studio killed mid-save then leaves the
        // previous layout intact rather than a half-written file that reads as corrupt -- which
        // would cost the user their arrangement for a reason that had nothing to do with it.
        const std::filesystem::path temporary =
            path.parent_path() / (path.filename().string() + ".tmp");
        {
            std::ofstream stream{temporary, std::ios::binary | std::ios::trunc};
            if (!stream) { return fail("cannot write '" + temporary.generic_string() + "'"); }

            stream << Json::write(document, true);
            if (!stream)
            {
                return fail("failed while writing '" + temporary.generic_string() + "'");
            }
        }

        std::filesystem::rename(temporary, path, code);
        if (code)
        {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return fail("cannot replace '" + path.generic_string() + "': " + code.message());
        }
        return true;
    }

    bool StudioWorkspaceStore::rewrite(const std::function<bool(JsonValue&)>& change,
                                       std::string* outProblem) const
    {
        // Read first, always. This runs on exit and on every Save Layout As, and a Studio that
        // rewrote the whole file from what it happened to hold in memory would throw away a layout
        // saved by a second Studio running beside it.
        JsonValue document = JsonValue::makeObject();

        std::ifstream stream{std::filesystem::path{path_}, std::ios::binary};
        if (stream)
        {
            std::ostringstream contents;
            contents << stream.rdbuf();
            const JsonParseResult parsed = Json::parse(contents.str());

            // A file this build cannot read is replaced rather than merged into: half-keeping a
            // document whose shape is unknown is how one bad write becomes a permanent one.
            if (parsed.succeeded && parsed.value.isObject()
                && parsed.value["fileVersion"].asInt(0) <= kFileVersion)
            {
                document = parsed.value;
            }
        }

        document.set("fileVersion", kFileVersion);
        if (!change(document))
        {
            if (outProblem != nullptr) { *outProblem = "there was no such saved layout"; }
            return false;
        }
        return writeDocument(document, outProblem);
    }

    bool StudioWorkspaceStore::save(const JsonValue& layout, std::string* outProblem) const
    {
        return rewrite([&layout](JsonValue& document) {
            document.set("layout", layout);
            return true;
        }, outProblem);
    }

    bool StudioWorkspaceStore::saveNamed(std::string_view name, const JsonValue& layout,
                                         std::string* outProblem) const
    {
        const std::string clean = sanitizeName(name);
        if (clean.empty())
        {
            if (outProblem != nullptr)
            {
                *outProblem = "a saved layout needs a name of at most "
                            + std::to_string(kMaximumNameLength) + " characters";
            }
            return false;
        }

        return rewrite([&](JsonValue& document) {
            JsonValue named = document["named"].isArray() ? document["named"]
                                                          : JsonValue::makeArray();
            JsonValue replaced = JsonValue::makeArray();
            for (const JsonValue& entry : named.getElements())
            {
                // Replaced rather than appended twice: two rows with the same name is a menu where
                // one of them is unreachable.
                if (entry["name"].asString() != clean) { replaced.append(entry); }
            }

            JsonValue entry = JsonValue::makeObject();
            entry.set("name", clean);
            entry.set("layout", layout);
            replaced.append(std::move(entry));

            document.set("named", std::move(replaced));
            return true;
        }, outProblem);
    }

    bool StudioWorkspaceStore::removeNamed(std::string_view name, std::string* outProblem) const
    {
        const std::string clean = sanitizeName(name);
        if (clean.empty()) { return false; }

        return rewrite([&](JsonValue& document) {
            if (!document["named"].isArray()) { return false; }

            bool removed = false;
            JsonValue kept = JsonValue::makeArray();
            for (const JsonValue& entry : document["named"].getElements())
            {
                if (entry["name"].asString() == clean) { removed = true; continue; }
                kept.append(entry);
            }
            if (!removed) { return false; }

            document.set("named", std::move(kept));
            return true;
        }, outProblem);
    }

    StudioWorkspaceDocument StudioWorkspaceStore::load() const
    {
        StudioWorkspaceDocument result;
        if (path_.empty()) { return result; }

        std::ifstream stream{std::filesystem::path{path_}, std::ios::binary};
        if (!stream)
        {
            // A first run, not a fault.
            return result;
        }

        std::ostringstream contents;
        contents << stream.rdbuf();

        const JsonParseResult parsed = Json::parse(contents.str());
        if (!parsed.succeeded)
        {
            result.problem = "the stored workspace layout could not be read (" + parsed.errorMessage
                           + "), so the default arrangement was used";
            return result;
        }

        const int version = parsed.value["fileVersion"].asInt(0);
        if (version > kFileVersion)
        {
            // Refused rather than guessed at. A newer Studio may have written fields whose absence
            // means something, and half-reading a layout is a worse outcome than the default one.
            result.problem = "the stored workspace layout was written by a newer CNA Studio (file "
                             "version " + std::to_string(version) + "), so the default arrangement "
                             "was used";
            return result;
        }

        result.layout = parsed.value["layout"];
        result.found = true;

        for (const JsonValue& entry : parsed.value["named"].getElements())
        {
            // Sanitized on the way in as well as on the way out. This file is plain JSON in the
            // user's configuration directory, so a name that never went through saveNamed is an
            // ordinary thing to find rather than a corruption.
            const std::string name = sanitizeName(entry["name"].asString());
            if (name.empty()) { continue; }
            result.named.push_back(StudioNamedLayout{name, entry["layout"]});
        }
        std::sort(result.named.begin(), result.named.end(),
                  [](const StudioNamedLayout& lhs, const StudioNamedLayout& rhs) {
                      return lhs.name < rhs.name;
                  });
        return result;
    }

    bool StudioWorkspaceStore::forget() const
    {
        if (path_.empty()) { return false; }
        std::error_code code;
        return std::filesystem::remove(std::filesystem::path{path_}, code) && !code;
    }
}
