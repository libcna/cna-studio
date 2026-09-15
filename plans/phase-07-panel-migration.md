# Phase 7 — Existing-panel migration

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-07001` … `STUDIO-07999` and are never reused.

**Purpose.** Port every prototype panel onto the Studio UI and retire the Dear ImGui presentation.

**Exit criteria.** Feature, input, docking and visual parity, proven panel by panel against the Phase 0 inventory — then ImGui is removed deliberately.

**Progress:** 21 of 27 complete `█████████░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-07001` | Compatibility adapter so unported panels keep working during the migration | 🔄 | `STUDIO-06015` |
| `STUDIO-07002` | Port the main menu bar | ✅ | `STUDIO-06003` |
| `STUDIO-07003` | Port the toolbar | 🔄 | `STUDIO-06006` |
| `STUDIO-07004` | Port the status bar | ✅ | `STUDIO-06007` |
| `STUDIO-07005` | Port the Console / Output Log | ✅ | `STUDIO-07001` |
| `STUDIO-07006` | Port the Hierarchy panel (World Outliner) | ✅ | `STUDIO-07001` |
| `STUDIO-07007` | Port the Inspector panel (Details) | ✅ | `STUDIO-07001`, `STUDIO-03035` |
| `STUDIO-07008` | Port the Content Browser | ✅ | `STUDIO-07001`, `STUDIO-03034` |
| `STUDIO-07009` | Port the viewport container | 🔄 | `STUDIO-04012` |
| `STUDIO-07010` | Port the Build panel | ✅ | `STUDIO-07001`, `STUDIO-02040`, `STUDIO-03036` |
| `STUDIO-07011` | Port the Diagnostics panel | ✅ | `STUDIO-07001`, `STUDIO-02022` |
| `STUDIO-07012` | Port the Validation panel | ✅ | `STUDIO-07001`, `STUDIO-03034` |
| `STUDIO-07013` | Port the History panel | ✅ | `STUDIO-07001`, `STUDIO-03034` |
| `STUDIO-07014` | Port the Comparison panel | ✅ | `STUDIO-07001` |
| `STUDIO-07015` | One log model, read by both consoles | ✅ | — |
| `STUDIO-07016` | Panel content seam: the shell hosts a ported panel's content | ✅ | `STUDIO-06018` |
| `STUDIO-07017` | A module for the ported panels, above widgets and document alike | ✅ | `STUDIO-07016` |
| `STUDIO-07018` | Editors for the property kinds the Details panel shows read-only | ✅ | `STUDIO-07007`, `STUDIO-03036` |
| `STUDIO-07019` | An undoable command for an entity's enabled flag | ✅ | `STUDIO-07007` |
| `STUDIO-07020` | Prove parity against the Phase 0 panel and shortcut inventory | ✅ | `STUDIO-00014`, `STUDIO-07014` |
| `STUDIO-07021` | Prove input parity: keyboard, mouse, drag and drop, clipboard, text editing | ✅ | `STUDIO-07020` |
| `STUDIO-07022` | Prove docking parity | ✅ | `STUDIO-07020` |
| `STUDIO-07023` | Visual acceptance review against the Phase 0 reference screenshots | ✅ | `STUDIO-00013`, `STUDIO-07020` |
| `STUDIO-07024` | Layers panel: the project's render layers and what is on each | ✅ | `STUDIO-03034` |
| `STUDIO-07030` | Remove the Dear ImGui panel implementations | ⬜ | `STUDIO-07021`, `STUDIO-07022`, `STUDIO-07023` |
| `STUDIO-07031` | Remove the `CNA_STUDIO_WITH_IMGUI` option and the vendored source | ⬜ | `STUDIO-07030` |
| `STUDIO-07099` | Guard test: production Studio UI has no dependency on Dear ImGui | ⬜ | `STUDIO-07031` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-07001` — Compatibility adapter so unported panels keep working during the migration

**Acceptance.** Both UIs coexist in one running Studio; the strangler migration proceeds panel by panel with tests green throughout

**What holds today.** The seam exists and five panels have gone through it:
`StudioShell::setPanelContent` hosts a ported panel's content, an unported panel is the empty
surface it always was, and both consoles read one `StudioLog`.

**The binding moved out of the CNA-linked module** (`StudioShellPanels`), and that was not
housekeeping. There is exactly one place a running Studio is created — behind a CNA checkout — so
the ported panels could only be *seen* in a build with CNA, and the headless shell preview, which
is the only visual test this project has on a machine with no GPU or display, photographed five
empty rectangles where the panels are. Nothing about binding a panel needs CNA. The same binding
now serves the editor and the preview, so what CI photographs is what a user sees.

What does not hold yet is the words *one running Studio*: the two presentations are still two entry
points, `--ui=imgui` and `--ui=studio`, rather than one process showing ported and unported panels
side by side. That is the remaining half, and it is what `STUDIO-06015` is waiting on

### `STUDIO-07002` — Port the main menu bar

**Acceptance.** Every item the prototype's menu bar draws is reachable from the native one.

**Done.** `docs/MIGRATION-INVENTORY.md`'s menu table accounts for every row the prototype draws and
every one resolves. The native bar is ahead rather than level: nine menus to three, built from the
registry so a row cannot exist and do nothing, with nested submenus, hover opening, keyboard
traversal and context menus (`STUDIO-06003`, `STUDIO-06017`).

**Plugin menus were the one architectural item**, and are the difference the registry was for. The
prototype *draws* them — it walks the extension registry every frame and calls `beginMenu` and
`menuItem` — which works, and which is exactly why a plugin command there can never have a shortcut,
never be greyed out, never appear on a toolbar and never show up in the shortcut editor. It is not a
command, it is a row.

`bindStudioPluginMenus` makes each one a registry action under `studio.plugin.`, and the menu names
the id. A menu a plugin asks for by a name Studio already uses *is* that menu, with one separator
before the plugin's rows, rather than a second menu of the same name beside it — which is what Dear
ImGui produces, because `BeginMenu` has no opinion about a title it has already seen. A menu only
the plugin knows the name of is created before Help.

Rebuilt from a **revision counter** on the extension registry rather than from a callback: a
callback would put the menu rebuild inside a plugin's load, which is where a plugin that throws
would take the menu bar with it. Unloading takes the rows and the commands with it, because a row
left behind calls an `invoke` pointing into a library the host has closed — and reloading three
times leaves one of everything rather than three.

**What writing it found.** `StudioActionRegistry::add` returns whether it *replaced* a command, not
whether it succeeded. Reading it as success dropped every plugin row while registering every plugin
action, which is the shape of bug that looks like the menus being wrong rather than the caller.

### `STUDIO-07003` — Port the toolbar

**Acceptance.** Every control the prototype's toolbars offer is reachable from the native one.

**In progress, and the remainder is three rows.** The prototype has no application toolbar at all;
its controls live inside the Viewport panel, which means they move and resize with it and vanish if
it is closed. The native toolbar is icons at the top of the window, driven by the registry.

`docs/MIGRATION-INVENTORY.md`'s toolbar table accounts for all eleven controls. Play, Stop, Pause,
Step, the manipulator and the backend chooser are answered; **the tilemap tool, the tile index and
2D/3D** are not, and are waiting on the phases that own them (25 and 11).

### `STUDIO-07004` — Port the status bar

**Acceptance.** Whatever the prototype's status bar says is said by the native one.

**Done by there being nothing to port.** The prototype has no status bar: grep the panel sources and
there is not one. The native bar (`STUDIO-06007`) is therefore new rather than a port, and carries
what is open, whether it is saved, what is running with a progress bar and a Stop, the active target
profile, and what Studio itself is drawing on.

Recorded rather than quietly ticked, because "nothing to port" and "ported" are different facts and
only one of them is a reason to stop looking.

### `STUDIO-07005` — Port the Console / Output Log

**Acceptance.** The Output Log on the Studio UI, at feature parity with the ImGui Console: copy,
clear, a severity filter, and following new output. The legacy panel keeps working, unchanged, until
`STUDIO-07030` deletes it

**Why this panel first.** It is the simplest panel that is still a real one — a filtered, scrolling
list with a toolbar — so it exercises what a panel actually needs from the new UI (scrolling,
virtualised rows, a row of controls, retained view state) without also needing a property grid, a
tree, drag and drop or a graphics device. If the strangler seam is wrong, this is where it is
cheapest to find out. It was: porting it is what turned up the need for `STUDIO-07015`,
`STUDIO-07016` and `STUDIO-03033`, none of which existed before something had to use them

**What the port gained over the original.** Repeated messages collapse with a count, so four hundred
identical warnings stop burying the one line that matters; rows are virtualised, so a
hundred-thousand-line log costs what a ten-line one costs; the empty state distinguishes "nothing
logged" from "four hundred messages your filter is hiding"; and the severity filter is four buttons
rather than a combo, so the current one is readable without opening anything

**Verification.** `tests/StudioLogPanelTests.cpp` for the model, the panel and the seam;
`CnaStudioNativeShellOutputLog` draws it on a real CNA device and asserts on the rows it put on
screen — a count, not a triangle total, because a ported panel and the empty surface it replaced
both draw *some* geometry and only the row count tells them apart

### `STUDIO-07015` — One log model, read by both consoles

**Acceptance.** `StudioLog` holds the messages; `StudioUi` writes into it; the ImGui console and the
Studio one both read it. Bounded, with repeats collapsed and counted

**Why it comes before any porting.** Two logs would make the migration impossible to check: every
difference between the panels would be a difference in what was logged rather than in how it was
drawn, and nobody could tell a faithful port from a plausible-looking one

**Verification.** `BothConsolesReadOneLog`, plus `ImGuiUiRoutesLogMessagesIntoTheSharedModel`, which
also holds the legacy accessors to their existing shape — the ImGui panels are a compatibility
fallback until they are deleted, not something to break on the way past

### `STUDIO-07016` — Panel content seam: the shell hosts a ported panel's content

**Acceptance.** `StudioShell::setPanelContent` gives a registered panel a content function, called
once per pass with the panel's content rectangle, inside the panel's own clip and id scope — so two
panels can each have a widget called "clear" without sharing retained state, focus or capture, and
content that overruns its panel is cut off rather than drawn over its neighbour. Only the active tab
of a leaf is called: a panel behind another is not drawn and not described, so it costs nothing

**Verification.** `TheOutputLogDrawsItsMessagesThroughTheShell` and
`APanelWithNoContentCostsNothingAndDrawsAnEmptySurface`

### `STUDIO-07020` — Prove parity against the Phase 0 panel and shortcut inventory

**Acceptance.** Every inventoried panel, menu item, toolbar control and shortcut ticked off item by item, not by impression

**Done, and what "ticked off" turned out to mean.** `STUDIO-00014` already checked the inventory in
one direction: an item the list calls answered must resolve to a registered panel with content or to
a command that exists on the same chord. That direction cannot catch the failure this task is about.
A piece of the prototype the list never mentioned passes every one of those checks, for the plain
reason that the list is what they read.

So the tests now read the prototype's own source — every `src/panels/*Panel.cpp`, every `menuItem`
in `MainMenuBar`, every `button` and `propertyField` in `ViewportPanel`'s two toolbars, and every
`isShortcutPressed` in `StudioApplication::handleShortcuts` — and require each item they find to
appear in `docs/MIGRATION-INVENTORY.md` with a decided status. The toolbar table is checked in both
directions, because it is new and a table claiming parity for controls the prototype does not have
would be a claim about an editor nobody is shipping.

**What it found.** The toolbars had never been inventoried at all: the panels, menus and shortcuts
had been, and four things live only on a toolbar — Pause, Step, which backend to launch on, and the
tilemap tool. The tile-index control was the one nothing else would have caught, drawn a hundred
lines below the rest of the toolbar and only when the paint or fill tool is active.

**What this does not establish.** That the answered items *behave* the same. Coverage is what this
task proves: every item accounted for, with a reason attached to each that is not. Behaviour is
`STUDIO-07021` for input, `STUDIO-07022` for docking and `STUDIO-07023` against the reference
screenshots, and the ⬜ rows are what keeps `STUDIO-06015` and `STUDIO-07030` from being true.

**Since.** Crash recovery was the first of those rows to close, and closing it showed what the
inventory is worth: everything *about* recovery already worked and was shared — the snapshot format,
the store, the atomic write — so it read as done from every angle except the one that mattered.
Nobody was running it. The Dear ImGui host wrote snapshots and offered what it found; the native
host runs a different loop and did neither, so a user on `--ui=studio` had no crash recovery at all
and nothing said so. It is one `StudioRecoverySession` now, driven by whichever host is running.

File > Exit was the second, and it was the odd one out: the command existed and the CNA host closed
the window, but the host watched `invokedActions()` for the id rather than the command having a
handler — so the command never ran, nothing asked about unsaved changes, and no other host could
close at all. It is `StudioShell::setQuitHandler` now, a seam like the clipboard and the workspace.
That work also found that a caller polling `dialogResult()` on a later frame sees the answer only
while nothing renders in between; dialogs deliver their answer to whoever asked now, which is what
made "Escape means Cancel" true rather than nearly true.

Rename in place was the third, and it found a fourth disagreeing chord: `F2` is Rename in the
prototype and in every file manager, and natively it was Build. Build is `Ctrl+B` now. Writing the
editable row also found that `studioTextField`'s commit-on-focus-loss had never been able to run —
every field in Studio silently threw away an edit the user clicked away from.

### `STUDIO-07021` — Prove input parity: keyboard, mouse, drag and drop, clipboard, text editing

**Acceptance.** Every capability the prototype's panels are written against has a named native
answer and a named test that exercises it.

**Why the surface was not enough.** `STUDIO-07020` accounts for the prototype's panels, menu items,
toolbar controls and shortcuts. Underneath all of them is `StudioUi` — the interface every prototype
panel calls — and a method on it with no native answer is a thing the ported panels *cannot do*,
whatever the inventory says about the panel that used it. A panel can be ported, appear as ✅, and
still be poorer than the one it replaced.

So `docs/UI-CAPABILITY-PARITY.md` lists all thirty-nine, and the suite checks it in both directions
and one more: **every row names a test, and that test has to exist**. A parity document is otherwise
a list of claims, and the claim that costs nothing to write is the one nobody goes back to
substantiate — the check caught three names invented while writing the table, on its first run.

**What it found.** Nothing missing, which is the answer worth recording. The four capabilities that
differ — `isRunning`, `sameLine`, `setNextItemWidth`, and `DockSide` on `beginPanel` — differ
because the native design does not have the problem they solve. `sameLine` and `setNextItemWidth`
are a cursor-based layout saying "beside the last one" and "this wide"; the native UI has no cursor,
so a caller splits the rectangle it was given, and the failure they exist to work around cannot
happen.

### `STUDIO-07022` — Prove docking parity

**Acceptance.** Every panel the prototype opens on a given side is on a matching side of the native
default arrangement.

**What the prototype actually promises.** A side, and nothing else: `beginPanel("Inspector",
DockSide::Right)`. That is the whole of it, so that is what parity means here, and the rest of what
the native model can do — dragging a tab to any edge, tabbing panels together, floating one into its
own window, saving an arrangement under a name — is more than the prototype offers rather than
parity with it.

**Checked against the resolved geometry**, not against the calls that built the tree: the tree can
be right and the layout wrong, and only one of the two is what a user looks at. Against the *group's*
rectangle rather than the panel's own, because six of the ten share a tab strip and asking where the
tab in front is would report the other six as placed nowhere — a fact about tabs rather than about
where the panel lives. And measured relative to the viewport rather than to the window, because
"beside the viewport" is what a side means, and a fraction of the window would have to be rewritten
whenever the default proportions were tuned.

### `STUDIO-07023` — Visual acceptance review against the Phase 0 reference screenshots

**Acceptance.** The two UIs captured side by side, at the same sizes, showing the same project, and
a written judgement about the difference.

**`docs/VISUAL-ACCEPTANCE.md`**, over the four captures in `docs/reference/`. The review is a
judgement and says so: no test can assert that one editor looks better than another. What the suite
checks is that the four captures exist, that they are the sizes the review claims — read from the
PNG headers rather than from the filenames, which are a claim rather than a fact — and that
everything the review calls unanswered is unanswered in the inventory too, so the two documents
cannot come to disagree about what is missing.

**What it found, which is why it exists.** Three things, all in one place and none of them visible
to the panel inventory: the prototype's Inspector shows the **Scene Environment** (ambient colour
and fog), an editable **Grid Snap**, and the project's **layer list** when nothing is selected, and
the native Details panel shows only "Select an entity to see its details." The inventory accounts
for *panels*, and the Inspector is ported — what it structurally cannot see is that a ported panel
shows something else entirely in a state nobody thought to compare. They are inventory rows now.

**And what it found about itself.** The native shell is ahead of the prototype on everything a user
sees first — real type, nine menus to three, a toolbar and a status bar the prototype has not got at
all — and was behind it on one screen's worth of scene-level settings.

**Closed.** The Details panel standing idle shows the project, an editable grid snap, the scene
environment and the project's layer names with add, rename and remove, each through the command
history. The reference captures are left as they were: they are the "before" of the migration, and
re-taking them to hide what the review found would be the wrong kind of tidy.

**And it found something older on the way.** `studioTextField` committed *twice* for any caller that
normalises what it stores — which is every numeric field in Studio: "00.5" typed, "0.5" written
back, the editing session left open across the difference, and the next frame reading it as an
uncommitted edit. Every such edit landed twice, once as the change and once as a no-op that still
took an undo slot, so Ctrl+Z appeared to do nothing before it did something. Enter ends the session
now, and `ACallerThatNormalisesWhatItStoresStillCommitsOnce` is the case the plain round-trip test
could not reach.

### `STUDIO-07031` — Remove the `CNA_STUDIO_WITH_IMGUI` option and the vendored source

**Acceptance.** Removed deliberately, with `THIRD_PARTY_NOTICES.md` updated to match what is actually shipped

### `STUDIO-07099` — Guard test: production Studio UI has no dependency on Dear ImGui

**Acceptance.** Fails the build if the dependency returns, whether through code or through CMake

### `STUDIO-07006` — Port the Hierarchy panel (World Outliner)

**Acceptance.** The scene's entities as a tree: parents before children, the document's own sibling
order, a disclosure triangle only where there are children, the component summary that tells a
camera from a sprite at a glance, and clicking a row selecting it

**Selection goes through the context.** The viewport, the inspector and the gizmos all read
`StudioContext`'s selection. A panel that kept its own would disagree with the rest of the editor
the moment anything else changed it — and would do so silently, which is the worst way for two
views of one document to diverge

**The sibling order is the scene's, not the panel's.** Showing siblings differently from how the
document holds them is how a user reorders something in one place and cannot find it in another

**Verification.** `tests/StudioOutlinerPanelTests.cpp` splits the two failures a screenshot cannot
tell apart: what the tree *is* (`studioOutlinerRows`, asserted without a frame) and what a user can
*do* to it (driven through the real widget with synthesised input). Plus a two-thousand-deep chain,
because a crash on opening somebody's scene is the worst outcome an outliner has

### `STUDIO-07017` — A module for the ported panels, above widgets and document alike

**Acceptance.** `cna-studio-shell-panels`, linking `cna-studio-ui-core` and `cna-studio-context`.
Every panel ported in this phase lives here

**Why it exists.** The Output Log could go in ui-core because its model is part of `cna-studio-ui`,
which ui-core already depends on. The outliner reads a `SceneDocument` and writes a selection, and
ui-core depends on neither — deliberately, because that is what keeps the widget layer reusable and
testable without a document model. A panel is the seam where the two are put together, and a seam
deserves somewhere to be

### `STUDIO-07007` — Port the Inspector panel (Details)

**Acceptance.** The selected entity's name, enabled flag and components, with every property shown
and the simple kinds editable. Every edit goes through the command history, so Ctrl+Z reaches it

**The first ported panel that writes.** That is what makes it different from the outliner and what
decides its tests: showing a scene wrong is a bad afternoon, editing one wrong is a lost afternoon's
work. Nothing here touches an entity directly except the enabled flag, which has no command yet
(`STUDIO-07019`) and is recorded as such rather than left looking undoable and not being

**What is editable, and what is honestly not.** Booleans, integers, floats, strings, enumerations
and the two- and three-component vectors. Colours, quaternions, rectangles, references, lists and
structures are *shown* with what they hold and labelled as not editable yet — a property nobody can
see is worse than one nobody can change, and a control that looked editable and silently did nothing
would be worse than both. `STUDIO-07018` wants pickers rather than more text fields: a colour typed
as four numbers and a rotation typed as four is how an inspector gets a reputation

**A component the registry does not know still shows its properties.** A scene authored by a plugin
that is not loaded must be readable, or opening it looks like data loss

**Verification.** `tests/StudioDetailsPanelTests.cpp` — the components shown, the two empty states,
a rename reaching the history and surviving an undo, one axis of a vector edited without disturbing
the other two, text that is not a number rejected rather than turned into zero, and an entity
deleted while selected reported rather than dereferenced

### `STUDIO-07018` — Editors for the property kinds the Details panel shows read-only

**Acceptance.** A colour picker, a rotation editor, a rectangle editor, asset and entity pickers,
and list add/remove/reorder. Pickers, not text fields: four numbers is not a colour

### `STUDIO-07019` — An undoable command for an entity's enabled flag

**Acceptance.** `SceneCommands` gains a set-enabled command and the Details panel routes the
checkbox through it, like every other edit

### `STUDIO-07008` — Port the Content Browser

**Acceptance.** The project's assets as folders and files: folders before their contents, files
sorted within a folder, each file showing its type and each folder how much is in it, and clicking
a file selecting it

**It reads the asset database, not the filesystem.** The database is what knows an asset's stable
id, its type, and whether its source has gone. A browser that walked the directory instead would
show files Studio does not track and hide the one fact that matters about a tracked file whose
source has vanished

**Folders are derived from paths.** A folder therefore exists exactly when something tracked is in
it. An empty directory on disk does not appear, which is the honest answer: showing it would promise
a place to put things the database does not know about

**A missing source is dimmed, not disabled.** The distinction was found by a test that could not
click the row it was meant to. An asset whose file has gone should read as wrong at a glance *and*
stay selectable — it is the row a user most needs to click, because clicking it is how they find
out what references the lost file. `StudioTreeRow` grew a `muted` flag so "looks wrong" and "cannot
be touched" stopped being the same thing

**Verification.** `tests/StudioContentBrowserTests.cpp` — folders derived and ordered, collapsing
hiding subfolders as well as files, a missing source listed and marked and still clickable, types
and counts, a click selecting a file and not a folder, and an empty project saying so

### `STUDIO-07010` — Port the Build panel

**Acceptance.** The Build panel on the Studio UI, at parity with the ImGui one: what will be built,
the exact commands, a Build button, progress and the log tail

**It is not a straight port, and could not have been.** The ImGui panel offers two axes — a platform
triple and a graphics backend — because that is all the prototype's model had. Studio's model is the
six-axis `StudioTargetProfile` (`STUDIO-02040`): operating system, architecture, CNA platform, CNA
renderer, configuration and the optional subsystems, with validation that knows which combinations
CNA will actually configure. **That model had no user interface at all**, and a project could only
change what it ships on by editing its `.cnaproject` in a text editor. This is that interface.

**A profile belongs to the project, not to the panel.** The legacy panel kept its chosen platform
and backend in its own members, so what the user chose was forgotten when the panel closed and was
never saved — and the Build button could therefore build something the Play button would not. Here
the profile list *is* the project's, edited in place.

**The lists offer only what CNA will configure.** Renderers are filtered by the chosen operating
system, and reserved platform names — ones CNA's build recognises and refuses — are left out.
Offering Direct3D on a Linux target and then failing validation would be the tool asking the user to
discover a rule it already knows, at the cost of a full configure. The profile's *own* renderer is
always listed even when this system cannot build it, because a blank control reads as "Studio lost
your setting" rather than "this combination does not exist", and validation can only explain a value
the user can still see.

**It found a real defect.** Every project that predates profiles carries CNA's upper-case identity
in `defaultGraphicsBackend`, and that string became the migrated profile's renderer verbatim — so
`StudioTargetProfile::renderer`, documented as lower case, was sometimes `"OPENGLES3"`. Every
renderer comparison in Studio was therefore a case-insensitive one, a rule that holds until the
place that forgets it: this panel, whose renderer control was blank against a list of lower-case
names. Validation now normalises the spelling in place, silently, because nothing about the target
changed — only how it is written down.

**Verification.** `tests/StudioBuildPanelTests.cpp`: no project, a project that always has a target,
the filtered renderer list, an axis edit reaching the *project* rather than the panel, the request
matching the project's active profile, a subsystem reaching the CMake arguments, no phase violations
across repeated frames, and the Build button refused on an unbuildable profile. Plus
`CnaStudioShellPreviewBuildPanel`, which photographs it through the rasterizer

### `STUDIO-07012` — Port the Validation panel

**Acceptance.** Scene validation and broken asset references on the Studio UI, in one report, with
a path from a finding to the thing at fault

**Called Problems**, because that is the panel the shell already has and the word covers both
reports. They stay in one list for the reason they were put there originally (legacy ED-310): a user
whose model has the wrong material on it does not know in advance whether that is a structural
problem or a broken reference, and asking them to look in two places to find out is asking them to
know the answer first.

**Its good state is emptiness, which is exactly what makes it easy to ship broken** — a panel that
found nothing and a panel that never ran look identical. So both groups always show, each with a
count that reads `none` rather than being absent, and the empty case is a test rather than the
absence of one.

**Severity is a colour, not a word.** The tree gained a per-row colour for its detail column, used
here for `error` and `warning`. A list that says which in grey words is a list the eye has to read
line by line, which defeats the point of a report.

**Clearing a reference acts on the selection.** The ImGui panel puts a `Clear` button beside every
broken asset: that reads fine with three and badly with thirty, and it has no keyboard path at all.
Here the action sits above the list — the ordinary editor shape, reachable by Tab, and drawn
disabled until a broken asset is selected rather than drawn enabled and then doing nothing.

**Both repair paths are here.** Clearing goes through the command history like any other change to
the scene, and dragging the right asset from the Content Browser onto the broken row relinks it —
the same command either way, because clearing is relinking to nothing. The drag arrived with
`STUDIO-03023`; the row declares `dropType` and the panel reports what landed on it.

**Verification.** `tests/StudioProblemsPanelTests.cpp`: a clean scene saying so, a broken reference
grouped with everything that refers to it, severity carried as a colour, clicking a finding asking
for the entity at fault, the toolbar refused until a broken asset is selected and acting on it by id
when one is, clicking a row selecting it, and no phase violations across repeated frames. Plus
`CnaStudioShellPreviewProblemsPanel`

### `STUDIO-07013` — Port the History panel

**Acceptance.** The undo stack as a list, showing where the cursor is, what has been undone and
where the document last agreed with the file on disk — and clicking a row goes there

**Rows are positions, not entries.** Row *i* is the document after *i* commands, so there is one
more row than there are entries. That extra row — the document as it was opened — is the one a user
reaching for "put it back how it was" is actually aiming at, and a list of entries alone can take
them everywhere except there. It is the first test in the file for that reason.

**Navigating is undo and redo, not a jump.** Clicking a row runs the commands between here and
there one at a time, through the same `CommandHistory` that Ctrl+Z uses. Setting the cursor directly
would leave the document and the history describing different things, and a command that refused
would be skipped silently instead of stopping the walk. The walk is bounded by the entry count on
both sides, because `undo()` and `redo()` report failure rather than throwing and a loop that
trusted the cursor to move would spin.

**And it is reported rather than applied where it is found.** Navigating runs commands, which
changes the very list being drawn: half the rows would describe one history and half another.

**Undone entries are marked, not hidden**, because they are precisely what a user is trying to get
back to — a list that hides them has no forward direction at all. The saved position is marked in
its own colour: "where was this when I last saved it" is the question behind most uses of an undo
list.

**The panel is new to the native shell's layout**, docked beside Details and Material.

**Verification.** `tests/StudioHistoryPanelTests.cpp`: the empty history still having the position
it started from, one more row than commands, undone entries marked and muted, the saved position
marked, navigating backwards and forwards one command at a time with the document following,
navigating to where you already are running nothing, a click reporting rather than moving, and a
click on the current position asking for nothing. Plus `CnaStudioShellPreviewHistoryPanel`

### `STUDIO-07018` — Editors for the property kinds the Details panel shows read-only

**Acceptance.** Every kind the schema declares gets a control rather than a summary, and the
controls suit what the value *is* rather than what it is stored as

**An enumeration is chosen, not typed.** It is a closed set the descriptor already names, and a text
field over one is a field where every typo produces a scene the loader will refuse to open. The
drop-down (`STUDIO-03036`) is what this was waiting for. An enumeration whose descriptor declares no
options still falls back to typing, because a drop-down over nothing is a control that cannot be
used at all.

**A quaternion is edited as Euler degrees.** Its components are not numbers a person can reason
about: nobody knows what to type into `w` to turn something thirty degrees, and four independent
numbers is how you produce a value that is not a rotation at all. The conversion is
`SceneTransform`'s, in XNA's own convention — an editor that agreed with itself but not with the
runtime would show angles the game does not produce.

**A colour gets a swatch and four channels**, in the 0..255 the value is stored in rather than a
normalised range the user would have to convert to. Not a colour *picker*: that is its own control
and its own task. The swatch is what makes a row of four numbers legible as a colour at all.

**A reference gets a picker over what exists.** Nobody types a UUID, and a reference to something
that is not there is exactly the state the Problems panel exists to report. `(none)` comes first,
because clearing a reference is an ordinary thing to want; an entity is never offered itself; and a
reference to something that has gone still shows its id rather than reading as `(none)`, which would
look like the value had been cleared.

**One loop draws every row of numbers.** A vector, a quaternion, a rectangle and a colour are all "a
row of boxes with different letters on them", and writing that once is what keeps the column widths,
the font, the select-all behaviour and the parsing identical across them. Six copies is how a
property grid ends up with one field that commits per keystroke and five that do not.

**What is still a summary**: lists and structures, which need nested editing rather than another
control. The panel now *counts* what it could not edit (`readOnlyProperties`), so a kind falling
through is a number a test asserts on rather than a line of grey text somebody has to notice — which
is how a kind stays unimplemented long after the widget it needed arrived.

**Verification.** `tests/StudioDetailsPanelTests.cpp`: a quaternion shown as angles, every declared
kind getting a control with the fall-through count at zero, and the two that legitimately remain
summaries still saying what they hold. Plus `CnaStudioShellPreviewDetailsPanel`, which photographs
the panel with a sprite selected — asset picker, colour swatch, integer rectangle and enumeration
all in one frame

### `STUDIO-07019` — An undoable command for an entity's enabled flag

**Acceptance.** Turning an entity off goes through the command history like every other edit

**It was the one edit in the inspector Ctrl+Z could not reach**, and it is the one somebody does by
accident: the flag decides whether an entity renders, ticks and answers queries at all.

**A command that changes nothing is refused rather than executed.** An undo stack with no-ops in it
makes Ctrl+Z appear to do nothing, which is worse than doing the wrong thing, because the user
cannot tell how many more to press. Repeated flips merge under `MergeWithPrevious`, so dragging a
checkbox back and forth is one step rather than twenty.

**Verification.** `tests/StudioDetailsPanelTests.cpp`: the flag reaching the document and undo
returning it, a no-op refused, a missing entity refused, and repeated flips merging into one step
that undoes to where it started rather than to the intermediate state

### `STUDIO-07011` — Port the Diagnostics panel

**Acceptance.** What this Studio is running on, including what Studio's host contract made of the
renderer, and a way to get it out of the window

**It takes a snapshot, not a device.** The ImGui panel reaches through the application object into
the viewport and asks the live graphics device what it can do — which is why it could never be
tested without one, and why it could not exist in the CNA-free build at all. Here the *host* fills
in a plain struct once a frame and the panel draws it. A build with no device fills in the honest
empty answer and the panel says `unknown` rather than leaving cells blank: a blank reads as a panel
that failed to draw.

**It reports the host capability contract**, which the ImGui panel never could — `STUDIO-02022`
landed after it. Each requirement's outcome is shown with its severity in colour, and the renderer's
own words survive into the text report, because that is the part that answers *why*.

**It exists to be pasted.** "Why does a model look different on that machine" is the first question
of every graphics bug report, and a panel somebody has to transcribe by hand is a panel nobody puts
in one. `Copy report` hands back exactly the text the rows show, through the same clipboard seam the
Output Log uses, and degrades visibly where CNA's Devices module is off (gap G-02).

**What is not carried over**: the live player input snapshot, which belongs with play mode in the
native shell and has nothing to report until it exists there.

**Verification.** `tests/StudioDiagnosticsPanelTests.cpp` — the cases a live device would have made
untestable: no device at all, a renderer that fails the contract, no player builds, every known
renderer listed with its host tier, the text report carrying the renderer's own reason, `Copy
report` handing back that same text, and no phase violations across repeated frames. Plus
`CnaStudioShellPreviewDiagnosticsPanel`

### `STUDIO-07009` — Port the viewport container

**Acceptance.** The scene on screen in the native shell, navigable, and a click in it selects what
it hits

**In progress.** What holds: the scene is composited (`STUDIO-04012`), the wheel zooms about the
pointer, the middle *or* right button pans, a click picks the topmost sprite and Ctrl adds to the
selection, a click on nothing clears it, and **all three manipulators drag** — translate
axis-constrained, rotate about the ring, scale as a screen-space ratio — with Ctrl snapping to the
project's step or the visible grid, **on one entity or on a whole selection**. What does not: the 3D
view toggle, tilemap painting, and forwarding input to a running player. Each is its own task and
each is a real piece of the prototype's viewport.

**A group drag is one gesture applied many times, not many gestures.** The three drags above compute
*what the gesture is* — how far along an axis, through what angle, by what factor — and the
multi-drag turns that one answer into an edit per entity. Two gesture implementations would be two
chances for the group and the entity under the cursor to disagree. The manipulator sits at the
average of the selection's positions, which is where the renderer already draws it, so hit-testing
happens where the gizmo *is*; and the pivot is the one captured when the drag began, because the
entities move as it proceeds and a centre recomputed from them chases itself.

**Two group drags are two undo entries.** They share a merge-key shape, so without a counter
distinguishing them the second would merge into the first and one Ctrl+Z would jump back past a
gesture the user had already finished.

**The whole drag is one undo entry.** The first frame opens it and every frame after merges into
it. Sixty entries a second is an undo stack nobody can use: Ctrl+Z would rewind the gesture frame by
frame, and nobody counts frames.

**While a manipulator has the pointer, nothing else does.** The press that grabs an arm must not
also pick — a user aiming at an arm lying over another sprite would select that sprite and lose the
thing they were moving — and the drag must not also pan, or the camera would take the entity with
it. The manipulator is therefore resolved before the camera and before the selection, and it returns
immediately. That ordering was wrong when first written, and the test that catches it holds the
middle button down through a gizmo drag.

**A release ends it wherever the pointer is.** A drag that only ended when the release landed back
inside the viewport would leave the gizmo stuck to the cursor the moment somebody let go over a
panel.

**And the toolbar's three transform buttons finally do something.** They have been drawing since
the toolbar existed. They are checkable now, so the toolbar shows which manipulator is on — three
buttons that look the same whichever is active is three buttons nobody trusts — and switching mode
ends any drag in flight, because a half-finished translate would otherwise keep writing positions
after the user reached for Rotate. The renderer is told the same mode, so the manipulator drawn is
the one a drag will grab.

**The panel draws nothing**, which is what makes this half testable at all. The scene arrives as a
texture; this is the camera the pointer moves and what a click in it selects, and both are
arithmetic over a camera and a document. So the tests drive it the way a user does — a pointer over
a rectangle — with no graphics device anywhere.

**Zoom is about the pointer and multiplicative.** About the centre makes a user chase the thing they
were looking at across the screen; additive makes the first notch out of a close view do almost
nothing and the first notch out of a far one leap.

**Both pan buttons.** A trackpad has no middle button, and a viewport a laptop cannot pan is a
viewport half the users cannot use. A pan that *started* outside the viewport does not move it: the
pointer crosses the viewport during every drag of a splitter or a dock tab, and a camera that jumped
whenever one passed over would be unusable.

**A missed Ctrl-click does not clear the selection.** Clearing on a click that hits nothing is how a
user deselects without a keyboard, and wiping a careful multi-selection because one additive click
missed would be unforgivable.

**Verification.** `tests/StudioViewportPanelTests.cpp`: the camera told its extent, zoom keeping the
world point under the pointer, both pan buttons, a pan that began elsewhere ignored, picking,
clearing, Ctrl adding, Ctrl-missing leaving the selection alone, the reported world position, and an
empty rectangle doing nothing rather than dividing by it — plus the manipulators: a press on
an arm dragging rather than selecting, an axis-constrained move, one undo entry for a whole drag
that undoes to where it started, a release outside the viewport ending it, `GizmoMode::None`
picking as before, the rotate and scale manipulators writing their own property, a drag that does
not also pan, and the toolbar choosing the manipulator through the real shell

### `STUDIO-07024` — Layers panel: the project's render layers and what is on each

**Acceptance.** Every layer the project declares, in draw order, with what is on it — and clicking
one selects everything there

**New rather than ported**, which is why it has an id of its own. The native shell registered a
`Layers` tab from the start and it has been an empty rectangle ever since; the prototype has no such
panel at all, so there was nothing to port.

**The order is the meaning.** A project's layers are a list rather than a set because index 0 draws
first. Sorting them by name would read tidier and say nothing.

**It answers the one question the outliner cannot.** The outliner is ordered by the hierarchy and a
layer cuts across it, so "what is on the background" has no answer there. Clicking a layer selects
everything on it, which is how a user turns "the background is wrong" into something they can edit.

**An entity with no Layer component is on the first layer**, which is what the runtime does with
one. Reporting it as belonging to nothing would hide every entity in a project that has never
touched layers. And an empty layer is shown dimmed rather than hidden: hiding it would make a user
wonder where the layer they just added went.

**Verification.** `tests/StudioLayersPanelTests.cpp`: draw order preserved, the implicit first
layer, an empty layer dimmed rather than hidden, entities listed under their layer, clicking a layer
selecting all of it, clicking one entity selecting just that, and no project saying so. Plus
`EveryPanelWithoutContentIsNamedRatherThanBeingAnEmptyRectangle`, which is the same discipline as
the unimplemented-command guard: a panel that draws nothing is on a list with a reason, or the
build fails

### `STUDIO-07014` — Port the Comparison panel

**Acceptance.** The Backends panel runs the open scene on every discovered player build and reports
where the pictures disagree, from the native shell, with no Dear ImGui and no graphics device.

**It reads a snapshot, not the run.** The legacy panel held its own `BackendComparison`, which is
why it was never tested: showing it anything at all meant launching several games. Here the run is
owned by `StudioShellPanels` — so it survives the tab being closed, which half an hour of launching
games deserves — and the panel takes the entries, the state and the verdict as plain values. Every
case a real run makes expensive to reach is then a unit test: a capture that never arrived, two
frames of different sizes, a renderer that would not launch.

**A row opens.** Two renderers disagreeing is the start of an investigation, not the end of one, so
each row carries how many pixels, how far apart, and where on the picture. A band along one edge is
a viewport or scissor problem; a scattering over one sprite is a filtering one — the rectangle
usually is the diagnosis, and the legacy panel printed it as an indented line of text.

**"Waiting" and "never arrived" are different sentences.** The same empty entry means be patient
during a run and "this renderer produced nothing" after it, and a user cannot act on the second
while it is worded as the first — so the run's state decides which is said, and the colour with it.

**The tolerance is clamped, not trusted.** 255 calls every pair of images identical, which is a
comparison that can never report anything: a control that can be set to "always agree" is worse than
no control.

**Verification.** `tests/StudioComparisonPanelTests.cpp`: the two empty states, Compare and Cancel
never both offered, a problem said before the button and not hiding a run already in flight, the
reference named on its own row, agreement and disagreement differing in words *and* colour, the
bounding box present only where something differs, a collapsed renderer keeping its verdict, a
missing capture reading differently during and after a run, a size mismatch not reported as a
disagreement, a launch failure carrying its reason, the verdict withheld until the run finishes, and
the tolerance clamped — checked by removing the clamp and watching it fail
