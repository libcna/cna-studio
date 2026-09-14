// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Assets/AssetDatabase.hpp
 * @brief Stable identity, import settings and dependency tracking for a project's assets.
 *
 * The central rule is that **nothing references an asset by path** (ANALYSIS.md decision D-08).
 * A scene stores a Uuid; the database maps that Uuid to whatever path the file currently has.
 * Moving `Assets/player.png` into `Assets/Characters/` then costs nothing -- no scene is touched,
 * no reference breaks, and the move produces a one-line diff in a sidecar file instead of a
 * hundred-line diff across every scene that used the texture.
 *
 * The identity lives in a `.cnaasset` sidecar next to the source file, so it survives the source
 * being edited by an external tool, and so it can be committed to git alongside the art.
 */

#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "CNA/Editor/Core/FormatMigration.hpp"
#include "CNA/Editor/Core/Json.hpp"
#include "CNA/Editor/Core/Uuid.hpp"

namespace CNA::Editor
{
    /** @brief The kinds of asset the Phase 1/2 editor understands. */
    enum class AssetType
    {
        /** @brief A file whose type no importer claims. Tracked, given an id, never imported. */
        Unknown,
        Texture2D,
        SpriteFont,
        SoundEffect,
        Song,
        Effect,
        Model,
        Scene,
        /** @brief A `.cnaprefab`: one reusable entity subtree, instantiated into scenes. */
        Prefab,
        /** @brief A `.cnamaterial`: an authored material a `ModelRenderer` can override with. */
        Material,
        /** @brief Any file the project wants tracked verbatim, e.g. a JSON data table. */
        RawData
    };

    /** @brief Returns the stable textual name of @p type as written into `.cnaasset` files. */
    const char* toString(AssetType type);

    /** @brief Parses the name produced by toString(); returns AssetType::Unknown on failure. */
    AssetType parseAssetType(std::string_view text);

    /**
     * @brief One tracked asset.
     *
     * @c sourcePath is relative to the project root and uses forward slashes on every platform, so
     * that a `.cnaasset` file committed on Linux resolves identically on Windows.
     */
    struct AssetRecord
    {
        Uuid id;
        std::string sourcePath;
        AssetType type = AssetType::Unknown;

        /** @brief Type id of the importer that processes this asset, e.g. "CNA.TextureImporter". */
        std::string importerId;

        /** @brief Importer-specific settings, serialised verbatim into the `.cnaasset` sidecar. */
        JsonValue importerSettings;

        /**
         * @brief Ids of assets this one references.
         *
         * Populated by importers (a Model referencing its textures) and by the scene compiler.
         * Drives reimport ordering and the "unused assets" and "missing reference" reports.
         */
        std::vector<Uuid> dependencies;

        /**
         * @brief Source file size and modification time at the last successful import.
         *
         * Compared against the file on disk to decide whether a reimport is needed. Deliberately
         * *not* a content hash: hashing every asset on every project open is the kind of thing
         * that makes an editor take thirty seconds to start on a real project. A hash can be added
         * later as an opt-in for correctness-critical pipelines.
         */
        std::uint64_t sourceSize = 0;
        std::int64_t sourceModifiedTime = 0;
    };

    /** @brief Outcome of a scan or load, with any non-fatal problems collected for the console. */
    struct AssetScanResult
    {
        bool succeeded = false;
        std::string errorMessage;
        std::vector<std::string> warnings;

        std::size_t discoveredCount = 0;
        std::size_t newCount = 0;
        std::size_t movedCount = 0;
        std::size_t missingCount = 0;
    };

    /**
     * @brief The project's asset index.
     *
     * Owns the id-to-record mapping and the sidecar files. It does *not* load asset content --
     * that is the importers' job, and in the multi-process play mode it is the player's job. The
     * database is pure metadata, which is what keeps it usable from a headless build.
     */
    /**
     * @brief Returns the migration chain that upgrades a `.cnaasset` sidecar.
     *
     * Empty today. A sidecar this build cannot upgrade keeps its id and loses only its importer
     * settings -- the id is the one thing scenes reference (D-08), and regenerating it would break
     * every reference to the asset, which is a far worse outcome than an importer setting reverting
     * to its default.
     */
    [[nodiscard]] const FormatMigrator& getAssetFormatMigrator();

    class AssetDatabase
    {
    public:
        /** @brief The `formatVersion` this build writes into `.cnaasset` sidecars. */
        static constexpr int kFormatVersion = 1;

        /** @brief The sidecar file extension, appended to the full source file name. */
        static constexpr const char* kSidecarExtension = ".cnaasset";

        /**
         * @brief Points the database at a project root. Does not scan.
         * @param projectRoot Absolute path to the directory containing the `.cnaproject` file.
         */
        void setProjectRoot(std::string projectRoot);

        /** @brief Returns the current project root. */
        [[nodiscard]] const std::string& getProjectRoot() const { return projectRoot_; }

        /**
         * @brief Walks @p relativeAssetDirectory and reconciles the index with what is on disk.
         *
         * For each file found: an existing sidecar supplies the id; a missing sidecar means a new
         * asset, which is given a fresh id and a sidecar. Records whose source file has vanished
         * are kept and reported as missing rather than deleted -- a file that is gone today may be
         * a git checkout away from returning, and deleting the record would break every reference.
         */
        AssetScanResult scan(const std::string& relativeAssetDirectory = "Assets");

        /** @brief Returns the record for @p id, or nullptr when unknown. */
        [[nodiscard]] const AssetRecord* find(const Uuid& id) const;

        /**
         * @brief Returns a mutable record for @p id, or nullptr when unknown.
         *
         * Only commands should use this. Everything else takes the const overload, which is what
         * keeps "the asset database changes only through the undo stack" checkable by reading the
         * call sites rather than by trusting them (D-06).
         */
        [[nodiscard]] AssetRecord* findMutable(const Uuid& id);

        /** @brief Returns the record whose source path is @p relativePath, or nullptr. */
        [[nodiscard]] const AssetRecord* findByPath(std::string_view relativePath) const;

        /** @brief Returns every record, ordered by source path. */
        [[nodiscard]] std::vector<const AssetRecord*> getAll() const;

        /** @brief Returns the number of tracked assets. */
        [[nodiscard]] std::size_t getCount() const { return recordsById_.size(); }

        /**
         * @brief Registers @p record, replacing any record with the same id.
         * @return False when @p record has no valid id.
         */
        bool add(AssetRecord record);

        /**
         * @brief Removes @p id from the index. Returns true when a record was removed.
         *
         * Deliberately narrow: the database does **not** drop a record because its file went
         * missing (see scan()), because a file gone today may be a `git checkout` away from
         * returning and deleting the record would break every reference to it. This exists for the
         * one case where that reasoning does not apply -- undoing the creation of an asset the
         * editor itself just wrote, where the file is going away too and nothing ever referenced it.
         */
        bool removeRecord(const Uuid& id);

        /** @brief Returns true when @p id is tracked but its source file is not on disk. */
        [[nodiscard]] bool isMissing(const Uuid& id) const;

        /** @brief Returns the ids of every tracked asset whose source file is absent. */
        [[nodiscard]] std::vector<Uuid> getMissingAssets() const;

        /**
         * @brief Moves an asset's source file and its sidecar to @p newRelativePath.
         *
         * The asset keeps its id, so **no scene is touched** (ANALYSIS.md decision D-08). That is
         * the whole point: a reference is a Uuid, and moving a file is a filesystem operation, not
         * a document one. An editor that rewrote every scene on a rename would turn tidying an
         * asset folder into a review of the entire project.
         *
         * The source file and the sidecar move together or not at all. A half-moved asset -- data
         * in one place, metadata in another -- is worse than a failed move, because the next scan
         * would give the orphaned file a new id and silently break every reference to it.
         *
         * @return False when the id is unknown, the destination is occupied, the destination
         *         escapes the project root, or the filesystem refuses; @p errorMessage says which.
         */
        bool moveAsset(const Uuid& id, const std::string& newRelativePath,
                       std::string* errorMessage = nullptr);

        /** @brief Writes the sidecar for @p id. Returns false when the id is unknown or I/O fails. */
        bool writeSidecar(const Uuid& id, std::string* errorMessage = nullptr) const;

        /** @brief Returns the absolute path of @p relativePath within the project. */
        [[nodiscard]] std::string resolvePath(std::string_view relativePath) const;

        /** @brief Guesses an AssetType from a file extension. Case-insensitive. */
        [[nodiscard]] static AssetType guessTypeFromExtension(std::string_view path);

        /** @brief Returns the default importer type id for @p type, or an empty string. */
        [[nodiscard]] static std::string defaultImporterFor(AssetType type);

        /** @brief Drops every record. The project root is kept. */
        void clear();

    private:
        [[nodiscard]] static JsonValue recordToJson(const AssetRecord& record);
        [[nodiscard]] static AssetRecord recordFromJson(const JsonValue& json, std::string relativePath);

        std::string projectRoot_;
        std::unordered_map<Uuid, AssetRecord> recordsById_;
        std::map<std::string, Uuid> idsByPath_;
    };
}
