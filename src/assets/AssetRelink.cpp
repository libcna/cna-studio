// SPDX-License-Identifier: MS-PL
#include "CNA/Studio/Assets/AssetRelink.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <utility>

namespace CNA::Studio
{
    namespace
    {
        /** @brief @p text lower-cased, ASCII only, which is what a file-name comparison needs. */
        std::string lowered(std::string_view text)
        {
            std::string out;
            out.reserve(text.size());
            for (const char character : text)
            {
                out.push_back(static_cast<char>(
                    std::tolower(static_cast<unsigned char>(character))));
            }
            return out;
        }

        /** @brief The last segment of a project-relative path. */
        std::string_view fileNameOf(std::string_view path)
        {
            const std::size_t slash = path.find_last_of('/');
            return slash == std::string_view::npos ? path : path.substr(slash + 1);
        }

        /** @brief The part of a file name before its last dot, or all of it. */
        std::string_view stemOf(std::string_view name)
        {
            const std::size_t dot = name.find_last_of('.');
            return dot == std::string_view::npos || dot == 0 ? name : name.substr(0, dot);
        }

        /** @brief How good a match @p candidateName is for @p wantedName; -1 is no match. */
        int rankOf(std::string_view wantedName, std::string_view candidateName)
        {
            if (candidateName == wantedName) { return 0; }

            // The extension decides the asset's type on the next scan, so a `.png` offered in place
            // of a `.wav` is not a candidate however similar the names are. A same-stem match is
            // still offered, but below every exact one.
            if (stemOf(candidateName) == stemOf(wantedName)) { return 1; }
            return -1;
        }
    }

    std::vector<RelinkCandidate> studioRelinkCandidates(const AssetDatabase& assets,
                                                        const Uuid& missingId, std::size_t limit)
    {
        const AssetRecord* missing = assets.find(missingId);

        // An id the database does not know has no name to search on. That is the *other* kind of
        // broken reference -- a scene pointing at an asset that was never imported here -- and its
        // repair is dropping the right asset onto the row, which needs no suggestion from a name
        // nobody has.
        if (missing == nullptr) { return {}; }

        const std::string wantedName = lowered(fileNameOf(missing->sourcePath));
        if (wantedName.empty()) { return {}; }

        struct Ranked
        {
            int rank;
            RelinkCandidate candidate;
        };
        std::vector<Ranked> found;

        // --- Files on disk that nothing tracks ---------------------------------------------------
        //
        // First, because pointing the record back at its file touches no scene, and relinking
        // references edits every scene that used it. An untracked file is always the better answer
        // when there is one.
        const std::string& root = assets.getProjectRoot();
        if (!root.empty())
        {
            std::error_code error;
            std::filesystem::recursive_directory_iterator walk{
                root, std::filesystem::directory_options::skip_permission_denied, error};

            if (!error)
            {
                const std::filesystem::path rootPath{root};
                for (const std::filesystem::directory_entry& entry : walk)
                {
                    if (!entry.is_regular_file(error)) { continue; }

                    const std::string relative =
                        entry.path().lexically_relative(rootPath).generic_string();
                    if (relative.empty() || relative.rfind("..", 0) == 0) { continue; }

                    // A sidecar is metadata, not an asset. Offering one as a candidate would point
                    // the record at the file describing it.
                    if (relative.size() > std::string_view{AssetDatabase::kSidecarExtension}.size()
                        && relative.compare(
                               relative.size()
                                   - std::string_view{AssetDatabase::kSidecarExtension}.size(),
                               std::string_view{AssetDatabase::kSidecarExtension}.size(),
                               AssetDatabase::kSidecarExtension)
                               == 0)
                    {
                        continue;
                    }

                    if (assets.findByPath(relative) != nullptr) { continue; }

                    const int rank = rankOf(wantedName, lowered(fileNameOf(relative)));
                    if (rank < 0) { continue; }

                    RelinkCandidate candidate;
                    candidate.kind = RelinkCandidate::Kind::UntrackedFile;
                    candidate.path = relative;
                    candidate.reason = rank == 0 ? "same name" : "same name, different extension";
                    found.push_back(Ranked{rank, std::move(candidate)});
                }
            }
        }

        // --- Tracked assets that look like the one that went -------------------------------------
        //
        // Ranked below every untracked file, because this repair rewrites scenes. It is the right
        // one when a scan has already given the returned file a new id: two records exist, and the
        // scenes are pointing at the one whose file is gone.
        for (const AssetRecord* record : assets.getAll())
        {
            if (record->id == missingId) { continue; }
            if (assets.isMissing(record->id)) { continue; }

            const int rank = rankOf(wantedName, lowered(fileNameOf(record->sourcePath)));
            if (rank < 0) { continue; }

            RelinkCandidate candidate;
            candidate.kind = RelinkCandidate::Kind::TrackedAsset;
            candidate.path = record->sourcePath;
            candidate.assetId = record->id;
            candidate.reason = record->type == missing->type ? "same name and kind" : "same name";
            found.push_back(Ranked{rank + 2, std::move(candidate)});
        }

        std::stable_sort(found.begin(), found.end(), [](const Ranked& a, const Ranked& b) {
            if (a.rank != b.rank) { return a.rank < b.rank; }

            // Alphabetical within a rank, so the list a user reads is the same one twice. The walk
            // order of a directory is not something to show anybody.
            return a.candidate.path < b.candidate.path;
        });

        std::vector<RelinkCandidate> candidates;
        candidates.reserve(std::min(limit, found.size()));
        for (Ranked& entry : found)
        {
            if (candidates.size() >= limit) { break; }
            candidates.push_back(std::move(entry.candidate));
        }
        return candidates;
    }

    RelinkAssetFileCommand::RelinkAssetFileCommand(AssetDatabase& assets, Uuid assetId,
                                                   std::string newRelativePath)
        : assets_(&assets), assetId_(assetId), newPath_(std::move(newRelativePath))
    {
        const AssetRecord* record = assets_->find(assetId_);
        if (record == nullptr)
        {
            error_ = "no asset with that id";
            return;
        }

        oldPath_ = record->sourcePath;
        if (oldPath_ == newPath_)
        {
            error_ = "the asset is already there";
            return;
        }

        if (!assets_->isMissing(assetId_))
        {
            // The file is where the record says it is, so this is a *move*, and moving a file is
            // MoveAssetCommand's job. Repointing here would leave the original behind to be given a
            // fresh id by the next scan -- one file becoming two assets, silently.
            error_ = "'" + oldPath_ + "' is still on disk; move it instead";
            return;
        }

        if (assets_->findByPath(newPath_) != nullptr)
        {
            // Already tracked, so the repair is to relink the *references* to that asset rather
            // than to give one file two records.
            error_ = "'" + newPath_ + "' is already tracked";
            return;
        }

        std::error_code error;
        if (!std::filesystem::exists(assets_->resolvePath(newPath_), error))
        {
            error_ = "'" + newPath_ + "' is not on disk";
            return;
        }

        valid_ = true;
    }

    void RelinkAssetFileCommand::execute()
    {
        if (!valid_) { return; }
        if (!assets_->repointAsset(assetId_, newPath_, &error_)) { valid_ = false; }
    }

    void RelinkAssetFileCommand::undo()
    {
        if (!valid_) { return; }

        // Back to a path with nothing on it, which is exactly right: undoing a relink restores the
        // state the user had, and that state was an asset whose file was missing.
        assets_->repointAsset(assetId_, oldPath_, &error_);
    }

    std::string RelinkAssetFileCommand::getDescription() const
    {
        return "Relink '" + oldPath_ + "' to '" + newPath_ + "'";
    }
}
