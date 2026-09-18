# Phase 35 — Production polish

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-35001` … `STUDIO-35999` and are never reused.

**Purpose.** The long-running quality campaign that separates a tool that works from a tool people choose.

**Exit criteria.** Never "done" — the standard is that nothing in a normal workflow looks unfinished.

---

## CNA Studio Visual Quality 1.0

> A milestone inside this phase, brought forward from "near the end" to **now**, for a reason worth
> writing down.

**The old acceptance criterion has expired.** `docs/VISUAL-ACCEPTANCE.md` compared the native shell
against the Dear ImGui prototype and asked whether it was better. It was, on every row, and the
comparison has stopped telling us anything: the prototype is a debug-UI toolkit used as an editor,
and clearing it is not evidence of anything a user would care about. Measuring against it now would
let Studio stop improving while the number still said it was winning.

**The new one.** At 1920×1080, on first launch, CNA Studio must read as *a serious modern 3D
game-development environment* rather than as a custom developer tool that is tidier than ImGui. The
World Outliner, the viewport, the Details panel and the Content Browser have to form one coherent
professional workspace — not four rectangles of the same colour with different text in them.

**Why now rather than at the end.** Every panel built after this point inherits whatever visual
language exists when it is written. A tool that is restyled at the end is a tool with twenty panels
to restyle; one that is restyled now has six, and the twenty that follow are built against a
finished vocabulary.

**The benchmark is professional editors in general** — Unreal, Unity, Godot, Blender, and the better
IDEs and DCC tools — read for *principles*: density, hierarchy, typography, focus, state, chrome,
discoverability, contrast, how assets are presented. **Nothing is copied.** No branding, no assets,
no icons, no layout, no trade dress. Where these tools agree on something, they agree because it is
true — axis colours, a property grid's shape, what a tab strip looks like — and Studio follows the
truth rather than any one product's expression of it.

**Progress:** 15 of 38 complete `████░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-35001` | Consistent visual language audit across every panel | ⬜ | `STUDIO-07023` |
| `STUDIO-35002` | Interaction predictability audit | ⬜ | `STUDIO-07021` |
| `STUDIO-35003` | Error message quality pass | ⬜ | — |
| `STUDIO-35004` | Empty state quality pass | ⬜ | `STUDIO-06013` |
| `STUDIO-35005` | Tooltip coverage and quality | ⬜ | `STUDIO-03021` |
| `STUDIO-35006` | Drag-and-drop polish across every surface that accepts a drop | ⬜ | `STUDIO-03023` |
| `STUDIO-35007` | Context menu coverage and consistency | ⬜ | `STUDIO-06005` |
| `STUDIO-35008` | Progress reporting accuracy and cancellation coverage | ⬜ | `STUDIO-30001` |
| `STUDIO-35009` | Sensible defaults review | ⬜ | `STUDIO-06009` |
| `STUDIO-35010` | No unfinished debug text in any normal workflow | ⬜ | — |
| `STUDIO-35020` | Tab strips read as tab strips: a recess, raised tabs, a raised active one | ✅ | — |
| `STUDIO-35021` | Panel chrome: outlines, seams and the window's own edges | ✅ | `STUDIO-35020` |
| `STUDIO-35022` | Typographic scale: panel titles, section headers, body, secondary, status | 🔄 | — |
| `STUDIO-35023` | Review the IBM Plex weights, sizes and atlas resolution against real panels | ⬜ | `STUDIO-35022` |
| `STUDIO-35030` | Scene-content and asset-kind icons, and a row that shows what it is | ✅ | — |
| `STUDIO-35031` | List rows: alternating fill, hover, indent guides, selection order | ✅ | — |
| `STUDIO-35032` | Vector fields with always-visible axis letters in the gizmo's own colours | ✅ | — |
| `STUDIO-35033` | Property grid alignment: label column, value column, nesting, reset markers | ⬜ | `STUDIO-35032` |
| `STUDIO-35036` | Every primitive the draw list emits names a texture a backend can resolve | ✅ | — |
| `STUDIO-35037` | A float field shows the number that was typed, not its binary representation | ✅ | — |
| `STUDIO-35034` | Entity and asset reference fields that show what they point at | ⬜ | `STUDIO-35033` |
| `STUDIO-35035` | Multi-selection state in the Details panel | ⬜ | `STUDIO-35033` |
| `STUDIO-35040` | Content Browser card grid, with a list/grid switch | ✅ | `STUDIO-35030` |
| `STUDIO-35041` | Real content thumbnails, cached and generated off the frame | ✅ | `STUDIO-35040` |
| `STUDIO-35042` | Content Browser search and type filters | ⬜ | `STUDIO-35040` |
| `STUDIO-35050` | Viewport toolbar: view, transform mode, space and snap, over the scene | ✅ | `STUDIO-35021` |
| `STUDIO-35051` | Viewport grid that reads as a ground plane, with origin axes | ⬜ | — |
| `STUDIO-35052` | An orientation widget in the viewport corner | ⬜ | `STUDIO-35050` |
| `STUDIO-35053` | Selection feedback in the viewport: outline, pivot, bounds | ⬜ | — |
| `STUDIO-35060` | World Outliner: a visibility toggle on every row | ✅ | `STUDIO-35031` |
| `STUDIO-35063` | Guard test: a row's trailing toggle is drawn over the row fill, not under it | ✅ | `STUDIO-35060` |
| `STUDIO-35062` | A lock concept in the scene document, and its outliner affordance | ⬜ | `STUDIO-35060` |
| `STUDIO-35061` | World Outliner: search, filter and prefab indicators | ⬜ | `STUDIO-35031` |
| `STUDIO-35070` | Status bar density and legibility | ✅ | `STUDIO-35021` |
| `STUDIO-35071` | Toolbar ergonomics: grouping, hover, active and disabled states | 🔄 | `STUDIO-35021` |
| `STUDIO-35080` | Visual regression suite 2.0: five resolutions, both themes, four scenarios | ✅ | `STUDIO-35021` |
| `STUDIO-35081` | Replace the prototype comparison with the professional-environment criterion | ✅ | — |
| `STUDIO-35082` | Tolerant golden comparison and region-occupancy probes | ⬜ | `STUDIO-35080` |

---

## Visual Quality 1.0 — acceptance and verification

### `STUDIO-35020` — Tab strips read as tab strips

**Acceptance.** Which panel a tab strip is showing, and which panels it could show, are answerable
without clicking anything.

**What was wrong.** Three surfaces at one value. The strip was `PanelHeader`, an inactive tab drew
*nothing at all* so the strip showed through, and the active tab was four values above it — so a
strip of four panels read as four labels floating on a bar, and which of them were tabs was
something the user learned by clicking one.

**Four changes, and the first is the one that matters.** The strip is now a **recess**:
`TabStripBackground`, below both the tabs and the panel body. A tab is a raised thing sitting in a
recess, and that is a shape the eye reads before it reads anything else. Inactive tabs are a real
surface (`TabInactive`), separated from each other by a hairline of the strip's own colour —
without it two adjacent tabs are one wide surface with two labels in it. The active tab keeps its
accent rule and takes **weight** rather than size: a larger active tab would change the strip's
height when the selection moved, and every tab in the window would jump.

**Verification.** `TheBackgroundLayersAreActuallyDistinctFromOneAnother` asserts the ordering and a
minimum separation in both themes. Four values is about where a step stops being visible on an LCD;
below that the layering is notional, and it is invisible in the tokens — `rgb(32,34,38)` and
`rgb(34,36,40)` look like two decisions.

### `STUDIO-35021` — Panel chrome

**Acceptance.** A user can see where one panel ends and the next begins, and where the application
ends and the workspace begins.

**How.** An outline per dock leaf in a token of its own, `PanelOutline`, **darker** than either
panel it divides — lighter reads as a highlight, as though something were raised along that edge,
and a workspace whose every seam is raised is busier than one made of seams. The splitter gutter is
the same token, so a splitter at rest is indistinguishable from the seam it sits in, which is what
a splitter should be until somebody reaches for it. The menu bar, toolbar and status bar take
`WindowChrome` and the outline rather than the in-content separator: the toolbar used to be
`PanelBackground` with a faint rule, which put it at the same value as the panel under it and left
the two reading as one surface with a line drawn across it.

**Adjacent leaves overlap their strokes** along a shared edge, deliberately. The token is the same,
so two strokes and one are the same pixels — and the alternative is a dock tree that knows which of
its edges are interior, which is a lot of arithmetic to save a redundant line.

### `STUDIO-35030` — Scene-content and asset-kind icons

**Acceptance.** A row in the World Outliner or the Content Browser says what it *is* before it is
read.

**Sixteen new icons**, drawn on the same 0..16 grid with the same primitives as the existing
twenty-three, so the set stays one visual language rather than becoming two. Entity, Camera, Light,
Mesh, Sprite, Prefab; Texture, Material, Audio, Scene; Visible, Hidden, Lock, Unlock, Add, Select.
No vendored asset, no licence question, crisp at every DPI scale.

**Several were redrawn after looking at them at sixteen pixels**, which is the size they are used
at and a different question from whether the shape is right: a cube in elevation is a square, and
half this set is already squares, so Entity is isometric; a stills camera is a rounded rectangle
with a circle in it, and a circle in a rectangle is also the Material icon, so Camera is the
movie-camera silhouette; a speaker with two arcs puts the outer arc three pixels from the edge where
it reads as a smudge, so Audio has one; an eye drawn as an ellipse with a dot in it is a fried egg,
so Visible is two arcs meeting at the corners.

**The outliner decides an entity's icon from its components**, not from a field — a scene has no
such field and inventing one would put a presentation concern into the document format, where it
would then have to be migrated, validated and exported. Ordered by how much the answer tells a user:
an entity with a camera and a light is a **camera** with a light attached, because the camera is
what somebody was scanning for.

### `STUDIO-35031` — List rows

**Acceptance.** A wide row can be traced across the panel, the pointer says what it is over, and a
nested row shows what it is nested under.

**An alternating fill four values off the panel** — enough to trace a row across nine hundred pixels
of outliner, not enough to read as a stripe, because a visible stripe is a 1990s table. Keyed on the
row's index in the **model** rather than on a visible counter: keyed on what is on screen, scrolling
makes the whole list flicker between two phases, which is far worse than no striping at all.

**And it found a painting-order defect immediately.** The disclosure triangle was drawn before the
row's background, so every fill went straight over it — expandable rows lost their triangle **on
alternate lines only**, which reads as a data problem rather than as a painting order. The triangle
is drawn after the fills now; its *interaction* stays where it was, because input order is not
paint order and moving both would have changed which widget claims a click.

### `STUDIO-35032` — Vector fields with axis letters

**Acceptance.** A row of three numbers reads as a vector without a second row of labels above it.

**The letters were already there and could never be seen.** They were on `placeholder`, which shows
only while a field is *empty* — so every populated Position, Rotation and Scale in Studio was three
unlabelled boxes, which is precisely the case the letters exist for. `StudioTextFieldOptions::prefix`
draws inside the field, always, with the text area inset past it so a long value scrolls rather than
running underneath.

**In the gizmo's own colours.** Red, green, blue in axis order is the convention every 3D tool
shares, and it has to match the viewport's — an inspector teaching a mapping the viewport
contradicts is worse than no colour coding, because the user learns it and is then wrong. Four new
roles, `AxisX`/`AxisY`/`AxisZ`/`AxisW`, desaturated from the primaries: a saturated red field label
beside a saturated green one vibrates, and a property grid is read for hours.

**Three details that each took a look at the result to find.** The letters are upper case to a user
and lower case in the code, because the names are also the widget ids and the retained state behind
every one of these fields is keyed on them — renaming an id to change a letter's case is how a field
forgets what was typed into it. `pitch`/`yaw`/`roll` come out as X/Y/Z, which is the correct mapping
and is what makes the Transform block read as one grid rather than as three rows with three
vocabularies. And a colour's R/G/B/A get **no** letters: the swatch beside them says which is which
far better than a letter can, and four letters cost exactly the width `255` needs — with them, every
`Tint` in Studio read `R 2… G 2… B 2… A 2…`.

**Verification.** `TheThreeAxisColoursAreTellableApartAndAreRedGreenBlueInOrder`, asserted as "each
axis's own channel dominates" rather than against exact values, so the colours can be retuned for
contrast without the test becoming a copy of the theme.

### `STUDIO-35033` — Property grid alignment

**Acceptance.** The label column is sized from what the labels need rather than from a fraction of
the panel, values line up down the grid, nested properties are indented and a property that differs
from its default is marked. A three-digit coordinate is legible at the Details panel's default
width.

**The evidence, measured rather than asserted.** The label column is `round(width * 0.38)`, fixed.
At the default dock width that is 96 px for the word "Position" and 148 px for three numbers, an
axis letter each and two gaps — and the moment the panel grows a scrollbar, which any entity with
three components does, the ten pixels it takes turn a Position of `320, 240` into `3…, 2…`. The
same panel one dock-width wider shows it perfectly, which is why this has been easy to miss:
captures were taken of entities with two components.

The fraction is also wrong in the other direction. A `Loop` checkbox gets 148 px of control column
and sits 96 px from its own label, so a Transform block reads as two columns of unrelated things
rather than as a grid. A column sized to the widest label in the *visible* set, clamped to a
sensible range, is what every other property grid does — and the note on `splitRow` explains why the
fraction was chosen (a column that resizes as the selection changes makes every control jump), which
is a real problem that a clamp and a minimum solve without a fraction.

**Verification.** A capture at the default Details width of an entity with a Transform, a Sprite
Animation and an Audio Source on it — the case that produced the evidence above — plus a unit test
that a three-digit coordinate is not truncated at that width.

### `STUDIO-35081` — Replace the prototype comparison

**Acceptance.** `docs/VISUAL-ACCEPTANCE.md` states the professional-environment criterion, and the
prototype comparison is kept as history rather than as the standard.

**Done** — the reasoning is at the top of this section. The prototype captures stay in
`docs/reference/`: they are what the migration was checked against and deleting them would delete
the evidence for `STUDIO-06015`.

### `STUDIO-35050` — Viewport toolbar

**Acceptance.** The view, the transform mode, the transform space and snapping are visible and
operable where the user is already looking.

**Over the image rather than above it.** A strip that took height from the viewport would make the
scene smaller, and the scene is what the panel is for. Inset from the corner, rounded, on the popup
surface rather than the panel's — this sits above an image whose colour is whatever the user's level
happens to be.

**Driven by the action registry**, which is what makes it nearly free: a disabled command greys out
here, a checkable one shows its state, a rebound shortcut appears in its tooltip, and none of that
is code in the toolbar. A viewport toolbar with its own copies of those would be the second place
"is Rotate armed" is decided, and the two would disagree the first time one of them changed.

**Deliberately short.** Every control on it is also on a menu; these are the ones whose *state* a
user needs to see while dragging, which is the only reason to spend viewport on them. Ordered as the
work is — which projection, then what a drag does, then what it does it in, then whether it snaps.

**And it is measured before it is drawn.** A viewport too small for the strip gets none rather than
a clipped one: the buttons a clipped toolbar did draw are still clickable, which is worse than no
toolbar at all.

### `STUDIO-35037` — A float field shows the number that was typed

**Acceptance.** A property whose value is `0.6` reads `0.6`, in an editable field and in a
read-only summary alike, and the text shown always parses back to the identical float — so a field
nobody touches cannot drift. A plain decimal is preferred to shorter scientific notation.

**Verification.** `NumberTextTests.cpp`, and a 1400×900 capture of the Details panel over an audio
source with a volume of 0.6, a pan of -0.25 and a pitch of 0.1.

**Found by looking at the panel**, not by reading the code. Verifying `STUDIO-07044` needed an
entity with an audio source on it, and the first one written had a volume of 0.6 — which the
Details panel showed as **`0.600000024`**, beside a pitch of `0.100000001`. Every value in the
example project happens to be exactly representable (positions of 200, scales of 1), which is why
a panel that has been captured at five resolutions in both themes had never shown it.

`%.9g` is the precision that round-trips every binary32, which is why it was chosen. Nine
significant digits of a float are nine digits of its *binary representation*, and the last three of
them are noise. `PropertyValue::toString` had the same mistake in the other direction — `%g`'s six
digits print `33.3333` for a float that is `33.333332`, so the summary named a different number
from the field. Both are `studioFormatFloat` now: the shortest decimal that reads back as the same
float.

**And "shortest" turned out to be the wrong question on its own.** The first implementation
answered 200 with `2e+02`, which is two characters shorter and reads back exactly — so the Position
fields, which had been right all along, started showing scientific notation. A plain decimal wins
whenever one round-trips; scientific notation is left to the values that have no other form.

The same defect is in the *file* as well as on the screen, which is `STUDIO-02042`.

### `STUDIO-35036` — Every primitive names a resolvable texture

**Acceptance.** No draw command reaches a UI render backend naming a texture the backend cannot
resolve, because both backends drop such a command silently and correctly.

**A real defect, found by drawing something that had never been drawn.** `StudioDrawList::drawLine`
has two paths: axis-aligned lines become quads through `addQuad`, which uses the font atlas's
reserved white texel, and **diagonal lines emitted their command against `kUiTextureNone`** with
zeroed UVs. The comment said "with no atlas the texture is `kUiTextureNone` and the UVs are simply
unused", which was true of the *coordinates* and not of the *command*: both backends skip a command
whose texture they cannot resolve — deliberately, because one naming a texture that was never
created would otherwise sample whatever happens to be bound.

**So every diagonal line in Studio was invisible on a real device**, and had been for as long as the
draw list existed. Nothing noticed because almost nothing drew one: separators, borders, grid rules
and panel chrome are all axis-aligned. It surfaced the day `STUDIO-35030`'s icon set arrived, as an
isometric cube that drew as three bars.

**The software rasterizer drew them correctly the whole time**, which is why no headless capture
showed it, and that is worth stating as a property of the harness rather than as bad luck: the
preview is a second implementation of the same geometry, and the two agreeing is the thing being
tested rather than something to rely on. The A/B comparison of `STUDIO-04025` exists for exactly
this shape of failure between the two *CNA* backends; this is the same failure one level up.

**Verification.** `EveryPrimitiveTheDrawListEmitsNamesADrawableTexture` binds a stand-in atlas and
asserts every emitted command names it. Without an atlas the default genuinely *is*
`kUiTextureNone` — correct for a draw list nobody will render, and exactly the state in which the
assertion would say nothing.

### `STUDIO-35040` — Content Browser card grid

**Acceptance.** A folder of textures reads as a folder of textures rather than as a list of file
names, and a user can switch between the two presentations.

**Why a browser needs both.** A list is dense, scannable by eye and the right answer for a folder of
two hundred scripts. It is the wrong answer for a folder of textures, where the thing a user is
looking for is a *picture* — and a browser that shows only names makes them open each one to find
it. An asset is a thing with an appearance, and a browser that shows only its name is a file
manager.

**The grid shows one folder; the tree shows the whole project.** That is the difference between the
two presentations rather than an incidental one: a grid of every asset under a folder is a wall, and
the folder a user is *in* is the unit they think in. So the grid gets a breadcrumb, every segment
clickable — a path drawn as text says where the user is and leaves going up a level to a control
that does not exist.

**The root crumb is called "Project", not "Assets"**, although the root usually *contains* a folder
called Assets. That is exactly why: a breadcrumb reading "Assets / Assets / Textures" is a user
wondering which of the two they are in.

**Folders before files, always.** A user navigating is looking for a folder and a user browsing is
looking at assets, and the first of those is the one interrupted by having to scan past two hundred
textures. A folder's card counts everything *underneath* it rather than its immediate children:
"3 items" on a folder nobody has opened is the number that decides whether opening it is worth the
click.

**A folder card opens on a single click**, unlike the tree, where a click selects and the triangle
opens. A grid has no triangle and no second thing to click — a folder card that needed a double
click would be one a user opens by accident on the first try and not at all on the second.

**Cards are culled against the viewport rather than left to the scissor.** A project with four
thousand assets in one folder would otherwise describe four thousand widgets to show twenty, and
every one of them registers with the input router whether or not it is visible.

**The icon is drawn into the rectangle a thumbnail will use**, at 56% of the card, so `STUDIO-35041`
replaces the picture without moving anything. Labels are truncated rather than wrapped: two lines of
file name would make the cards different heights, and a grid whose rows do not line up is not a grid.

**Verification.** `TheGridShowsOneFoldersImmediateContentsAndNotTheWholeProject`,
`AFolderCardSaysHowMuchIsUnderIt`, `TheBreadcrumbNamesTheRootAndEveryLevelBelowIt`,
`AMissingAssetsCardSaysSoInTheWarningColour` and `BothViewsHaveANameAndTheGridIsTheDefault` — all
over `studioContentCards` and `studioContentBreadcrumb`, which take a database and a string and need
no frame. Deciding what a folder holds and deciding where a card goes fail separately, and a
screenshot cannot tell them apart.

### `STUDIO-35080` — Visual regression suite 2.0

**Acceptance.** The shell is captured at every resolution and theme this project claims to support,
with the project open so the panels hold real content, and each capture asserts more than that a
file appeared.

**The defect it closed first.** `--shell-preview` — the only visual harness this project has without
a GPU, and the source of every golden image — did not apply `--screenshot-min-colors`. It could
rasterise a frame of nothing, write a perfectly valid PNG and exit zero. That is exactly the failure
`STUDIO-04015` closed for the *windowed* capture, one harness down, and it is worse there: this is
the harness that runs on every commit.

The check runs **before the file is written**, for the reason the windowed one does — a blank
capture must not leave a picture behind for somebody to look at and believe.

**Fourteen cases.** Five resolutions in both themes, plus four scenarios at one size each. The
matrix is `1280x720`, `1600x900`, `1920x1080`, `2560x1440` and `3840x2160`; the last two are in
the list although no runner has such a display, because the preview needs none and **the size a
layout breaks at is usually the one nobody photographed**.

**256 distinct colours as the floor**, which is not a threshold any garbage frame passes and is not
so high that a legitimately quiet frame fails. A shell frame at any of these sizes has well over a
thousand — antialiased text alone spreads at every glyph edge — and a frame that rendered to nothing
has one, or two where something was cleared.

**The scenarios are about state rather than resolution**, so each is at whichever size it is
meaningful at. The context menu is at 1280x720 and at one exact point, because a context menu needs
the pointer over something that offers one: the Content Browser's tab, which is there at that size
and somewhere else at any other. A scenario that silently photographed no popup would be the more
expensive mistake, so the run fails rather than capturing the shell at rest.

**What is not done.** Tolerant golden comparison against stored references, and region-occupancy
probes — "the viewport occupies the middle 60% of the window" — are the next layer and are
`STUDIO-35082`. The floor here separates *drew nothing* from *drew something*; it does not separate
*drew the right thing*.

### `STUDIO-35060` — A visibility toggle on every outliner row

**Acceptance.** An entity can be shown or hidden from its row, through the history.

**Why it has to be on the row.** An outliner where hiding an entity means selecting it, finding the
Details panel and unticking a box is one where nobody hides anything — and hiding things is how a
large scene is worked on at all.

**The entity's `enabled` flag rather than a second "visible" one.** A scene has no such field, and
inventing one would put a presentation concern into the document format, where it would then have
to be migrated, validated and exported — and it would be a *second* thing that hides an entity,
which is one too many.

**Drawn only while the row is hovered or the toggle is off**, which is what every outliner that has
one does: a column of forty identical eyes is a column of noise, and the rows that matter are the
ones *not* in the default state. The hit area is described in both cases either way — a button that
existed only while hovered would be one a user cannot click, because the frame in which they press
is the frame it was there. Only the *drawing* is conditional.

**And the label stops where the toggle starts, hovered or not.** Text that reflowed as the pointer
crossed a row would be the most distracting thing in the panel.

**A press on the toggle is not a press on the row.** The toggle is described after the row so it
wins the click — the later of two overlapping widgets is the one a press lands on — and the panel
returns after handling it rather than falling through, because handling both would hide an entity
and select it in one gesture.

**Interaction there, drawing further down.** See `STUDIO-35063`.

### `STUDIO-35063` — Guard test: the toggle is drawn over the row fill

**Acceptance.** A tree row drawn with its toggle shown has something other than the row's own
background as the last geometry in the strip the toggle occupies.

**The defect it closes, which had already happened twice.** A tree row is described as *one widget
covering the whole line*, so anything that has to win the click against it is described before it —
and is therefore drawn before the row's background, which then paints over it.

The disclosure triangle went first, and it presented as a data problem: expandable rows lost their
triangle on alternate lines only, because the alternating fill is the one that was covering it. The
visibility toggle went second and presented as nothing at all. The click toggled, the tooltip
appeared, the panel test asserted `toggledRowAction`, the outliner test asserted `toggleOn` — and
the eye was never once on screen. **A feature that works and cannot be seen is worse than one that
is missing, because nothing reports it.**

**Asserted on the geometry, not on a capture.** A golden image would catch this, but only if the
golden had been taken while the toggle was right; the golden for a row nobody has hovered would
have been taken while it was wrong, and would then have defended the defect. The test walks the
emitted vertices and requires the last one in the toggle's strip not to carry the fill colour.

**Verified by reintroducing the defect.** The test fails against the original drawing order and
passes against the corrected one; an ordering assertion that has never been shown to fail is an
assertion about nothing.

### `STUDIO-35062` — A lock concept in the scene document

**Not done, and it is not a visual task.** Locking an entity against selection and editing needs a
field in the scene format, its migration, its export behaviour and a rule in every place that picks
or edits. `STUDIO-35060` covers visibility because `enabled` already exists and already means what
it needs to mean; there is no equivalent for lock, and adding an icon for a state nothing enforces
would be worse than having neither.

### `STUDIO-35070` — Status bar density and legibility

**Acceptance.** The bar's facts can be told apart at a glance, and the one a user reads deliberately
is not the faintest text in the window.

**Two changes, both about hierarchy rather than about size.** Each right-hand fact is a *label* and
a *value* now rather than one grey sentence: "Renderer: OPENGL4 on SDL3" in one colour is a string a
reader has to parse, and the label dimmed with the value at full weight is two things they can pick
out — the value being the half anybody is looking for. And the left-hand message, which is the
answer to "what am I looking at", is primary text unconditionally. It used to share the build
target's grey unless the scene was dirty, which made the project's own name the faintest deliberate
text in the window.

### `STUDIO-35041` — Real content thumbnails, cached and generated off the frame

**Done**, and it is the visible end of a chain rather than a feature on its own: `STUDIO-10014`
gave Studio a CPU decoder, `STUDIO-09003` made the thumbnails on worker threads, `STUDIO-09004`
keyed them on what they were made from, and this draws them.

**Three responsibilities, deliberately kept apart.** The cache has pixels and no idea what a texture
is. The grid wants a texture and cannot make one. Only `cna-studio-viewport` may touch a graphics
device (decision D-03). So the browser gains a `StudioContentBrowserServices` with one function —
asset id to `UiTextureId` — the binder builds it from the cache and the host's uploader, and the
viewport owns the texture because a texture's lifetime is the device's business.

**Unset is the quiet default, not a degraded mode.** Every CNA-free build, the headless preview and
the whole test suite leave the seam empty, and the grid draws its icons exactly as it did before
thumbnails existed. That is why `STUDIO-35040` put the icon in the rectangle a thumbnail would use:
nothing moves when one arrives, and nothing moves when one cannot be made.

**The host caches on the thumbnail's key, not on the asset's id.** It is asked once per visible card
per draw pass — forty times a frame, twice a frame — so re-uploading would be a texture upload per
card per pass, which is precisely what a thumbnail was supposed to save. Comparing the pixels
instead would cost more than the upload it avoided, so `StudioThumbnail` carries the content-and-
settings key it was made under and the host compares that. It changes exactly when the picture does.

**And the host is told when to let go.** The cache is bounded at 256 entries and a host's textures
are not, so without an eviction hook a project of a hundred thousand images fills a GPU with
pictures of folders nobody is in — a leak invisible until somebody profiles memory on a real
project. `StudioThumbnailCache::setOnDropped` closes that, and a test holds it.

**What a headless suite can and cannot check.** It cannot look at a thumbnail: there is no device,
so no texture is ever resolved. What it can check is that the grid *asks*, that it draws an image
when a resolver answers and an icon when it does not, and that `thumbnailsDrawn` counts them —
because a grid that had silently stopped asking would look identical in every screenshot this
project takes. The upload itself is compiled in the CNA configuration and exercised there.
