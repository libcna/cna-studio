# Phase 6 — Studio shell

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-06001` … `STUDIO-06999` and are never reused.

**Purpose.** The application frame: menus, toolbar, status bar, the command registry that everything routes through, and preferences.

**Exit criteria.** Menus, toolbars and keyboard shortcuts all invoke the same command objects, and the shell looks like production software.

**Progress:** 13 of 23 complete `███████░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-06001` | Central command and action registry | ✅ | `STUDIO-03012` |
| `STUDIO-06002` | Register the core command set | ✅ | `STUDIO-06001` |
| `STUDIO-06023` | Bind the core commands to the editor, with live enablement | ✅ | `STUDIO-06002`, `STUDIO-06022` |
| `STUDIO-06003` | Application menu bar | ✅ | `STUDIO-06002` |
| `STUDIO-06004` | Submenus, separators, checkable items and shortcut hints | ✅ | `STUDIO-06003` |
| `STUDIO-06005` | Context menus | ⬜ | `STUDIO-06003` |
| `STUDIO-06006` | Main toolbar | 🔄 | `STUDIO-06002` |
| `STUDIO-06007` | Status bar | 🔄 | `STUDIO-06001` |
| `STUDIO-06008` | Keyboard shortcut dispatch with scope precedence | ✅ | `STUDIO-06001` |
| `STUDIO-06009` | Preferences model, separate from project settings | ⬜ | `STUDIO-06001` |
| `STUDIO-06010` | Preferences persistence, versioning and migration | ⬜ | `STUDIO-06009` |
| `STUDIO-06011` | Preferences UI | ⬜ | `STUDIO-06009` |
| `STUDIO-06012` | Shortcut rebinding UI with conflict detection | 🔄 | `STUDIO-06008` |
| `STUDIO-06013` | Empty states for every panel | ⬜ | `STUDIO-06003` |
| `STUDIO-06014` | Notification and toast system for background results | ⬜ | `STUDIO-06007` |
| `STUDIO-06016` | Shell preview entry point on the real executable | ✅ | `STUDIO-06003` |
| `STUDIO-06015` | The `cna-studio` executable starts on the new shell by default | 🔄 | `STUDIO-06003`, `STUDIO-05009` |
| `STUDIO-06017` | Nested submenus, opening on hover, with keyboard traversal | ✅ | `STUDIO-06004` |
| `STUDIO-06018` | `StudioShell`: the application frame as an interactive object driving the frame lifecycle | ✅ | `STUDIO-03015`, `STUDIO-03031` |
| `STUDIO-06019` | Capture the shell's interaction states from the preview entry point | ✅ | `STUDIO-06016`, `STUDIO-06018` |
| `STUDIO-06020` | `--ui=studio`: the native shell in a real window, on a real CNA device | ✅ | `STUDIO-06018`, `STUDIO-02021` |
| `STUDIO-06021` | Window smoke tests for the native shell, on a real renderer | ✅ | `STUDIO-06020` |
| `STUDIO-06022` | The native shell opens a project, on the editor's own context | ✅ | `STUDIO-06020` |

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

**In progress.** Save, undo/redo, the three transform modes, the grid toggle, play/stop and build
are present, registry-driven, correctly enabled and disabled, and invoke the same actions the menus
do. **Snapping and the build target profile are not**: snapping has no action yet and the target
profile needs the model of `STUDIO-02040`

### `STUDIO-06007` — Status bar

**Acceptance.** Current renderer, target profile, background job progress, project state

**In progress.** Two text slots exist and are drawn. Background job progress and the target profile
need the services that own them

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

### `STUDIO-06010` — Preferences persistence, versioning and migration

**Acceptance.** Corrupt preferences never make a project unopenable; the fallback is reported, not silent

### `STUDIO-06013` — Empty states for every panel

**Acceptance.** A panel with nothing in it explains what it is for and what to do next, rather than showing blank space

### `STUDIO-06016` — Shell preview entry point on the real executable

**Acceptance.** `cna-studio --shell-preview=PATH` renders the native shell and writes a PNG, with `--shell-size`, `--shell-scale` and `--shell-theme`. Headless: no GPU, no display. A preview rather than `--ui=studio`, because the shell is not yet interactive and a flag that opened an unresponsive window would be the worse lie

**Verification.** Five CTest cases: three renders and two malformed-argument rejections

### `STUDIO-06015` — The `cna-studio` executable starts on the new shell by default

**Acceptance.** The new shell becomes the default as soon as it is good enough for daily development, with the legacy UI still reachable behind a flag

**Status.** `--ui=studio` exists and runs the real thing (`STUDIO-06020`), so the remaining work is
the word *default*, not the word *reachable*. It stays 🔄 until the shell hosts a migrated panel:
a default that opens an editor with no inspector would be a regression however good it looks.

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
