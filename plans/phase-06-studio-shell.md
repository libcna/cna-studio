# Phase 6 — Studio shell

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-06001` … `STUDIO-06999` and are never reused.

**Purpose.** The application frame: menus, toolbar, status bar, the command registry that everything routes through, and preferences.

**Exit criteria.** Menus, toolbars and keyboard shortcuts all invoke the same command objects, and the shell looks like production software.

**Progress:** 24 of 24 complete `████████████`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-06001` | Central command and action registry | ✅ | `STUDIO-03012` |
| `STUDIO-06002` | Register the core command set | ✅ | `STUDIO-06001` |
| `STUDIO-06023` | Bind the core commands to the editor, with live enablement | ✅ | `STUDIO-06002`, `STUDIO-06022` |
| `STUDIO-06003` | Application menu bar | ✅ | `STUDIO-06002` |
| `STUDIO-06004` | Submenus, separators, checkable items and shortcut hints | ✅ | `STUDIO-06003` |
| `STUDIO-06005` | Context menus | ✅ | `STUDIO-06003`, `STUDIO-06017` |
| `STUDIO-06006` | Main toolbar | ✅ | `STUDIO-06002`, `STUDIO-07009` |
| `STUDIO-06007` | Status bar | ✅ | `STUDIO-06001`, `STUDIO-02040` |
| `STUDIO-06008` | Keyboard shortcut dispatch with scope precedence | ✅ | `STUDIO-06001` |
| `STUDIO-06009` | Preferences model, separate from project settings | ✅ | `STUDIO-06001` |
| `STUDIO-06010` | Preferences persistence, versioning and migration | ✅ | `STUDIO-06009` |
| `STUDIO-06011` | Preferences UI | ✅ | `STUDIO-06009`, `STUDIO-03036` |
| `STUDIO-06012` | Shortcut rebinding UI with conflict detection | ✅ | `STUDIO-06008`, `STUDIO-06010` |
| `STUDIO-06013` | Empty states for every panel | ✅ | `STUDIO-06003` |
| `STUDIO-06014` | Notification and toast system for background results | ✅ | `STUDIO-06007` |
| `STUDIO-06016` | Shell preview entry point on the real executable | ✅ | `STUDIO-06003` |
| `STUDIO-06015` | The `cna-studio` executable starts on the new shell by default | ✅ | `STUDIO-06003`, `STUDIO-05009` |
| `STUDIO-06017` | Nested submenus, opening on hover, with keyboard traversal | ✅ | `STUDIO-06004` |
| `STUDIO-06018` | `StudioShell`: the application frame as an interactive object driving the frame lifecycle | ✅ | `STUDIO-03015`, `STUDIO-03031` |
| `STUDIO-06019` | Capture the shell's interaction states from the preview entry point | ✅ | `STUDIO-06016`, `STUDIO-06018` |
| `STUDIO-06020` | `--ui=studio`: the native shell in a real window, on a real CNA device | ✅ | `STUDIO-06018`, `STUDIO-02021` |
| `STUDIO-06021` | Window smoke tests for the native shell, on a real renderer | ✅ | `STUDIO-06020` |
| `STUDIO-06022` | The native shell opens a project, on the editor's own context | ✅ | `STUDIO-06020` |
| `STUDIO-06024` | The About dialog, saying what this build actually is | ✅ | `STUDIO-03040` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-06001` — Central command and action registry

**Acceptance.** Commands carry id, label, icon, enablement predicate, shortcut and handler. Menus, toolbars and shortcuts invoke the same object; no command logic lives in button-drawing code

### `STUDIO-06002` — Register the core command set

**Acceptance.** Save, Save All, Undo, Redo, Duplicate, Delete, Focus Selected, Play, Stop, Build, Package, Toggle Grid, Translate, Rotate, Scale

### `STUDIO-06003` — Application menu bar

**Acceptance.** File, Edit, View, Project, Build, Play, Tools, Window, Help — driven by the command registry

**How it was met.** A menu is a title and a list of **action ids**. Every label, shortcut hint,
check mark and greyed-out row is read from the registry when the row is drawn, so an action whose
enablement predicate turns false greys out in the menu, in the toolbar and in the shortcut table on
the same frame without any of the three being told. A menu with no entries is disabled rather than
opening an empty box

**Verification.** `tests/StudioShellInteractionTests.cpp`: click opens and closes, moving across the
bar switches menus without a second click, click-outside and Escape dismiss without invoking, the
rightmost menu stays on screen at every tested DPI scale, and an empty menu refuses to open

### `STUDIO-06018` — `StudioShell`: the application frame as an interactive object

**Acceptance.** One object owns the shell's state — which menu is open, which row the keyboard is
on, which tab each dock shows — drives all five frame phases over it, and routes every activation
through the one action registry. The draw-only `drawStudioShell` it replaces is removed rather than
kept alongside: two shell renderers would drift, and the one the tests exercised would be the one
nobody ran

**Verification.** `tests/StudioShellInteractionTests.cpp` and the golden-image cases in
`tests/StudioShellTests.cpp`, which now drive the real shell

### `STUDIO-06019` — Capture the shell's interaction states from the preview entry point

**Acceptance.** `--shell-pointer=X,Y`, `--shell-mouse-down` and `--shell-open-menu=TITLE` make
hover, pressed and open-menu states reproducible from a script that cannot see the picture it asked
for. A shell captured only at rest leaves every hover, pressed and highlight token untested by any
image, and those are the tokens a theme gets wrong

**Verification.** Four CTest cases: two renders and two malformed-argument rejections

### `STUDIO-06004` — Submenus, separators, checkable items and shortcut hints

**Done.** Separators, checkable items with a reserved check column, and right-aligned shortcut
hints that widen the menu rather than being clipped. Submenus were the piece that kept this at 🔄
— the arrow was drawn for an item that declared one and nothing opened — and they landed with
`STUDIO-06017`

### `STUDIO-06006` — Main toolbar

**Acceptance.** Save, undo/redo, transform mode, snapping, play controls, build target profile

**Done.** Save, undo/redo, the three transform modes, the grid toggle, play/stop and build are
present, registry-driven, correctly enabled and disabled, and invoke the same actions the menus do —
and the three transform buttons now *set the manipulator* rather than merely drawing, checkable so
the toolbar shows which is on.

**Snapping and the target profile are not toolbar buttons, and should not be.** Snapping is a
modifier held during a drag (Ctrl), which is what the prototype did and what every editor does — a
button for it would be a mode the user has to remember they are in. The build target belongs to the
Build panel, where the whole six-axis profile is editable (`STUDIO-07010`), rather than to a
drop-down in a toolbar that can show one axis of six.

**Play and Stop invoke, and their handlers are Phase 16's**: the actions are registered, bound to
the toolbar and correctly enabled, and the native shell has no play service yet to run them

### `STUDIO-06007` — Status bar

**Acceptance.** Current renderer, target profile, background job progress, project state

**Fields rather than two strings.** Each part answers a different question — what is open, whether
it is saved, what is running, what this project ships on, and what Studio itself is drawing on — and
a caller composing them into one line would be deciding the layout. The shell lays them out, which
is what keeps a build's progress bar in the same place whatever the project is called, and the parts
are laid out **right to left** so that a long project name cannot push the renderer off the end.

**The target is not the renderer.** What the project ships on and what Studio is drawing with are
different facts, and a bar that said one where it meant the other is how somebody tests on the wrong
backend for a week. `studioTargetProfileSummary` is the one place that words a profile, because it
is shown here, in the Build panel's target list and in a build log.

**The job list is rebuilt every poll, never edited.** The bar reports what is running *now*, and
"now" is what a poll is for: a list that was edited would leave a progress bar on screen for a build
that finished. A job whose length is not known reports that rather than inventing a bar — a player
runs for as long as the user plays, and guessing at that would be lying in the one place the editor
reports facts. Every job may name a command that stops it, and the bar draws it as a button: a job
the user can see running and cannot stop is the worst kind of progress report. That is what
`studio.build.cancel` is for, which nothing had bound.

**A problem is sticky where a message is not.** The message is rebuilt from the document on every
poll, so a reason a project would not open would be overwritten by the next frame, leaving "No
project open" — true and useless. The problem stays up, in the error colour, until a project opens
and answers it.

**Verification.** `tests/StudioStatusBarTests.cpp`: one wording for a profile, the empty state, what
is open and what it ships on, the target being the project's rather than Studio's renderer, the
unsaved mark appearing and clearing through a real command, a problem surviving a poll and being
cleared by an open, a running job's Stop invoking its own command and being greyed when that command
is, a finished job leaving no bar, an unknown length drawing none, and the bar producing valid draw
data with no phase violations at 320, 640, 1280 and 2560 wide

### `STUDIO-06008` — Keyboard shortcut dispatch with scope precedence

**Acceptance.** A shortcut inside a text field does not trigger a global command

**How it was met.** A chord is dispatched through the registry, never by a widget, and after the
input pass has described everything — so the dispatcher can see that a field declared itself as
taking typed input, which a dispatcher running first never can. Two rules apply: while a menu is
open the menu owns the keyboard, and while typed input is being consumed an unmodified chord is a
character rather than a command

**Verification.** `tests/StudioShellInteractionTests.cpp`: a chord invokes through the registry,
a held chord fires once rather than every frame, `F` types rather than framing the selection while
a field is active and frames it when none is, an open menu suppresses global chords, and a disabled
action is refused from its shortcut as well as from its menu row

### `STUDIO-06009` — Preferences model, separate from project settings

**Acceptance.** Theme, UI scaling, font size, viewport navigation, camera speed, autosave, external IDE, CMake path, build preferences, default workspace, shortcuts

**The separation is the point, and it goes wrong quietly.** A project's settings travel with the
project — which renderers it ships on, where its scenes live. A user's preferences travel with the
*person* — which theme they can read, how fast the camera moves under their hand, where their
compiler is. A theme committed to version control makes every teammate's Studio dark; a build
directory kept per-user makes a project build differently for each of them. So this file lives beside
the workspace layout, in the user's configuration directory, and the project file knows nothing about
it.

**Everything is clamped, on the way in as well as out.** A UI scale of zero has no pixels and a font
size of zero has no text, and a user who reached either would have to find and delete the file to get
back. Zero autosave is the exception: "do not autosave" is a thing a user can mean, so it is kept
where one second is raised.

**A rebinding is stored as the chord text the menus show.** A file a person may open should read as
what they see in the UI rather than as a key code they would have to look up — so
`describeStudioShortcut` gained an inverse, and a test walks every key this build can name to prove
the two agree. A chord that round-tripped differently would be a shortcut that changed on restart.

### `STUDIO-06010` — Preferences persistence, versioning and migration

**Acceptance.** Corrupt preferences never make a project unopenable; the fallback is reported, not silent

**A missing field *is* the migration.** Every value has a usable default, so a file written by an
older Studio reads correctly by leaving the fields it never heard of alone, and needs no per-version
code. The version number exists for the other direction: a file from a *newer* Studio is refused
rather than half-read, because a newer one may write a field whose absence means something.

**Reported, never silent, never fatal.** The worst outcome of an unreadable file is a Studio that
looks like a fresh install, which is one dialog away in any case — but a user whose theme reverted
deserves to know why, so the reason reaches the Output Log on the first frame. A first run is *not*
a problem to report: doing so would train them to ignore the channel that reports the real ones.

### `STUDIO-06012` — Shortcut rebinding UI with conflict detection

**Acceptance.** A user can rebind a command from the UI, a chord already bound is refused with the
command that holds it named, and the binding survives a restart.

**Done.** The Shortcuts section of the Preferences panel lists every command with its chord and a
Change button; the armed row takes the next *chord* rather than the next key, so holding Ctrl before
pressing the letter does not bind the command to Ctrl-and-nothing. A chord another command holds is
refused with the holder named, and the row stays armed, because the user's next act is to try a
different chord rather than to find the button again. The accepted binding is written into the
preferences as an override, replacing any earlier one for that command, so it survives a restart —
`STUDIO-06010` applies them on start-up and the menus show the new chord text.

Escape and Tab cannot be bound. They are the two keys that get a user *out* of an armed row, and a
rebinding screen is the one place where losing them cannot be undone.

**What writing the editor found.** Listing every command in one flat list showed something the menus
hide: twelve rows read `Close` or `Float`, one pair per panel, because a label is written for the
menu it sits in and the surrounding menu says what it is about. So the editor draws each command's
description beside its label, and the refusal message adds the description when — and only when —
another command shares the label. `studioShortcutConflictMessage` is a function rather than a string
composed inside the draw pass, because the draw list holds glyphs and "the conflict is named" is
precisely what this task has to prove.

### `STUDIO-06011` — Preferences UI

**Acceptance.** Every preference in the model is reachable and changeable, and a change takes effect
where the user can see it.

**A panel rather than a modal.** Preferences are read and changed *while* working — "the camera is
too fast" is noticed with a hand on the mouse — and a dialog makes answering it a trip out of and
back into the viewport. A dockable panel lets the user put it beside the viewport, change a value and
watch the viewport answer. That is also why every change applies immediately rather than on an OK
button: a preferences page with Apply is one where the user finds out whether they liked it only
after committing to it.

**Applied before it is persisted.** A write that fails still leaves the user looking at what they
chose, so they can see it worked and decide what to do about the file — rather than a Studio that
reverted and a message about a disk.

**Reset asks.** It is the one control here that discards decisions the user made deliberately; every
other change is a single value they can put back.

**Verification.** `tests/StudioPreferencesTests.cpp`: the JSON round trip, a chord stored as menu
text and every nameable key round-tripping, clamping, zero autosave kept where one second is raised,
one answer per command, a rebinding reaching the registry while an unknown command is skipped — then
the file: round trip, a first run reporting nothing, a corrupt file falling back and saying so, a
newer file refused, an older file keeping what it said, a hand-edited value clamped on the way in, an
unparseable chord skipped — then the panel: it draws with no phase violation, a change reaching the
theme, a failed save still taking effect, Reset asking first, and the Open-with row reading the
shell's layouts rather than a copy

### `STUDIO-06013` — Empty states for every panel

**Acceptance.** A panel with nothing in it explains what it is for and what to do next, rather than showing blank space

**It is the state a user meets first.** A fresh Studio is nothing but empty panels, and a blank
rectangle is indistinguishable from one whose content failed to draw — which is the reading a new
user will actually take.

**Checked by counting *words*, not geometry.** Text and fills batch into the same draw command
against the same atlas texture, which is what makes them fast and what makes "did this panel say
anything, or is it a coloured rectangle?" unanswerable from the draw data. `StudioDrawList` counts
glyphs now, as a diagnostic beside the frame's phase-violation count, and the guard asserts every
panel with content draws some with nothing open. It fails in both directions, like the other guards.

**`StudioShell::describePanelContent`** is what made it writable: one panel described on its own,
with the same id scope and clip the dock gives it. It is not test scaffolding — a capture of one
panel at full size and a preview of what a panel would show both want exactly this.

**What it found.** Every ported panel already had an empty state — they were written that way, one
at a time — except the viewport, which had no *content* at all without a graphics device. A build
with no device therefore showed a bare grid with no hint that the grid was a viewport rather than a
panel that had failed. The content is bound with or without a camera now: it still takes no input
without one, but it says which of the two things is missing, and the headless preview — the only
visual test this project has without a GPU — shows it.

**Verification.** `tests/StudioEmptyStateTests.cpp`: every panel with content draws text with
nothing open, and every panel survives being described at 0x0, 12x8 and 40x400 with no phase
violation and valid draw data — a panel dragged very narrow is ordinary, and an empty state that
broke there would break in the one arrangement nobody photographs

### `STUDIO-06014` — Notification and toast system for background results

**Acceptance.** A result the user is no longer watching for announces itself over the workspace,
says what it was, offers the panel that explains it, and does not have to be dismissed to get on
with the work. A failure stays until it is dismissed. Everything announced is also in the log.

**Why the status bar was not enough.** Studio had two places to put a fact and both of them have to
be *looked at*. The status bar is one line that the next poll overwrites — a build that failed is
replaced by "No build running" the moment it stops — and the Output Log is a panel that may not even
be open. A build takes minutes, which is exactly long enough for the user to go and read something
else, and that is the case notifications exist for.

**What is announced.** A build finishing, either way; a package written or refused; a renderer
comparison finishing, saying whether they agreed rather than merely that it is over; and a player
that crashed. A player the user *closed* is not announced: they were looking at it, and telling
them what they just did is noise. Nor is a launch that was refused — they had just pressed Play, and
the status bar's problem line already has it.

**An error stays.** Four seconds is right for "Exported" and useless for "Build failed": the reason
to raise a failure is that nobody was watching, and one that waited four seconds and left is a
failure the user meets again later with less context. Warnings get longer than ordinary results and
still go, because there are many of them and each one holding the corner of the editor until it is
acknowledged would make every one an interruption.

**The countdown stops while the pointer is over the stack.** A toast that vanished while it was
being read, or while the pointer was travelling to its Show Build, is worse than one that never
appeared — and the button is the reason a failure is announced rather than logged.

**A toast is not the record.** The centre writes every notification into the `StudioLog` as it is
posted, rather than each caller remembering to do both, because every caller remembering is every
caller eventually not remembering. That is also what the "and N more in the Output Log" line points
at when a burst is larger than the stack.

**Two things this needed that are worth naming.** The input axis gained a layer: a toast is drawn
over the corner of the workspace *and over whatever has been floated there*, so it has to take input
above a float, below popups and menus, and below a modal. And `studio.window.showPanel.<id>` is a
new command, separate from the Window menu's toggle, because "Show Build" on a toast pressed while
Build is open must not be the thing that closes it.

**What writing it found.** Encoding "sticky" as a negative countdown reads well and is wrong the
first time a countdown overshoots its last tick: a four-second toast ticked by five becomes
indistinguishable from one that was never meant to expire, and stays on the screen for ever. It is a
flag now.

### `STUDIO-06016` — Shell preview entry point on the real executable

**Acceptance.** `cna-studio --shell-preview=PATH` renders the native shell and writes a PNG, with `--shell-size`, `--shell-scale` and `--shell-theme`. Headless: no GPU, no display. A preview rather than `--ui=studio`, because the shell is not yet interactive and a flag that opened an unresponsive window would be the worse lie

**Verification.** Five CTest cases: three renders and two malformed-argument rejections

### `STUDIO-06015` — The `cna-studio` executable starts on the new shell by default

**Acceptance.** The new shell becomes the default as soon as it is good enough for daily development, with the legacy UI still reachable behind a flag

**Done.** `cna-studio` with no `--ui` opens the native shell on any build with a CNA device.
`--ui=imgui` still runs the legacy editor, which is what keeps this a switch rather than a removal:
the prototype stays a migration fallback until `STUDIO-07030` deletes it deliberately.

**The condition this waited on was evidence, and the evidence is in.**
`docs/MIGRATION-INVENTORY.md` now answers every row the prototype fills — every panel, every menu,
every shortcut, every toolbar control, both views — and its *Not yet answered* table holds one entry,
material editing, which is a panel the prototype does not have either. The parity proofs
(`STUDIO-07020`…`07023`) are all done, and the visual review records the native shell as ahead on
everything a user sees first. Two earlier readings of this task's own status were stale in turn: it
waited on "the shell hosting a migrated panel" long after every panel was ported, and then on the 3D
view until `STUDIO-11001` answered that too.

**Resolved in `main`, not defaulted in the option struct**, because the answer depends on the build.
A Studio without CNA has no window to open either UI in, and `--headless` already means the console
UI — resolving to the native shell there would open a window for a run that asked for none. Both of
those fall back to the legacy path, where "no window" already had an answer.

**It caught one flag that was only ever answered by the prototype.** `--host-capabilities` prints
the contract and exits, and that lived on the Dear ImGui host alone — so with the default switched,
the query opened a window and ran until it timed out. The native host evaluates the same contract
already, for the same `STUDIO-02021` reason; it prints and exits on it now. Left as it was, that
flag would have broken the day `STUDIO-07030` deleted the path that answered it, and the failure
would have looked like the deletion rather than like this switch.

**What tests it.** A CTest case runs `cna-studio` with no `--ui` at all and asserts on the status
line only the native shell prints; another asks for `--ui=imgui` by name and expects it to work. The
legacy window cases now name `--ui=imgui` explicitly rather than relying on the default, which is
what keeps them about the editor they were written for instead of quietly becoming a second set of
native-shell tests — and what keeps the fallback covered rather than retired by accident.

### `STUDIO-06020` — `--ui=studio`: the native shell in a real window, on a real CNA device

**Acceptance.** `cna-studio --ui=studio` opens a window, drives `StudioShell` from CNA's own input
through `CnaUiPlatform`, and draws its `UiDrawData` through `CnaUiRenderer` on whichever renderer
the build selected. It refuses, with the reason, on a device that fails the host capability
contract (`STUDIO-02022`), and refuses `--screenshot` without `--frames` before opening anything --
the shell runs until the window closes, so a capture it can never reach would hang rather than
fail. Honours `--shell-scale` and `--shell-theme`; reports renderer, frames, display size, draw
calls and triangles when given a frame limit

**Why a separate entry point from the ImGui host.** The two draw entirely different things and the
migration ends by deleting one of them, which is far easier when there is one to delete rather than
a branch to unpick

### `STUDIO-06021` — Window smoke tests for the native shell, on a real renderer

**Acceptance.** CTest opens the shell on a real CNA device in the dark theme, the light theme and
at 2x, and asserts a **non-zero** draw-call and triangle count in each. A shell that built its
widget tree and submitted nothing would clear the window to the theme background, write a valid
screenshot of an empty frame and exit zero; the counts are what separate that from one that drew.
Plus a rejection case for `--screenshot` without `--frames`, and, in the CNA-free configuration, a
case proving `--ui=studio` fails with an explanation rather than silently falling back to ImGui

**Verification.** `CnaStudioNativeShellWindowSmoke`, `…LightTheme`, `…HiDpi`,
`CnaStudioNativeShellScreenshotNeedsFrameLimit`, `CnaStudioNativeShellNeedsCna`

### `STUDIO-06022` — The native shell opens a project, on the editor's own context

**Acceptance.** `cna-studio --ui=studio --project=P` opens the project on a real `StudioContext` --
the same object the ImGui editor uses, not a second one shaped like it -- and the shell shows what
it opened: the project and scene in the status bar, and everything opening had to say in the Output
Log

**Order matters.** The log sink is installed before the project is opened, so an importer fact
applied or an asset that would not parse lands in the Output Log rather than being lost before
anything was listening. And the project is opened before the window, so the first frame already
shows it: a shell that opened empty and then filled in reads as a shell that failed and recovered

**A project that will not open is reported, not fatal.** An editor that refused to start because one
project would not load leaves the user with no way to open a different one

**Verification.** `CnaStudioNativeShellWithAProject` asserts on the status bar, because a row count
says the Output Log drew something and only the status bar says the context opened what it was
given. `CnaStudioNativeShellDoesNotModifyTheProject` hashes every file under the example project
before and after: applying an importer fact on first open is intended, doing it on every open fills
a repository with diffs nobody made

### `STUDIO-06023` — Bind the core commands to the editor, with live enablement

**Acceptance.** Undo, Redo, Save and Delete reach a real `StudioContext`, from the menu, the toolbar
and the keyboard alike. Each carries an enablement predicate asked at the moment the answer is
needed, so Undo greys out the instant the history empties — a control that looks available and
refuses is indistinguishable from one that is broken

**Binding is not declaring.** The registry already holds every command with its label, menu and
shortcut; what it could not know is what any of them *do*. An id the registry does not carry is
skipped rather than added, because the menus are built from the registry and an action invented at
binding time would be one no menu shows

**Where it lives.** `cna-studio-shell-panels`, not the CNA-linked host: it is the same seam as a
panel — the shell on one side, the document model on the other — and putting it in the host would
have made the one thing worth testing here untestable without a window

**A command with no handler is not available.** Every half-migrated menu row used to draw as
though it worked, and a control that looks available and then does nothing is indistinguishable
from one that is broken — worse than a greyed-out row, because the user cannot tell whether to
report it. The registry now refuses to call such a command *enabled*, whatever its predicate says,
so the whole class of dead controls disappeared from the UI in one edit.

**That immediately broke tooltips on greyed-out controls, which is where they matter most.** A
disabled widget is deliberately not *hovered* — otherwise things beneath it would light up through
it — so nothing under the pointer answered for one. Hover ("what would respond to a click") and the
pointer's target ("what is the pointer on") are different questions; the router answers both now,
and the tooltip asks the second. "Why is this greyed out" is exactly the moment somebody hovers for
an explanation.

**The ones still unbound are named, not discovered.** `EveryCommandThatIsStillUnimplementedIsNamedRatherThanDiscovered`
holds the list with a reason for each, and fails in both directions: binding one fails until its
name is removed, and adding an unbound command fails until somebody writes down what it is waiting
for. Seven remain — two file dialogs, Quit, the grid toggle, Play, Stop and About — and each is
waiting on something real.

**`StudioShell::invoke` is public now, and that is the point.** `actions()` was already public, so
anything needing to run a command could reach the registry directly and skip the shell's record of
what ran and what was refused — which is the only thing that makes a menu row quietly doing nothing
discoverable without a debugger. One public route that keeps the books beats a private one everybody
goes around

**Verification.** `tests/StudioShellActionTests.cpp` — Undo disabled until there is something to
undo, Ctrl+Z and the menu reaching the same object, a delete that undoes, every menu row naming an
action the registry carries, and each action saying what it did (with the undo description read
*before* the undo, because afterwards it names a different entry)

### `STUDIO-06017` — Nested submenus, opening on hover, with keyboard traversal

**Acceptance.** A submenu opens beside the row that owns it, to any depth, reachable by pointer and
by keyboard, and staying on screen

**The model is a path, not a pointer.** Menus are rebuilt from their definitions every frame, so
anything holding a node into the tree would dangle the first time a menu changed while it was open
— and menus *do* change while open, because the Window menu's panel list is rebuilt whenever a
panel registers. What persists between frames is which row of each level is open (`submenuPath_`)
and which row of each level is highlighted; the popups themselves are laid out fresh each frame by
walking that path. A path that no longer names a submenu simply stops the walk.

**The delay exists for one gesture: cutting the corner.** The natural way to reach a submenu is to
move diagonally towards it, which drags the pointer across one or two of the rows in between. A menu
that switched on the first frame of that would slam the submenu shut halfway to it, and the user
would learn to travel in an L rather than trust the menu. So a *sibling* row has to hold the pointer
for a quarter of a second before it wins, while a row with nothing open at its level opens
immediately. `CuttingTheCornerDoesNotSlamTheSubmenuShut` crosses a row for half the delay and is
confirmed to fail when the wait is removed.

**Placement.** Beside its parent row, flipped to the other side when it would run off the right
edge, and lifted rather than clipped when it would run off the bottom. Its first row lines up with
the row that opened it, which is what makes the pair read as one gesture rather than as a popup that
appeared somewhere else on screen.

**The keyboard is not a second implementation.** Right enters the highlighted submenu, or moves to
the next menu in the bar when the row has none; Left backs out of the deepest submenu, or moves to
the previous menu; Escape backs out one level before closing the whole menu; Enter opens a submenu
row rather than closing the menu and running nothing. Up and Down move inside the *deepest* open
popup — arrowing in a menu whose submenu is open must move inside the submenu, not behind it.

**The trail stays lit.** A row whose submenu is open keeps its highlight while the pointer is inside
that submenu. Without it the chain behind the pointer goes dark and the user cannot see which rows
they came through, which is the whole navigational value of a nested menu.

**It fixed a latent highlight bug.** `studioMenuItem` emphasised a row from its own hit test, and
`interaction.hovered` is true for *every* widget whose rectangle holds the pointer. That was
invisible while popups never overlapped; a submenu flipped to the left sits on top of its parent,
and both rows would have lit up. It now reads the router's hover *winner*, which is the honest
definition of "the row under the pointer".

**Shipped in the Window menu.** `Window ▸ Panels ▸ …` lists every registered panel as a checkable
show/hide toggle, built from the panel list rather than written down: the command for each panel is
registered by `registerPanel`, its check state is pulled from `isPanelOpen` so a panel closed by
dragging its tab away shows as unchecked without anybody telling the menu, and a panel the user must
not close (the viewport) is drawn disabled rather than drawn enabled and then refusing.

**Verification.** `tests/StudioSubmenuTests.cpp` — the model (levels, depth beyond one, a submenu
row reporting its own label rather than an action id, closing the chain with its menu), the pointer
(opening on rest, the corner-cutting delay and its counterpart, clicking a submenu row, invoking
from inside one, pressing outside, the lit trail), the keyboard (Right in, Left out, Escape by one
level, Enter opening rather than closing, arrows moving inside the deepest popup, and Right on a
plain row still walking the bar), and the Window panel list. Plus `CnaStudioShellPreviewSubmenu`,
which photographs one through the rasterizer, and `CnaStudioRejectsUnknownShellSubmenu`

### `STUDIO-06005` — Context menus

**Acceptance.** A right-click opens a menu at the pointer offering what applies to the thing under
it, with the same rows, submenus, keyboard and dismissal as a menu-bar menu

**One chain, two roots.** A context menu is not a second popup implementation; it is the existing
chain with its first level anchored at a point instead of under a menu-bar title. Everything below
that — submenus, the corner-cutting delay, the arrow keys, Escape backing out one level, the
blocking layer, the "press elsewhere to cancel" rule — is the same code. Two implementations would
have drifted, and the drift would have shown up as a context menu whose submenus behaved subtly
differently from the File menu's.

**Placement flips rather than slides.** A menu opened near the right or bottom edge moves to the
other side of the pointer, not along the edge. Sliding would leave the popup under the pointer, and
the first thing the user did would be to choose a row by accident.

**Its rows are copied, not referenced.** Whoever opens a context menu builds it from what was
right-clicked, and that selection can change — or be deleted — while the menu is up. Copying is what
keeps the popup describing what the user asked about rather than following the ground out from
under itself.

**Only one chain at a time.** Right-clicking with the File menu down replaces it, and opening a
menu-bar menu closes a context menu. Two popups competing for the keyboard is a state with no
correct behaviour, so it is made unreachable rather than handled.

**Either button dismisses it.** A right-click elsewhere is a request for a *different* context
menu, and one that left the first up would stack popups.

**An empty context menu does not open.** A popup with nothing in it is a rectangle the user has to
click away, and it is exactly what a caller produces when the thing right-clicked offers no
commands.

**Shipped on panel tabs**, the first thing in the shell that has a context worth a menu: Close, a
rule, and the same `Panels` submenu the Window menu carries — because the user who has just closed a
panel is exactly the user who needs to find it again. Close is a real registered command per panel
(`studio.window.closePanel.<id>`), enabled only while the panel is open and closable, so the
viewport's row is drawn disabled rather than drawn enabled and then refusing. Right-clicking a tab
selects it first: a menu acting on a tab the user could not see was chosen would be acting behind
their back. And it is routed from each tab's own rectangle rather than from the strip, so a
right-click in the empty space beside the last tab does nothing rather than acting on whichever
panel happened to be nearest.

**Verification.** `tests/StudioContextMenuTests.cpp` — placement and edge flipping, submenus inside
one, invoking and closing, dismissal with either button, Escape one level at a time, arrows and
Enter, sideways arrows *not* walking the menu bar, one chain at a time, the blocking layer and a
tab that stops being hoverable underneath it, the empty menu, and the five tab-menu behaviours.
Plus `CnaStudioShellPreviewContextMenu`, which photographs one, and
`CnaStudioRejectsAContextMenuThatNeverOpens`

### `STUDIO-06024` — The About dialog, saying what this build actually is

**Acceptance.** `studio.help.about` opens a real modal rather than being a menu row that does
nothing, and what it says is supplied by whoever assembled Studio.

**The host sets the text.** The version, the renderer and the platform are facts about a *build*,
and a shell carrying its own copy would be a second place they could be wrong — About is exactly
the dialog people quote in a bug report. The UI core ships the lines it can honestly say for
itself, so a preview with no device still shows something true rather than an empty box.

**`--shell-invoke=ID`** runs a command before a preview capture, because a modal is reached by a
menu item and answered by a keystroke, neither of which a still capture can perform. Without it the
one thing CI could never photograph would be the thing that covers everything else
