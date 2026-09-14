// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Assets/AssetDatabase.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace CNA::Editor
{
    namespace
    {
        struct AssetTypeName
        {
            AssetType type;
            const char* name;
        };

        constexpr std::array<AssetTypeName, 11> kAssetTypeNames{{
            {AssetType::Unknown, "Unknown"},
            {AssetType::Texture2D, "Texture2D"},
            {AssetType::SpriteFont, "SpriteFont"},
            {AssetType::SoundEffect, "SoundEffect"},
            {AssetType::Song, "Song"},
            {AssetType::Effect, "Effect"},
            {AssetType::Model, "Model"},
            {AssetType::Scene, "Scene"},
            {AssetType::Prefab, "Prefab"},
            {AssetType::Material, "Material"},
            {AssetType::RawData, "RawData"},
        }};

        std::string toLowerCase(std::string_view text)
        {
            std::string result{text};
            std::transform(result.begin(), result.end(), result.begin(),
                           [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
            return result;
        }

        /** @brief Converts a filesystem path to the forward-slash form stored in project files. */
        std::string toPortablePath(const std::filesystem::path& path)
        {
            std::string text = path.generic_string();
            return text;
        }
    }

    const char* toString(AssetType type)
    {
        for (const auto& entry : kAssetTypeNames)
        {
            if (entry.type == type) { return entry.name; }
        }
        return "Unknown";
    }

    AssetType parseAssetType(std::string_view text)
    {
        for (const auto& entry : kAssetTypeNames)
        {
            if (text == entry.name) { return entry.type; }
        }
        return AssetType::Unknown;
    }

    void AssetDatabase::setProjectRoot(std::string projectRoot)
    {
        projectRoot_ = std::move(projectRoot);
    }

    std::string AssetDatabase::resolvePath(std::string_view relativePath) const
    {
        if (projectRoot_.empty()) { return std::string{relativePath}; }
        return toPortablePath(std::filesystem::path{projectRoot_} / std::filesystem::path{relativePath});
    }

    AssetType AssetDatabase::guessTypeFromExtension(std::string_view path)
    {
        const std::string extension = toLowerCase(std::filesystem::path{path}.extension().string());

        if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp"
            || extension == ".tga" || extension == ".dds" || extension == ".gif")
        {
            return AssetType::Texture2D;
        }
        if (extension == ".spritefont" || extension == ".fnt" || extension == ".ttf") { return AssetType::SpriteFont; }
        if (extension == ".wav" || extension == ".xwb" || extension == ".xsb") { return AssetType::SoundEffect; }
        if (extension == ".ogg" || extension == ".mp3" || extension == ".flac") { return AssetType::Song; }
        if (extension == ".fx" || extension == ".hlsl" || extension == ".glsl") { return AssetType::Effect; }
        if (extension == ".gltf" || extension == ".glb" || extension == ".fbx" || extension == ".obj"
            || extension == ".cnj")
        {
            return AssetType::Model;
        }
        if (extension == ".cnascene") { return AssetType::Scene; }
        if (extension == ".cnaprefab") { return AssetType::Prefab; }
        if (extension == ".cnamaterial") { return AssetType::Material; }
        if (extension == ".json" || extension == ".xml" || extension == ".txt" || extension == ".csv")
        {
            return AssetType::RawData;
        }
        return AssetType::Unknown;
    }

    std::string AssetDatabase::defaultImporterFor(AssetType type)
    {
        switch (type)
        {
            case AssetType::Texture2D: return "CNA.TextureImporter";
            case AssetType::SpriteFont: return "CNA.SpriteFontImporter";
            case AssetType::SoundEffect: return "CNA.SoundEffectImporter";
            case AssetType::Song: return "CNA.SongImporter";
            case AssetType::Effect: return "CNA.EffectImporter";
            case AssetType::Model: return "CNA.ModelImporter";
            case AssetType::Scene: return "CNA.SceneImporter";

            // No importer. A prefab is authored by the editor and read by the editor; there is no
            // conversion step for one, and inventing an importer with no settings would put an
            // empty section in the inspector for every prefab in the project.
            case AssetType::Prefab: return {};

            // Same reasoning as a prefab: a `.cnamaterial` is the editor's own output, read
            // directly, so there is nothing to import and no settings to offer for importing it.
            case AssetType::Material: return {};
            case AssetType::RawData: return "CNA.RawDataImporter";
            case AssetType::Unknown: return {};
        }
        return {};
    }

    const AssetRecord* AssetDatabase::find(const Uuid& id) const
    {
        const auto found = recordsById_.find(id);
        return found == recordsById_.end() ? nullptr : &found->second;
    }

    AssetRecord* AssetDatabase::findMutable(const Uuid& id)
    {
        const auto found = recordsById_.find(id);
        return found == recordsById_.end() ? nullptr : &found->second;
    }

    const AssetRecord* AssetDatabase::findByPath(std::string_view relativePath) const
    {
        const auto found = idsByPath_.find(std::string{relativePath});
        return found == idsByPath_.end() ? nullptr : find(found->second);
    }

    std::vector<const AssetRecord*> AssetDatabase::getAll() const
    {
        std::vector<const AssetRecord*> records;
        records.reserve(idsByPath_.size());
        for (const auto& [path, id] : idsByPath_)
        {
            if (const AssetRecord* record = find(id)) { records.push_back(record); }
        }
        return records;
    }

    bool AssetDatabase::add(AssetRecord record)
    {
        if (!record.id.isValid()) { return false; }

        // A record may be re-added with a new source path (an asset that moved). Drop the stale
        // path index entry first, or findByPath would keep resolving the old location.
        if (const auto existing = recordsById_.find(record.id); existing != recordsById_.end())
        {
            idsByPath_.erase(existing->second.sourcePath);
        }

        const Uuid id = record.id;
        const std::string path = record.sourcePath;
        recordsById_[id] = std::move(record);
        if (!path.empty()) { idsByPath_[path] = id; }
        return true;
    }

    bool AssetDatabase::moveAsset(const Uuid& id, const std::string& newRelativePath,
                                  std::string* errorMessage)
    {
        const auto fail = [&](std::string reason) {
            if (errorMessage != nullptr) { *errorMessage = std::move(reason); }
            return false;
        };

        AssetRecord* record = findMutable(id);
        if (record == nullptr) { return fail("no asset with that id"); }
        if (newRelativePath.empty()) { return fail("destination path is empty"); }
        if (record->sourcePath == newRelativePath) { return true; }

        // A destination that climbs out of the project would put the asset somewhere the project
        // cannot describe, and the relative path stored in the sidecar would stop meaning anything.
        const std::filesystem::path normalised =
            std::filesystem::path{newRelativePath}.lexically_normal();
        if (normalised.is_absolute() || normalised.native().rfind("..", 0) == 0)
        {
            return fail("destination must stay inside the project");
        }

        const std::string destination = normalised.generic_string();
        if (findByPath(destination) != nullptr) { return fail("'" + destination + "' is already tracked"); }

        std::error_code errorCode;
        const std::filesystem::path from{resolvePath(record->sourcePath)};
        const std::filesystem::path to{resolvePath(destination)};

        if (std::filesystem::exists(to, errorCode)) { return fail("'" + destination + "' already exists"); }

        std::filesystem::create_directories(to.parent_path(), errorCode);
        if (errorCode) { return fail("cannot create '" + to.parent_path().generic_string() + "'"); }

        errorCode.clear();
        std::filesystem::rename(from, to, errorCode);
        if (errorCode) { return fail("cannot move '" + record->sourcePath + "': " + errorCode.message()); }

        // The sidecar follows the file. If it will not, the move is undone rather than left half
        // done: an orphaned source file gets a fresh id on the next scan, which silently breaks
        // every reference to it.
        const std::filesystem::path sidecarFrom{from.generic_string() + kSidecarExtension};
        const std::filesystem::path sidecarTo{to.generic_string() + kSidecarExtension};

        errorCode.clear();
        if (std::filesystem::exists(sidecarFrom, errorCode))
        {
            errorCode.clear();
            std::filesystem::rename(sidecarFrom, sidecarTo, errorCode);
            if (errorCode)
            {
                std::error_code rollback;
                std::filesystem::rename(to, from, rollback);
                return fail("cannot move the sidecar for '" + record->sourcePath + "': "
                            + errorCode.message());
            }
        }

        idsByPath_.erase(record->sourcePath);
        record->sourcePath = destination;
        idsByPath_[destination] = id;

        // Rewritten because the sidecar records its own path nowhere -- but its stamp is about the
        // file, and a move is a good moment to be sure the two agree.
        writeSidecar(id);
        return true;
    }

    bool AssetDatabase::removeRecord(const Uuid& id)
    {
        const auto found = recordsById_.find(id);
        if (found == recordsById_.end()) { return false; }

        idsByPath_.erase(found->second.sourcePath);
        recordsById_.erase(found);
        return true;
    }

    bool AssetDatabase::isMissing(const Uuid& id) const
    {
        const AssetRecord* record = find(id);
        if (record == nullptr) { return false; }
        return !std::filesystem::exists(resolvePath(record->sourcePath));
    }

    std::vector<Uuid> AssetDatabase::getMissingAssets() const
    {
        std::vector<Uuid> missing;
        for (const auto& [id, record] : recordsById_)
        {
            if (!std::filesystem::exists(resolvePath(record.sourcePath))) { missing.push_back(id); }
        }
        std::sort(missing.begin(), missing.end());
        return missing;
    }

    const FormatMigrator& getAssetFormatMigrator()
    {
        static const FormatMigrator migrator{"asset sidecar", AssetDatabase::kFormatVersion};
        return migrator;
    }

    JsonValue AssetDatabase::recordToJson(const AssetRecord& record)
    {
        JsonValue json = JsonValue::makeObject();
        json.set("formatVersion", JsonValue{kFormatVersion});
        json.set("id", JsonValue{record.id.toString()});
        json.set("type", JsonValue{toString(record.type)});
        if (!record.importerId.empty()) { json.set("importer", JsonValue{record.importerId}); }
        if (!record.importerSettings.isNull()) { json.set("settings", record.importerSettings); }

        if (!record.dependencies.empty())
        {
            JsonValue dependencies = JsonValue::makeArray();
            for (const Uuid& dependency : record.dependencies)
            {
                dependencies.append(JsonValue{dependency.toString()});
            }
            json.set("dependencies", std::move(dependencies));
        }

        if (record.sourceSize != 0 || record.sourceModifiedTime != 0)
        {
            JsonValue stamp = JsonValue::makeObject();
            stamp.set("size", JsonValue{static_cast<std::int64_t>(record.sourceSize)});
            stamp.set("modifiedTime", JsonValue{record.sourceModifiedTime});
            json.set("sourceStamp", std::move(stamp));
        }
        return json;
    }

    AssetRecord AssetDatabase::recordFromJson(const JsonValue& json, std::string relativePath)
    {
        AssetRecord record;
        record.id = Uuid::parse(json["id"].asString());
        record.sourcePath = std::move(relativePath);
        record.type = parseAssetType(json["type"].asString());
        record.importerId = json["importer"].asString();
        record.importerSettings = json["settings"];

        for (const JsonValue& dependency : json["dependencies"].getElements())
        {
            const Uuid id = Uuid::parse(dependency.asString());
            if (id.isValid()) { record.dependencies.push_back(id); }
        }

        const JsonValue& stamp = json["sourceStamp"];
        record.sourceSize = static_cast<std::uint64_t>(stamp["size"].asNumber());
        record.sourceModifiedTime = static_cast<std::int64_t>(stamp["modifiedTime"].asNumber());
        return record;
    }

    bool AssetDatabase::writeSidecar(const Uuid& id, std::string* errorMessage) const
    {
        const AssetRecord* record = find(id);
        if (record == nullptr)
        {
            if (errorMessage != nullptr) { *errorMessage = "unknown asset id " + id.toString(); }
            return false;
        }

        const std::string sidecarPath = resolvePath(record->sourcePath) + kSidecarExtension;
        std::ofstream stream{sidecarPath, std::ios::binary | std::ios::trunc};
        if (!stream)
        {
            if (errorMessage != nullptr) { *errorMessage = "cannot write '" + sidecarPath + "'"; }
            return false;
        }

        stream << Json::write(recordToJson(*record), true);
        return static_cast<bool>(stream);
    }

    AssetScanResult AssetDatabase::scan(const std::string& relativeAssetDirectory)
    {
        AssetScanResult result;

        if (projectRoot_.empty())
        {
            result.errorMessage = "no project root set";
            return result;
        }

        const std::filesystem::path assetRoot = std::filesystem::path{projectRoot_} / relativeAssetDirectory;
        std::error_code errorCode;
        if (!std::filesystem::exists(assetRoot, errorCode))
        {
            // An absent asset directory is a perfectly normal state for a brand-new project, so
            // this succeeds with a warning rather than failing.
            result.succeeded = true;
            result.warnings.push_back("asset directory '" + relativeAssetDirectory + "' does not exist yet");
            return result;
        }

        std::filesystem::recursive_directory_iterator iterator{
            assetRoot, std::filesystem::directory_options::skip_permission_denied, errorCode};
        if (errorCode)
        {
            result.errorMessage = "cannot walk '" + toPortablePath(assetRoot) + "': " + errorCode.message();
            return result;
        }

        const std::filesystem::path rootPath{projectRoot_};
        for (const std::filesystem::directory_entry& entry : iterator)
        {
            if (!entry.is_regular_file(errorCode)) { continue; }

            const std::filesystem::path& path = entry.path();
            // Sidecars describe assets; they are not assets themselves.
            if (path.extension() == kSidecarExtension) { continue; }

            const std::string relativePath = toPortablePath(std::filesystem::relative(path, rootPath, errorCode));
            if (errorCode || relativePath.empty())
            {
                result.warnings.push_back("cannot make '" + toPortablePath(path) + "' relative to the project root");
                errorCode.clear();
                continue;
            }

            ++result.discoveredCount;

            const std::string sidecarPath = toPortablePath(path) + kSidecarExtension;
            AssetRecord record;
            bool isNew = true;

            if (std::filesystem::exists(sidecarPath, errorCode))
            {
                std::ifstream stream{sidecarPath, std::ios::binary};
                std::ostringstream buffer;
                buffer << stream.rdbuf();
                JsonParseResult parsed = Json::parse(buffer.str());
                if (parsed.succeeded)
                {
                    const FormatMigrationResult migration =
                        getAssetFormatMigrator().migrate(parsed.value);

                    if (migration.succeeded)
                    {
                        record = recordFromJson(parsed.value, relativePath);
                        for (const std::string& step : migration.applied)
                        {
                            result.warnings.push_back("sidecar '" + relativePath + kSidecarExtension
                                                      + "' upgraded from an older format: " + step);
                        }
                    }
                    else
                    {
                        // The id survives even when nothing else does. Scenes reference assets by
                        // id (D-08), so regenerating it would break every reference in the project
                        // -- a far worse outcome than an importer setting reverting to its default.
                        // The sidecar is left on disk untouched, so a build that understands it
                        // still can.
                        record = AssetRecord{};
                        record.id = Uuid::parse(parsed.value["id"].asString());
                        record.sourcePath = relativePath;
                        record.type = guessTypeFromExtension(relativePath);
                        record.importerId = defaultImporterFor(record.type);

                        result.warnings.push_back("sidecar '" + relativePath + kSidecarExtension
                                                  + "': " + migration.errorMessage
                                                  + "; its id was kept and its settings ignored");
                    }

                    isNew = !record.id.isValid();
                    if (isNew)
                    {
                        result.warnings.push_back("sidecar '" + relativePath + kSidecarExtension
                                                  + "' has no valid id; a new id was assigned");
                    }
                }
                else
                {
                    result.warnings.push_back("sidecar '" + relativePath + kSidecarExtension
                                              + "' is malformed (" + parsed.errorMessage
                                              + "); a new id was assigned");
                }
            }

            if (isNew)
            {
                record = AssetRecord{};
                record.id = Uuid::generate();
                record.sourcePath = relativePath;
                record.type = guessTypeFromExtension(relativePath);
                record.importerId = defaultImporterFor(record.type);
                ++result.newCount;
            }
            else if (const AssetRecord* previous = find(record.id); previous != nullptr
                     && previous->sourcePath != relativePath)
            {
                // The same id now lives at a different path: the file was moved or renamed. Every
                // scene referencing it keeps working, because scenes reference the id.
                ++result.movedCount;
            }

            record.sourceSize = static_cast<std::uint64_t>(entry.file_size(errorCode));
            if (errorCode) { record.sourceSize = 0; errorCode.clear(); }

            // Stored in seconds, not in the clock's native ticks. A nanosecond count is around
            // 4.6e18, which is past the range a double represents exactly -- and JSON numbers are
            // doubles, so the native value would round-trip through the sidecar wrong and make
            // every asset look modified on every scan. Seconds are exact and are finer-grained
            // than any reimport decision needs.
            const auto writeTime = entry.last_write_time(errorCode);
            record.sourceModifiedTime =
                errorCode ? 0
                          : std::chrono::duration_cast<std::chrono::seconds>(writeTime.time_since_epoch()).count();
            errorCode.clear();

            add(record);

            if (isNew && !writeSidecar(record.id))
            {
                result.warnings.push_back("cannot write sidecar for '" + relativePath
                                          + "'; its id will not survive a restart");
            }
        }

        result.missingCount = getMissingAssets().size();
        result.succeeded = true;
        return result;
    }

    void AssetDatabase::clear()
    {
        recordsById_.clear();
        idsByPath_.clear();
    }
}
