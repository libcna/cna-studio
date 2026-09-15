// SPDX-License-Identifier: MS-PL
/**
 * @file StudioRecovery.cpp
 * @brief When a snapshot is written, when it is dropped, and what to do with one that was found.
 *
 * Moved here from `StudioApplication` unchanged in behaviour, so both Studio UIs run one flow
 * rather than two that agree today.
 */

#include "CNA/Studio/StudioRecovery.hpp"

#include "CNA/Studio/StudioContext.hpp"

#include <ctime>

namespace CNA::Studio
{
    StudioRecoverySession::StudioRecoverySession(StudioContext& context) : context_(context) {}

    void StudioRecoverySession::setDirectory(const std::string& directory)
    {
        if (directory.empty()) { return; }
        store_ = RecoveryStore{directory};
    }

    void StudioRecoverySession::reset()
    {
        recoverable_.reset();
        suspensionReported_ = false;
    }

    bool StudioRecoverySession::scan()
    {
        reset();

        // Not conditioned on the interval, deliberately -- and this is a change from what the Dear
        // ImGui host did. Turning autosave off stops new snapshots being *written*; it cannot mean
        // that one already on disk becomes unreachable. A user who turns autosave off after a crash
        // would otherwise have lost the work permanently, with the file sitting there.
        if (!context_.hasProject()) { return false; }

        recoverable_ = store_.findForProject(context_.getProject().getFilePath());
        if (!recoverable_) { return false; }

        // A warning, not information: the alternative reading of this state is that the user's
        // last session ended without saving, and either way there is work on disk that the
        // document in front of them does not contain.
        context_.log(LogSeverity::Warning,
                     "Unsaved changes to scene '" + recoverable_->sceneName + "' from "
                         + formatRecoveryTime(recoverable_->savedAtSeconds)
                         + " were found. File > Recover Unsaved Scene restores them; "
                           "File > Discard Recovered Scene throws them away.");
        return true;
    }

    bool StudioRecoverySession::recover()
    {
        if (!recoverable_) { return false; }

        const RecoverySnapshot snapshot = *recoverable_;

        const SceneLoadResult result =
            context_.getScene().loadFromJson(snapshot.scene, context_.getComponentRegistry());
        if (!result.succeeded)
        {
            // The snapshot stays. A recovery that failed is not a reason to delete the only copy
            // of the work it was holding.
            context_.log(LogSeverity::Error, "Cannot recover the scene: " + result.errorMessage);
            return false;
        }

        for (const std::string& warning : result.warnings)
        {
            context_.log(LogSeverity::Warning, "Recovered scene: " + warning);
        }

        context_.clearSelection();
        context_.getHistory().clear();

        // The recovered document was never saved, so no position in the fresh history is the file
        // on disk. Saying otherwise would let the user close the editor believing it was.
        context_.getHistory().markUnsaved();

        reset();

        context_.log(LogSeverity::Info,
                     "Recovered scene '" + context_.getScene().getName() + "' from "
                         + formatRecoveryTime(snapshot.savedAtSeconds)
                         + ". The file on disk is unchanged until you save. Undo history was not "
                           "recovered.");
        return true;
    }

    bool StudioRecoverySession::discard()
    {
        if (!recoverable_) { return false; }

        const std::string name = recoverable_->sceneName;
        store_.discard(recoverable_->sceneId);
        reset();

        context_.log(LogSeverity::Info, "Discarded the recovered copy of '" + name + "'.");
        return true;
    }

    void StudioRecoverySession::discardForCurrentScene()
    {
        store_.discard(context_.getScene().getSceneId());
        written_ = false;
    }

    void StudioRecoverySession::update(double deltaSeconds)
    {
        if (intervalSeconds_ <= 0.0) { return; }

        const SceneDocument& scene = context_.getScene();

        if (!context_.getHistory().isDirty())
        {
            // The document matches its file, so there is nothing a snapshot could rescue. Dropping
            // it here is what stops the next start-up offering a recovery of work already saved --
            // an offer that trains users to click "discard" without reading it.
            if (written_)
            {
                store_.discard(scene.getSceneId());
                written_ = false;
            }
            elapsedSeconds_ = 0.0;
            return;
        }

        elapsedSeconds_ += deltaSeconds;
        if (elapsedSeconds_ < intervalSeconds_) { return; }
        elapsedSeconds_ = 0.0;

        // Never write over work from a previous session that the user has not answered for yet.
        // The current session's unsaved seconds are worth less than the previous session's unsaved
        // hours, and the snapshot file is keyed by scene id, so this would overwrite it.
        if (recoverable_ && recoverable_->sceneId == scene.getSceneId())
        {
            if (!suspensionReported_)
            {
                suspensionReported_ = true;
                context_.log(LogSeverity::Warning,
                             "Autosave is suspended while recovered work from a previous session is "
                             "waiting. Recover it or discard it from the File menu.");
            }
            return;
        }

        RecoverySnapshot snapshot;
        snapshot.projectPath = context_.getProject().getFilePath();
        snapshot.scenePath = context_.getScenePath();
        snapshot.sceneName = scene.getName();
        snapshot.sceneId = scene.getSceneId();
        snapshot.savedAtSeconds = static_cast<std::int64_t>(std::time(nullptr));
        snapshot.scene = scene.toJson();

        std::string errorMessage;
        if (!store_.write(snapshot, &errorMessage))
        {
            // Said once. An editor that repeats a filesystem complaint every thirty seconds is one
            // whose console nobody reads.
            if (!failureReported_)
            {
                failureReported_ = true;
                context_.log(LogSeverity::Error,
                             "Cannot write a crash-recovery snapshot: " + errorMessage);
            }
            return;
        }

        written_ = true;
        failureReported_ = false;
    }
}
