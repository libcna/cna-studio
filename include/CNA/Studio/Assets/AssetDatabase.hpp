// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Studio/Assets/AssetDatabase.hpp
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

#include "CNA/Studio/Core/FormatMigration.hpp"
#include "CNA/Studio/Core/Json.hpp"
#include "CNA/Studio/Core/Uuid.hpp"

namespace CNA::Studio
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
         * @brief Source file size and modification time **as last seen**.
         *
         * Written by a scan and kept up to date by `AssetWatcher`, so it describes the file on disk
         * rather than the state anything was imported from. The doc comment used to say "at the
         * last successful import" and the watcher had been overwriting it since `STUDIO-07051`,
         * which left nothing recording when an asset was actually imported -- see @ref importedSize.
         *
         * Deliberately *not* a content hash: hashing every asset on every project open is the kind
         * of thing that makes an editor take thirty seconds to start on a real project. A hash can
         * be added later as an opt-in for correctness-critical pipelines.
         */
        std::uint64_t sourceSize = 0;
        std::int64_t sourceModifiedTime = 0;

        /**
         * @brief Size and modification time at the last successful import (`plan.md` STUDIO-10001).
         *
         * The pair @ref sourceSize is compared *against* to decide whether a reimport is due. Both
         * are maintained without touching the filesystem -- the watcher updates one and a reimport
         * updates the other -- so "does this need reimporting" is answerable once per row of the
         * Content Browser without a syscall, which is what `STUDIO-30015` spent its effort on.
         *
         * Zero means never imported, which is what a freshly scanned asset is until something
         * imports it.
         */
        std::uint64_t importedSize = 0;
        std::int64_t importedModifiedTime = 0;

        /**
         * @brief Whether the source file was on disk the last time anybody looked.
         *
         * `plan.md` STUDIO-30012. Cached rather than asked, because the Content Browser asks once
         * per row per pass and the answer changes only when something changes it. Maintained by
         * `AssetDatabase` for everything Studio does, and refreshed by `AssetWatcher` for what it
         * does not -- see `AssetDatabase::isMissing` for when.
         *
         * Not serialised: a sidecar recording that a file existed on somebody else's machine last
         * Tuesday would be a fact about their disk, not about this one.
         */
        bool sourcePresent = true;
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

        /**
         * @brief The path-to-id index, in path order (`plan.md` STUDIO-09016).
         *
         * Exposed deliberately, and the container type is part of the contract: what a content
         * browser needs from a hundred-thousand-asset project is *range* queries — everything under
         * one folder, and the ability to skip a whole subtree by seeking past it — and an ordered
         * map is what answers those. `getAll()` cannot: it copies every pointer in the project to
         * show forty of them, which is the cost this exists to avoid.
         *
         * Read-only, so nothing outside can put the index out of step with the records.
         */
        [[nodiscard]] const std::map<std::string, Uuid>& getPathIndex() const { return idsByPath_; }

        /**
         * @brief How many assets sit **directly** in each folder, keyed by the folder's path.
         *
         * `plan.md` STUDIO-09016. Maintained incrementally, the way the missing count is, because
         * the Content Browser's folder pane wants it once per row on every frame — and deriving it
         * by walking every record was an O(project) pass per frame that nothing noticed at two
         * hundred assets and nothing survives at a hundred thousand.
         *
         * *Directly*, not cumulatively, which is the decision `STUDIO-09001` already made for the
         * pane and which the grid's folder cards now agree with: a cumulative count makes `Assets`
         * read as holding the whole project — true, and useless.
         *
         * The project root is the empty key. Folders with nothing directly in them have no entry;
         * they exist because something is under them, which is what @ref getFolderPaths answers.
         */
        [[nodiscard]] const std::map<std::string, std::size_t>& getFolderCounts() const
        {
            return folderCounts_;
        }

        /** @brief How many assets sit directly in @p folder. Zero for one that holds only folders. */
        [[nodiscard]] std::size_t getDirectAssetCount(std::string_view folder) const;

        /**
         * @brief How many assets sit in @p folder **or anywhere under it**.
         *
         * Kept beside the direct count rather than instead of it, because the two answer questions
         * the browser asks in different places and both answers were arrived at deliberately. The
         * folder *pane* shows the direct count — "how much will I see when I click this"
         * (`STUDIO-09001`); a folder *card* in the grid shows this one — "is it worth opening"
         * (`STUDIO-07008`). Deriving either on demand is a walk; maintaining both is two integers
         * per folder and a loop over the path's ancestors when one asset moves.
         */
        [[nodiscard]] std::size_t getTotalAssetCount(std::string_view folder) const;

        /**
         * @brief Every folder that holds anything, in path order, with its cumulative count.
         *
         * `plan.md` STUDIO-30022. The same map @ref getTotalAssetCount answers from, exposed
         * because it is the only index in the database whose keys are *folders* — and enumerating
         * a folder's immediate subfolders from the path index means stepping over every file in
         * it, since files and folders interleave alphabetically and no seek can skip them.
         *
         * That was the last O(folder) pass in the Content Browser, and it was invisible: it ran
         * inside `studioContentCardCount`, whose whole purpose is to answer without touching a
         * record. At a hundred thousand assets it was eight thousand map steps and eight thousand
         * substring allocations, twice a frame, to discover that a leaf folder has no subfolders.
         *
         * Ordered, and every ancestor is present, so a caller walks a folder's children with one
         * `lower_bound` per child and skips each subtree in a seek.
         */
        [[nodiscard]] const std::map<std::string, std::size_t>& getFolderTotals() const
        {
            return folderTotals_;
        }

        /**
         * @brief Every folder the tracked paths imply, in path order, the project root excluded.
         *
         * Derived from the folders that hold something plus their ancestors, so a folder exists
         * exactly when something tracked is in it or under it — the rule `STUDIO-09001` set, now
         * answered without walking every asset.
         */
        [[nodiscard]] std::vector<std::string> getFolderPaths() const;

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

        /**
         * @brief Returns true when @p id is tracked but its source file is not on disk.
         *
         * Reads a **cached** answer and touches no filesystem (`plan.md` STUDIO-30012). It used to
         * be a `std::filesystem::exists()` per call, which the Content Browser makes once per row
         * per pass: about 3 000 stat calls a frame at 1 500 assets, synchronously, in the middle of
         * describing the UI, and 61% of that panel's frame cost (`STUDIO-30014`).
         *
         * ### When the answer is refreshed, which is a product decision rather than an optimisation
         *
         * - **Immediately**, for anything Studio does itself: a scan, an add, a move, a relink.
         *   Those go through this class, so the cache cannot be behind them.
         * - **Within the watcher's interval** — half a second by default — for a file added or
         *   removed by something else. `AssetWatcher` already stats every tracked file on its poll,
         *   so keeping the cache up to date there costs nothing that was not being spent.
         * - **On demand**, through @ref refreshPresence, for an explicit Refresh.
         *
         * Never noticing would be wrong: "what did I break when I moved that folder" is the
         * question a content browser is most often opened to answer. Noticing per frame is what
         * used to happen and is what made it expensive.
         */
        [[nodiscard]] bool isMissing(const Uuid& id) const;

        /** @brief Returns the ids of every tracked asset whose source file is absent. */
        [[nodiscard]] std::vector<Uuid> getMissingAssets() const;

        /**
         * @brief How many tracked assets have no source file, in constant time.
         *
         * The Content Browser wants the *number*, not the list, on every frame; building the list
         * to call `.size()` on it was a second full pass over the database (`STUDIO-30014`).
         */
        [[nodiscard]] std::size_t getMissingCount() const { return missingCount_; }

        /**
         * @brief Asks the filesystem again about every tracked asset, or about one.
         *
         * The explicit invalidation half of the caching strategy. Costs one stat per record, so it
         * belongs to a scan, a watcher poll or a Refresh — never to drawing.
         *
         * @param id One asset, or the nil id for all of them.
         * @return How many records changed their presence.
         */
        std::size_t refreshPresence(const Uuid& id = {});

        /**
         * @brief Records that @p id's file is or is not there, without asking the filesystem.
         *
         * For a caller that has *just looked* -- `AssetWatcher`, which stats every tracked file on
         * its poll anyway. It goes through the database rather than writing `sourcePresent`
         * directly so that the missing *count* is maintained with it: two places deciding what
         * "missing" means is how a count and a list come to disagree.
         *
         * @param id The asset. Unknown ids are ignored.
         * @param present What the caller saw.
         * @return True when this changed the answer.
         */
        bool setAssetPresent(const Uuid& id, bool present);

        /**
         * @brief How many times this database has asked the operating system whether a file exists.
         *
         * Exposed so that "drawing performs no filesystem access proportional to the number of
         * assets" is something a **test** can assert rather than something a benchmark implies.
         * A number that only ever goes up, and whose *not* going up is the property under test.
         */
        [[nodiscard]] std::uint64_t getPresenceProbeCount() const { return presenceProbes_; }

        /**
         * @brief Whether moveAsset() would be allowed to move @p id to @p newRelativePath.
         *
         * The same rule, asked without doing anything: an undoable move has to know *before* it is
         * pushed onto the history whether it can succeed, because a command that lands in the undo
         * stack and then quietly does nothing is worse than one that was refused -- the user is
         * told it worked, and Ctrl+Z appears to do nothing too.
         *
         * @param id The asset.
         * @param newRelativePath Where it would go.
         * @param errorMessage Set when the answer is false. Optional.
         * @return True when the move would be attempted.
         */
        [[nodiscard]] bool canMoveAsset(const Uuid& id, const std::string& newRelativePath,
                                        std::string* errorMessage = nullptr) const;

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

        /**
         * @brief Points @p id at @p newRelativePath without touching the filesystem.
         *
         * `plan.md` STUDIO-09013. Unlike moveAsset(), this moves *nothing*: it is how a record
         * whose file went missing is pointed at the file again once it has turned up somewhere
         * else. The id does not change, so no scene is touched -- every reference that was broken
         * becomes correct, and every reference that was correct stays so.
         *
         * A sidecar is written at the new location, because that is what makes the repair survive
         * a restart: without one the next scan would give the file a fresh id and break every
         * reference again.
         *
         * Whether the *old* file is still there is deliberately not checked here -- undoing a
         * relink points the record back at a path that has nothing on it, and that is correct.
         * @ref RelinkAssetFileCommand is where that policy lives.
         *
         * @param id The asset.
         * @param newRelativePath Where its file actually is, project-relative.
         * @param errorMessage Set on failure. Optional.
         * @return False when the id is unknown, the path is empty or escapes the project, or
         *         another record already claims it.
         */
        bool repointAsset(const Uuid& id, const std::string& newRelativePath,
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

        /** @brief Stats @p path, counting the probe, and returns whether it is there. */
        [[nodiscard]] bool probe(const std::string& relativePath) const;

        /** @brief Sets @p record's presence, keeping @ref missingCount_ in step. */
        void setPresence(AssetRecord& record, bool present);

        std::string projectRoot_;
        std::unordered_map<Uuid, AssetRecord> recordsById_;
        std::map<std::string, Uuid> idsByPath_;

        /** @brief Adds or removes @p relativePath's folder from @ref folderCounts_. */
        void countPath(const std::string& relativePath, bool added);

        /** @brief How many records have `sourcePresent == false`, maintained incrementally. */
        std::size_t missingCount_ = 0;

        /** @brief Direct asset count per folder, maintained incrementally. See getFolderCounts(). */
        std::map<std::string, std::size_t> folderCounts_;

        /** @brief Count including descendants, per folder. See getTotalAssetCount(). */
        std::map<std::string, std::size_t> folderTotals_;

        /** @brief Counts filesystem presence checks, so a test can assert drawing makes none. */
        mutable std::uint64_t presenceProbes_ = 0;
    };
}
