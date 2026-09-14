# Phase 5 — Docking and workspace

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-05001` … `STUDIO-05999` and are never reused.

**Purpose.** A first-class panel shell: docking, tab groups, splitters, floating panels and persistent layouts.

**Exit criteria.** A user can rearrange the whole workspace, restore defaults, and have their arrangement survive a restart and a Studio upgrade.

**Progress:** 2 of 13 complete `██░░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-05001` | Create `cna-studio-ui-docking`; dock node tree model | ⬜ | `STUDIO-03015` |
| `STUDIO-05002` | Split nodes horizontally and vertically | ✅ | `STUDIO-05001` |
| `STUDIO-05003` | Resizable splitters with minimum sizes and correct cursor shapes | ⬜ | `STUDIO-05002` |
| `STUDIO-05004` | Tab stacks with reordering | ⬜ | `STUDIO-05001` |
| `STUDIO-05005` | Dock a panel to an edge or into a tab group by drag, with drop-target preview | ⬜ | `STUDIO-05004` |
| `STUDIO-05006` | Undock to a floating panel | ⬜ | `STUDIO-05005` |
| `STUDIO-05007` | Hide, show and close panels | 🔄 | `STUDIO-05001` |
| `STUDIO-05008` | Serialize the workspace layout | ⬜ | `STUDIO-05001` |
| `STUDIO-05009` | Restore the default layout | ⬜ | `STUDIO-05008` |
| `STUDIO-05010` | Named saved layouts | ⬜ | `STUDIO-05008` |
| `STUDIO-05011` | Layout migration across Studio versions | ⬜ | `STUDIO-05008` |
| `STUDIO-05012` | A corrupt layout file never prevents Studio from starting | ⬜ | `STUDIO-05011` |
| `STUDIO-05013` | Tab strips that switch the active panel on click | ✅ | `STUDIO-03031` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-05013` — Tab strips that switch the active panel on click

**Acceptance.** Each dock region draws a tab per panel, the active one is marked by an accent rule
rather than a fill alone, and clicking a tab makes its panel active. The modified marker is a dot
rather than an asterisk in the label, so editing a document does not shift every tab in the strip

**Verification.** `tests/StudioShellInteractionTests.cpp`

### `STUDIO-05006` — Undock to a floating panel

**Acceptance.** Floating windows only where the platform supports them; degrades to an in-shell floating layer otherwise

### `STUDIO-05008` — Serialize the workspace layout

**Acceptance.** Deterministic, versioned, and stored in user preferences rather than in shared project data

### `STUDIO-05011` — Layout migration across Studio versions

**Acceptance.** A layout from an older version opens, with unknown panels dropped and reported — never a failure to start

**Verification.** Test with a synthetic old-version layout and an unknown panel id

### `STUDIO-05012` — A corrupt layout file never prevents Studio from starting

**Acceptance.** Falls back to the default layout and reports what it could not read

