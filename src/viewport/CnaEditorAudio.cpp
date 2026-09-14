// SPDX-License-Identifier: MS-PL
/**
 * @file CnaEditorAudio.cpp
 * @brief The CNA-backed audio preview.
 *
 * One clip at a time, loaded straight from the asset's own file through CNA's public
 * `SoundEffect(path)` constructor -- no content pipeline, no `.xnb`, and therefore nothing this
 * editor is forbidden from touching (D-01).
 */

#include "CNA/Editor/Viewport/EditorAudio.hpp"

#include <exception>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "CNA/Editor/Assets/AssetDatabase.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundState.hpp"

namespace CNA::Editor
{
    namespace
    {
        namespace XnaAudio = Microsoft::Xna::Framework::Audio;
    }

    /** @brief Plays one preview at a time through CNA. */
    class CnaEditorAudio final : public EditorAudio
    {
    public:
        explicit CnaEditorAudio(const AssetDatabase& assets) : assets_(&assets) {}

        [[nodiscard]] const char* getBackendName() const override { return "cna"; }

        bool play(const Uuid& assetId, float volume, float pitch, float pan) override
        {
            stop();

            const AssetRecord* record = assets_->find(assetId);
            if (record == nullptr) { return false; }

            XnaAudio::SoundEffect* effect = load(assetId, assets_->resolvePath(record->sourcePath));
            if (effect == nullptr) { return false; }

            try
            {
                // An *instance*, not SoundEffect::Play(). The fire-and-forget call hands back no
                // handle, so an editor using it can only ever change its own belief about what is
                // audible -- Stop would stop nothing, and a clip that ended would still be
                // reported as playing. An instance is the thing that can actually be stopped and
                // actually be asked.
                instance_ = std::make_unique<XnaAudio::SoundEffectInstance>(effect->CreateInstance());

                // XNA's own ranges, taken straight from the component being previewed, so what the
                // editor plays is what the game will play.
                instance_->setVolumeProperty(volume);
                instance_->setPitchProperty(pitch);
                instance_->setPanProperty(pan);
                instance_->Play();
                return true;
            }
            catch (const std::exception&)
            {
                // A device that refuses a clip is a report, not a crash: an editor that died
                // because a preview would not play would be worse than one that stays quiet.
                instance_.reset();
                return false;
            }
        }

        void stop() override
        {
            if (!instance_) { return; }

            try
            {
                instance_->Stop();
            }
            catch (const std::exception&)
            {
                // Nothing useful to do with a device that will not stop a clip, and dropping the
                // instance below is the strongest thing available anyway.
            }
            instance_.reset();
        }

        [[nodiscard]] bool isPlaying() const override
        {
            // Asked of the instance rather than remembered, so a clip that has simply finished
            // stops being reported as playing. A remembered flag would leave the panel showing a
            // Stop button for a sound that ended seconds ago.
            if (!instance_) { return false; }
            return instance_->getStateProperty() == XnaAudio::SoundState::Playing;
        }

    private:
        /** @brief Returns the loaded effect for @p assetId, loading it once. */
        XnaAudio::SoundEffect* load(const Uuid& assetId, const std::string& path)
        {
            if (const auto found = effects_.find(assetId); found != effects_.end())
            {
                return found->second.get();
            }
            if (failed_.count(assetId) > 0) { return nullptr; }

            try
            {
                auto effect = std::make_unique<XnaAudio::SoundEffect>(path);
                XnaAudio::SoundEffect* raw = effect.get();
                effects_.emplace(assetId, std::move(effect));
                return raw;
            }
            catch (const std::exception&)
            {
                // Remembered as failed, so a broken clip costs one attempt rather than one per
                // press -- the same rule the scene renderer's texture cache follows.
                failed_.insert(assetId);
                return nullptr;
            }
        }

        const AssetDatabase* assets_;
        std::unordered_map<Uuid, std::unique_ptr<XnaAudio::SoundEffect>> effects_;
        std::unordered_set<Uuid> failed_;

        /**
         * @brief The one preview that can be audible, or nothing.
         *
         * Replaced on every play(), which is what makes "one clip at a time" true rather than
         * merely intended: the previous instance is destroyed and its track goes with it.
         */
        std::unique_ptr<XnaAudio::SoundEffectInstance> instance_;
    };

    std::unique_ptr<EditorAudio> createCnaEditorAudio(const AssetDatabase& assets)
    {
        return std::make_unique<CnaEditorAudio>(assets);
    }
}
