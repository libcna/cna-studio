# Phase 9 — Content Browser 2

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-09001` … `STUDIO-09999` and are never reused.

**Purpose.** Turn the prototype asset browser into a professional content workflow that scales to a real project.

**Exit criteria.** Tens of thousands of assets browse, search and filter responsively, and no file operation can break a scene reference.

**Progress:** 16 of 17 complete `███████████░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-09001` | Folder tree and breadcrumb navigation | ✅ | `STUDIO-07008` |
| `STUDIO-09002` | Grid and list views | ✅ | `STUDIO-09001` |
| `STUDIO-09003` | Thumbnail generation as cancellable background jobs | ✅ | `STUDIO-30001`, `STUDIO-10014` |
| `STUDIO-09004` | Thumbnail cache keyed by content, invalidated on reimport | ✅ | `STUDIO-09003` |
| `STUDIO-09005` | Search across name, type and path | ✅ | `STUDIO-09001` |
| `STUDIO-09006` | Filter by asset type, and sorting | ✅ | `STUDIO-09005` |
| `STUDIO-09007` | Favourites and recent assets | ✅ | `STUDIO-09001` |
| `STUDIO-09008` | Drag and drop into the viewport, Inspector and hierarchy | ✅ | `STUDIO-03023` |
| `STUDIO-09009` | Rename, move, duplicate and delete, all undoable | ✅ | `STUDIO-09001` |
| `STUDIO-09010` | Reimport, preserving Studio-side import settings | ✅ | `STUDIO-10001` |
| `STUDIO-09011` | Reveal in the system file manager | ✅ | `STUDIO-09001` |
| `STUDIO-09012` | Dependency view: references-to and referenced-by | ✅ | `STUDIO-09001` |
| `STUDIO-09013` | Missing asset handling with a clear path to relink | ✅ | `STUDIO-09012` |
| `STUDIO-09014` | Asset metadata and import settings UI | ✅ | `STUDIO-09001` |
| `STUDIO-09015` | Source file tracking and derived-data cache separation | ⬜ | `STUDIO-09004` |
| `STUDIO-09016` | Virtualised browsing for very large asset counts | ✅ | `STUDIO-30010` |
| `STUDIO-09017` | A reference on a component with no descriptor survives a save and reload | ✅ | — |

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

### `STUDIO-09003` — Thumbnail generation as cancellable background jobs

**Done**, once `STUDIO-10014` supplied the missing half. `StudioThumbnailCache` decodes and
downscales on worker threads, files the result on the main thread from `drain`, and cancels the work
for anything the browser has scrolled past.

**Drawing asks and never waits.** `find` is a hash lookup that touches no filesystem and starts
nothing, because the browser calls it forty times a frame over a project of a hundred thousand
assets. A thumbnail cache is *precisely* the feature that would put file access back on the draw
path — checking freshness with a `stat` is the obvious way to write it — so freshness comes from the
record's stamp, the way `STUDIO-30012` says, and a test counts the probes to prove it.

**Wanting is separate from asking, and belongs to the binder.** The browser already knows what is on
screen — that is `studioContentCardWindow`'s window, from `STUDIO-09016` — so it *reports*
`visibleAssets` and `StudioShellPanels` hands the set to the cache each poll. Panels report, the
binder acts (`docs/ARCHITECTURE.md` §10.1); starting and cancelling background work is not something
a draw path does.

**Cancellation is the feature, not tidiness.** Scrolling past an asset before its thumbnail is made
is the common case: a user flicking through two thousand textures wants the forty they stop on, and
the nineteen hundred they flew past are work nobody will look at. Without cancellation the queue
becomes a record of everywhere the user has been, and the thumbnails they are actually looking at
arrive last. An asset leaving the wanted set cancels its job on the next pump, and
`getCancelledCount()` exists because a number that stays at zero while somebody scrolls would mean
the cancellation never engaged and nothing else would say so.

**A failure is cached.** A file that is not really a PNG would otherwise be decoded again on every
pump, for ever — which is the case a cache exists to stop. So is the bound: 256 entries, several
screenfuls, about sixteen megabytes. A cache that kept every thumbnail it ever made would be a
memory leak with a justification.

**The extension is checked before anything is queued**, so a folder of audio and scenes does not
produce a job per file that then fails. A file that lies about its extension still fails at the
decode, which is where a wrong answer costs one job rather than a queue full of them.

**Budgeted at both ends.** The pump submits at most four jobs a poll and the job system refuses when
full (`STUDIO-30002`); a refusal is "not now" and the next poll offers the same work again. One poll
cannot turn a folder into a queue.

**What it does not do: draw them.** That is `STUDIO-35041`, which is a different problem — an
arbitrary image needs a texture, and the grid draws through the font atlas today. The card layout
was written for it (`STUDIO-35040` put the icon in the rectangle a thumbnail will use), so the
drawing drops in without moving anything. Splitting it that way is what let this task be finished
rather than half-finished.

**Measured**: `--ui-benchmark=content` is unchanged and inside budget — reporting the visible window
costs nothing a benchmark can see. Green under AddressSanitizer and ThreadSanitizer, which is where
a feature that decodes untrusted files on worker threads has to be checked.

### Superseded reasoning, kept because the blockage was the interesting part

**Blocked, and on the half nobody expected.** `STUDIO-30001`'s job system is done, so the
*cancellable background job* part is available. What is not is the decoding: the only thing in the
project that turns an image file into pixels goes through CNA's `Texture2D` and therefore through
the graphics device, which is not thread-safe and belongs to the main thread. `readImageSize` reads
headers and no pixels.

A job that read the file's bytes and handed them to the main thread to decode would move the cheap
half off the frame and leave the expensive half on it. That is worth something and is not what this
task says, so it is recorded as blocked rather than delivered as a smaller thing wearing this task's
name.

Filed as `STUDIO-10014`, which is a decision about a dependency rather than an implementation.

### `STUDIO-09004` — Thumbnail cache keyed by content, invalidated on reimport

**Done.** Two keys, because they answer different questions.

**The stamp decides whether to look.** Size, modification time, source path — and now the importer
settings — all read from the record without asking the filesystem anything. That is what lets
`pump` run every poll. The settings are the "invalidated on reimport" half: a reimport changes what
a thumbnail should look like *without touching the file*, so a cache keyed only on the file goes on
showing the old picture, which a user reads as the editor lying rather than as a cache being stale.

**The content decides whether to decode.** A SHA-256 of the source bytes, computed on the worker,
because reading a file is exactly what a poll must not do. Two assets holding identical bytes share
one decode: a texture copied into three folders is read three times and decoded once.

**The sharing registry is consulted from the worker, which is the point.** The obvious
implementation — decode, then notice on the main thread that these pixels were already known —
saves memory and no work at all. So the job hashes first, asks a small mutex-guarded map held by
`shared_ptr`, and returns without ever calling the decoder on a hit. The `shared_ptr` is the
lifetime answer: a job in flight outliving the cache is not something anybody should have to reason
about at a teardown.

**Keyed on bytes *and* settings together**, which a test failure taught rather than a design
review. The first version shared on content alone, and the reimport test caught it immediately: the
same file under different settings is a different picture, and sharing on content alone hands one
asset another's answer — the sort of wrong that looks right. The separator is a null byte, so no
pair of inputs can be spelled two ways.

**`getSharedCount()` is separate from `getGeneratedCount()`** because "made" and "already had these
bytes" are different facts, and a single counter would hide whichever mattered. A sharing count that
stays at zero in a project with duplicated textures means the content key is doing nothing, and
nothing else would say so.

**What this narrows, honestly.** The stamp still decides *when* to look, so a file rewritten to the
same length within the same second — the hole `STUDIO-09003`'s tests documented — is still invisible
until something else prompts a look. The content key removes the second half of that hole (having
looked, the cache now notices), and the remaining half is the watcher's rather than the cache's.

**SHA-256 moved into `cna-studio-core`** (`CNA/Studio/Core/Sha256.hpp`) rather than being lifted
from the provenance test that first needed it. Both callers now use one implementation, and its
FIPS 180-4 known-answer vectors — including the million-'a' case, which is the one that catches a
padding length written in bytes where it should be bits — test the shipped code rather than a copy
of it. The file form hashes in chunks, because a cache key must be computable for a file bigger than
it is comfortable to hold.

**Not done here: persistence across sessions.** The obvious next want, and where derived data is
allowed to live on disk is `STUDIO-09015`'s decision, which depends on this one. Keeping them apart
is what stops a cache directory appearing before anybody has said where such things belong.

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

### `STUDIO-09007` — Favourites and recent assets

**Done.** Two rows above the project in the folder pane, each appearing only when it has something
in it, and a star in the right-click menu.

**A real project's tree is deep and the working set is small** — the four textures this week's work
is about, and whatever was touched five minutes ago. Without somewhere to keep them, every return
trip is the same walk down the same folders, which is why every professional content browser has
both of these and why neither is a feature anybody asks for by name.

**They are kept apart rather than merged into one "quick access" list**, because they answer
different questions. A favourite is a *decision* and stays until it is unmade; a recent entry is a
side effect and is pushed out by the next thing. A list that mixed them would lose a deliberate
choice to a morning's browsing. It also decides the ordering: favourites append (so the one starred
last week stays where it was put) and recent moves to the front.

**Ids, not paths.** A favourite survives the file being moved or renamed, exactly as a scene's
reference does (D-08). A list of paths would quietly rot as the project is tidied, which is the one
thing a *favourite* must not do. Entries whose asset is gone for good are pruned on load, so a
project somebody tidied elsewhere does not open with rows that cannot be clicked.

**User state, not project data.** It lives beside the user's other Studio state, because it is about
one person's week rather than about the game — and because a project file that changed whenever
somebody clicked an asset would make every branch conflict on it. The file is named after the
project *and* a hash of its path: the name so somebody who opens the directory can tell which is
which, the hash so two projects called `Game` do not share a file. The hash is FNV-1a written out
rather than `std::hash`, whose value is allowed to differ between runs — which is the one property
this must not have.

**The pseudo-folders live in the same field a real folder path does**, and that is safe rather than
lucky: `describeStudioAssetNameProblem` refuses `<` and `>` in a name, so `<favourites>` is a path
no user can create. Every reader of `StudioContentBrowserState::folder` — the breadcrumb, the cards,
the menu — keeps working unchanged.

**Neither row appears while its list is empty**, because an empty "Favourites" row teaches the user
that the feature does nothing, which is the one lesson a shortcut list must not teach.

**A starred asset is marked with colour rather than a star glyph.** The shipped typeface is
rasterised on demand and has no `U+2605`, so a star would be a tofu box beside every favourite —
the same reason the search field has no magnifier, and the same task that fixes both
(`STUDIO-04019`). A column of its own was the other option and is worse: one that is empty on
ninety-nine rows in a hundred costs width and says nothing.

**Recent is recorded on *selection***, because selecting is what a user does to look at an asset and
is the only moment the browser can be sure they meant that one. Re-selecting what is already first
reports no change, which is what stops clicking one asset rewriting the file once a frame.

**The panel reports, the binder writes** — the same division as everywhere else here, and what makes
this testable without a home directory. A machine with nowhere to keep user state still gets working
lists for the session; they are simply not written, which is a better answer than refusing to star
anything.

**Verification.** `tests/StudioContentBrowserTests.cpp`: the two orderings and their difference,
bounding, a nil id being no entry, a round trip through a file with pruning of an asset that went
away, a missing file and a corrupt one both reading as empty, two same-named projects not sharing a
file, the pane offering each row only when it has content and putting both above the project, the
listing carrying each asset's location and its star, and the menu naming which way the toggle will
go — including for a missing asset, which everything else in that menu refuses.

### `STUDIO-09008` — Drag and drop into the viewport, Inspector and hierarchy

**One of the three already worked.** The Inspector has accepted an asset dropped onto a reference
field since `STUDIO-07018` — dropping the texture onto the slot that wants it, which is the gesture
a picker is the fallback for. The viewport and the hierarchy had no drop target at all.

**What an asset becomes is one decision, in one place.** `Scene/AssetDrop.hpp`: a texture is a
`SpriteRenderer`, a model a `ModelRenderer`, a sound or a song an `AudioSource`. Two call sites each
with their own `switch` would be two answers that drift, and the drift shows up as "it works if I
drop it on the tree".

**A prefab is not one of those.** Dropping it instantiates a whole subtree and records the link back
to the asset — `InstantiatePrefabCommand`, a different command — so `studioAssetDropKind` names the
three cases rather than one of them returning half of another.

**A refusal names the kind.** "That cannot be dropped here" is a sentence people read twice and
learn nothing from; the kind is what tells them whether they grabbed the wrong file. A scene gets
the one refusal that is a *different action* rather than a missing one, so it points at the action:
"A scene is opened rather than placed in another scene."

**Dropped where it was let go.** In a scene with anything in it, the origin is under something else,
so an editor that put every dropped thing there would make the gesture a step towards moving it
rather than a way of placing it. A drop on a tree row has no world position and honestly says so by
using the origin — inventing one from the row's y would put things in a line nobody asked for.

**A transform first, and always**, with the component's declared defaults applied before the
reference is set. A dropped asset behaves like one added through the inspector rather than like an
entity carrying one property and no others.

**The tree's `dropType` became `dropTypes`.** An outliner row means "reparent" to an entity and "put
one of these in the scene" to an asset, and a widget that allowed one type per row would make the
second of those a different widget. Told apart by the payload's *type*, reported as `droppedType`,
rather than by guessing from the value — two UUIDs look identical.

**Panels report, the binder acts** — the division every other outcome here follows. It is also what
makes the whole gesture testable: the viewport test asserts where the drop landed without anything
being created, and the end-to-end test asks `placeDroppedAsset` directly without a pointer.

**Verification.** `tests/StudioOutlinerPanelTests.cpp`: the three kinds each becoming the right
component with its defaults and a transform, named after the file without its extension; the
refusals, including the scene's different wording; and — through a real shell frame — an asset drop
on a row being reported without reparenting anything or touching the undo stack.
`tests/StudioViewportPanelTests.cpp` drops into the view and asserts the world position is the one
the camera gives for the pointer. `tests/StudioShellActionTests.cpp` runs the whole thing through
`StudioShellPanels`: an entity that references the model, positioned where it was dropped, selected,
one undo entry, and a refusal that says why.

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

### `STUDIO-09011` — Reveal in the system file manager

**Done.** "Show in Folder" on an asset and on a folder, in the right-click menu.

**The moment it exists for**: an asset is in Studio and the next thing to do with it is not — open
it in Krita, drop it into a chat, check what git thinks of it. Without this the user copies the path
out of the inspector and pastes it into a file manager, which is a thing people do dozens of times a
day and complain about once.

**The three platforms cannot do the same thing, and that is reported rather than hidden.**
`explorer /select,<path>` and `open -R <path>` open the folder *and highlight the file*; on Linux
there is no portable way to highlight one — `xdg-open` takes a directory, and the desktop's own
manager opens it. Some managers support a selection through D-Bus and no two agree, so the honest
answer is the containing folder and `StudioRevealCommand::selectsTheFile` says which a caller got.

**`xdg-open` is given the folder, not the file.** On a file it opens whatever application claims the
type — an image viewer for a texture — which is a different action from the one asked for.

**The command is separate from running it**, so *what gets run* is testable on a machine with no
desktop, which is every machine this project's tests run on. The Windows form is the reason: the
comma is part of the switch and there is no space after it, and `explorer /select, C:\x` opens the
user's documents folder instead. That is a detail nobody remembers twice and a test remembers
always.

**Double-forked on POSIX**, so the file manager is reparented to init and outlives Studio. A direct
child would have to be waited for, and a file manager the user is about to work in is not something
an editor should be able to close by exiting.

**Reported by the panel, launched by the binder** — the same division every other outcome here
follows. A panel that launched a process would be one no test could drive.

**A missing file is not offered it**, because a file manager opened onto a path that is not there
lands somewhere arbitrary and reads as the editor having lost the file.

**Verification.** `tests/StudioContentBrowserTests.cpp`: the command built for the host platform
including the Windows comma and the Linux folder-not-file rule, a directory passed through as
itself, a path that is not there refused with a reason, and the menu offering the row for an asset
and a folder while greying it for a missing file.

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

**Done.** A folder holding a hundred thousand files costs a screenful to browse, in both
presentations and in the folder pane, and `tests/StudioLargeProjectTests.cpp` holds it there at the
full hundred thousand.

**The bound that was missing was on *building*, not on drawing.** `studioTreeView` has culled its
drawing since `STUDIO-03034` and the grid since `STUDIO-30010`, and both looked virtualised: the
number of widgets described was already a screenful. What neither could bound is the model the
caller hands them. The list built a `StudioTreeRow` per asset — three strings each — twice a frame,
and the grid built a `StudioContentCard` per asset to call `.size()` on it. At two hundred assets
that is invisible. At a hundred thousand it is the whole frame, and no amount of culling inside the
loop touches it, because the loop is not where the cost is.

So the panel now asks *how many* (`studioContentCardCount`, from the database's own folder counts),
sizes the scroll region from that, asks which slice is worth having, and builds only that slice
(`studioContentCardWindow`). The grid did this at `STUDIO-30010`; this task is the list, and the
`AssetDatabase` indexes both of them stand on.

**The window is asked after the scroll region is open, not before.** This is the part that reads
like an awkward API and is not negotiable. Where a pass has scrolled to depends on the wheel and on
a scrollbar thumb being dragged, and `studioBeginScroll` consumes both — on the input pass only, so
that the draw pass sees the same position the input pass acted on. A window computed *before* that
would answer differently in the two passes of a scrolling frame, and two passes that disagree about
which rows exist are two passes that disagree about widget identity: a click would land on the row
that was there before the wheel turned.

That is why `studioTreeView` was split rather than given a window parameter. A caller that wants to
build only the window opens the scroll region itself, asks `studioTreeWindow`, builds the slice, and
draws it with `studioTreeRows` — which is the shape the grid already had. `studioTreeView` is now
that sequence for a caller that holds the whole list, so every other tree in Studio is unchanged.

**Counted, not timed.** `StudioContentBrowserResult::rowsBuilt` reports how many cards the panel
constructed, because `rowsDrawn` cannot tell the two failures apart: a view that builds a hundred
thousand rows and draws forty has a perfectly bounded `rowsDrawn` and is exactly what this task
existed to remove. A wall-clock assertion would have said the same thing and said it differently on
every machine. Where the order cannot be answered by position — a search, a sort by size — the
listing genuinely has to be built to be sliced, and `rowsBuilt` says so rather than flattering the
panel.

**Also asserted: the window and the drawing agree.** `ScrollingAHundredThousandAssetsShowsTheRowsTheScrollbarSaysItDoes` scrolls six hundred rows down by the wheel and clicks. A slice indexed from
its own start, or positioned at its own offset rather than the list's, selects the first file in the
folder; that is the failure a window makes possible and a full list cannot, and it is not visible in
a screenshot of a folder of identically named files.


### `STUDIO-09017` — A reference on a component with no descriptor survives a save and reload

**Found by `STUDIO-09012`**, and pre-existing rather than caused by it. A component the build has no
descriptor for had its properties read back with their types *inferred from the JSON shape*
(`EntityJson.cpp`), and an asset reference is written as a bare UUID string — indistinguishable from
a string property holding one. So after a save and a reload the reference was a `String`, and
neither the dependency index nor `findMissingReferences` could see it. The case it cost is a plugin
that failed to load, which is precisely the file both of those are opened for.

**The information has to be written while it still exists.** Inference cannot recover it: by the
time the type is needed, the descriptor that knew it is gone. So the type is recorded whenever it is
*known* — that is, whenever the descriptor is present — under a reserved `$refs` key inside the
component's own object, so the hint travels with the thing it describes.

**Only the two types that serialise as a bare UUID.** An asset reference and an entity reference are
the ambiguous pair; everything else `readUntypedJson` infers from the JSON's own shape. Recording
more would add noise to every component in every scene to buy nothing.

**Both kinds are told apart, which is the reason the naive fix was rejected.** "A string that parses
as a UUID is an asset reference" would turn every entity reference on a plugin-less component into a
reported broken asset. The hint says which.

**Additive, so there is no migration and no version bump.** A file without the hint reads exactly as
it did; an older Studio reading a file *with* one ignores a key it does not know. The exported
runtime (`Runtime/SceneLoader.hpp`) keeps a component's JSON verbatim and reads named fields out of
it, so a shipped game never sees the extra key either. Both directions are asserted, because
otherwise this would be a format break wearing the clothes of a bug fix.

**The hint is not a property.** A component that grew one would write it back out twice and show it
in an inspector as a field nobody declared.

**Verification.** `tests/AssetDependencyTests.cpp`: an asset reference *and* an entity reference on a
descriptor-less component surviving a round trip as themselves, a string property beside them
staying a string, the component carrying three properties rather than four, the dependency index
seeing the reference it was blind to, and a scene written before the hint existed loading unchanged
with no `$refs` in it.
