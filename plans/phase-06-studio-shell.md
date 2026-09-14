# Phase 6 — Studio shell

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-06001` … `STUDIO-06999` and are never reused.

**Purpose.** The application frame: menus, toolbar, status bar, the command registry that everything routes through, and preferences.

**Exit criteria.** Menus, toolbars and keyboard shortcuts all invoke the same command objects, and the shell looks like production software.

**Progress:** 7 of 19 complete `████░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-06001` | Central command and action registry | ✅ | `STUDIO-03012` |
| `STUDIO-06002` | Register the core command set | ✅ | `STUDIO-06001` |
| `STUDIO-06003` | Application menu bar | ✅ | `STUDIO-06002` |
| `STUDIO-06004` | Submenus, separators, checkable items and shortcut hints | 🔄 | `STUDIO-06003` |
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
| `STUDIO-06017` | Nested submenus, opening on hover, with keyboard traversal | ⬜ | `STUDIO-06004` |
| `STUDIO-06018` | `StudioShell`: the application frame as an interactive object driving the frame lifecycle | ✅ | `STUDIO-03015`, `STUDIO-03031` |
| `STUDIO-06019` | Capture the shell's interaction states from the preview entry point | ✅ | `STUDIO-06016`, `STUDIO-06018` |

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

**In progress.** Separators, checkable items with a reserved check column, and right-aligned
shortcut hints that widen the menu rather than being clipped are done and tested. **Submenus are
not**, and the task stays 🔄 until they are: the arrow is drawn for an item that declares one, but
nothing opens. `STUDIO-06017` is the remaining work

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

