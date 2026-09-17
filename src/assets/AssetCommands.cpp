// SPDX-License-Identifier: MS-PL
#include "CNA/Studio/Assets/AssetCommands.hpp"
#include <system_error>
#include <algorithm>
#include <cctype>
#include <utility>
#include <optional>
#include <iterator>
#include <fstream>
#include <filesystem>

namespace CNA::Studio
{
    MoveAssetCommand::MoveAssetCommand(AssetDatabase& assets, Uuid assetId, std::string newRelativePath)
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

        // Asked before the command is offered to the history rather than discovered at execute()
        // time. A move that lands in the undo stack and then quietly fails tells the user it
        // worked, and the Ctrl+Z that would put it right appears to do nothing either.
        if (!assets_->canMoveAsset(assetId_, newPath_, &error_)) { return; }

        valid_ = true;
    }

    void MoveAssetCommand::execute()
    {
        if (!valid_) { return; }
        if (!assets_->moveAsset(assetId_, newPath_, &error_))
        {
            // A move that fails leaves the asset exactly where it was, so the command is retired
            // rather than left in a state where undo would move something it never moved.
            valid_ = false;
        }
    }

    void MoveAssetCommand::undo()
    {
        if (!valid_) { return; }
        assets_->moveAsset(assetId_, oldPath_, &error_);
    }

    std::string MoveAssetCommand::getDescription() const
    {
        return "Move '" + oldPath_ + "' to '" + newPath_ + "'";
    }

    SetImporterSettingCommand::SetImporterSettingCommand(AssetDatabase& assets,
                                                         Uuid assetId,
                                                         std::string settingName,
                                                         PropertyValue newValue)
        : assets_(&assets),
          assetId_(assetId),
          settingName_(std::move(settingName)),
          newValue_(std::move(newValue))
    {
        const AssetRecord* record = assets_->find(assetId_);
        if (record == nullptr) { return; }

        // Absent and present-but-default are different states: the first must undo back to absent
        // so the sidecar does not grow a field the user never set.
        const JsonValue& stored = record->importerSettings[settingName_];
        if (!stored.isNull())
        {
            oldValue_ = PropertyValue::fromJson(stored, newValue_.getType());
            hadOldValue_ = true;
        }

        valid_ = true;
    }

    void SetImporterSettingCommand::apply(const PropertyValue& value) const
    {
        AssetRecord* record = assets_->findMutable(assetId_);
        if (record == nullptr) { return; }

        if (record->importerSettings.isNull()) { record->importerSettings = JsonValue::makeObject(); }
        record->importerSettings.set(settingName_, value.toJson());

        // Written through immediately. An import setting that lived only in memory would be lost
        // on the next scan, and the user would have no way to tell that from it having no effect.
        assets_->writeSidecar(assetId_);
    }

    void SetImporterSettingCommand::execute()
    {
        if (!valid_) { return; }
        apply(newValue_);
    }

    void SetImporterSettingCommand::undo()
    {
        if (!valid_) { return; }

        if (hadOldValue_)
        {
            apply(oldValue_);
            return;
        }

        // The setting was absent before, so undo removes it rather than writing a default. A
        // sidecar that accumulated every field the user ever glanced at would make every asset's
        // diff noise.
        if (AssetRecord* record = assets_->findMutable(assetId_); record != nullptr)
        {
            record->importerSettings.remove(settingName_);
            assets_->writeSidecar(assetId_);
        }
    }

    std::string SetImporterSettingCommand::getDescription() const
    {
        return "Set import setting '" + settingName_ + "'";
    }

    std::string SetImporterSettingCommand::getMergeKey() const
    {
        return "importer:" + assetId_.toString() + ":" + settingName_;
    }

    bool SetImporterSettingCommand::mergeWith(const StudioCommand& newer)
    {
        const auto* other = dynamic_cast<const SetImporterSettingCommand*>(&newer);
        if (other == nullptr || other->assetId_ != assetId_ || other->settingName_ != settingName_)
        {
            return false;
        }

        // Keep this command's original value (the undo target) and adopt the newer final one.
        newValue_ = other->newValue_;
        return true;
    }
}

namespace CNA::Studio
{
    namespace
    {
        /** @brief Reads a whole file, or returns nothing when it is not there. */
        std::optional<std::string> readWholeFile(const std::string& path)
        {
            std::ifstream stream{path, std::ios::binary};
            if (!stream) { return std::nullopt; }
            return std::string{std::istreambuf_iterator<char>{stream},
                               std::istreambuf_iterator<char>{}};
        }

        /** @brief Writes @p text over @p path, creating the directories above it. */
        bool writeWholeFile(const std::string& path, const std::string& text)
        {
            std::error_code error;
            const std::filesystem::path parent = std::filesystem::path{path}.parent_path();
            if (!parent.empty()) { std::filesystem::create_directories(parent, error); }

            std::ofstream stream{path, std::ios::binary | std::ios::trunc};
            if (!stream) { return false; }
            stream << text;
            return stream.good();
        }
    }

    SetMaterialCommand::SetMaterialCommand(std::string absolutePath, MaterialDocument material,
                                           std::string fieldName)
        : absolutePath_(std::move(absolutePath)), newMaterial_(std::move(material)),
          fieldName_(std::move(fieldName))
    {
        // Captured at construction rather than at execute(), so that a command built, executed,
        // undone and redone replays the same original bytes every time.
        if (const std::optional<std::string> existing = readWholeFile(absolutePath_))
        {
            previousText_ = *existing;
            existedBefore_ = true;
        }
    }

    void SetMaterialCommand::execute()
    {
        succeeded_ = writeWholeFile(absolutePath_, Json::write(newMaterial_.toJson(), true));
    }

    void SetMaterialCommand::undo()
    {
        // An edit is undone by putting the old bytes back; a *create* has no old bytes, and the
        // honest reversal there is to remove the file rather than to leave an empty one behind.
        if (!existedBefore_)
        {
            std::error_code error;
            std::filesystem::remove(absolutePath_, error);
            return;
        }

        writeWholeFile(absolutePath_, previousText_);
    }

    std::string SetMaterialCommand::getDescription() const
    {
        return existedBefore_ ? "Set material " + fieldName_ : "Create material";
    }

    std::string SetMaterialCommand::getMergeKey() const
    {
        // The path *and* the field: two materials edited in turn are two entries, and so are two
        // different fields of one material.
        return "material:" + absolutePath_ + ":" + fieldName_;
    }

    bool SetMaterialCommand::mergeWith(const StudioCommand& newer)
    {
        const auto* other = dynamic_cast<const SetMaterialCommand*>(&newer);
        if (other == nullptr || other->absolutePath_ != absolutePath_) { return false; }

        // The newer value wins and this command keeps its *own* previousText_, so one Ctrl+Z goes
        // back to before the whole drag rather than to the middle of it.
        newMaterial_ = other->newMaterial_;
        return true;
    }

    // --- Rename, duplicate and delete (STUDIO-09009) ---------------------------------------------

    namespace
    {
        /** @brief The index one past the last `/` in @p path, i.e. where its file name starts. */
        std::size_t fileNameStart(std::string_view path)
        {
            const std::size_t slash = path.find_last_of('/');
            return slash == std::string_view::npos ? 0 : slash + 1;
        }

        /** @brief @p name without its extension, and the extension, `.` included. */
        std::pair<std::string_view, std::string_view> splitExtension(std::string_view name)
        {
            // From the *last* dot, and never from a leading one: `.gitignore` is a name, not an
            // empty name with an extension, and a copy of it called ` 2.gitignore` is nonsense.
            const std::size_t dot = name.find_last_of('.');
            if (dot == std::string_view::npos || dot == 0) { return {name, {}}; }
            return {name.substr(0, dot), name.substr(dot)};
        }

        /** @brief Whether @p stem is a DOS device name Windows still refuses to create. */
        bool isReservedDeviceName(std::string_view stem)
        {
            std::string upper;
            upper.reserve(stem.size());
            for (const char c : stem)
            {
                upper.push_back(static_cast<char>(
                    std::toupper(static_cast<unsigned char>(c))));
            }

            static constexpr std::string_view kReserved[] = {
                "CON", "PRN", "AUX", "NUL",
                "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
                "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
            return std::find(std::begin(kReserved), std::end(kReserved), upper)
                != std::end(kReserved);
        }
    }

    std::string describeStudioAssetNameProblem(std::string_view name)
    {
        if (name.empty()) { return "A name cannot be empty."; }

        // Not a rename. A name with a separator in it is a user who meant to drag the file
        // somewhere, and silently turning it into a move would put the asset where they can no
        // longer see it.
        if (name.find('/') != std::string_view::npos || name.find('\\') != std::string_view::npos)
        {
            return "A name cannot contain a path separator.";
        }

        if (name == "." || name == "..") { return "'" + std::string{name} + "' is not a name."; }

        for (const char c : name)
        {
            if (static_cast<unsigned char>(c) < 0x20)
            {
                return "A name cannot contain control characters.";
            }

            // The strictest of the platforms a project might be checked out on, rather than the
            // host's own rules: a name Linux accepts and Windows refuses is a repository that one
            // colleague cannot clone, and the person who made it never finds out.
            if (std::string_view{"<>:\"|?*"}.find(c) != std::string_view::npos)
            {
                return std::string{"A name cannot contain "} + c + ".";
            }
        }

        if (name.back() == '.' || name.back() == ' ')
        {
            return "A name cannot end with a dot or a space.";
        }

        if (isReservedDeviceName(splitExtension(name).first))
        {
            return "'" + std::string{name} + "' is a reserved device name on Windows.";
        }

        return {};
    }

    std::string studioAssetPathRenamedTo(std::string_view path, std::string_view name)
    {
        return std::string{path.substr(0, fileNameStart(path))} + std::string{name};
    }

    std::string studioAssetUnusedPathLike(const AssetDatabase& assets, std::string_view path)
    {
        const std::size_t start = fileNameStart(path);
        const std::string_view folder = path.substr(0, start);
        const auto [stem, extension] = splitExtension(path.substr(start));

        // " 2" rather than " copy": the second duplicate of a file is "Crate 3", where the second
        // "Crate copy" would have to become "Crate copy copy", which is what every file manager
        // that chose the word ends up with.
        for (int suffix = 2; suffix < 10000; ++suffix)
        {
            std::string candidate = std::string{folder} + std::string{stem} + " "
                                  + std::to_string(suffix) + std::string{extension};

            // Both the database *and* the disk. An untracked file in the assets folder -- one a
            // scan has not seen yet -- is still a file the copy would overwrite.
            if (assets.findByPath(candidate) == nullptr)
            {
                std::error_code error;
                if (!std::filesystem::exists(assets.resolvePath(candidate), error))
                {
                    return candidate;
                }
            }
        }

        return {};
    }

    std::unique_ptr<StudioCommand> studioMoveFolderCommand(AssetDatabase& assets,
                                                            std::string_view folder,
                                                            std::string_view newFolder,
                                                            std::string* error)
    {
        const auto fail = [error](std::string text) -> std::unique_ptr<StudioCommand> {
            if (error != nullptr) { *error = std::move(text); }
            return nullptr;
        };

        if (folder.empty()) { return fail("the project root is not a folder that can be moved"); }
        if (newFolder.empty()) { return fail("a folder cannot be moved to the project root"); }
        if (folder == newFolder) { return fail("the folder is already there"); }

        // Moving a folder into itself would rename its own prefix out from under the loop below and
        // produce paths that nest forever. The check is on `folder + "/"` so that `Assets2` is not
        // mistaken for a child of `Assets`.
        if (newFolder.substr(0, folder.size()) == folder
            && newFolder.size() > folder.size() && newFolder[folder.size()] == '/')
        {
            return fail("a folder cannot be moved inside itself");
        }

        // Every segment of the destination, not just the last: a folder renamed to something
        // Windows refuses is a repository one colleague cannot clone, and the same rules that
        // apply to a file name apply to the directory holding it.
        for (std::size_t start = 0; start <= newFolder.size();)
        {
            const std::size_t slash = newFolder.find('/', start);
            const std::string_view name = slash == std::string_view::npos
                ? newFolder.substr(start)
                : newFolder.substr(start, slash - start);

            if (const std::string problem = describeStudioAssetNameProblem(name); !problem.empty())
            {
                return fail(problem);
            }
            if (slash == std::string_view::npos) { break; }
            start = slash + 1;
        }

        const std::string prefix = std::string{folder} + "/";
        auto composite = std::make_unique<CompositeCommand>(
            "Move '" + std::string{folder} + "' to '" + std::string{newFolder} + "'");

        for (const AssetRecord* record : assets.getAll())
        {
            if (record->sourcePath.rfind(prefix, 0) != 0) { continue; }

            std::string destination =
                std::string{newFolder} + "/" + record->sourcePath.substr(prefix.size());

            auto move = std::make_unique<MoveAssetCommand>(assets, record->id,
                                                           std::move(destination));
            if (!move->isValid()) { return fail(move->getError()); }
            composite->add(std::move(move));
        }

        if (composite->isEmpty()) { return fail("'" + std::string{folder} + "' holds nothing"); }
        return composite;
    }

    DuplicateAssetCommand::DuplicateAssetCommand(AssetDatabase& assets, Uuid assetId)
        : assets_(&assets), sourceId_(assetId)
    {
        const AssetRecord* record = assets_->find(sourceId_);
        if (record == nullptr)
        {
            error_ = "no asset with that id";
            return;
        }

        sourcePath_ = record->sourcePath;
        copyPath_ = studioAssetUnusedPathLike(*assets_, sourcePath_);
        if (copyPath_.empty())
        {
            error_ = "no free name beside '" + sourcePath_ + "'";
            return;
        }

        std::error_code error;
        if (!std::filesystem::exists(assets_->resolvePath(sourcePath_), error))
        {
            // A missing source is a tracked asset whose file has gone, which the browser already
            // marks. Copying nothing would produce an empty file with a fresh id -- an asset that
            // looks real and is not.
            error_ = "'" + sourcePath_ + "' is not on disk";
            return;
        }

        // Once, here, rather than in execute(): undo-then-redo must restore *the same* copy. A
        // redo that minted a fresh id would leave every reference the user had since made to the
        // copy pointing at nothing.
        copyId_ = Uuid::generate();
        valid_ = true;
    }

    void DuplicateAssetCommand::execute()
    {
        if (!valid_) { return; }

        const std::optional<std::string> bytes =
            readWholeFile(assets_->resolvePath(sourcePath_));
        if (!bytes.has_value())
        {
            error_ = "cannot read '" + sourcePath_ + "'";
            valid_ = false;
            return;
        }

        if (!writeWholeFile(assets_->resolvePath(copyPath_), *bytes))
        {
            error_ = "cannot write '" + copyPath_ + "'";
            valid_ = false;
            return;
        }

        const AssetRecord* source = assets_->find(sourceId_);

        AssetRecord copy;
        copy.id = copyId_;
        copy.sourcePath = copyPath_;
        if (source != nullptr)
        {
            // Type, importer and its settings come across. A duplicate that reverted to the
            // importer defaults would be a copy the user has to configure again, and the
            // difference would show up only at build time.
            copy.type = source->type;
            copy.importerId = source->importerId;
            copy.importerSettings = source->importerSettings;
            copy.dependencies = source->dependencies;
            copy.sourceSize = source->sourceSize;
            copy.sourceModifiedTime = source->sourceModifiedTime;
        }
        else
        {
            copy.type = AssetDatabase::guessTypeFromExtension(copyPath_);
            copy.importerId = AssetDatabase::defaultImporterFor(copy.type);
        }

        assets_->add(std::move(copy));
        assets_->writeSidecar(copyId_, &error_);
    }

    void DuplicateAssetCommand::undo()
    {
        if (!valid_) { return; }

        std::error_code error;
        std::filesystem::remove(assets_->resolvePath(copyPath_), error);
        std::filesystem::remove(assets_->resolvePath(copyPath_) + AssetDatabase::kSidecarExtension,
                                error);

        // The one case removeRecord() exists for: the editor itself wrote this asset, the file is
        // going away with it, and nothing ever referenced it.
        assets_->removeRecord(copyId_);
    }

    std::string DuplicateAssetCommand::getDescription() const
    {
        return "Duplicate '" + sourcePath_ + "'";
    }

    DeleteAssetCommand::DeleteAssetCommand(AssetDatabase& assets, Uuid assetId) : assets_(&assets)
    {
        const AssetRecord* record = assets_->find(assetId);
        if (record == nullptr)
        {
            error_ = "no asset with that id";
            return;
        }

        record_ = *record;

        // Captured before anything is removed. A file that cannot be read produces an invalid
        // command rather than a deletion whose undo would be a menu entry that lies.
        const std::string absolute = assets_->resolvePath(record_.sourcePath);
        const std::optional<std::string> bytes = readWholeFile(absolute);
        if (!bytes.has_value())
        {
            error_ = "cannot read '" + record_.sourcePath + "'";
            return;
        }
        contents_ = *bytes;

        if (const std::optional<std::string> sidecar =
                readWholeFile(absolute + AssetDatabase::kSidecarExtension))
        {
            // Verbatim rather than regenerated, so a sidecar someone hand-edited -- or one written
            // by a newer build carrying fields this one does not understand -- comes back as it was.
            sidecar_ = *sidecar;
            hadSidecar_ = true;
        }

        valid_ = true;
    }

    void DeleteAssetCommand::execute()
    {
        if (!valid_) { return; }

        const std::string absolute = assets_->resolvePath(record_.sourcePath);
        std::error_code error;
        std::filesystem::remove(absolute, error);
        std::filesystem::remove(absolute + AssetDatabase::kSidecarExtension, error);

        // The record goes too, so the asset stops being listed rather than becoming a missing one.
        // A user who deletes a file and is then shown it in red has not been told the delete
        // worked; they have been shown a problem they just caused and cannot tell apart from one
        // they did not.
        assets_->removeRecord(record_.id);
    }

    void DeleteAssetCommand::undo()
    {
        if (!valid_) { return; }

        const std::string absolute = assets_->resolvePath(record_.sourcePath);
        if (!writeWholeFile(absolute, contents_))
        {
            error_ = "cannot restore '" + record_.sourcePath + "'";
            return;
        }

        // The same id it had. Every scene that referenced this asset was left untouched by the
        // delete -- its reference dangled -- so restoring under a new id would break exactly the
        // references the undo is meant to repair.
        assets_->add(record_);

        if (hadSidecar_) { writeWholeFile(absolute + AssetDatabase::kSidecarExtension, sidecar_); }
        else { assets_->writeSidecar(record_.id, &error_); }
    }

    std::string DeleteAssetCommand::getDescription() const
    {
        return "Delete '" + record_.sourcePath + "'";
    }
}
