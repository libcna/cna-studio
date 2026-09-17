// SPDX-License-Identifier: MS-PL
/**
 * @file AssetShortcuts.cpp
 * @brief Starred and recently used assets, per project and per user.
 *
 * `plan.md` STUDIO-09007.
 */

#include "CNA/Studio/Assets/AssetShortcuts.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/Core/Json.hpp"
#include "CNA/Studio/Core/UserPaths.hpp"

namespace CNA::Studio
{
    namespace
    {
        /**
         * @brief FNV-1a over @p text, as eight hexadecimal digits.
         *
         * Written out rather than reached for, because what is wanted is a *stable* short name for
         * a path and nothing else: not a cryptographic digest, not `std::hash` — whose value is
         * allowed to differ between runs of the same program, which is exactly the one property
         * this must not have.
         */
        std::string shortHashOf(std::string_view text)
        {
            std::uint32_t hash = 2166136261u;
            for (const char character : text)
            {
                hash ^= static_cast<std::uint8_t>(character);
                hash *= 16777619u;
            }

            std::ostringstream out;
            out << std::hex << std::nouppercase;
            out.width(8);
            out.fill('0');
            out << hash;
            return out.str();
        }

        /** @brief @p name with anything that is not a letter, digit, dash or dot removed. */
        std::string sanitised(std::string_view name)
        {
            std::string out;
            out.reserve(name.size());
            for (const char character : name)
            {
                const unsigned char byte = static_cast<unsigned char>(character);
                if (std::isalnum(byte) != 0 || character == '-' || character == '_')
                {
                    out.push_back(character);
                }
            }
            return out;
        }

        /** @brief Reads a JSON array of id strings, skipping anything that is not one. */
        std::vector<Uuid> idsFrom(const JsonValue& json)
        {
            std::vector<Uuid> ids;
            for (const JsonValue& element : json.getElements())
            {
                const Uuid id = Uuid::parse(element.asString());

                // A file somebody edited, or one written by a build whose format moved on. An
                // unreadable entry is dropped rather than making the whole list unreadable: this
                // is a convenience, and a corrupt convenience must not cost a project its opening.
                if (id.isValid()) { ids.push_back(id); }
            }
            return ids;
        }

        JsonValue idsTo(const std::vector<Uuid>& ids)
        {
            JsonValue array = JsonValue::makeArray();
            for (const Uuid& id : ids) { array.append(JsonValue{id.toString()}); }
            return array;
        }
    }

    bool StudioAssetShortcuts::isFavourite(const Uuid& id) const
    {
        return std::find(favourites.begin(), favourites.end(), id) != favourites.end();
    }

    bool StudioAssetShortcuts::toggleFavourite(const Uuid& id)
    {
        if (!id.isValid()) { return false; }

        const auto found = std::find(favourites.begin(), favourites.end(), id);
        if (found != favourites.end())
        {
            favourites.erase(found);
            return false;
        }

        // Appended rather than prepended: a favourite is a decision, and a list that reordered
        // itself every time one was added would make the user hunt for the one they starred last
        // week. Recent is the list that moves.
        favourites.push_back(id);
        return true;
    }

    bool StudioAssetShortcuts::remember(const Uuid& id, std::size_t limit)
    {
        if (!id.isValid()) { return false; }

        const auto found = std::find(recent.begin(), recent.end(), id);
        if (found == recent.begin() && found != recent.end())
        {
            // Already the most recent. Reported as unchanged so that clicking one asset repeatedly
            // does not rewrite the file once a frame.
            return false;
        }

        if (found != recent.end()) { recent.erase(found); }
        recent.insert(recent.begin(), id);

        if (limit > 0 && recent.size() > limit) { recent.resize(limit); }
        return true;
    }

    std::size_t StudioAssetShortcuts::prune(const AssetDatabase& assets)
    {
        std::size_t dropped = 0;

        const auto keep = [&assets, &dropped](std::vector<Uuid>& ids) {
            const auto gone = std::remove_if(ids.begin(), ids.end(), [&assets](const Uuid& id) {
                return assets.find(id) == nullptr;
            });
            dropped += static_cast<std::size_t>(std::distance(gone, ids.end()));
            ids.erase(gone, ids.end());
        };

        keep(favourites);
        keep(recent);
        return dropped;
    }

    std::string StudioAssetShortcutStore::defaultPathFor(const std::string& projectFilePath)
    {
        if (projectFilePath.empty()) { return {}; }

        const std::string stateDirectory = getStudioStateDirectory();
        if (stateDirectory.empty()) { return {}; }

        const std::filesystem::path project{projectFilePath};
        std::string name = sanitised(project.stem().string());
        if (name.empty()) { name = "project"; }

        // The name *and* the hash: the name is what makes the directory readable by a person who
        // opens it, and the hash is what keeps two projects called `Game` in different places from
        // sharing a file.
        return (std::filesystem::path{stateDirectory} / "assets"
                / (name + "-" + shortHashOf(project.generic_string()) + ".json"))
            .generic_string();
    }

    StudioAssetShortcuts StudioAssetShortcutStore::load() const
    {
        StudioAssetShortcuts shortcuts;
        if (path_.empty()) { return shortcuts; }

        std::ifstream stream{path_, std::ios::binary};
        if (!stream) { return shortcuts; }

        std::ostringstream buffer;
        buffer << stream.rdbuf();

        const JsonParseResult parsed = Json::parse(buffer.str());
        if (!parsed.succeeded) { return shortcuts; }

        shortcuts.favourites = idsFrom(parsed.value["favourites"]);
        shortcuts.recent = idsFrom(parsed.value["recent"]);

        if (shortcuts.recent.size() > kMaximumRecent) { shortcuts.recent.resize(kMaximumRecent); }
        return shortcuts;
    }

    bool StudioAssetShortcutStore::save(const StudioAssetShortcuts& shortcuts,
                                        std::string* outProblem) const
    {
        const auto fail = [outProblem](std::string reason) {
            if (outProblem != nullptr) { *outProblem = std::move(reason); }
            return false;
        };

        if (path_.empty()) { return fail("there is nowhere to keep this user's Studio state"); }

        std::error_code code;
        const std::filesystem::path file{path_};
        std::filesystem::create_directories(file.parent_path(), code);
        if (code) { return fail("cannot create '" + file.parent_path().generic_string() + "'"); }

        JsonValue json = JsonValue::makeObject();
        json.set("version", JsonValue{kFileVersion});
        json.set("favourites", idsTo(shortcuts.favourites));
        json.set("recent", idsTo(shortcuts.recent));

        std::ofstream stream{file, std::ios::binary | std::ios::trunc};
        if (!stream) { return fail("cannot write '" + path_ + "'"); }

        stream << Json::write(json, true);
        if (!stream.good()) { return fail("cannot write '" + path_ + "'"); }
        return true;
    }
}
