# Phase 6 — Studio shell

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-06001` … `STUDIO-06999` and are never reused.

**Purpose.** The application frame: menus, toolbar, status bar, the command registry that everything routes through, and preferences.

**Exit criteria.** Menus, toolbars and keyboard shortcuts all invoke the same command objects, and the shell looks like production software.

**Progress:** 1 of 16 complete `█░░░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-06001` | Central command and action registry | ⬜ | `STUDIO-03012` |
| `STUDIO-06002` | Register the core command set | ⬜ | `STUDIO-06001` |
| `STUDIO-06003` | Application menu bar | 🔄 | `STUDIO-06002` |
| `STUDIO-06004` | Submenus, separators, checkable items and shortcut hints | ⬜ | `STUDIO-06003` |
| `STUDIO-06005` | Context menus | ⬜ | `STUDIO-06003` |
| `STUDIO-06006` | Main toolbar | 🔄 | `STUDIO-06002` |
| `STUDIO-06007` | Status bar | 🔄 | `STUDIO-06001` |
| `STUDIO-06008` | Keyboard shortcut dispatch with scope precedence | ⬜ | `STUDIO-06001` |
| `STUDIO-06009` | Preferences model, separate from project settings | ⬜ | `STUDIO-06001` |
| `STUDIO-06010` | Preferences persistence, versioning and migration | ⬜ | `STUDIO-06009` |
| `STUDIO-06011` | Preferences UI | ⬜ | `STUDIO-06009` |
| `STUDIO-06012` | Shortcut rebinding UI with conflict detection | ⬜ | `STUDIO-06008` |
| `STUDIO-06013` | Empty states for every panel | ⬜ | `STUDIO-06003` |
| `STUDIO-06014` | Notification and toast system for background results | ⬜ | `STUDIO-06007` |
| `STUDIO-06016` | Shell preview entry point on the real executable | ✅ | `STUDIO-06003` |
| `STUDIO-06015` | The `cna-studio` executable starts on the new shell by default | 🔄 | `STUDIO-06003`, `STUDIO-05009` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-06001` — Central command and action registry

**Acceptance.** Commands carry id, label, icon, enablement predicate, shortcut and handler. Menus, toolbars and shortcuts invoke the same object; no command logic lives in button-drawing code

### `STUDIO-06002` — Register the core command set

**Acceptance.** Save, Save All, Undo, Redo, Duplicate, Delete, Focus Selected, Play, Stop, Build, Package, Toggle Grid, Translate, Rotate, Scale

### `STUDIO-06003` — Application menu bar

**Acceptance.** File, Edit, View, Project, Build, Play, Tools, Window, Help — driven by the command registry

### `STUDIO-06006` — Main toolbar

**Acceptance.** Save, undo/redo, transform mode, snapping, play controls, build target profile

### `STUDIO-06007` — Status bar

**Acceptance.** Current renderer, target profile, background job progress, project state

### `STUDIO-06008` — Keyboard shortcut dispatch with scope precedence

**Acceptance.** A shortcut inside a text field does not trigger a global command

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

