// SPDX-License-Identifier: MS-PL
#include "CNA/Editor/Assets/AssetCommands.hpp"
#include <system_error>
#include <optional>
#include <iterator>
#include <fstream>
#include <filesystem>

namespace CNA::Editor
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
        if (newPath_.empty())
        {
            error_ = "destination path is empty";
            return;
        }

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

    bool SetImporterSettingCommand::mergeWith(const EditorCommand& newer)
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

namespace CNA::Editor
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

    bool SetMaterialCommand::mergeWith(const EditorCommand& newer)
    {
        const auto* other = dynamic_cast<const SetMaterialCommand*>(&newer);
        if (other == nullptr || other->absolutePath_ != absolutePath_) { return false; }

        // The newer value wins and this command keeps its *own* previousText_, so one Ctrl+Z goes
        // back to before the whole drag rather than to the middle of it.
        newMaterial_ = other->newMaterial_;
        return true;
    }
}
