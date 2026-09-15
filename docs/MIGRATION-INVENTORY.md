# Prototype inventory — the migration checklist

`plan.md` STUDIO-00014.

Every panel, menu item and keyboard shortcut the Dear ImGui prototype offers, with the source file
that owns it and what the native Studio shell does about it. Phase 7 proves parity **against this
list**, item by item, rather than by impression — and the list is checked by the test suite
(`STUDIO-00014`), so an item that quietly loses its native counterpart fails the build rather than
being noticed by somebody a year later.

## How to read it

| Column | Meaning |
|--------|---------|
| Native | The shell panel id, the action id, or `—` when nothing native answers it yet |
| Status | ✅ answered natively · 🔄 answered but not yet proven equivalent · ⬜ no native answer |

An item with status ✅ **must** resolve: a panel id must be registered and have content, and an
action id must exist in the registry. That is what makes this a checklist rather than a record of
intentions. An item with ⬜ carries the reason it has none, in the same discipline as the
unimplemented-command and empty-panel guards.

The prototype is `src/panels/*.cpp` and `src/app/StudioApplication.cpp`; it is a *temporary*
compatibility fallback and is deleted by `STUDIO-07030`, at which point this file becomes the record
of what was replaced.

---

## Panels

| Prototype panel | Source | Native | Status |
|-----------------|--------|--------|--------|
| Scene Hierarchy | `src/panels/HierarchyPanel.cpp` | `outliner` | ✅ |
| Inspector | `src/panels/InspectorPanel.cpp` | `details` | ✅ |
| Assets | `src/panels/AssetBrowserPanel.cpp` | `content` | ✅ |
| Console | `src/panels/ConsolePanel.cpp` | `output` | ✅ |
| Build | `src/panels/BuildPanel.cpp` | `build` | ✅ |
| Validation | `src/panels/ValidationPanel.cpp` | `problems` | ✅ |
| History | `src/panels/HistoryPanel.cpp` | `history` | ✅ |
| Diagnostics | `src/panels/DiagnosticsPanel.cpp` | `diagnostics` | ✅ |
| Backends | `src/panels/ComparisonPanel.cpp` | `comparison` | ✅ |
| Viewport | `src/panels/ViewportPanel.cpp` | `viewport` | 🔄 |

The Viewport is 🔄 rather than ✅ because the native one composites the 2D scene, navigates, picks
and manipulates, and the prototype's also has the 3D view and tilemap painting. Those are the
substance of what keeps `STUDIO-06015` from being true, and they are listed under *Not yet answered*
below rather than hidden inside a ✅.

Panels the native shell adds, which the prototype has no equivalent for: `layers`, `preferences`,
`material` (registered, no content yet — Phase 19).

---

## Menu items

| Menu | Item | Shortcut | Native | Status |
|------|------|----------|--------|--------|
| File | New Scene | `Ctrl+N` | `studio.file.newScene` | ✅ |
| File | Save Scene | `Ctrl+S` | `studio.file.save` | ✅ |
| File | Recover Unsaved Scene | — | — | ⬜ |
| File | Discard Recovered Scene | — | — | ⬜ |
| File | Exit | `Alt+F4` | `studio.file.quit` | 🔄 |
| Edit | Undo | `Ctrl+Z` | `studio.edit.undo` | ✅ |
| Edit | Redo | `Ctrl+Y` | `studio.edit.redo` | ✅ |
| Edit | Duplicate | `Ctrl+D` | `studio.edit.duplicate` | ✅ |
| Edit | Delete | `Delete` | `studio.edit.delete` | ✅ |
| View | Frame Selected | `F` | `studio.view.focusSelected` | ✅ |
| View | 2D / 3D View | `2` / `3` | — | ⬜ |
| View | Grid on Scene / Ground Plane | — | — | ⬜ |
| View | Translate Gizmo | `W` | `studio.view.translate` | ✅ |
| View | Rotate Gizmo | `E` | `studio.view.rotate` | ✅ |
| View | Scale Gizmo | `R` | `studio.view.scale` | ✅ |
| View | Use Local / World Space | `X` | `studio.view.toggleGizmoSpace` | ✅ |
| *plugin* | Plugin menus and commands | — | — | ⬜ |

**Recover / Discard Unsaved Scene** are ⬜ because crash recovery has no native surface yet; the
model (`RecoverySnapshot`) is shared and works, so this is a menu binding rather than a feature.

**Exit** is 🔄 because the command exists and is refused: the shell has no way to ask its host to
close, which is what `STUDIO-06015` needs anyway.

**Plugin menus** are ⬜ and are the one item here that is genuinely architectural. The prototype
lets a plugin add a top-level menu and commands under it; the native shell's menus are built from
action ids, so a plugin would register actions and a menu definition rather than draw rows. That is
Phase 28 work.

---

## Keyboard shortcuts

Taken from `StudioApplication::handleShortcuts`. Each must resolve to a native action bound to the
**same** chord: a shortcut that moved is a shortcut every existing user has to relearn.

| Chord | Prototype action | Native | Status |
|-------|------------------|--------|--------|
| `Ctrl+Z` | Undo | `studio.edit.undo` | ✅ |
| `Ctrl+Y` | Redo | `studio.edit.redo` | ✅ |
| `Ctrl+N` | New scene | `studio.file.newScene` | ✅ |
| `Ctrl+S` | Save scene | `studio.file.save` | ✅ |
| `Ctrl+D` | Duplicate selection | `studio.edit.duplicate` | ✅ |
| `Delete` | Delete selection | `studio.edit.delete` | ✅ |
| `F2` | Rename selection | — | ⬜ |
| `F` | Frame selection | `studio.view.focusSelected` | ✅ |
| `2` | 2D view | — | ⬜ |
| `3` | 3D view | — | ⬜ |
| `W` | Translate gizmo | `studio.view.translate` | ✅ |
| `E` | Rotate gizmo | `studio.view.rotate` | ✅ |
| `R` | Scale gizmo | `studio.view.scale` | ✅ |
| `X` | Toggle gizmo space | `studio.view.toggleGizmoSpace` | ✅ |

`F2` is ⬜ because renaming in place is an outliner behaviour the native tree view does not have
yet; the Details panel's name field edits the same thing.

### What writing this list down found

Three chords disagreed with the prototype's, which is exactly the class of thing an inventory exists
to catch and nothing else would have:

- **`Ctrl+N` meant a different thing.** New *Scene* in the prototype, New *Project* natively. The
  frequent one keeps the plain chord; New Project moved to `Ctrl+Shift+N`, as in most IDEs. There
  was no native New Scene command at all, so it was added.
- **`Ctrl+D` was taken by Open Project.** It is Duplicate in the prototype and in every editor that
  has a duplicate; native Duplicate had been pushed to `Ctrl+Shift+D`. Open Project moved to the
  conventional `Ctrl+O`, which needed a key the vocabulary did not have.
- **`X` had no native command.** The gizmo space could be changed from nothing but code. It is a
  toggle now, as it is in the prototype, and checkable so a toolbar can show which space is on.

Each would have shipped as "the shortcut I have used for a year does something else now", which is
the worst kind of regression: it works, so nothing reports it.

---

## Not yet answered, and what each needs

These are the reason the native shell is not the default (`STUDIO-06015`). None is a research
project; each is a panel or a mode with a known shape.

| What | Prototype home | What it needs |
|------|----------------|---------------|
| The 3D view | `ViewportPanel`, `StudioApplication` | A 3D camera and mesh drawing in the native viewport (Phase 11) |
| Tilemap painting | `ViewportPanel`, `StudioTool` | Paint, erase, pick and fill as viewport tools with a brush (Phase 25 adjacent) |
| Material editing | — | There is no `.cnamaterial` editor to port; the `material` panel is registered and empty (Phase 19) |
| Crash recovery surface | `MainMenuBar` | Two menu bindings over `RecoverySnapshot`, which already works |
| Plugin menus | `MainMenuBar::drawPluginMenus` | Plugins registering actions and menu definitions rather than drawing rows (Phase 28) |
| Rename in place | `HierarchyPanel` | An editable tree row |
