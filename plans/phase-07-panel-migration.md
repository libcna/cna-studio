# Phase 7 — Existing-panel migration

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-07001` … `STUDIO-07999` and are never reused.

**Purpose.** Port every prototype panel onto the Studio UI and retire the Dear ImGui presentation.

**Exit criteria.** Feature, input, docking and visual parity, proven panel by panel against the Phase 0 inventory — then ImGui is removed deliberately.

**Progress:** 13 of 26 complete `██████░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-07001` | Compatibility adapter so unported panels keep working during the migration | 🔄 | `STUDIO-06015` |
| `STUDIO-07002` | Port the main menu bar | ⬜ | `STUDIO-06003` |
| `STUDIO-07003` | Port the toolbar | ⬜ | `STUDIO-06006` |
| `STUDIO-07004` | Port the status bar | ⬜ | `STUDIO-06007` |
| `STUDIO-07005` | Port the Console / Output Log | ✅ | `STUDIO-07001` |
| `STUDIO-07006` | Port the Hierarchy panel (World Outliner) | ✅ | `STUDIO-07001` |
| `STUDIO-07007` | Port the Inspector panel (Details) | ✅ | `STUDIO-07001`, `STUDIO-03035` |
| `STUDIO-07008` | Port the Content Browser | ✅ | `STUDIO-07001`, `STUDIO-03034` |
| `STUDIO-07009` | Port the viewport container | 🔄 | `STUDIO-04012` |
| `STUDIO-07010` | Port the Build panel | ✅ | `STUDIO-07001`, `STUDIO-02040`, `STUDIO-03036` |
| `STUDIO-07011` | Port the Diagnostics panel | ✅ | `STUDIO-07001`, `STUDIO-02022` |
| `STUDIO-07012` | Port the Validation panel | ✅ | `STUDIO-07001`, `STUDIO-03034` |
| `STUDIO-07013` | Port the History panel | ✅ | `STUDIO-07001`, `STUDIO-03034` |
| `STUDIO-07014` | Port the Comparison panel | ⬜ | `STUDIO-07001` |
| `STUDIO-07015` | One log model, read by both consoles | ✅ | — |
| `STUDIO-07016` | Panel content seam: the shell hosts a ported panel's content | ✅ | `STUDIO-06018` |
| `STUDIO-07017` | A module for the ported panels, above widgets and document alike | ✅ | `STUDIO-07016` |
| `STUDIO-07018` | Editors for the property kinds the Details panel shows read-only | ✅ | `STUDIO-07007`, `STUDIO-03036` |
| `STUDIO-07019` | An undoable command for an entity's enabled flag | ✅ | `STUDIO-07007` |
| `STUDIO-07020` | Prove parity against the Phase 0 panel and shortcut inventory | ⬜ | `STUDIO-00014`, `STUDIO-07014` |
| `STUDIO-07021` | Prove input parity: keyboard, mouse, drag and drop, clipboard, text editing | ⬜ | `STUDIO-07020` |
| `STUDIO-07022` | Prove docking parity | ⬜ | `STUDIO-07020` |
| `STUDIO-07023` | Visual acceptance review against the Phase 0 reference screenshots | ⬜ | `STUDIO-00013`, `STUDIO-07020` |
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
selection, and a click on nothing clears it. What does not: the gizmos, dragging an entity, the 3D
view toggle, tilemap painting, and forwarding input to a running player. Each is its own task and
each is a real piece of the prototype's viewport.

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
empty rectangle doing nothing rather than dividing by it
