// SPDX-License-Identifier: MS-PL
/**
 * @file StudioShellPanels.cpp
 * @brief Binding every ported panel to a shell.
 */

#include "CNA/Studio/ShellPanels/StudioShellPanels.hpp"

#include "CNA/Studio/ShellPanels/StudioPluginMenus.hpp"

#include "CNA/Studio/Assets/AssetDatabase.hpp"
#include "CNA/Studio/Assets/AssetShortcuts.hpp"
#include "CNA/Studio/Project/Project.hpp"
#include "CNA/Studio/Project/ProjectTemplate.hpp"

#include <ctime>
#include "CNA/Studio/Project/ProjectValidation.hpp"
#include "CNA/Studio/Project/StudioReveal.hpp"
#include "CNA/Studio/Project/RecentProjects.hpp"
#include "CNA/Studio/Scene/AssetDrop.hpp"
#include "CNA/Studio/Scene/PrefabCommands.hpp"
#include "CNA/Studio/Scene/SceneCommands.hpp"
#include "CNA/Studio/Scene/SceneDocument.hpp"
#include "CNA/Studio/ShellPanels/StudioComparisonPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioComparisonService.hpp"
#include "CNA/Studio/ShellPanels/StudioContentBrowser.hpp"
#include "CNA/Studio/ShellPanels/StudioDetailsPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioDiagnosticsPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioHistoryPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioLayersPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioOutlinerPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioPreferencesPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioProblemsPanel.hpp"
#include "CNA/Studio/ShellPanels/StudioViewportPanel.hpp"
#include "CNA/Studio/StudioAssetReload.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/UiCore/StudioLogPanel.hpp"
#include "CNA/Studio/UiCore/StudioWidgets.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace CNA::Studio
{
    StudioShellPanels::StudioShellPanels(StudioShell& shell, StudioContext& context, StudioLog& log,
                                         StudioShellPanelServices services)
        : shell_(&shell), context_(context), log_(log), services_(std::move(services)),
          build_(context, log, [this](StudioNotification notification) {
              notify(std::move(notification));
          }),
          // The sink rather than a direct call on the shell, so the service needs no shell to be
          // constructed and a test can read what it raised without one.
          play_(context, log, [this](StudioNotification notification) {
              notify(std::move(notification));
          }),
          // The builds arrive as a provider rather than as a copy or as the play service itself:
          // the list changes when Studio rescans, and handing over the play service would hand
          // over the player process and the input bridge with it.
          comparison_(context, log,
                      [this](StudioNotification notification) { notify(std::move(notification)); },
                      [this]() -> const std::vector<PlayerBuild>& { return play_.builds(); },
                      services_.readImage, services_.writeImage),
          // The theme goes out through a sink for the reason every other outcome here does: the
          // service needs no shell to be constructed, and a test reads what was applied.
          preferences_(log, [this](StudioTheme theme) {
              if (shell_ != nullptr) { shell_->setTheme(std::move(theme)); }
          })
    {
        // Every command that lands, mirrored to a running game exactly as it happens to the
        // document -- an inspector edit, a gizmo drag, an undo -- so Play shows what the editor
        // shows without every editing surface having to know a player might be listening.
        context_.setCommandObserver([this](const StudioCommand& command) {
            mirrorCommandToPlayer(command);

            // Any command may have changed a reference -- retargeting a sprite, deleting an
            // entity, moving an asset. A flag rather than a rebuild: the answer is only looked at
            // when an asset is inspected, and rebuilding on every gizmo drag would read every
            // scene in the project sixty times a second.
            dependenciesStale_ = true;

            // And any command may have written an asset *file*: a material edit, a prefab Apply.
            // Dropping the whole cache is coarse and correct; keeping a document Studio has just
            // overwritten would show the user the version before their own edit.
            documents_.invalidate();
        });

        buildPanel_ = std::make_unique<StudioBuildPanel>(context_, build_.process());

        // A toast is ephemeral by design, which makes it the wrong place to keep anything. Wired
        // here because this is where the shell and the log meet: the shell is CNA-free and knows
        // nothing about a log, and every caller remembering to do both is every caller eventually
        // not doing both.
        shell.notifications().setLog(&log_);

        bind(shell);
    }

    bool StudioShellPanels::placeDroppedAsset(const Uuid& assetId, const StudioVector3& position,
                                             const Uuid& parentId)
    {
        const AssetRecord* record = context_.getAssets().find(assetId);
        if (record == nullptr) { return false; }

        // A prefab is a subtree with a link back to the asset, so it is a different command rather
        // than a different branch of the same one.
        if (studioAssetDropKind(record->type) == StudioAssetDropKind::Prefab)
        {
            const PrefabDocument* prefab =
                documents_.prefab(context_.getAssets(), assetId, context_.getComponentRegistry());
            if (prefab == nullptr)
            {
                log_.append(LogSeverity::Warning,
                            "Could not read '" + record->sourcePath + "'.");
                return false;
            }

            auto command = std::make_unique<InstantiatePrefabCommand>(
                context_.getScene(), *prefab, assetId, parentId);
            if (!command->isValid())
            {
                log_.append(LogSeverity::Warning,
                            "'" + record->sourcePath + "' has nothing in it to place.");
                return false;
            }

            const Uuid rootId = command->getRootId();
            context_.execute(std::move(command));
            context_.select(rootId);

            // Positioned after it exists, and only when asked to: the hierarchy drops at no
            // particular place, and moving a prefab instance to the origin because nobody said
            // otherwise would be an edit nobody asked for.
            log_.append(LogSeverity::Info, "Placed '" + record->sourcePath + "'.  Undo with Ctrl+Z.");
            return true;
        }

        StudioEntity entity;
        if (!studioEntityForAsset(context_.getAssets(), assetId, context_.getComponentRegistry(),
                                  position, entity))
        {
            // Said rather than silent. A drag that is refused with no explanation is one the user
            // repeats, and then repeats more slowly.
            log_.append(LogSeverity::Warning, describeStudioAssetDropRefusal(*record));
            return false;
        }

        auto command = std::make_unique<CreateEntityCommand>(context_.getScene(), std::move(entity));
        const Uuid created = command->getEntityId();
        context_.execute(std::move(command));

        // Selected, so the next thing the user does happens to what they just placed -- which is
        // almost always moving it.
        context_.select(created);
        log_.append(LogSeverity::Info, "Placed '" + record->sourcePath + "'.  Undo with Ctrl+Z.");
        return true;
    }

    const AssetDependencyIndex* StudioShellPanels::dependencyIndex()
    {
        if (dependenciesStale_)
        {
            const AssetDependencyScan scan =
                dependencies_.build(context_.getAssets(), context_.getComponentRegistry());

            // Each file that could not be read, once. A project with one broken prefab still has a
            // useful answer for every other asset in it, and an index that said nothing about the
            // broken one would make the missing edges look like an asset nothing references.
            for (const std::string& warning : scan.warnings)
            {
                log_.append(LogSeverity::Warning, "Dependency index: " + warning + ".");
            }

            dependenciesStale_ = false;
        }

        // The open scene last, and every time: it may hold unsaved edits, and a dependency view
        // built only from files is right until the user changes something -- which is exactly when
        // they ask. It walks one document rather than the project, so it is cheap enough to redo.
        //
        // The context holds the scene's path as it was opened, which is absolute; the database
        // speaks in project-relative paths. A scene outside the project root comes back starting
        // with "..", which matches nothing -- which is the right answer for a file the project does
        // not contain.
        if (!context_.getScenePath().empty() && !context_.getAssets().getProjectRoot().empty())
        {
            const std::string relative =
                std::filesystem::path{context_.getScenePath()}
                    .lexically_relative(context_.getAssets().getProjectRoot())
                    .generic_string();

            if (const AssetRecord* scene = context_.getAssets().findByPath(relative);
                scene != nullptr)
            {
                dependencies_.observeScene(context_.getScene(), scene->id, scene->sourcePath);
            }
        }

        return &dependencies_;
    }

    void StudioShellPanels::setPlayerBuilds(std::vector<PlayerBuild> builds)
    {
        play_.setBuilds(std::move(builds));
        // The Diagnostics panel reports the same list. One setter, so the two cannot disagree
        // about what this Studio can run -- and this is the half that is not the play service's,
        // which is why setting the builds is still a method here rather than only on it.
        diagnostics_.players = play_.builds();
    }

    void StudioShellPanels::poll(double nowSeconds)
    {
        pollPlugins();
        pollRecovery(nowSeconds);
        // Once. It was called twice, which was harmless only because the service reports a
        // *transition* and the second call always saw the state it had just recorded -- a duplicate
        // that survived because the thing it duplicated is idempotent.
        (void)build_.poll();
        counts_.playerMessages += play_.poll();
        (void)comparison_.poll(nowSeconds);
        pollInteractionEnd();
        pollAssetChanges(nowSeconds);

        // Thumbnails for whatever the browser last drew, and cancellation for whatever it has
        // scrolled past (STUDIO-09003). Budgeted for the same reason the drain below is: one poll
        // must not turn a folder into a queue, and the next poll is a sixtieth of a second away.
        // The host is told when a thumbnail is dropped, so its textures are bounded by the cache
        // rather than by everything the user has ever scrolled past (STUDIO-35041). Installed here
        // rather than at construction because the services arrive afterwards.
        if (services_.releaseThumbnail && !thumbnailReleaseInstalled_)
        {
            thumbnails_.setOnDropped(services_.releaseThumbnail);
            thumbnailReleaseInstalled_ = true;
        }

        (void)thumbnails_.pump(jobs_, context_.getAssets(), 4);

        // Re-reading what the watcher saw change, off the frame (STUDIO-10011). Pumped after the
        // poll that queued it, so a file saved this frame is submitted this frame rather than
        // waiting for the next one.
        (void)imports_.pump(jobs_, context_.getAssets());

        // Taken rather than read, so each failure is reported once: a loop that read the list every
        // frame would write the same broken texture to the log sixty times a second
        // (`plan.md` STUDIO-10013). A worker cannot log -- it is given a job context and nothing
        // else -- so the reason travels back on the queue and is said here.
        for (const StudioImportFailure& failure : imports_.takeFailures())
        {
            log_.append(LogSeverity::Warning,
                        "Could not import '" + failure.sourcePath + "': " + failure.reason + ".");
        }

        // The one crossing background work makes into the document (STUDIO-30001). Budgeted, so a
        // burst of jobs finishing together is spread over frames rather than producing one long
        // one -- the last step of background work staying background.
        (void)jobs_.drain(8);

        publishStatus();
    }

    void StudioShellPanels::loadAssetShortcuts()
    {
        shortcutsPath_ = StudioAssetShortcutStore::defaultPathFor(context_.getProject().getFilePath());
        contentState_.shortcuts = StudioAssetShortcutStore{shortcutsPath_}.load();

        // Pruned on load, so a project somebody tidied elsewhere does not open with a list of rows
        // that cannot be clicked -- and so the file stops carrying them.
        if (contentState_.shortcuts.prune(context_.getAssets()) > 0) { saveAssetShortcuts(); }
    }

    void StudioShellPanels::saveAssetShortcuts()
    {
        if (shortcutsPath_.empty()) { return; }

        std::string problem;
        if (!StudioAssetShortcutStore{shortcutsPath_}.save(contentState_.shortcuts, &problem))
        {
            // Trace rather than a warning: this is a convenience file, and a user whose home
            // directory is read-only does not need a red line every time they click an asset.
            log_.append(LogSeverity::Trace, "Could not keep the asset shortcuts: " + problem + ".");
        }
    }

    void StudioShellPanels::pollAssetChanges(double nowSeconds)
    {
        // A delta from a monotonic clock, because that is what paces the watcher and what a test
        // drives. The first call produces zero rather than "everything since the epoch", which
        // would make the first frame of a session poll whether it was due or not.
        const double delta = lastPollSeconds_ < 0.0 ? 0.0
                                                    : std::max(0.0, nowSeconds - lastPollSeconds_);
        lastPollSeconds_ = nowSeconds;

        StudioAssetReloadSinks sinks;
        if (services_.invalidateRenderedAsset)
        {
            sinks.invalidateRendered = services_.invalidateRenderedAsset;
        }
        sinks.reloadInPlayer = [this](const Uuid& assetId) { (void)play_.reloadAsset(assetId); };

        const StudioAssetReloadResult reload = studioPollAssetChanges(watcher_, context_, sinks, delta);

        // The reload reports which assets went stale; queueing the re-read is this layer's job,
        // because this is the layer that owns the job system. Reading them where the watcher
        // noticed would put a glTF parse back on the frame, which is what STUDIO-10011 took off it.
        if (!reload.needsReimport.empty()) { (void)imports_.request(reload.needsReimport); }
    }

    void StudioShellPanels::notify(StudioNotification notification)
    {
        if (shell_ != nullptr)
        {
            shell_->notifications().post(std::move(notification));
            return;
        }

        // No shell to show it on -- a preview, or a test holding the panels alone. The message is
        // still the point, so it goes to the log the centre would have written it to.
        std::string line = notification.title;
        if (!notification.detail.empty()) { line += " -- " + notification.detail; }
        log_.append(studioNotificationLogSeverity(notification.severity), line);
    }

    void StudioShellPanels::mirrorCommandToPlayer(const StudioCommand& command)
    {
        // A dynamic_cast rather than a virtual on StudioCommand: asking a command to describe
        // itself in protocol terms would put the wire format into cna-studio-scene, which links
        // neither the bridge nor anything that knows a player exists.
        const auto* setProperty = dynamic_cast<const SetPropertyCommand*>(&command);
        if (setProperty == nullptr) { return; }

        // The document's own value, not the command's: after an undo the live value is the old
        // one, and the document is the only thing that is right in both directions.
        const StudioEntity* entity = context_.getScene().findEntity(setProperty->getEntityId());
        if (entity == nullptr) { return; }

        const StudioComponent* component = entity->findComponent(setProperty->getComponentTypeId());
        if (component == nullptr) { return; }

        play_.mirrorEdit(setProperty->getEntityId(), setProperty->getComponentTypeId(),
                         setProperty->getPropertyName(),
                         component->getProperty(setProperty->getPropertyName()));
    }

    void StudioShellPanels::requestQuit()
    {
        if (shell_ == nullptr) { return; }

        // Asked before anything closes. Losing an afternoon's work to a menu item is the one
        // mistake an editor must not let a user make in a single click, and the question has to be
        // asked by whoever knows the document rather than by the window manager.
        if (context_.getHistory().isDirty())
        {
            StudioDialogRequest request;
            request.title = "Quit CNA Studio";
            request.lines = {"The scene has unsaved changes."};
            // Save last, and the default: it is the answer that loses nothing, and a dialog whose
            // Enter throws work away is a dialog people learn to read slowly and still get wrong.
            request.buttons = {"Cancel", "Discard", "Save and Quit"};
            request.cancelButton = 0;
            request.defaultButton = 2;
            shell_->openDialog(std::move(request), [this](const StudioDialogResult& answer) {
                // Dismissed without answering is Cancel: a dialog escaped is a question withdrawn,
                // and the one answer that must never be inferred is the one that discards work.
                if (answer.dismissed || answer.chosen == 0) { return; }

                if (answer.chosen == 2)
                {
                    if (context_.getScenePath().empty() || !context_.saveScene())
                    {
                        // Refused rather than quitting anyway. The user asked to save *and* quit,
                        // and doing the second half after failing the first is the worst reading.
                        log_.append(LogSeverity::Error,
                                    "Could not save the scene, so CNA Studio is still open.");
                        return;
                    }
                    recovery_.discardForCurrentScene();
                }

                closeStudio();
            });
            return;
        }

        closeStudio();
    }

    void StudioShellPanels::closeStudio()
    {
        if (shell_ != nullptr && shell_->requestQuit()) { return; }

        // Said rather than nothing happening. A preview and a test both reach here, and so does a
        // host that forgot the seam -- and a Quit that silently does nothing reads as a broken menu.
        log_.append(LogSeverity::Warning,
                    "Nothing here can close CNA Studio: this build has no window to close.");
    }

    void StudioShellPanels::pollInteractionEnd()
    {
        if (shell_ == nullptr) { return; }

        // `STUDIO-07055`. A merge key answers "is this the same edit" -- entity, component,
        // property -- and cannot answer "is this the same *interaction*", because two drags of one
        // field are identical by every property the key can see and differ only in that the user
        // let go in between. Without this, scrubbing a position, going for coffee and scrubbing it
        // again would be *one* undo entry, and Ctrl+Z would throw away both.
        //
        // The prototype has done this since gizmo drags existed (`isAnyItemActive`); the native
        // shell never did, because until now nothing on it merged. The material editor was already
        // pushing `MergeWithPrevious`, so two separate material edits were already folding into
        // one -- a live defect that had nothing to report it.
        //
        // Read from the frame the shell drew *last* time, because poll runs before the next one.
        // That closes the chain one frame after the button comes up, which is a frame in which
        // nothing was pushed.
        if (!shell_->frame().router().activeId().isValid())
        {
            context_.getHistory().endInteraction();
        }
    }

    void StudioShellPanels::pollPlugins()
    {
        if (shell_ == nullptr) { return; }

        // A revision rather than a callback. Rebuilding the menus from inside a plugin's load is
        // rebuilding them inside code that may throw, and a plugin that failed halfway would take
        // the menu bar with it.
        const std::uint64_t revision = context_.getPluginExtensions().revision();
        if (revision == pluginRevision_) { return; }
        pluginRevision_ = revision;

        const std::size_t rows = bindStudioPluginMenus(*shell_, context_, log_);
        counts_.pluginMenuRows = rows;
    }

    void StudioShellPanels::pollRecovery(double nowSeconds)
    {
        // A delta from the clock the host already passes, rather than a second one. `poll` is the
        // only thing these panels run every frame, so this is where the seconds are.
        // Read every poll rather than applied when the panel changes it. The preferences also
        // arrive by being *assigned* -- the host loads them from disk straight into this object --
        // and a setting that only takes effect down one of the two paths is a setting that works
        // when you change it and not when you restart.
        recovery_.setIntervalSeconds(static_cast<double>(preferences_.model().autosaveSeconds));

        const double delta = recoveryLastSeconds_ < 0.0 || nowSeconds < recoveryLastSeconds_
            ? 0.0
            : nowSeconds - recoveryLastSeconds_;
        recoveryLastSeconds_ = nowSeconds;

        // The project changing is what "a project was opened" looks like from here. Watching the
        // path rather than being told means the scan happens however the project was opened -- the
        // command, the command line, or a host that opened one before these panels existed.
        const std::string project = context_.hasProject()
            ? context_.getProject().getFilePath() : std::string{};
        if (project != recoveryProject_)
        {
            recoveryProject_ = project;
            if (recovery_.scan() && shell_ != nullptr)
            {
                // Announced rather than only logged. This arrives at start-up, when the log has
                // just filled with everything else start-up says, and it is the one message whose
                // whole point is that the user was not there for what produced it.
                const RecoverySnapshot* snapshot = recovery_.recoverable();
                StudioNotification notification;
                notification.id = "studio.recovery";
                notification.severity = StudioNotificationSeverity::Warning;
                notification.title = "Unsaved work was found";
                notification.detail = "Scene '" + snapshot->sceneName + "' from "
                                    + formatRecoveryTime(snapshot->savedAtSeconds);
                notification.actionId = "studio.file.recoverScene";
                notification.actionLabel = "Recover";
                notification.seconds = -1.0f;
                notify(std::move(notification));
            }
        }

        recovery_.update(delta);
    }

    void StudioShellPanels::publishStatus()
    {
        if (shell_ == nullptr) { return; }

        StudioStatusModel& status = shell_->status();

        if (context_.hasProject())
        {
            status.message = context_.getProject().getName() + "  --  "
                           + context_.getScene().getName();
            status.target = studioTargetProfileSummary(
                context_.getProject().getActiveTargetProfile());
            // A project that opened answers whatever went wrong before it.
            status.problem.clear();
        }
        else
        {
            status.message = "No project open";
            status.target.clear();
        }

        // The document's own answer rather than a flag somebody has to remember to set: an
        // unsaved-changes mark that can be wrong is worse than none, because it is the one thing
        // a user checks before closing the window.
        status.modified = context_.hasProject() && context_.getHistory().isDirty();

        // Rebuilt every poll rather than edited, so a job that ended cannot leave a bar behind:
        // the status bar reports what is running now, and "now" is what a poll is for.
        status.jobs.clear();

        if (build_.process().getState() == BuildState::Running)
        {
            StudioStatusJob job;
            const std::size_t steps = build_.process().getSteps().size();
            const std::size_t done = build_.process().getStepNumber();
            job.label = steps > 0
                ? "Building, step " + std::to_string(std::min(done + 1, steps)) + " of "
                      + std::to_string(steps)
                : std::string{"Building"};
            job.progress = steps > 0 ? static_cast<float>(done) / static_cast<float>(steps) : -1.0f;
            job.stopActionId = "studio.build.cancel";
            status.jobs.push_back(std::move(job));
        }

        if (comparison_.isRunning())
        {
            StudioStatusJob job;
            job.label = std::string{"Comparing renderers: "} + toString(comparison_.run().getState());
            // No progress: the run is waiting on several games to open windows, and a bar that
            // guessed at how long that takes would be inventing a number in the one place the
            // editor reports facts.
            status.jobs.push_back(std::move(job));
        }

        if (play_.isRunning())
        {
            StudioStatusJob job;
            // Which of the two, because a paused game and a running one look identical from the
            // editor -- the window is there either way -- and the difference is the whole point of
            // having paused it.
            job.label = play_.state() == StudioPlayState::Paused ? "Paused" : "Playing";
            job.stopActionId = "studio.play.stop";
            status.jobs.push_back(std::move(job));
        }
    }

    void StudioShellPanels::setViewportServices(StudioCamera2D& camera, StudioCamera3D& camera3D,
                                                SpriteSizeProvider spriteSize)
    {
        services_.camera = &camera;
        services_.camera3D = &camera3D;
        services_.spriteSize = std::move(spriteSize);
        // Re-bound rather than checked per frame: the content callback captures `this` and reads
        // the services through it, so the only thing that has to happen here is that the viewport
        // gains content it did not have when there was no camera to drive.
        bindViewport(*shell_);
    }

    void StudioShellPanels::sayViewportIsEmpty(StudioFrame& frame, const UiRect& bounds,
                                               std::string_view headline, std::string_view hint)
    {
        if (!frame.isDrawPass() || bounds.isEmpty()) { return; }

        const StudioTheme& theme = frame.theme();
        const float line = std::ceil(frame.measureText(StudioFontRole::Body, "Ag").height()
                                     + static_cast<float>(theme.metric(StudioMetric::SpacingSmall)));

        // Centred, because a message in the corner of an otherwise empty rectangle reads as a
        // stray label rather than as the thing the rectangle is saying.
        UiRect box{bounds.left(), bounds.centerY() - line, bounds.width, line};
        studioDrawText(frame, box, headline, StudioFontRole::Body,
                       theme.color(StudioColorRole::TextSecondary), StudioTextAlign::Center);
        box.y += line;
        studioDrawText(frame, box, hint, StudioFontRole::BodySmall,
                       theme.color(StudioColorRole::TextSecondary), StudioTextAlign::Center);
    }

    PlayerInputSnapshot studioPlayerInputFrom(const StudioFrame& frame, const UiRect& bounds,
                                              bool pointerInside)
    {
        static constexpr std::array<std::pair<UiKey, const char*>, 12> kForwarded{{
            {UiKey::W, "W"}, {UiKey::A, "A"}, {UiKey::S, "S"}, {UiKey::D, "D"},
            {UiKey::Q, "Q"}, {UiKey::E, "E"}, {UiKey::R, "R"}, {UiKey::F, "F"},
            {UiKey::Space, "Space"}, {UiKey::Enter, "Enter"}, {UiKey::Escape, "Escape"},
            {UiKey::Tab, "Tab"}}};

        const UiInputState& input = frame.input();

        PlayerInputSnapshot snapshot;
        for (const auto& [key, name] : kForwarded)
        {
            if (input.isKeyDown(key)) { snapshot.keys.emplace_back(name); }
        }

        if (!pointerInside) { return snapshot; }

        snapshot.mouseX = input.mouseX - bounds.left();
        snapshot.mouseY = input.mouseY - bounds.top();
        snapshot.surfaceWidth = bounds.width;
        snapshot.surfaceHeight = bounds.height;
        snapshot.leftButton = input.isMouseDown(UiMouseButton::Left);
        snapshot.middleButton = input.isMouseDown(UiMouseButton::Middle);
        snapshot.rightButton = input.isMouseDown(UiMouseButton::Right);
        snapshot.wheel = input.wheelY;
        return snapshot;
    }

    void StudioShellPanels::bindViewport(StudioShell& shell)
    {
        // The viewport (STUDIO-07009) draws nothing: the scene is a texture the shell composites,
        // and this is the camera the pointer moves and what a click in it selects.
        //
        // The *content* is bound whether or not there is a camera. It used to be bound only with
        // one, on the reasoning that a viewport swallowing clicks and moving nothing is worse than
        // one that plainly does not respond -- which is right about the clicks and wrong about the
        // words: a build with no graphics device then showed a bare grid and no hint that the grid
        // was a viewport rather than a panel that had failed (STUDIO-06013). It says so now, and
        // still takes no input.
        if (services_.camera == nullptr)
        {
            shell.setPanelContent("viewport", [](StudioFrame& frame, const UiRect& bounds) {
                sayViewportIsEmpty(frame, bounds, "This build has no graphics device.",
                                   "The viewport cannot draw the scene without one.");
            });
            return;
        }

        // X, which the prototype has and the native shell did not (docs/MIGRATION-INVENTORY.md).
        // A toggle rather than two commands, as it is there: there are two spaces, and a toggle
        // needs no second binding to get back.
        if (const StudioAction* existing = shell.actions().find("studio.view.toggleGizmoSpace"))
        {
            StudioAction action = *existing;
            action.checkable = true;
            action.isChecked = [this] { return viewportState_.space == GizmoSpace::Local; };
            action.run = [this] {
                viewportState_.space = viewportState_.space == GizmoSpace::World
                    ? GizmoSpace::Local : GizmoSpace::World;
                // Any drag in flight ends with the space that owned it, for the same reason a mode
                // change does: a translate half-finished in world space would keep writing world
                // deltas into a local transform.
                viewportState_.endDrag();
            };
            shell.actions().add(std::move(action));
        }

        // Which plane the 3D grid lies in (STUDIO-07056). Written straight to the preferences
        // rather than to the viewport state, because the state is copied *from* the preferences
        // every frame -- writing to it would be writing to something about to be overwritten, and
        // the setting would take effect for exactly one frame. It is also what makes the choice
        // survive a restart without a second place to store it.
        if (const StudioAction* existing = shell.actions().find("studio.view.gridOnGroundPlane"))
        {
            StudioAction action = *existing;
            action.checkable = true;
            action.isChecked = [this] { return preferences_.model().gridOnGroundPlane; };
            // Enabled only where it changes something. The 2D view has one plane and no choice to
            // make about it -- and a command that visibly does nothing is a bug report waiting to
            // be filed. Greyed out rather than absent, so a user who went looking for it finds it.
            action.isEnabled = [this] { return viewportState_.view == StudioViewportView::ThreeD; };
            action.run = [this] {
                const bool ground = !preferences_.model().gridOnGroundPlane;
                preferences_.model().gridOnGroundPlane = ground;

                // Through `apply` rather than by assigning the model alone: applying is what
                // writes them to disk, and a setting changed in the record and not on disk is one
                // that works until the user restarts.
                (void)preferences_.apply();

                log_.append(LogSeverity::Info,
                            ground ? "The 3D grid is on the ground plane."
                                   : "The 3D grid is on the scene's own plane.");
            };
            shell.actions().add(std::move(action));
        }

        // New Project and Open Project, both of which are the Project Hub with a different tab in
        // front. The commands existed with no handler since STUDIO-06001, which draws them greyed
        // out for ever -- a File menu whose first two rows do nothing is worse than one without
        // them.
        //
        // No file dialog: there is no modal window yet (STUDIO-03022 covers the layering, not the
        // window), so Open Project shows the recent list, which is where a user's own projects
        // are. A file dialog is the half this gets when there is a window to put one in.
        for (const auto& [id, page] :
             {std::pair{"studio.file.newProject", StudioProjectHubPage::New},
              std::pair{"studio.file.openProject", StudioProjectHubPage::Recent}})
        {
            const StudioAction* existing = shell.actions().find(id);
            if (existing == nullptr) { continue; }

            StudioAction action = *existing;
            action.run = [this, &shell, page] {
                hubState_.page = page;
                if (!shell.isPanelOpen("projecthub")) { (void)shell.openPanel("projecthub"); }
                (void)shell.activatePanel("projecthub");
            };
            shell.actions().add(std::move(action));
        }

        // The two views. Exclusive and checkable for the same reason the tools are: a viewport can
        // only be showing one of them, and a menu that cannot say which is one a user tests by
        // pressing it.
        for (const auto& [id, view] : {std::pair{"studio.view.2d", StudioViewportView::TwoD},
                                       std::pair{"studio.view.3d", StudioViewportView::ThreeD}})
        {
            const StudioAction* existing = shell.actions().find(id);
            if (existing == nullptr) { continue; }

            StudioAction action = *existing;
            action.checkable = true;
            action.isChecked = [this, view] { return viewportState_.view == view; };
            action.run = [this, view] {
                if (viewportState_.view == view) { return; }
                viewportState_.view = view;

                // Every gesture ends with the view that owned it. A gizmo drag half-finished in
                // the 2D view would otherwise keep writing positions from a projection that is no
                // longer on screen, and a navigation gesture would resume mid-orbit.
                viewportState_.endDrag();
                viewportState_.navigating = false;
                viewportState_.fillStart.reset();

                // The first switch frames the scene, and only the first: the default camera looks
                // straight down an axis, so an unframed 3D view opens on a grid with the level
                // somewhere off the edge of it. Framing on *every* switch would be worse than not
                // framing at all -- a user who set up a view, glanced at 2D and came back would
                // find their angle thrown away.
                if (view == StudioViewportView::ThreeD && !framedIn3D_
                    && services_.camera3D != nullptr)
                {
                    if (const std::optional<WorldBounds3D> bounds = computeSceneBounds3D(
                            context_.getScene(), services_.spriteSize,
                            context_.makeMeshProvider()))
                    {
                        services_.camera3D->frame(*bounds);
                        framedIn3D_ = true;
                    }
                }

                // Said out loud, because the two views share a panel and the change is dramatic
                // enough that a user who pressed 3 by accident deserves to be told what they
                // pressed -- and told how to get back.
                log_.append(LogSeverity::Info,
                            view == StudioViewportView::ThreeD
                                ? "Viewport: 3D. Drag to orbit, Shift-drag to pan, wheel to zoom. "
                                  "Press 2 for the 2D view."
                                : "Viewport: 2D.");
            };
            shell.actions().add(std::move(action));
        }

        // The tilemap tools. Checkable so the toolbar shows which press-means-what is armed, and
        // exclusive because a press is one thing: arming two would be arming neither.
        for (const auto& [id, tool] :
             {std::pair{"studio.view.tool.select", StudioViewportTool::Select},
              std::pair{"studio.view.tool.paint", StudioViewportTool::PaintTiles},
              std::pair{"studio.view.tool.erase", StudioViewportTool::EraseTiles},
              std::pair{"studio.view.tool.pick", StudioViewportTool::PickTile},
              std::pair{"studio.view.tool.fill", StudioViewportTool::FillTiles}})
        {
            const StudioAction* existing = shell.actions().find(id);
            if (existing == nullptr) { continue; }

            StudioAction action = *existing;
            action.checkable = true;
            action.isChecked = [this, tool] { return viewportState_.tool == tool; };
            action.run = [this, tool] {
                viewportState_.tool = tool;
                // Any gesture in flight ends with the tool that owned it: a fill half-dragged when
                // the user reaches for Erase would otherwise commit as a fill on the next release.
                viewportState_.fillStart.reset();
                viewportState_.endDrag();
            };
            shell.actions().add(std::move(action));
        }

        // The toolbar's three transform buttons, which have been drawing and doing nothing since
        // the toolbar existed. Bound here rather than with the document commands because the mode
        // they set is the viewport's, and checkable so the toolbar shows which one is on -- three
        // buttons that all look the same whichever is active is three buttons nobody trusts.
        for (const auto& [id, mode] : {std::pair{"studio.view.translate", GizmoMode::Translate},
                                       std::pair{"studio.view.rotate", GizmoMode::Rotate},
                                       std::pair{"studio.view.scale", GizmoMode::Scale}})
        {
            const StudioAction* existing = shell.actions().find(id);
            if (existing == nullptr) { continue; }

            StudioAction action = *existing;
            action.checkable = true;
            action.isChecked = [this, mode] { return viewportState_.mode == mode; };
            action.run = [this, mode] {
                viewportState_.mode = mode;
                // Any drag in flight ends with the mode that owned it: a translate half-finished
                // when the user reaches for Rotate would otherwise keep writing positions.
                viewportState_.endDrag();
            };
            shell.actions().add(std::move(action));
        }

        // Focus Selected, which the View menu and the F key have both been offering since the
        // shell existed. Only bindable now that there is a camera to move.
        if (const StudioAction* found = shell.actions().find("studio.view.focusSelected"))
        {
            StudioAction focus = *found;
            focus.isEnabled = [this] { return !context_.getSelection().empty(); };
            focus.run = [this] {
                // Whichever camera the user is actually looking through (`plan.md` STUDIO-11003).
                // This moved the 2D camera in both views, so pressing F in the 3D viewport
                // rearranged a camera nobody was looking through and appeared to do nothing at all.
                const bool framed =
                    viewportState_.view == StudioViewportView::ThreeD
                        ? (services_.camera3D != nullptr
                           && studioFrameSelection3D(context_, *services_.camera3D,
                                                     services_.spriteSize,
                                                     context_.makeMeshProvider()))
                        : (services_.camera != nullptr
                           && studioFrameSelection(context_, *services_.camera,
                                                   services_.spriteSize));
                if (!framed)
                {
                    log_.append(LogSeverity::Trace,
                                "Nothing selected has a position to frame.");
                }
            };
            shell.actions().add(std::move(focus));
        }

        // How the 3D view draws geometry (`plan.md` STUDIO-11010). Checkable and exclusive, and
        // enabled only in the 3D view where the choice means something.
        for (const auto& [id, shading] :
             {std::pair{"studio.view.shading.shaded", StudioViewportShading::Shaded},
              std::pair{"studio.view.shading.wireframe", StudioViewportShading::Wireframe},
              std::pair{"studio.view.shading.shadedWireframe",
                        StudioViewportShading::ShadedWireframe}})
        {
            const StudioAction* existing = shell.actions().find(id);
            if (existing == nullptr) { continue; }

            StudioAction action = *existing;
            action.checkable = true;
            action.isChecked = [this, shading] { return viewportState_.shading == shading; };
            action.isEnabled = [this] {
                return viewportState_.view == StudioViewportView::ThreeD;
            };
            action.run = [this, shading] { viewportState_.shading = shading; };
            shell.actions().add(std::move(action));
        }

        // The bounds overlay (`plan.md` STUDIO-11008). Exclusive and checkable, exactly as the
        // shading modes above: three answers to one question, and the toolbar has to show which.
        for (const auto& [id, display] :
             {std::pair{"studio.view.bounds.off", BoundsDisplay::None},
              std::pair{"studio.view.bounds.selected", BoundsDisplay::Selected},
              std::pair{"studio.view.bounds.all", BoundsDisplay::All}})
        {
            const StudioAction* existing = shell.actions().find(id);
            if (existing == nullptr) { continue; }

            StudioAction action = *existing;
            action.checkable = true;
            action.isChecked = [this, display] { return viewportState_.boundsOverlay == display; };
            action.isEnabled = [this] {
                return viewportState_.view == StudioViewportView::ThreeD;
            };
            action.run = [this, display] { viewportState_.boundsOverlay = display; };
            shell.actions().add(std::move(action));
        }

        // The sphere is its own toggle rather than a fourth mode, because it answers a different
        // question from "which entities": how much bigger than the box is the sphere a
        // `BoundingSphere` test would use. Enabled only when something is being overlaid at all --
        // a switch that does nothing visible is a switch a user presses twice and distrusts.
        if (const StudioAction* existing = shell.actions().find("studio.view.bounds.spheres");
            existing != nullptr)
        {
            StudioAction action = *existing;
            action.checkable = true;
            action.isChecked = [this] { return viewportState_.boundingSpheres; };
            action.isEnabled = [this] {
                return viewportState_.view == StudioViewportView::ThreeD
                    && viewportState_.boundsOverlay != BoundsDisplay::None;
            };
            action.run = [this] { viewportState_.boundingSpheres = !viewportState_.boundingSpheres; };
            shell.actions().add(std::move(action));
        }

        // The six axis-aligned views (`plan.md` STUDIO-11004). Enabled only in the 3D view, where
        // they mean something: the 2D view has one axis to look along and no choice to make about
        // it. Disabled rather than hidden, for the reason the ground-plane toggle above is -- a
        // user who went looking for them should find them and see why they are greyed out.
        for (const auto& [id, standardView] :
             {std::pair{"studio.view.front", StudioStandardView::Front},
              std::pair{"studio.view.back", StudioStandardView::Back},
              std::pair{"studio.view.left", StudioStandardView::Left},
              std::pair{"studio.view.right", StudioStandardView::Right},
              std::pair{"studio.view.top", StudioStandardView::Top},
              std::pair{"studio.view.bottom", StudioStandardView::Bottom}})
        {
            const StudioAction* existing = shell.actions().find(id);
            if (existing == nullptr) { continue; }

            StudioAction action = *existing;
            action.isEnabled = [this] {
                return viewportState_.view == StudioViewportView::ThreeD
                    && services_.camera3D != nullptr;
            };
            action.run = [this, standardView] {
                if (services_.camera3D == nullptr) { return; }
                studioApplyStandardView(*services_.camera3D, standardView);
            };
            shell.actions().add(std::move(action));
        }

        shell.setPanelContent("viewport", [this](StudioFrame& frame, const UiRect& bounds) {
            // The first thing a user sees, and with no project it was a grid and nothing else --
            // no hint that a project is what is missing, and none that the grid is a viewport
            // rather than a panel that failed to draw (STUDIO-06013).
            //
            // **And a scene, not a project** (STUDIO-07047). This used to gate on the project
            // alone, which made the viewport inert for every scene that had not been opened from
            // one -- including the `Untitled` scene a Studio started with no project now opens, so
            // the World Outliner listed a camera the viewport refused to navigate around. There is
            // something to show as soon as there is something in the scene.
            if (!context_.hasProject() && context_.getScene().getEntityCount() == 0)
            {
                sayViewportIsEmpty(frame, bounds, "No project open.",
                                   "Open one with File > Open Project, or --project.");
                return;
            }

            // The running game gets the pointer and the keys, after the editor's own handling and
            // not instead of it: play mode leaves the scene editable, and a drag that moves an
            // entity is also a drag the game may want to know about. Described here rather than in
            // the viewport panel because this is where the player lives -- the panel is arithmetic
            // over a camera and a document, and giving it a process to talk to would give it a
            // reason to need one.
            const auto forwardToPlayer = [&](bool pointerInside) {
                if (!frame.isInputPass() || play_.state() == StudioPlayState::Stopped) { return; }
                forwardInputToPlayer(studioPlayerInputFrom(frame, bounds, pointerInside));
            };

            // Read every frame rather than applied when the Preferences panel changes them, for
            // the reason the autosave interval is re-read every poll: preferences also arrive by
            // being *assigned*, when the host loads them from disk, and a setting that only takes
            // effect down one of the two paths is one that works when you change it and not when
            // you restart.
            const StudioPreferences& settings = preferences_.model();
            viewportState_.cameraSpeed = settings.cameraSpeed;
            viewportState_.invertZoom = settings.invertZoom;
            viewportState_.navigation = settings.navigation;
            viewportState_.gridOnGroundPlane = settings.gridOnGroundPlane;

            // The two views branch here, at the top, rather than inside one function that would
            // then be about both. They share the document and nothing below it: a press in 3D
            // orbits rather than pans, picks along a ray rather than against a layer order, and
            // has no tile under it at all.
            if (viewportState_.view == StudioViewportView::ThreeD && services_.camera3D != nullptr)
            {
                const StudioViewportResult view3D = studioViewportPanel3D(
                    frame, bounds, context_, *services_.camera3D, viewportState_,
                    services_.spriteSize);

                studioViewportToolbar(frame, bounds, shell_->actions());

                forwardToPlayer(view3D.pointerInside);

                if (!view3D.selectionChanged) { return; }
                ++counts_.viewportSelections;
                if (const StudioEntity* entity = context_.getScene().findEntity(view3D.picked))
                {
                    log_.append(LogSeverity::Trace, "Selected '" + entity->getName() + "'.");
                }
                else
                {
                    log_.append(LogSeverity::Trace, "Selection cleared.");
                }
                return;
            }

            const StudioViewportResult viewport = studioViewportPanel(
                frame, bounds, context_, *services_.camera, viewportState_, services_.spriteSize);

            // After the surface, so the overlay's field wins hover against the viewport underneath
            // it -- the surface is one widget covering the whole panel, and a field described
            // before it would be a field the viewport swallows every click of. The toolbar is
            // described for the same reason and in the same place.
            studioViewportToolbar(frame, bounds, shell_->actions());
            studioViewportToolOverlay(frame, bounds, viewportState_);

            forwardToPlayer(viewport.pointerInside);

            if (viewport.assetDropped.isValid())
            {
                // Where it was let go, and as a root entity: dropping into the world says where,
                // and says nothing about what it should be under.
                (void)placeDroppedAsset(viewport.assetDropped, viewport.assetDropPosition, Uuid{});
            }

            // No "camera changed" callback: the host renders the scene every frame anyway, and a
            // hook nothing sets is scaffolding rather than a seam.
            if (!viewport.selectionChanged) { return; }

            ++counts_.viewportSelections;
            if (const StudioEntity* entity = context_.getScene().findEntity(viewport.picked))
            {
                log_.append(LogSeverity::Trace, "Selected '" + entity->getName() + "'.");
            }
            else
            {
                log_.append(LogSeverity::Trace, "Selection cleared.");
            }
        });
    }

    void bindStudioProjectHub(StudioShellPanels& panels, std::string_view executablePath,
                              StudioLog& log)
    {
        StudioTemplateCatalogue templates;
        for (const std::string& searchPath : studioTemplateSearchPaths(executablePath))
        {
            for (const std::string& problem : templates.addSearchPath(searchPath))
            {
                // A manifest that cannot be read is somebody's template not working, which is a
                // problem worth a line. A search path that is not there is not, and the catalogue
                // already passes over those in silence.
                log.append(LogSeverity::Warning, "Project templates: " + problem);
            }
        }

        panels.setProjectHubServices(std::move(templates),
                                     StudioRecentProjectsStore::defaultPath());
    }

    void StudioShellPanels::setProjectHubServices(StudioTemplateCatalogue templates,
                                                  std::string recentProjectsPath)
    {
        templates_ = std::move(templates);
        recentProjectsPath_ = std::move(recentProjectsPath);
    }

    void StudioShellPanels::rememberProject(const std::string& projectFilePath,
                                            std::int64_t nowSeconds)
    {
        if (recentProjectsPath_.empty() || projectFilePath.empty()) { return; }

        std::string problem;
        if (!StudioRecentProjectsStore{recentProjectsPath_}.remember(
                projectFilePath, context_.getProject().getName(), nowSeconds, &problem))
        {
            // A list that could not be written is a convenience that did not work, and nothing
            // more: said once, at trace, rather than raised at somebody who was opening a project.
            log_.append(LogSeverity::Trace, "Recent projects: " + problem);
        }
    }

    void StudioShellPanels::openProjectFromHub(StudioShell& shell,
                                               const std::string& projectFilePath)
    {
        const std::string unavailable = describeStudioProjectAvailability(projectFilePath);
        if (!unavailable.empty())
        {
            log_.append(LogSeverity::Error, "Cannot open '" + projectFilePath + "': " + unavailable);
            return;
        }

        if (!context_.openProject(projectFilePath))
        {
            log_.append(LogSeverity::Error, "'" + projectFilePath + "' could not be opened.");
            return;
        }

        // Everything wrong with it, said once and where it can be acted on (STUDIO-08012). A
        // project with a missing startup scene still opens: a broken project is exactly the
        // project somebody needs the editor for.
        for (const StudioProjectDiagnostic& diagnostic :
             validateStudioProject(context_.getProject(), context_.getLanguages()))
        {
            const LogSeverity severity =
                diagnostic.severity == StudioProjectDiagnosticSeverity::Error ? LogSeverity::Error
                : diagnostic.severity == StudioProjectDiagnosticSeverity::Warning
                    ? LogSeverity::Warning
                    : LogSeverity::Info;
            log_.append(severity, diagnostic.toLine());
        }

        // Wall-clock, because the list is ordered by when a project was last opened and that
        // outlives the process. `poll`'s clock is monotonic and says nothing about when; passing
        // it here would give every row the same zero and leave the ordering to file order.
        rememberProject(projectFilePath, static_cast<std::int64_t>(std::time(nullptr)));

        // Before the view, so the Content Browser's first frame already has them.
        loadAssetShortcuts();

        applyProjectDefaultView(shell);

        // The viewport, because a project that opened has a world in it and the Hub has done its
        // job. Leaving the Hub in front would make the next thing the user does be closing a tab.
        (void)shell.activatePanel("viewport");
    }

    void StudioShellPanels::applyProjectDefaultView(StudioShell& shell)
    {
        // STUDIO-11014. A new CNA-native Empty 3D project opens directly into its 3D world
        // viewport, because a 3D world opened in the 2D view is a grid with the level somewhere
        // off the edge of it -- and "press 3" is a first five minutes nobody should have.
        //
        // Through the *command* rather than by assigning the state, and that is the whole reason
        // this is four lines rather than one: `studio.view.3d` also frames the camera on the scene,
        // ends any gesture in flight and says in the log which view is now on. Setting
        // `viewportState_.view` here would open a 3D view looking straight down an axis at nothing,
        // which is exactly the picture this task exists to prevent.
        const std::string& view = context_.getProject().getDefaultView();
        if (view.empty()) { return; }

        const std::string actionId =
            view == "3d" ? "studio.view.3d" : (view == "2d" ? "studio.view.2d" : std::string{});
        if (actionId.empty())
        {
            log_.append(LogSeverity::Warning,
                        "This project asks to open in the '" + view
                            + "' view, which Studio does not have. Opening in 2D.");
            return;
        }

        (void)shell.actions().invoke(actionId);
    }

    void StudioShellPanels::bind(StudioShell& shell)
    {
        // The World Outliner (STUDIO-07006), the second ported panel and the first that reads the
        // document model rather than a log.
        shell.setPanelContent("outliner", [this](StudioFrame& frame, const UiRect& bounds) {
            const StudioOutlinerResult outliner =
                studioOutlinerPanel(frame, bounds, context_, outlinerState_);
            if (frame.isDrawPass())
            {
                counts_.outlinerRowsDrawn = outliner.rowsDrawn;
                counts_.outlinerRowsTotal = outliner.rowsTotal;
            }

            // An asset dropped on a row goes into the scene under it (STUDIO-09008). At the origin
            // rather than at the pointer: a tree has no world position, and inventing one from the
            // row's y would put things in a line nobody asked for.
            if (outliner.assetDropped.isValid())
            {
                (void)placeDroppedAsset(outliner.assetDropped, StudioVector3{},
                                        outliner.assetDropParent);
            }
            // Said out loud, because a refused drop is a tree that did not change and a successful
            // one on a collapsed parent can be too -- the row moves inside something the user
            // cannot see. Without a line here the two are the same picture (STUDIO-07058).
            if (outliner.reparentRefused)
            {
                log_.append(LogSeverity::Warning,
                            "An entity cannot be moved inside one of its own children.");
            }

            if (!outliner.selectionChanged) { return; }

            const std::vector<Uuid>& selection = context_.getSelection();
            if (selection.empty())
            {
                log_.append(LogSeverity::Trace, "Selection cleared.");
            }
            else if (const StudioEntity* entity = context_.getScene().findEntity(selection.back()))
            {
                log_.append(LogSeverity::Trace,
                            "Selected '" + entity->getName() + "' ("
                                + std::to_string(selection.size()) + " selected).");
            }
        });

        // The Content Browser (STUDIO-07008), the fourth ported panel.
        shell.setPanelContent("content", [this](StudioFrame& frame, const UiRect& bounds) {
            // The browser asks for a texture; the cache has the pixels; only the host can make one
            // (STUDIO-35041). Three responsibilities, and the binder is where they meet -- which is
            // why the resolver is built here rather than handed to the panel as state.
            StudioContentBrowserServices services;
            if (services_.uploadThumbnail)
            {
                services.thumbnailTexture = [this](const Uuid& id) -> UiTextureId {
                    const StudioThumbnail* thumbnail = thumbnails_.find(id);
                    if (thumbnail == nullptr || thumbnail->isEmpty()) { return kUiTextureNone; }
                    return services_.uploadThumbnail(id, *thumbnail);
                };
            }

            const StudioContentBrowserResult content =
                studioContentBrowser(frame, bounds, context_, contentState_, services);
            if (frame.isDrawPass())
            {
                counts_.contentRowsDrawn = content.rowsDrawn;
                counts_.contentRowsTotal = content.rowsTotal;
                counts_.contentThumbnailsDrawn = content.thumbnailsDrawn;

                // What is on screen is what is worth a thumbnail (STUDIO-09003). Recorded on the
                // draw pass and acted on in `poll`, so the drawing itself starts nothing: the
                // panel reports and the binder acts.
                thumbnails_.setWanted(content.visibleAssets);
            }
            if (content.shortcutsChanged) { saveAssetShortcuts(); }

            if (!content.lastOperation.message.empty() && content.lastOperation.applied)
            {
                log_.append(LogSeverity::Info, content.lastOperation.message + ".");
            }

            // Launching a process is the binder's business, not a panel's (STUDIO-09011). Reported
            // both ways round: a file manager that will not start is indistinguishable from one
            // that opened behind the editor's window, and only one of those is a problem.
            if (!content.revealPath.empty())
            {
                std::string problem;
                if (studioRevealInFileManager(content.revealPath, &problem))
                {
                    log_.append(LogSeverity::Trace, "Showing '" + content.revealPath + "'.");
                }
                else
                {
                    log_.append(LogSeverity::Warning, "Could not show it: " + problem + ".");
                }
            }

            if (!content.selectedAsset.isValid()) { return; }

            const AssetRecord* record = context_.getAssets().find(content.selectedAsset);
            if (record != nullptr)
            {
                log_.append(LogSeverity::Trace, "Selected asset '" + record->sourcePath + "'.");
            }
        });

        // The Details panel (STUDIO-07007), the third ported and the first that writes to the
        // document. Every edit goes through the command history, so Ctrl+Z reaches it.
        shell.setPanelContent("details", [this](StudioFrame& frame, const UiRect& bounds) {
            StudioDetailsServices details_services;
            details_services.audio = services_.audio;
            details_services.thumbnail = services_.assetThumbnail;
            details_services.modelEffectName = services_.modelEffectName;

            // Asked for on the input pass only, so the rebuild -- which reads every scene, prefab
            // and material in the project -- happens once per frame at most, and the draw pass
            // describes exactly the graph the input pass routed against.
            details_services.dependencies =
                frame.isInputPass() ? dependencyIndex() : &dependencies_;
            details_services.documents = &documents_;

            const StudioDetailsResult details =
                studioDetailsPanel(frame, bounds, context_, details_services);
            if (frame.isDrawPass())
            {
                counts_.detailsRowsDrawn = details.rowsDrawn;
                counts_.audioPreviews = details.audio.controls;
                counts_.animationFrames = details.animationFrames;
                counts_.prefabOverrides = details.prefab.overrides;

                // Taken from the draw pass so the viewport draws what was on screen rather than
                // what the input pass decided a moment before the transport buttons were read.
                // Cleared when there is no preview, so a sprite goes back to its own frame the
                // moment the selection moves off it.
                animation_ = details.animation;
            }
            if (details.relinked)
            {
                log_.append(LogSeverity::Info, details.editedProperty + ".  Undo with Ctrl+Z.");
            }
            else if (details.edited)
            {
                // "Reset" and "Changed" are opposite things to say about a sidecar: one took a
                // field out of it and the other put one in, and the diff the user is about to
                // review shows which.
                log_.append(LogSeverity::Info,
                            (details.resetProperty ? "Reset " : "Changed ")
                                + details.editedProperty + ".  Undo with Ctrl+Z.");
            }

            // Revert and Apply (STUDIO-07042). Apply writes the prefab *file*, which every other
            // instance of it is about to be compared against, so a failure has to reach the user
            // rather than being a button that did nothing.
            if (!details.prefab.message.empty())
            {
                log_.append(details.prefab.failed ? LogSeverity::Warning : LogSeverity::Info,
                            details.prefab.failed ? details.prefab.message
                                                  : details.prefab.message + ".");
            }

            // Said out loud, both ways. A preview that will not load and a clip of silence sound
            // identical, and only one of them is something the user can fix -- which is the whole
            // reason `StudioAudio::play` returns anything at all (STUDIO-07044).
            if (details.audio.played)
            {
                log_.append(details.audio.started ? LogSeverity::Info : LogSeverity::Warning,
                            details.audio.started ? "Playing '" + details.audio.clip + "'."
                                                  : "Cannot play '" + details.audio.clip + "'.");
            }
            else if (details.audio.stopped)
            {
                log_.append(LogSeverity::Trace, "Preview stopped.");
            }
        });

        // The Material panel (STUDIO-07046). Registered since the shell existed and blank until
        // now: a tab a user could raise onto an empty rectangle, which reads as a broken editor
        // rather than an unfinished one. Phase 19 grows it into a material editor with a preview;
        // this is the panel it grows in.
        shell.setPanelContent("material", [this](StudioFrame& frame, const UiRect& bounds) {
            StudioDetailsServices details_services;
            details_services.audio = services_.audio;
            details_services.thumbnail = services_.assetThumbnail;
            details_services.modelEffectName = services_.modelEffectName;
            details_services.documents = &documents_;

            const StudioDetailsResult material =
                studioMaterialPanel(frame, bounds, context_, details_services);
            if (frame.isDrawPass()) { counts_.materialFields = material.materialFields; }
            if (material.edited)
            {
                log_.append(LogSeverity::Info,
                            "Changed " + material.editedProperty + ".  Undo with Ctrl+Z.");
            }
        });

        // The Build panel (STUDIO-07010), the fifth ported -- and the first interface the six-axis
        // target profile has ever had. Until now a project could only change what it ships on by
        // editing its `.cnaproject` in a text editor.
        shell.setPanelContent("build", [this](StudioFrame& frame, const UiRect& bounds) {
            const StudioBuildPanelResult panel = buildPanel_->draw(frame, bounds);
            if (panel.buildRequested)
            {
                std::string problem;
                if (build_.process().start(buildPanel_->planBuild(), &problem))
                {
                    log_.append(LogSeverity::Info, "Build started; log at " + build_.process().getLogPath());
                }
                else
                {
                    log_.append(LogSeverity::Error, "Cannot start the build: " + problem);
                }
            }
            if (panel.cancelRequested)
            {
                build_.process().cancel();
                log_.append(LogSeverity::Warning, "Build cancelled.");
            }
        });

        // The History panel (STUDIO-07013): the undo stack as a list a user can jump around in,
        // which is the only way to reach a state twenty commands back without counting Ctrl+Z.
        shell.setPanelContent("history", [this](StudioFrame& frame, const UiRect& bounds) {
            const StudioHistoryResult history =
                studioHistoryPanel(frame, bounds, context_, historyState_);
            if (frame.isDrawPass())
            {
                counts_.historyRowsDrawn = history.rowsDrawn;
                counts_.historyPositions = history.positions;
            }
            if (!history.navigateTo.has_value()) { return; }

            const std::size_t ran = studioNavigateHistory(context_, *history.navigateTo);
            if (ran == 0) { return; }

            log_.append(LogSeverity::Trace,
                        "History: moved to position " + std::to_string(context_.getHistory().getCursor())
                            + " of " + std::to_string(context_.getHistory().getCount()) + ".");
        });

        bindViewport(shell);

        // Build and Package, which the Build panel and the exporter can both already do. Bound
        // here rather than with the document commands because the process they drive is this
        // object's: two BuildProcesses would be two builds racing for one output directory.
        if (const StudioAction* found = shell.actions().find("studio.build.build"))
        {
            StudioAction build = *found;
            build.isEnabled = [this] {
                return context_.hasProject() && build_.process().getState() != BuildState::Running;
            };
            build.run = [this] {
                std::string problem;
                if (build_.process().start(buildPanel_->planBuild(), &problem))
                {
                    log_.append(LogSeverity::Info, "Build started; log at " + build_.process().getLogPath());
                }
                else
                {
                    log_.append(LogSeverity::Error, "Cannot start the build: " + problem);
                }
            };
            shell.actions().add(std::move(build));
        }

        // The one the status bar's Stop button invokes. A job the user can see running and cannot
        // stop is the worst kind of progress report.
        if (const StudioAction* found = shell.actions().find("studio.build.cancel"))
        {
            StudioAction cancel = *found;
            cancel.isEnabled = [this] { return build_.process().getState() == BuildState::Running; };
            cancel.run = [this] {
                build_.process().cancel();
                log_.append(LogSeverity::Warning, "Build cancelled.");
            };
            shell.actions().add(std::move(cancel));
        }

        if (const StudioAction* found = shell.actions().find("studio.play.play"))
        {
            StudioAction play = *found;
            play.isEnabled = [this] {
                return context_.hasProject() && !play_.builds().empty() && !play_.isRunning();
            };
            play.run = [this] { play_.start(); };
            shell.actions().add(std::move(play));
        }

        if (const StudioAction* found = shell.actions().find("studio.play.stop"))
        {
            StudioAction stop = *found;
            stop.isEnabled = [this] { return play_.isRunning(); };
            stop.run = [this] { play_.stop(); };
            shell.actions().add(std::move(stop));
        }

        if (const StudioAction* found = shell.actions().find("studio.edit.rename"))
        {
            StudioAction rename = *found;
            rename.isEnabled = [this] {
                return context_.getPrimarySelection().isValid()
                    || context_.getAssets().find(context_.getSelectedAsset()) != nullptr;
            };
            rename.run = [this] {
                if (shell_ == nullptr) { return; }

                // One F2, acting on whatever the inspector is showing (STUDIO-09009). The entity
                // selection and the asset selection are mutually exclusive by construction, so
                // there is no question of which this means.
                if (!context_.getPrimarySelection().isValid())
                {
                    const AssetRecord* record =
                        context_.getAssets().find(context_.getSelectedAsset());
                    if (record == nullptr) { return; }

                    (void)shell_->openPanel("content");
                    (void)shell_->activatePanel("content");

                    const std::size_t slash = record->sourcePath.find_last_of('/');
                    contentState_.tree.beginRename(
                        record->id.toString(),
                        slash == std::string::npos ? record->sourcePath
                                                   : record->sourcePath.substr(slash + 1));
                    return;
                }

                // The panel first, then the rename. Pressing F2 with the outliner behind another
                // tab would otherwise start an edit on a field nobody can see, and swallow the
                // typing that followed.
                (void)shell_->openPanel("outliner");
                (void)shell_->activatePanel("outliner");
                (void)studioBeginOutlinerRename(context_.getScene(),
                                                context_.getPrimarySelection(), outlinerState_);
            };
            shell.actions().add(std::move(rename));
        }

        if (const StudioAction* found = shell.actions().find("studio.file.quit"))
        {
            StudioAction quit = *found;
            // Enabled even where nothing can close: refusing it in a preview would grey out a row
            // that is perfectly real in the editor, and the refusal is reported rather than silent.
            quit.run = [this] { requestQuit(); };
            shell.actions().add(std::move(quit));
        }

        if (const StudioAction* found = shell.actions().find("studio.play.pause"))
        {
            StudioAction pause = *found;
            pause.isEnabled = [this] { return play_.state() != StudioPlayState::Stopped; };
            pause.isChecked = [this] { return play_.state() == StudioPlayState::Paused; };
            pause.run = [this] { (void)setPlayPaused(play_.state() != StudioPlayState::Paused); };
            shell.actions().add(std::move(pause));
        }

        if (const StudioAction* found = shell.actions().find("studio.play.step"))
        {
            // Enabled only while paused, because that is the only time it does anything. A control
            // that is live and does nothing is one the user stops believing.
            StudioAction step = *found;
            step.isEnabled = [this] { return play_.state() == StudioPlayState::Paused; };
            step.run = [this] { (void)stepPlayFrame(); };
            shell.actions().add(std::move(step));
        }

        if (const StudioAction* found = shell.actions().find("studio.play.restart"))
        {
            StudioAction restart = *found;
            restart.isEnabled = [this] {
                return context_.hasProject() && !play_.builds().empty();
            };
            restart.run = [this] { play_.restart(); };
            shell.actions().add(std::move(restart));
        }

        if (const StudioAction* found = shell.actions().find("studio.build.package"))
        {
            StudioAction package = *found;
            package.isEnabled = [this] { return context_.hasProject(); };
            package.run = [this] { (void)build_.packageProject(); };
            shell.actions().add(std::move(package));
        }

        // Both greyed out unless there is something to answer for. A menu row that offers to
        // discard work when there is none is a row a user has to read twice to be sure.
        if (const StudioAction* found = shell.actions().find("studio.file.recoverScene"))
        {
            StudioAction recover = *found;
            recover.isEnabled = [this] { return recovery_.hasRecoverable(); };
            recover.run = [this] {
                if (recovery_.recover() && shell_ != nullptr)
                {
                    shell_->notifications().dismiss("studio.recovery");
                }
            };
            shell.actions().add(std::move(recover));
        }

        if (const StudioAction* found = shell.actions().find("studio.file.discardRecovered"))
        {
            StudioAction drop = *found;
            drop.isEnabled = [this] { return recovery_.hasRecoverable(); };
            drop.run = [this] {
                if (recovery_.discard() && shell_ != nullptr)
                {
                    shell_->notifications().dismiss("studio.recovery");
                }
            };
            shell.actions().add(std::move(drop));
        }

        // The Layers panel (STUDIO-07024), which answers the one question the outliner cannot:
        // what is on this layer. The outliner is ordered by the hierarchy and a layer cuts across
        // it.
        shell.setPanelContent("layers", [this](StudioFrame& frame, const UiRect& bounds) {
            const StudioLayersResult layers =
                studioLayersPanel(frame, bounds, context_, layersState_);
            if (frame.isDrawPass()) { counts_.layerRowsDrawn = layers.rowsDrawn; }
            if (layers.selectEntities.empty()) { return; }

            context_.setSelection(layers.selectEntities);
            if (!layers.clickedLayer.empty())
            {
                log_.append(LogSeverity::Trace,
                            "Selected " + std::to_string(layers.selectEntities.size())
                                + " on layer '" + layers.clickedLayer + "'.");
            }
        });

        // The Project Hub (STUDIO-08001 … STUDIO-08004). It is given the templates and the recent
        // list by the host; a Studio that was given neither still draws it, saying there are no
        // templates -- the honest picture of a build whose templates were not installed.
        shell.setPanelContent("projecthub", [this, &shell](StudioFrame& frame,
                                                           const UiRect& bounds) {
            StudioProjectHubModel model;
            model.templates = &templates_;
            model.languages = &context_.getLanguages();
            if (!recentProjectsPath_.empty())
            {
                // Read each frame rather than cached, because availability is a fact about the
                // filesystem and the filesystem changes while Studio is running: a project deleted
                // in another window should grey out here without anybody pressing refresh.
                model.recent = StudioRecentProjectsStore{recentProjectsPath_}.load();
            }

            const StudioProjectHubResult hub =
                studioProjectHubPanel(frame, bounds, model, hubState_);

            if (frame.isDrawPass())
            {
                counts_.hubRecentRowsDrawn = hub.recentRowsDrawn;
                counts_.hubTemplateRowsDrawn = hub.templateRowsDrawn;
                return;
            }

            if (!hub.forgetProjectPath.empty() && !recentProjectsPath_.empty())
            {
                (void)StudioRecentProjectsStore{recentProjectsPath_}.forget(hub.forgetProjectPath);
            }

            if (!hub.openProjectPath.empty()) { openProjectFromHub(shell, hub.openProjectPath); }

            if (hub.createRequested)
            {
                StudioNewProjectResult created = createStudioProject(
                    studioProjectHubRequest(hubState_, context_.getLanguages()), templates_,
                    context_.getLanguages());

                // Kept on the state rather than shown once: a refusal that vanished on the next
                // frame would be a form that appears to do nothing when the Create button is
                // pressed, which is the single worst thing a New Project dialog can do.
                hubState_.problems = created.problems;

                for (const std::string& warning : created.warnings)
                {
                    log_.append(LogSeverity::Warning, "New project: " + warning);
                }

                if (created.succeeded())
                {
                    log_.append(LogSeverity::Info,
                                "Created " + std::to_string(created.writtenFiles.size())
                                    + " files in " + created.projectFilePath + ".");
                    openProjectFromHub(shell, created.projectFilePath);
                }
                else
                {
                    for (const StudioNewProjectProblem& problem : created.problems)
                    {
                        log_.append(LogSeverity::Error,
                                    "New project: " + problem.field + ": " + problem.message);
                    }
                }
            }
        });

        // The Problems panel (STUDIO-07012): scene validation and broken asset references, as one
        // report, because a user whose model has the wrong material on it does not know in advance
        // which of the two it is.
        shell.setPanelContent("problems", [this](StudioFrame& frame, const UiRect& bounds) {
            const StudioProblemsResult problems =
                studioProblemsPanel(frame, bounds, context_, problemsState_);
            if (frame.isDrawPass())
            {
                counts_.problemRowsDrawn = problems.rowsDrawn;
                counts_.brokenReferences = problems.brokenReferences;
                counts_.sceneErrors = problems.errors;
                counts_.sceneWarnings = problems.warnings;
            }
            if (problems.selectEntity.isValid()) { context_.select(problems.selectEntity); }
            if (!problems.clearAsset.isValid()) { return; }

            auto command = std::make_unique<RelinkAssetCommand>(
                context_.getScene(), problems.clearAsset, problems.relinkTo);
            if (!command->isValid()) { return; }

            const std::string summary = command->getDescription();
            context_.execute(std::move(command));
            log_.append(LogSeverity::Info, summary + ".  Undo with Ctrl+Z.");
        });

        // The Diagnostics panel (STUDIO-07011): what this Studio is running on, as a report
        // somebody can paste into a bug report rather than a screen somebody has to transcribe.
        shell.setPanelContent("diagnostics", [this](StudioFrame& frame, const UiRect& bounds) {
            // Read here rather than pushed by a host, because the atlas belongs to the shell and
            // every host has one: a build with no graphics device reports it too, which is where
            // a dropped glyph is most likely to be looked at and least likely to be pushed.
            if (const StudioFontAtlas* atlas = frame.fontAtlas())
            {
                diagnostics_.atlasSize = atlas->size();
                diagnostics_.atlasOccupancy = atlas->occupancy();
                diagnostics_.atlasGrowths = atlas->growths();
                diagnostics_.atlasDroppedGlyphs = atlas->droppedGlyphs();
            }

            const StudioDiagnosticsResult panel =
                studioDiagnosticsPanel(frame, bounds, diagnostics_, diagnosticsState_);
            if (frame.isDrawPass()) { counts_.diagnosticRowsDrawn = panel.rowsDrawn; }
            if (!panel.copyRequested) { return; }

            if (services_.setClipboardText && services_.setClipboardText(panel.copyText))
            {
                log_.append(LogSeverity::Info, "Copied the diagnostics report to the clipboard.");
            }
            else
            {
                log_.append(LogSeverity::Warning,
                            "This build has no clipboard: CNA's Devices module is off "
                            "(CNA gap G-02). Rebuild CNA with CNA_DEVICES=ON.");
            }
        });

        // The Backends panel (STUDIO-07014): the same scene on every installed player, and where
        // the pictures disagree. The run is owned here rather than by the panel, so closing the
        // tab does not abandon several games that are already starting.
        shell.setPanelContent("comparison", [this](StudioFrame& frame, const UiRect& bounds) {
            StudioComparisonView view;
            view.hasProject = context_.hasProject();
            view.tolerance = comparison_.tolerance();
            view.state = comparison_.run().getState();
            view.entries = &comparison_.run().getEntries();
            view.error = comparison_.run().getError();
            view.allAgree = comparison_.run().allBackendsAgree();
            view.builds = &play_.builds();
            view.playBackendIsOverride = !play_.buildOverride().empty();
            if (const PlayerBuild* build = play_.chooseBuild()) { view.playBackend = build->backend; }
            if (view.hasProject)
            {
                const ComparisonRequest probe = comparison_.makeRequest();
                view.outputDirectory = probe.outputDirectory;
                view.problem = describeComparisonProblem(probe);
            }

            const StudioComparisonResult panel =
                studioComparisonPanel(frame, bounds, view, comparisonState_);
            if (frame.isDrawPass()) { counts_.comparisonRowsDrawn = panel.rowsDrawn; }

            if (panel.playBackendChosen.has_value())
            {
                selectPlayerBuild(*panel.playBackendChosen);
            }
            if (panel.toleranceChanged) { comparison_.setTolerance(panel.tolerance); }
            if (panel.compareRequested) { (void)comparison_.start(); }
            if (panel.cancelRequested) { comparison_.cancel(); }
        });

        // The Preferences panel (STUDIO-06011): a panel rather than a modal, because preferences
        // are read and changed *while* working -- "the camera is too fast" is noticed with a hand
        // on the mouse, and a dialog makes answering it a trip out of and back into the viewport.
        shell.setPanelContent("preferences", [this, &shell](StudioFrame& frame,
                                                            const UiRect& bounds) {
            StudioPreferencesPanelContext context;
            context.layoutNames.reserve(shell.savedLayouts().size());
            for (const StudioNamedLayout& saved : shell.savedLayouts())
            {
                context.layoutNames.push_back(saved.name);
            }

            // The registry rather than a copy of it: rebinding writes through, and a copy would
            // rebind something nothing dispatches from.
            context.actions = &shell.actions();

            const StudioPreferencesPanelResult panel =
                studioPreferencesPanel(frame, bounds, preferences_.model(), context, shortcutEditor_);

            if (panel.resetRequested)
            {
                // Asked first, because it is the one control here that discards decisions the user
                // made deliberately -- and every other change is a single value they can put back.
                StudioDialogRequest request;
                request.title = "Reset preferences";
                request.lines = {"Put every preference back the way Studio ships?",
                                 "This cannot be undone."};
                request.buttons = {"Cancel", "Reset"};
                request.cancelButton = 0;
                // Delivered to the asker rather than polled for. Reading `dialogResult()` a frame
                // later works only while nothing renders in between, and a host that drew twice
                // before its next poll saw a default-constructed answer -- which is indistinguishable
                // from Cancel for this dialog and was the opposite of Cancel for the quit one.
                shell.openDialog(std::move(request), [this](const StudioDialogResult& answer) {
                    if (answer.dismissed || answer.chosen != 1) { return; }

                    preferences_.reset();
                    ++counts_.preferenceChanges;
                    log_.append(LogSeverity::Info, "Preferences reset to the defaults.");
                });
                return;
            }

            if (!panel.changed) { return; }
            ++counts_.preferenceChanges;
            (void)preferences_.apply();
        });

        // The first ported panel (STUDIO-07005). Drawn by the Studio UI, from a log no UI owns --
        // which is the whole shape of the strangler migration: the ImGui Console reads the same
        // model and keeps working until it is deleted.
        shell.setPanelContent("output", [this](StudioFrame& frame, const UiRect& bounds) {
            const StudioLogPanelResult panel = studioLogPanel(frame, bounds, log_);
            if (frame.isDrawPass())
            {
                counts_.logRowsDrawn = panel.rowsDrawn;
                counts_.logRowsMatching = panel.rowsMatching;
            }
            if (panel.cleared) { log_.clear(); }
            if (!panel.copyRequested) { return; }

            // CNA gap G-02: the clipboard is behind a default-off CNA option, so this degrades
            // visibly rather than silently doing nothing.
            if (services_.setClipboardText && services_.setClipboardText(panel.copyText))
            {
                log_.append(LogSeverity::Info, "Copied the log to the clipboard.");
            }
            else
            {
                log_.append(LogSeverity::Warning,
                            "This build has no clipboard: CNA's Devices module is off "
                            "(CNA gap G-02). Rebuild CNA with CNA_DEVICES=ON.");
            }
        });
    }
}
