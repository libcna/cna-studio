# Phase 9 — Content Browser 2

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-09001` … `STUDIO-09999` and are never reused.

**Purpose.** Turn the prototype asset browser into a professional content workflow that scales to a real project.

**Exit criteria.** Tens of thousands of assets browse, search and filter responsively, and no file operation can break a scene reference.

**Progress:** 1 of 16 complete `░░░░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-09001` | Folder tree and breadcrumb navigation | ✅ | `STUDIO-07008` |
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

### `STUDIO-09001` — Folder tree and breadcrumb navigation

**Done.** A folders-only tree down the left of the Content Browser, with a draggable splitter, and
the breadcrumb `STUDIO-35040` already put above both views.

**Folders only, and that is the whole point.** A tree holding every asset in the project is a
second copy of the content pane; the reason to have a tree at all is to move between folders
without reading their contents. The list view's rows (`studioContentRows`) keep showing files
because that is what the list *is*; the pane's rows (`studioContentFolderRows`) never do.

**Beside both presentations rather than inside either.** Where a user is and what they are looking
at are two questions, and a navigation tree that appeared only in the grid would make switching
views also mean switching how you move around.

**Three decisions that each had an obvious wrong answer.**

- **Counts are what is *directly* in a folder**, not what is under it. A cumulative count makes
  `Assets` read as holding the whole project — true, and useless: the number a user wants beside a
  folder is how much they will see when they click it. A folder holding only other folders shows
  no count at all, because `0` beside a folder full of subfolders reads as empty.
- **`Project` is a row.** A tree whose only way back to the top is collapsing everything is a tree
  people navigate by clicking the breadcrumb instead, which makes half the pane decoration. It
  carries the empty path, which is what `StudioContentBrowserState::folder` already holds for the
  root — so navigating is one assignment rather than a special case at every reader. Its id is the
  constant `kStudioContentRootRowId` rather than the empty string, which would have shared its
  expansion with every row that had not been given one.
- **A folder with subfolders and no files of its own still gets a triangle.** Presenting a leaf
  that turns out to have children is the one thing a tree must never do. The check is a prefix
  match on `path + "/"`, so `Assets2` is not mistaken for a child of `Assets` however the strings
  sort.

**Its own expansion state**, kept apart from the list's. Two trees showing different things sharing
one expansion set means collapsing `Assets/Textures` in the navigation pane folds away the files
the content pane is showing, which reads as the browser losing its place.

**The pane hides rather than squeezing.** Dragging it below three rows' width collapses it to
nothing, and a browser docked into a narrow strip drops it entirely — two things too thin to read
are worse than one thing that fits.

**Verification.** `tests/StudioContentBrowserTests.cpp`: folders and no files at all (including a
file at the project root, which a "has a slash in it" filter would get wrong), the root row's
selection and id, direct-not-cumulative counts, the triangle rule and the `Assets2` prefix case,
collapse at both levels, and — through a real shell frame, in **both** views — the pane drawing and
a click on a folder navigating there.

### `STUDIO-09009` — Rename, move, duplicate and delete, all undoable

**Acceptance.** A move or rename preserves the asset UUID, so no scene is touched and no reference breaks

**Verification.** Test: move an asset referenced by a scene; the scene file is unchanged

### `STUDIO-09015` — Source file tracking and derived-data cache separation

**Acceptance.** Caches and derived content are clearly separated from authoritative source and are not version-controlled

### `STUDIO-09016` — Virtualised browsing for very large asset counts

**Acceptance.** Scales toward 100,000 assets without rescanning or rehashing everything

**Verification.** Stress test at 100,000 synthetic assets

