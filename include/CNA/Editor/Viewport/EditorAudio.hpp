// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file CNA/Editor/Viewport/EditorAudio.hpp
 * @brief Hearing a clip without leaving the editor.
 *
 * plan.md ED-304. The abstraction exists for the same reason `EditorViewport` does: playing a
 * sound needs CNA, exactly one module may link CNA (ANALYSIS.md decision D-03), and every panel
 * has to keep working in a headless run where there is no audio device at all.
 *
 * It lives beside the viewport rather than in a module of its own because that module *is* "the
 * one that links CNA" -- the name is narrower than the job, and inventing a second CNA-linking
 * module to widen it would trade a slightly awkward name for the one rule the build graph enforces.
 *
 * Deliberately tiny. The editor is not a mixer: it plays one clip at a time so an artist can hear
 * what they just imported, and anything richer belongs to the game rather than to the tool.
 */

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "CNA/Editor/Core/Uuid.hpp"

namespace CNA::Editor
{
    class AssetDatabase;

    /**
     * @brief Plays one asset at a time, for previewing.
     *
     * Volume, pitch and pan are XNA's own ranges, taken straight from the component being
     * previewed, so what the editor plays is what the game will play.
     */
    class EditorAudio
    {
    public:
        virtual ~EditorAudio() = default;

        /** @brief Returns a short name for the bound implementation, e.g. "cna" or "null". */
        [[nodiscard]] virtual const char* getBackendName() const = 0;

        /**
         * @brief Plays @p assetId once, replacing whatever was playing.
         *
         * @param volume 0..1, @param pitch -1..1 in octaves, @param pan -1..1 left to right --
         *        XNA's `SoundEffect::Play` parameters exactly.
         * @return False when the asset is unknown or its file will not load, which the caller
         *         reports; an editor that silently did nothing would be indistinguishable from a
         *         clip of silence.
         */
        virtual bool play(const Uuid& assetId, float volume, float pitch, float pan) = 0;

        /**
         * @brief Stops whatever is playing. Safe when nothing is.
         *
         * Actually stops it: the CNA implementation holds a `SoundEffectInstance` rather than using
         * the fire-and-forget `SoundEffect::Play()`, which hands back no handle and would leave
         * this able to change nothing but the editor's own belief about what is audible.
         */
        virtual void stop() = 0;

        /**
         * @brief Returns true while a preview is audible.
         *
         * A question about the *device*, not a remembered flag: a clip that has simply reached its
         * end is no longer playing, and a panel offering to stop a sound that finished seconds ago
         * is offering something that cannot happen.
         */
        [[nodiscard]] virtual bool isPlaying() const = 0;
    };

    /**
     * @brief The headless implementation: records what was asked for and plays nothing.
     *
     * Not a stub for tests alone -- `--headless` uses it, so the panel code that offers a preview
     * runs identically with and without an audio device, and there is no separate path to drift.
     */
    class NullEditorAudio final : public EditorAudio
    {
    public:
        /** @brief One recorded request. */
        struct Request
        {
            Uuid assetId;
            float volume = 1.0f;
            float pitch = 0.0f;
            float pan = 0.0f;
        };

        [[nodiscard]] const char* getBackendName() const override { return "null"; }

        bool play(const Uuid& assetId, float volume, float pitch, float pan) override
        {
            requests_.push_back(Request{assetId, volume, pitch, pan});
            playing_ = assetId.isValid();
            return playing_;
        }

        void stop() override { playing_ = false; }

        [[nodiscard]] bool isPlaying() const override { return playing_; }

        /** @brief Returns every play() this instance has been asked for, in order. */
        [[nodiscard]] const std::vector<Request>& getRequests() const { return requests_; }

    private:
        std::vector<Request> requests_;
        bool playing_ = false;
    };

    /**
     * @brief Creates the CNA-backed audio preview.
     *
     * @param assets Where a clip's file is resolved from; must outlive the returned object.
     */
    [[nodiscard]] std::unique_ptr<EditorAudio> createCnaEditorAudio(const AssetDatabase& assets);
}
