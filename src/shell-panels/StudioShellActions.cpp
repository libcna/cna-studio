// SPDX-License-Identifier: MS-PL
/**
 * @file StudioShellActions.cpp
 * @brief Binds the shell's actions to the editor.
 */

#include "CNA/Studio/ShellPanels/StudioShellActions.hpp"

#include "CNA/Studio/Core/StudioCommand.hpp"
#include "CNA/Studio/Scene/SceneCommands.hpp"
#include "CNA/Studio/Scene/TransformGizmos.hpp"
#include "CNA/Studio/Project/Project.hpp"
#include "CNA/Studio/Scene/SceneDocument.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/Ui/StudioLog.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"

#include <functional>
#include <vector>
#include <memory>
#include <string>
#include <utility>

namespace CNA::Studio
{
    int bindStudioShellActions(StudioShell& shell, StudioContext& context, StudioLog& log)
    {
        StudioActionRegistry& actions = shell.actions();
        int bound = 0;

        // Copied, amended and put back rather than mutated in place: the registry hands out const
        // references so nothing can quietly rewrite a command another part of the shell is
        // pointing at. An id the registry does not carry is skipped -- the menus are built from
        // the registry, so an action invented here would be one no menu shows.
        const auto bind = [&](const char* id, std::function<bool()> enabled,
                              std::function<void()> run) {
            const StudioAction* existing = actions.find(id);
            if (existing == nullptr) { return; }

            StudioAction amended = *existing;
            amended.isEnabled = std::move(enabled);
            amended.run = std::move(run);
            if (actions.add(std::move(amended))) { ++bound; }
        };

        bind("studio.edit.undo",
             [&context] { return context.getHistory().canUndo(); },
             [&context, &log] {
                 // The description is read before the undo, because afterwards it names whatever
                 // is now on top of the stack -- a different entry, and a message that would tell
                 // the user the wrong thing about what just happened.
                 const std::string what = context.getHistory().getUndoDescription();
                 if (context.getHistory().undo())
                 {
                     log.append(LogSeverity::Info, "Undid " + what + ".");

                     // A reversal is a document change like any other: a running game that saw the
                     // edit but not its undo would be showing a state that exists nowhere any more.
                     if (const StudioCommand* entry =
                             context.getHistory().getCommandAt(context.getHistory().getCursor()))
                     {
                         context.announceCommand(*entry);
                     }
                 }
             });

        bind("studio.edit.redo",
             [&context] { return context.getHistory().canRedo(); },
             [&context, &log] {
                 const std::string what = context.getHistory().getRedoDescription();
                 const std::size_t beforeRedo = context.getHistory().getCursor();
                 if (context.getHistory().redo())
                 {
                     log.append(LogSeverity::Info, "Redid " + what + ".");
                     if (const StudioCommand* entry = context.getHistory().getCommandAt(beforeRedo))
                     {
                         context.announceCommand(*entry);
                     }
                 }
             });

        bind("studio.file.save",
             [&context] { return context.hasProject(); },
             [&context, &log] {
                 if (context.saveScene())
                 {
                     log.append(LogSeverity::Info, "Saved " + context.getScene().getName() + ".");
                 }
                 else
                 {
                     log.append(LogSeverity::Error, "Could not save the scene.");
                 }
             });

        // New Scene, which the prototype has on Ctrl+N and the native shell did not have at all
        // (docs/MIGRATION-INVENTORY.md). Refused with unsaved changes rather than discarding them:
        // a key people press all day must not be able to throw work away without asking.
        bind("studio.file.newScene",
             [&context] { return context.hasProject(); },
             [&context, &log] {
                 if (context.getHistory().isDirty())
                 {
                     log.append(LogSeverity::Warning,
                                "Save the scene before starting a new one, or undo your changes.");
                     return;
                 }
                 context.newScene();
                 log.append(LogSeverity::Info, "Started a new scene.");
             });

        bind("studio.edit.delete",
             [&context] { return !context.getSelection().empty(); },
             [&context, &log] {
                 if (context.getSelection().empty()) { return; }

                 // **Every** selected entity, not the last one (STUDIO-07047). Deleting one of a
                 // selection of five and clearing the selection is the shape of bug a user reports
                 // as "Delete only sometimes works", because which one survived depended on the
                 // order they clicked.
                 //
                 // Roots only: a delete takes the whole subtree with it, so a selected descendant
                 // of a selected entity is already accounted for, and asking to delete it
                 // separately would push a command that finds nothing. The same helper the
                 // multi-selection gizmo uses.
                 const std::vector<Uuid> doomed =
                     findSelectionRoots(context.getScene(), context.getSelection());
                 if (doomed.empty()) { return; }

                 const StudioEntity* first = context.getScene().findEntity(doomed.front());
                 const std::string name = first != nullptr ? first->getName() : "entity";

                 // One entry for the whole action. Through the history, like every other edit:
                 // deleting is the single operation a user most needs to be able to take back.
                 auto batch = std::make_unique<CompositeCommand>(
                     "Delete " + std::to_string(doomed.size())
                     + (doomed.size() == 1 ? " entity" : " entities"));
                 for (const Uuid& entityId : doomed)
                 {
                     if (context.getScene().findEntity(entityId) == nullptr) { continue; }
                     batch->add(
                         std::make_unique<DeleteEntityCommand>(context.getScene(), entityId));
                 }
                 if (batch->isEmpty()) { return; }

                 context.execute(std::move(batch));
                 context.pruneSelection();
                 log.append(LogSeverity::Info,
                            doomed.size() == 1
                                ? "Deleted '" + name + "'."
                                : "Deleted " + std::to_string(doomed.size()) + " entities.");
             });

        bind("studio.file.saveAll",
             [&context] { return context.hasProject(); },
             [&context, &log] {
                 // The scene *and* the project. "Save All" that saved one of the two would be the
                 // command a user reaches for precisely when they cannot afford it to be partial.
                 const bool scene = context.saveScene();
                 std::string problem;
                 const bool project = context.getProject().saveToFile({}, &problem);

                 if (scene && project)
                 {
                     log.append(LogSeverity::Info, "Saved the scene and the project.");
                     return;
                 }
                 log.append(LogSeverity::Error,
                            std::string{"Save All did not complete: "}
                                + (scene ? "" : "the scene would not save. ")
                                + (project ? "" : "the project would not save. " + problem));
             });

        bind("studio.edit.duplicate",
             [&context] { return !context.getSelection().empty(); },
             [&context, &log] {
                 // Snapshotted, because the copies are selected as they are made and iterating the
                 // live selection would duplicate the copies as well.
                 const std::vector<Uuid> sources = context.getSelection();
                 if (sources.empty()) { return; }

                 // One entry for the whole action, for the reason Delete has one: duplicating five
                 // entities is one press of Ctrl+D, so undoing it is one press of Ctrl+Z.
                 auto batch = std::make_unique<CompositeCommand>(
                     "Duplicate " + std::to_string(sources.size())
                     + (sources.size() == 1 ? " entity" : " entities"));

                 std::vector<Uuid> copies;
                 for (const Uuid& sourceId : sources)
                 {
                     auto command =
                         std::make_unique<DuplicateEntityCommand>(context.getScene(), sourceId);
                     if (!command->isValid()) { continue; }
                     copies.push_back(command->getEntityId());
                     batch->add(std::move(command));
                 }
                 if (batch->isEmpty()) { return; }

                 // Asked *before* executing, because after it the description names a copy that did
                 // not exist when the question was asked.
                 const std::string what = batch->getDescription();
                 context.execute(std::move(batch));

                 // Selecting the copies is what makes "duplicate, then drag it somewhere" work
                 // without a trip back to the World Outliner (STUDIO-07047).
                 context.setSelection(std::move(copies));
                 log.append(LogSeverity::Info, what + ".");
             });

        return bound;
    }
}
