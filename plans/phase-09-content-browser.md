# Phase 9 — Content Browser 2

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-09001` … `STUDIO-09999` and are never reused.

**Purpose.** Turn the prototype asset browser into a professional content workflow that scales to a real project.

**Exit criteria.** Tens of thousands of assets browse, search and filter responsively, and no file operation can break a scene reference.

**Progress:** 0 of 16 complete `░░░░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-09001` | Folder tree and breadcrumb navigation | ⬜ | `STUDIO-07008` |
| `STUDIO-09002` | Grid and list views | ⬜ | `STUDIO-09001` |
| `STUDIO-09003` | Thumbnail generation as cancellable background jobs | ⬜ | `STUDIO-30001` |
| `STUDIO-09004` | Thumbnail cache keyed by content, invalidated on reimport | ⬜ | `STUDIO-09003` |
| `STUDIO-09005` | Search across name, type and path | ⬜ | `STUDIO-09001` |
| `STUDIO-09006` | Filter by asset type, and sorting | ⬜ | `STUDIO-09005` |
| `STUDIO-09007` | Favourites and recent assets | ⬜ | `STUDIO-09001` |
| `STUDIO-09008` | Drag and drop into the viewport, Inspector and hierarchy | ⬜ | `STUDIO-03023` |
| `STUDIO-09009` | Rename, move, duplicate and delete, all undoable | ⬜ | `STUDIO-09001` |
| `STUDIO-09010` | Reimport, preserving Studio-side import settings | ⬜ | `STUDIO-10001` |
| `STUDIO-09011` | Reveal in the system file manager | ⬜ | `STUDIO-09001` |
| `STUDIO-09012` | Dependency view: references-to and referenced-by | ⬜ | `STUDIO-09001` |
| `STUDIO-09013` | Missing asset handling with a clear path to relink | ⬜ | `STUDIO-09012` |
| `STUDIO-09014` | Asset metadata and import settings UI | ⬜ | `STUDIO-09001` |
| `STUDIO-09015` | Source file tracking and derived-data cache separation | ⬜ | `STUDIO-09004` |
| `STUDIO-09016` | Virtualised browsing for very large asset counts | ⬜ | `STUDIO-30010` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-09009` — Rename, move, duplicate and delete, all undoable

**Acceptance.** A move or rename preserves the asset UUID, so no scene is touched and no reference breaks

**Verification.** Test: move an asset referenced by a scene; the scene file is unchanged

### `STUDIO-09015` — Source file tracking and derived-data cache separation

**Acceptance.** Caches and derived content are clearly separated from authoritative source and are not version-controlled

### `STUDIO-09016` — Virtualised browsing for very large asset counts

**Acceptance.** Scales toward 100,000 assets without rescanning or rehashing everything

**Verification.** Stress test at 100,000 synthetic assets

