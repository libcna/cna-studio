# Phase 9 — Content Browser 2

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-09001` … `STUDIO-09999` and are never reused.

**Purpose.** Turn the prototype asset browser into a professional content workflow that scales to a real project.

**Exit criteria.** Tens of thousands of assets browse, search and filter responsively, and no file operation can break a scene reference.

**Progress:** 8 of 17 complete `█████░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-09001` | Folder tree and breadcrumb navigation | ✅ | `STUDIO-07008` |
| `STUDIO-09002` | Grid and list views | ✅ | `STUDIO-09001` |
| `STUDIO-09003` | Thumbnail generation as cancellable background jobs | ⬜ | `STUDIO-30001` |
| `STUDIO-09004` | Thumbnail cache keyed by content, invalidated on reimport | ⬜ | `STUDIO-09003` |
| `STUDIO-09005` | Search across name, type and path | ✅ | `STUDIO-09001` |
| `STUDIO-09006` | Filter by asset type, and sorting | ✅ | `STUDIO-09005` |
| `STUDIO-09007` | Favourites and recent assets | ⬜ | `STUDIO-09001` |
| `STUDIO-09008` | Drag and drop into the viewport, Inspector and hierarchy | ⬜ | `STUDIO-03023` |
| `STUDIO-09009` | Rename, move, duplicate and delete, all undoable | ✅ | `STUDIO-09001` |
| `STUDIO-09010` | Reimport, preserving Studio-side import settings | ⬜ | `STUDIO-10001` |
| `STUDIO-09011` | Reveal in the system file manager | ⬜ | `STUDIO-09001` |
| `STUDIO-09012` | Dependency view: references-to and referenced-by | ✅ | `STUDIO-09001` |
| `STUDIO-09013` | Missing asset handling with a clear path to relink | ✅ | `STUDIO-09012` |
| `STUDIO-09014` | Asset metadata and import settings UI | ✅ | `STUDIO-09001` |
| `STUDIO-09015` | Source file tracking and derived-data cache separation | ⬜ | `STUDIO-09004` |
| `STUDIO-09016` | Virtualised browsing for very large asset counts | ⬜ | `STUDIO-30010` |
| `STUDIO-09017` | A reference on a component with no descriptor survives a save and reload | ⬜ | — |

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

### `STUDIO-09002` — Grid and list views

**Done, and the work was making them the *same* view.** Both now draw `studioContentCards` — one
folder's subfolders and then its assets — so the grid and the list differ in presentation and in
nothing else.

**They did not before, and that was a real bug the folder pane exposed.** The list showed the whole
project as a tree and the grid showed one folder, so switching views also moved the user; and
`STUDIO-09001`'s navigation pane could only navigate one of the two. A browser where the same
folder selection means different things in two tabs is a browser that has two ideas of where you
are.

**In the list, entering a folder is a click**, the way it is in the grid and in every file manager,
rather than a disclosure triangle. An expansion would put two folders' contents on screen at once
and leave the breadcrumb describing only one of them. Rows are flat for the same reason:
indentation says "this is inside that", and inside one folder everything is at the same level.

**`studioContentRows` is deleted.** It built the whole-project tree and had no caller left; the
behaviours `STUDIO-07008` accepted on it are all still asserted, on the functions that do the job
now:

| What `STUDIO-07008` accepted | Where it is checked now |
|------------------------------|-------------------------|
| Folders derived from paths, parents first | `TheFolderPaneShowsFoldersAndNoFilesAtAll` |
| Collapsing hides subfolders as well as files | `CollapsingInTheFolderPaneHidesDescendantsAndCollapsingTheRootHidesEverything` |
| Files sort within a folder | `FilesSortWithinTheirFolderSoARescanDoesNotShuffleThem` |
| A missing source is listed and marked | `AnAssetWhoseFileHasGoneIsListedAndMarked` |
| …and is dimmed rather than disabled | `AMissingAssetsCardSaysSoInTheWarningColour`, plus the list's `muted` mapping |
| A file shows its type, a folder its count | `AFileShowsItsTypeAndAFolderShowsHowMuchIsInIt` |
| Clicking a file selects it, a folder does not | `ClickingAFileSelectsItAndClickingAFolderDoesNot`, which drives the real panel in list view |

The last of those is the one that mattered: it clicks through the shell rather than calling the
model, so it exercised the new list path without being rewritten.

### `STUDIO-09005` — Search across name, type and path

**Done.** A field in the bar; a non-empty query leaves the folder behind and looks at the whole
project.

**Scoping a search to the current folder is the wrong answer**, and it is the tempting one because
it is one line. It is the behaviour that makes somebody type a name, see nothing, and conclude the
asset is gone when it is one folder over.

**Matching is ranked, not merely filtered.** A search over name *and* type *and* path matches a
great deal, and the order is what makes the result usable: a name that starts with what was typed
(0), a name that contains it (1), a type that starts with it (2), a path that contains it (3), a
type that contains it (4). A list sorted purely by name would bury an exact hit under everything
whose path happens to contain the word. `studioContentMatches` and the ranking are separate from
the drawing, so they are tested with no frame at all.

**Each result says where it is**, in place of the kind — which the icon already shows. Two files
called `player.*` three folders apart are otherwise two identical rows, which is the one thing a
flat result list must not be.

**No folders in results.** A folder row's click means "go here" and an asset row's means "select
this", and a result list mixing the two is a list where the same gesture does different things.

### `STUDIO-09006` — Filter by asset type, and sorting

**Done.** A kind dropdown and Name/Kind/reverse buttons, behind a disclosure button rather than
always on the bar — four controls above the smallest panel in the default layout, for a browser
that most often needs none of them.

**The filter offers only the kinds the project actually holds.** One offering ten kinds a project
has none of is one nobody reads, and one whose length changes as a project grows is one that
teaches its own positions and then moves them.

**A filter never hides folders.** Filtering to textures and thereby hiding the folder the textures
are in is filtering a user out of their own project. Reversing leaves folders where they are for
the same reason: they are navigation, and navigation that reorders itself is navigation people stop
trusting.

**Writing the test found a real defect.** Sorting by kind compared `toString(type) < toString(type)`
— two `const char*`, so the comparison was on **pointers**: an order that is not alphabetical, not
stable across builds, and not necessarily the same twice within one. It is `std::string_view` now,
and the case that caught it asserts the actual sequence rather than "it is sorted".

**Narrowed and empty are different messages.** "This folder is empty" while a filter is on sends
somebody looking in the wrong place; "Nothing matches" names a control they can turn off.

### `STUDIO-09009` — Rename, move, duplicate and delete, all undoable

**Acceptance.** A move or rename preserves the asset UUID, so no scene is touched and no reference breaks

**Verification.** Test: move an asset referenced by a scene; the scene file is unchanged

**Done.** Rename in place in both views, move by dragging onto a folder, duplicate and delete from a
right-click menu — all four through `CommandHistory`, and `F2`, `Ctrl+D` and `Delete` reaching the
same operations from the keyboard.

**A rename is a move, so there is one code path rather than two.** The acceptance condition holds
because `MoveAssetCommand` is the only thing that changes an asset's path: a rename is a move whose
destination happens to be the same folder, and a folder rename is a `CompositeCommand` of the same
moves. Two implementations could have disagreed about whether the id survives; one cannot. The test
asserts the *file* is byte-identical, not that the scene still loads — a move that rewrote every
scene would pass the weaker check and the user would find out at review time.

**Four decisions, each with an obvious wrong answer.**

- **A duplicate gets a new id, generated once in the constructor.** New, because two files sharing
  an id would leave the database unable to say which one a scene references, and the first scan to
  notice would pick whichever it walked last. Once, because undo-then-redo must restore *the same*
  copy: a redo that minted a fresh id would strand every reference the user had since made to it.
- **A delete captures the bytes, and undo restores the same id.** A scene referencing the asset is
  not touched by the delete — the reference dangles until the undo — so a restore under a new id
  would break exactly the references the undo exists to repair. The bytes are read in the
  constructor, before anything is removed, so an unreadable file produces an invalid command rather
  than a deletion whose undo would be a menu entry that lies. That is also why an asset whose file
  has already gone cannot be deleted at all.
- **No confirmation dialog.** The undo stack *is* the confirmation. A dialog in front of an
  operation that is already reversible teaches people to dismiss dialogs, and the one that matters
  later gets dismissed too.
- **Names are checked against the strictest platform, not the host.** `<>:"|?*`, control
  characters, a trailing dot or space, and the DOS device names are refused on Linux as well, so a
  project renamed here still checks out on Windows. The person who would have created that
  repository is the one person who never finds out.

**A folder offers Rename and nothing else.** Duplicating one is copying every file under it, which
is a job rather than an edit (`STUDIO-30001`); an undoable folder delete would hold every byte
under it in the undo stack, which is reasonable for one texture and not for four hundred. Moving a
folder *is* offered, because it is N renames that capture nothing.

**`canMoveAsset` was extracted so validity and execution cannot disagree.** The destination checks
lived inside `moveAsset` and were therefore only discovered at execute time — which for a command
means it lands on the undo stack, does nothing, and tells the user it worked. The rule is now asked
once by the constructor and once by the move, from one function.

**The right-click menu is the panel's own, not the shell's.** `StudioShell::openContextMenu` takes
*action ids* from the registry, which is right for commands that also live in the menu bar and
wrong for rows that exist only while one asset is under the pointer: registering "Duplicate" as an
application action would put it in the command palette, where there is no pointer and nothing under
it. So `studioContextMenu` was added beside `studioDropdown`, on the same deferred-popup mechanism.

**One `F2`, one `Ctrl+D`, one `Delete`.** The entity selection and the asset selection are mutually
exclusive by construction — `select()` clears one and `selectAsset()` clears the other — so the
three existing edit actions were extended to act on whichever is live rather than given asset-only
twins the user would have to know the difference between.

**Right-click asks about the row under the pointer, never the selection.** A menu that read the
selection would be right whenever the user right-clicked what they had already selected — which is
most of the time, and never when it matters.

**Verification.** `tests/ProjectAndAssetTests.cpp`: the acceptance test (a scene file byte-identical
across a move *and* its undo), a rename keeping the id and taking the sidecar with it, name rules
against every platform, duplicate naming and id behaviour including redo, delete-and-undo restoring
bytes, id and importer settings, both operations refused on a missing source, a folder rename as
one undo entry with `Textures2` left alone, and the destinations a folder move refuses.
`tests/StudioContentBrowserTests.cpp`: the menu rows for an asset, a missing asset, a folder and
empty space; rename, move, duplicate and delete through the browser; a refused rename changing
nothing; and — through a real shell frame, in **both** views — a right-click asking about the row
rather than the selection. `tests/StudioShellActionTests.cpp`: `Ctrl+D` and `Delete` acting on the
selected asset, and greying out when nothing is selected. A guard test asserts the menu's shortcut
hints are the chords the action registry actually binds, because a hint that has drifted is a
promise the user stops trusting rather than a bug they report.

### `STUDIO-09012` — Dependency view: references-to and referenced-by

**Done.** `AssetDependencyIndex` builds the project's reference graph, and the asset inspector shows
both directions with every row clickable.

**Nothing on disk records this, which is why it needs an index.** A scene holds a Uuid, not a path
(D-08) — that is what makes moving a file free, and it is also what makes "what breaks if I delete
this?" unanswerable by looking at the file. The only way to know that `Level.cnascene` uses
`player.png` is to read every file that could hold an id.

**Both directions out of one pass**, because they are the same data read two ways. An index that
computed "referenced by" from one walk and "references" from another would be two things that could
disagree, and the disagreement would show up as a delete that looked safe.

**Four decisions with an obvious wrong answer.**

- **Stored properties, not descriptors.** A component whose plugin failed to load keeps its data and
  has its references counted like any other. A descriptor-driven walk would skip exactly the file a
  dependency view is opened for. (`STUDIO-09017` is the half of this that a save and reload still
  loses.)
- **A nil reference is an empty slot, not an edge.** A sprite with no texture yet points at nothing;
  a graph that recorded it would answer "what uses nothing?" with every empty slot in the project.
- **An unreadable file is a warning, not a failed build.** A project with one broken prefab still
  has a useful answer for every other asset in it, and an index that refused to build would take the
  view away precisely when something is wrong.
- **The open scene overrides what was read from disk.** `observeScene` *replaces* rather than merges,
  so a reference the user just removed leaves the answer. A view built only from files is right until
  the user edits something, which is exactly when they ask.

**Built lazily, and that is a compromise recorded rather than hidden.** The walk reads every scene,
prefab and material in the project, so it is rebuilt when an asset is inspected and something has
changed since — at most once per change, paid by the user who asked. `STUDIO-30001`'s background
jobs are what remove the pause.

**A row is a way through the graph, not a report.** "What uses this" is followed by "and what does
that use", so every row selects what it names. An id with no record is shown as `Missing: <id>`
rather than skipped — it is the single most useful row in the section.

**The asset inspector had three exits, and two of them skipped the section.** A material returned
from its own branch and an asset with no importer settings returned from another, so the dependency
view would have appeared for the one case in three that has an importer with settings to show. They
are now one exit.

**Verification.** `tests/AssetDependencyTests.cpp`: both directions from one build, materials'
texture fields, prefabs and importer-declared dependencies, nil references ignored, an unreadable
file warning without losing the rest, a component with no descriptor, and `observeScene` replacing
rather than merging. `tests/StudioDetailsPanelTests.cpp` drives the real panel through a shell
frame: the section finds the reference, a row click navigates to the file holding it, and a build
with no index says so rather than drawing what an unreferenced asset would look like.

### `STUDIO-09013` — Missing asset handling with a clear path to relink

**Done.** An asset whose file has gone says so in its inspector and offers where it probably went,
ranked, one click each.

**The report existed; the repair did not.** A record whose source has vanished is kept rather than
dropped (`AssetDatabase::scan`), because a scene references it by id and forgetting the record
would turn a fixable problem into a broken scene. What that bought was a red row in the Content
Browser and a line in the Problems panel — true, and not a fix. The user's remaining options were to
put the file back exactly where the path says, or to edit a sidecar by hand.

**The two repairs are different operations, and offering the wrong one is expensive.**

- **The file moved and nothing has re-imported it.** The id is still right; only the path is wrong.
  `RelinkAssetFileCommand` points the record at the file, and **no scene is touched** — every
  reference that was broken becomes correct and every reference that was correct stays so.
- **The file moved and a scan has already given it a new id.** Two records exist and the scenes
  point at the wrong one. `RelinkAssetCommand` rewrites the references — a scene edit, because the
  scenes really are wrong.

Confusing them produces two records for one file, or a scene rewritten when nothing was wrong with
it. So `RelinkCandidate` says which kind each suggestion is, the button's tooltip says which of the
two it is about to do, and each command refuses the case it is not for: a relink is refused while
the file is still on disk (that is a *move*), and refused when the destination is already tracked
(that is a reference relink).

**An untracked file always outranks a tracked one**, because the first repair edits nothing and the
second edits every scene that used the asset.

**The extension is part of the name, not a detail.** It decides the asset's type on the next scan,
so a `.jpg` offered in place of a `.png` ranks below every exact match — and is still offered,
because it is a better answer than nothing.

**`repointAsset` is not `moveAsset`.** It moves no file: it changes where a record points and writes
a sidecar at the new location. Without that sidecar the next scan would give the file a fresh id and
break every reference again, which is the failure the relink exists to end.

**Undo goes back to missing**, which is right: undoing a repair restores the state the user had, and
that state was an asset whose file was gone.

**One found limitation, recorded rather than hidden.** The candidate search walks the project root,
and an immediate-mode panel describes itself twice per frame, so it runs twice a frame while a
missing asset is selected. It is guarded by the asset actually being missing — a state a user is
looking at while they repair it, not one a project sits in — and `STUDIO-30001`'s background jobs
are where a search like this belongs when it is not.

**Verification.** `tests/AssetDependencyTests.cpp`: an untracked file preferred over a tracked
lookalike, sidecars never offered as candidates, a relink keeping the id and writing the sidecar at
the new location, undo returning to missing, both refusals, a tracked lookalike offered when a scan
has already re-imported the file, nothing suggested for an id the database does not know, and a
different extension ranked below an exact name. `tests/StudioDetailsPanelTests.cpp` drives the real
panel through a shell frame: the suggestion appears, clicking it repairs the asset and keeps its id,
and an asset with nothing that looks like it says so without the inspector losing its other rows.

### `STUDIO-09014` — Asset metadata and import settings UI

**Done.** The asset inspector now says what the file *is*, which of its import settings are
decisions rather than defaults, and offers a way back to the default.

**Most of the form already existed**, and saying so is the honest part of closing this: the panel
had the identity rows, the importer's heading, one editable row per declared setting, and the
importer's read-only facts (dimensions, mesh counts, font metrics) shown as text. Two things were
missing, and both were ways the panel could mislead.

**An overridden setting looked exactly like one at its default.** They are not the same thing: only
one of them is a decision, and the difference shows up on the day an importer's default changes —
every asset that was never touched follows the new default, and every asset that was does not. So an
override is marked, and the mark is on the *label*, which is the part that is there whatever kind of
editor the row carries.

**Reset removes rather than writes.** Writing the default into the sidecar would freeze the setting
at whatever this build thought the default was, and would put a field in every asset's diff that
nobody chose. `ClearImporterSettingCommand` takes it back out; undo puts back exactly the JSON that
was there, verbatim, because the command does not know the setting's declared type and
round-tripping through a guess would be a reset that quietly changed the value it restored. A
setting nobody set cannot be reset at all — an entry on the undo stack that does nothing reads as
Ctrl+Z having broken.

**The reset sits at the end of the row**, not beside the label: a control that pushed every editor
right by its own width on the rows that have one would make the grid ragged.

**Size and Modified come from the record, not from disk.** The record is what the last scan saw,
which is also what "needs reimporting" is decided against. A panel showing the file's *current* size
would disagree with the check that decides whether a reimport is pending, and the user would be
reading the one that cannot explain it.

**The File group sits below the settings**, because the top of an inspector is for what a user acts
on and this is what they check.

**Two formatters, exposed rather than buried.** Rounding a byte count and converting a filesystem
timestamp are both wrong-without-looking-wrong: binary units keep the number the same as the file
manager beside it, and the modification stamp is seconds on the *filesystem* clock, whose epoch is
not the system clock's everywhere — handing it to a Unix-seconds formatter would print a date decades
out. It goes through `formatRecoveryTime`, so Studio has one date format rather than two.

**Verification.** `tests/ProjectAndAssetTests.cpp`: a reset removing the setting from the record and
from the sidecar on disk, undo restoring it verbatim, and both refusals. `tests/StudioDetailsPanelTests.cpp`:
the byte formatter across its boundaries including the top of its unit table, "unknown" for an
absent stamp, and — through a real shell frame — an overridden setting being reset by clicking the
control the inspector offers, then undone.

### `STUDIO-09015` — Source file tracking and derived-data cache separation

**Acceptance.** Caches and derived content are clearly separated from authoritative source and are not version-controlled

### `STUDIO-09016` — Virtualised browsing for very large asset counts

**Acceptance.** Scales toward 100,000 assets without rescanning or rehashing everything

**Verification.** Stress test at 100,000 synthetic assets


### `STUDIO-09017` — A reference on a component with no descriptor survives a save and reload

**Why this exists.** Found by `STUDIO-09012`, and pre-existing rather than caused by it. A component
the editor has no descriptor for has its properties read back with their types *inferred from the
JSON shape* (`EntityJson.cpp`), and an asset reference is written as a bare UUID string — which is
indistinguishable from a string property that happens to hold one. So after a save and a reload the
reference is a `String`, and neither the dependency index nor `findMissingReferences` can see it.

The existing missing-reference test for components with no descriptor passes only because it
never round-trips. `AReferenceOnAnUnknownComponentIsLostByARoundTripUntilStudio09017` asserts the
limitation as it stands, so removing it is a test to change rather than a behaviour to discover.

**Why it matters.** The case it costs is a plugin that failed to load — precisely the file a
dependency view and a missing-reference report are opened for.

**Acceptance.** A scene saved and reloaded with a component the build has no descriptor for keeps
that component's asset and entity references *as* references, and they appear in both the dependency
index and the missing-reference report.

**What it will take.** The information is simply not in the file, so inference cannot recover it:
a type has to be written alongside the value for properties on components with no descriptor, which
is a format change and therefore a migration. Guessing "a string that parses as a UUID is an asset
reference" is rejected: an entity reference serialises identically, so the guess would silently
change one into the other.
