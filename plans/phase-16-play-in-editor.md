# Phase 16 — Play In Editor 2

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-16001` … `STUDIO-16999` and are never reused.

**Purpose.** Expand the separate-player architecture into a complete play-test workflow.

**Exit criteria.** Play, pause, step, stop, restart, live edits and crash isolation all work against a real game process.

**Progress:** 4 of 18 complete `██░░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-16001` | Play, Pause, Step, Stop, Restart over the bridge | ⬜ | `STUDIO-06002` |
| `STUDIO-16002` | Game logs routed into the Console with source attribution | ⬜ | `STUDIO-07005` |
| `STUDIO-16003` | Crash reporting when the player dies, without taking Studio with it | ⬜ | `STUDIO-16001` |
| `STUDIO-16004` | Live asset reload into the running player | ⬜ | `STUDIO-16001` |
| `STUDIO-16005` | Live property edits into the running player | ⬜ | `STUDIO-15011` |
| `STUDIO-16006` | Scene reload | ⬜ | `STUDIO-16004` |
| `STUDIO-16007` | Selected-entity synchronisation where feasible | ⬜ | `STUDIO-16005` |
| `STUDIO-16008` | Simulation mode | ⬜ | `STUDIO-16001` |
| `STUDIO-16009` | Possession and eject workflow | ⬜ | `STUDIO-16008` |
| `STUDIO-16010` | Screenshot and capture from the player | ⬜ | `STUDIO-16001` |
| `STUDIO-16011` | Renderer preview selection among the installed player builds | 🔄 | `STUDIO-02041` |
| `STUDIO-16012` | Play and Stop from the native shell, with mutually exclusive enablement | ✅ | `STUDIO-06023`, `STUDIO-07017` |
| `STUDIO-16013` | A player that cannot be launched is refused at the launch, not later | ✅ | `STUDIO-16012` |
| `STUDIO-16014` | The player's ending read from its process status and reported once | ✅ | `STUDIO-16012` |
| `STUDIO-16015` | Pause, Step and Restart from the native shell | ✅ | `STUDIO-16012` |
| `STUDIO-16020` | Investigate displaying player output inside a Studio viewport | 🔬 | `STUDIO-16010` |
| `STUDIO-16021` | Decide the native code reload strategy | 🔬 | `STUDIO-15008` |
| `STUDIO-16022` | Implement the chosen reload strategy | ⬜ | `STUDIO-16021` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-16001` — Play, Pause, Step, Stop, Restart over the bridge

**Acceptance.** Carried forward from the prototype and retested through the Studio UI

### `STUDIO-16011` — Renderer preview selection among the installed player builds

**Acceptance.** The user chooses which of the discovered `cna-player-<renderer>` builds Play
launches, and the choice survives a restart.

**In progress.** The choice is currently made *for* the user: Play takes the build matching the
active target profile's renderer, and falls back to whatever was discovered when no player for it
was built. That is the right default and it is not yet a selection — refusing to play because the
preferred renderer is missing would help nobody, but neither does a user who wants to check a
second backend having to edit the target profile to do it.

### `STUDIO-16015` — Pause, Step and Restart from the native shell

**Acceptance.** A running game can be paused and resumed, advanced one frame at a time while
paused, and restarted; the editor follows the player's state rather than announcing it, and every
control is offered only when it does something.

**The protocol was always there.** `PlayerHost` has honoured `Pause`, `Resume` and `StepFrame`
since play mode existed, with its own tests (`PlayerHostHonoursPauseStepAndResume`). What was
missing was an editor that sent them: the native shell had Play and Stop and nothing else, so the
inventory's toolbar table listed Pause, Resume and Step as unanswered.

**Follow, do not announce.** The editor's state changes only once `send` has put the request on the
wire. A toolbar that says "Paused" over a game that never got the message is worse than one that did
nothing, because the user then believes it.

**One checkable Pause answers two of the prototype's rows.** A button that renames itself between
Pause and Resume is one a user cannot find twice, and a toolbar has to *show* whether the game is
paused: the window is there either way, so nothing else says which. Step is enabled only while
paused, because the player ignores it otherwise and a control that is live and does nothing is how
a user learns to distrust a toolbar.

**Restart is new rather than ported.** The prototype has none. It is a stop and a start, not a
message asking the game to reload itself: the player reads the scene from disk when it starts, so
that is how a user sees the edits they have made since pressing Play. Offered before anything is
running too, so one intention is one button whatever the state.

### `STUDIO-16012` — Play and Stop from the native shell, with mutually exclusive enablement

**Acceptance.** `studio.play.play` and `studio.play.stop` are bound in the native shell, are
never both available, and Play is greyed out with no project, with no discovered player build, or
while a game is already running. Playing an unsaved scene is refused rather than saved silently:
the player is a separate process reading the scene from disk, and a user who has not saved
deliberately would otherwise find their file overwritten by pressing Play.

### `STUDIO-16013` — A player that cannot be launched is refused at the launch, not later

**Acceptance.** `PlayerProcess::start` returns false, with the path in the error, for a player
binary that is missing or cannot be executed — on POSIX as well as on Windows.

**Why it needed doing.** `fork` succeeds and `execv` fails in the *child*, which has nothing left
to return the failure to, so a missing binary used to look exactly like a player that started and
exited at once. The editor showed Play succeeding, put up a Stop button, and waited for a
connection that would never arrive. The child now reports `errno` back over a close-on-exec pipe,
which is empty on success precisely because the descriptor closes itself on exec.

### `STUDIO-16014` — The player's ending read from its process status and reported once

**Acceptance.** A player that finishes is reported as having exited and one that dies as having
crashed, decided by its wait status rather than by whether the socket happened to drop first; and
the report arrives exactly once however many times the editor asked whether it was still running.

**Why it needed doing.** Collecting a child is one-shot: whichever call waits on it first gets the
status and every later one gets nothing. The toolbar asks `isRunning()` every frame to decide
whether Stop is available, so the toolbar was consuming the exit and the poll that was supposed to
report it saw nothing to report — the editor was at its most likely to lose the message exactly
when it was doing its job.

### `STUDIO-16020` — Investigate displaying player output inside a Studio viewport

**Acceptance.** An efficient, process-safe mechanism, or a recorded decision not to. The separate-process architecture is not compromised to get an embedded image quickly

### `STUDIO-16021` — Decide the native code reload strategy

**Acceptance.** A recorded decision among restart-after-build, module reload in the player, and process replacement with state preservation. Reliability outweighs speed: the first shipped answer may simply be save, incremental compile, restart player, restore scene and camera context

