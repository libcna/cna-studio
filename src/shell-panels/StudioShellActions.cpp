// SPDX-License-Identifier: MS-PL
/**
 * @file StudioShellActions.cpp
 * @brief Binds the shell's actions to the editor.
 */

#include "CNA/Studio/ShellPanels/StudioShellActions.hpp"

#include "CNA/Studio/Scene/SceneCommands.hpp"
#include "CNA/Studio/Project/Project.hpp"
#include "CNA/Studio/Scene/SceneDocument.hpp"
#include "CNA/Studio/StudioContext.hpp"
#include "CNA/Studio/Ui/StudioLog.hpp"
#include "CNA/Studio/UiCore/StudioShell.hpp"

#include <functional>
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
                 }
             });

        bind("studio.edit.redo",
             [&context] { return context.getHistory().canRedo(); },
             [&context, &log] {
                 const std::string what = context.getHistory().getRedoDescription();
                 if (context.getHistory().redo())
                 {
                     log.append(LogSeverity::Info, "Redid " + what + ".");
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

                 // Through the history, like every other edit. Deleting an entity is the single
                 // operation a user most needs to be able to take back.
                 const Uuid target = context.getSelection().back();
                 const StudioEntity* entity = context.getScene().findEntity(target);
                 const std::string name = entity != nullptr ? entity->getName() : "entity";

                 context.execute(std::make_unique<DeleteEntityCommand>(context.getScene(), target));
                 context.clearSelection();
                 log.append(LogSeverity::Info, "Deleted '" + name + "'.");
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
                 if (context.getSelection().empty()) { return; }

                 const Uuid source = context.getSelection().back();
                 auto command = std::make_unique<DuplicateEntityCommand>(context.getScene(), source);

                 // Asked *before* executing, because after it the command owns the answer and the
                 // description names a copy that did not exist when the question was asked.
                 const std::string what = command->getDescription();
                 context.execute(std::move(command));
                 log.append(LogSeverity::Info, what + ".");
             });

        return bound;
    }
}
