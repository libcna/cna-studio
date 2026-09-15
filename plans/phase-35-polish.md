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

**Progress:** 8 of 34 complete `██░░░░░░░░░░`

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
| `STUDIO-35034` | Entity and asset reference fields that show what they point at | ⬜ | `STUDIO-35033` |
| `STUDIO-35035` | Multi-selection state in the Details panel | ⬜ | `STUDIO-35033` |
| `STUDIO-35040` | Content Browser thumbnail grid, with a list/grid switch | ⬜ | `STUDIO-35030` |
| `STUDIO-35041` | Real content thumbnails, cached and generated off the frame | ⬜ | `STUDIO-35040` |
| `STUDIO-35042` | Content Browser breadcrumbs, search and type filters | ⬜ | `STUDIO-35040` |
| `STUDIO-35050` | Viewport toolbar: view, transform mode, space and snap, over the scene | ✅ | `STUDIO-35021` |
| `STUDIO-35051` | Viewport grid that reads as a ground plane, with origin axes | ⬜ | — |
| `STUDIO-35052` | An orientation widget in the viewport corner | ⬜ | `STUDIO-35050` |
| `STUDIO-35053` | Selection feedback in the viewport: outline, pivot, bounds | ⬜ | — |
| `STUDIO-35060` | World Outliner: visibility and lock affordances per row | ⬜ | `STUDIO-35031` |
| `STUDIO-35061` | World Outliner: search, filter and prefab indicators | ⬜ | `STUDIO-35031` |
| `STUDIO-35070` | Status bar density and legibility | 🔄 | `STUDIO-35021` |
| `STUDIO-35071` | Toolbar ergonomics: grouping, hover, active and disabled states | 🔄 | `STUDIO-35021` |
| `STUDIO-35080` | Visual regression suite 2.0: five resolutions, both themes, ten scenarios | ⬜ | `STUDIO-35021` |
| `STUDIO-35081` | Replace the prototype comparison with the professional-environment criterion | ✅ | — |

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
