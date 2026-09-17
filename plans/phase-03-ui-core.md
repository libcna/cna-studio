# Phase 3 — Studio UI core

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-03001` … `STUDIO-03999` and are never reused.

**Purpose.** Build the widget, state, layout, input and styling foundations of an original editor UI, CNA-free and headless-testable so that everything except the pixels is decided in CI.

**Exit criteria.** A panel can be described, laid out, hit-tested, focused, keyboard-navigated and driven to produce draw data, entirely without a GPU.

**Progress:** 31 of 35 complete `██████████░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-03001` | Create the `cna-studio-ui-core` module with no CNA dependency | ✅ | `STUDIO-02001` |
| `STUDIO-03002` | Widget identity: stable ids derived from a scoped id stack | ✅ | `STUDIO-03001` |
| `STUDIO-03003` | Persistent per-widget state store keyed by id | ✅ | `STUDIO-03002` |
| `STUDIO-03004` | Design token model: the single source of visual truth | ✅ | `STUDIO-03001` |
| `STUDIO-03005` | Theme system with a dark professional default | ✅ | `STUDIO-03004` |
| `STUDIO-03006` | Semantic colour roles: normal, hover, pressed, selected, focused, disabled, warning, error, success | ✅ | `STUDIO-03004` |
| `STUDIO-03007` | Focus model: focus ring, focus scopes, focus restoration | ✅ | `STUDIO-03002` |
| `STUDIO-03008` | Tab navigation order | ✅ | `STUDIO-03007` |
| `STUDIO-03009` | Event model and input routing | ✅ | `STUDIO-03002` |
| `STUDIO-03010` | Mouse capture | ✅ | `STUDIO-03009` |
| `STUDIO-03011` | Hit-testing with nested clipping | ✅ | `STUDIO-03009` |
| `STUDIO-03012` | Command routing from the UI to the command registry | ✅ | `STUDIO-03009` |
| `STUDIO-03013` | Accessibility metadata on every widget: role, name, value, state | ⬜ | `STUDIO-03002` |
| `STUDIO-03014` | Headless test renderer capturing draw data and widget geometry | ✅ | `STUDIO-03009` |
| `STUDIO-03015` | Frame lifecycle: build, layout, input, draw, retain | ✅ | `STUDIO-03003` |
| `STUDIO-03020` | Cursor shape requests from widgets | ✅ | `STUDIO-03009` |
| `STUDIO-03021` | Tooltip model with delay, placement and dismissal | ✅ | `STUDIO-03009` |
| `STUDIO-03022` | Popup and modal layering with correct input blocking | ✅ | `STUDIO-03009` |
| `STUDIO-03023` | Drag and drop: sources, targets, payload typing, visual feedback | ✅ | `STUDIO-03010` |
| `STUDIO-03024` | Text selection model for text fields | ✅ | `STUDIO-03007` |
| `STUDIO-03025` | Clipboard integration through the platform seam | ✅ | `STUDIO-03024` |
| `STUDIO-03026` | UTF-8 and Unicode correctness through the whole text path | ✅ | `STUDIO-03024` |
| `STUDIO-03027` | IME support where the platform provides it | ⬜ | `STUDIO-03026` |
| `STUDIO-03028` | High-DPI scale factor threaded through layout and styling | ✅ | `STUDIO-03004` |
| `STUDIO-03029` | Keyboard shortcut matching and chords | ✅ | `STUDIO-03012` |
| `STUDIO-03030` | Restrained animation model: state transitions only, no decorative motion | ⬜ | `STUDIO-03004` |
| `STUDIO-03031` | Widget interaction helpers over `interact()`: button, toggle, checkbox, tab, menu item | ✅ | `STUDIO-03015` |
| `STUDIO-03032` | Text measurement seam: code-point-correct extents, baselines and truncation | ✅ | `STUDIO-03015` |
| `STUDIO-03033` | Scrollable regions: wheel, draggable thumb, and row virtualisation | ✅ | `STUDIO-03031`, `STUDIO-03018` |
| `STUDIO-03034` | Tree view: flattened rows, disclosure, indentation and selection | ✅ | `STUDIO-03033` |
| `STUDIO-03035` | Editable single-line text field over the selection model | ✅ | `STUDIO-03024`, `STUDIO-03025` |
| `STUDIO-03036` | Drop-down selection over a deferred popup | ✅ | `STUDIO-03022`, `STUDIO-03033` |
| `STUDIO-03040` | Modal dialogs: a window that owns the frame until it is answered | ✅ | `STUDIO-03022` |
| `STUDIO-03041` | Make paint order separable from input order, so a widget cannot describe itself under its own background | ⬜ | `STUDIO-03009` |
| `STUDIO-03042` | A widget that needs its own id twice asks once, so the collision detector means something | ✅ | `STUDIO-03002` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-03001` — Create the `cna-studio-ui-core` module with no CNA dependency

**Acceptance.** Links only `cna-studio-core`; the build graph makes a CNA include impossible

### `STUDIO-03002` — Widget identity: stable ids derived from a scoped id stack

**Acceptance.** The same widget keeps its id across frames and across sibling insertion; collisions are detected in debug builds rather than silently merging two widgets

**Verification.** Unit tests over id stability, nesting and collision detection

### `STUDIO-03003` — Persistent per-widget state store keyed by id

**Acceptance.** Scroll offsets, expansion, text-edit cursors survive frames; state for widgets not seen for N frames is reclaimed

### `STUDIO-03004` — Design token model: the single source of visual truth

**Acceptance.** Typography, font hierarchy, spacing scale, control heights, icon sizes, panel padding, separators, borders, radii, semantic colours, background layers and every interactive state are named values, not literals at call sites

**Verification.** A test asserts no widget hard-codes a colour or a pixel size

### `STUDIO-03005` — Theme system with a dark professional default

**Acceptance.** Tokens resolve through a theme; an alternative theme can be supplied without touching widgets

### `STUDIO-03006` — Semantic colour roles: normal, hover, pressed, selected, focused, disabled, warning, error, success

**Acceptance.** Every interactive state has a defined token; no widget invents a shade

### `STUDIO-03007` — Focus model: focus ring, focus scopes, focus restoration

**Acceptance.** Exactly one focused widget; focus survives panel switches; a closed popup returns focus

### `STUDIO-03008` — Tab navigation order

**Acceptance.** Tab and Shift-Tab traverse in a defined order, skipping disabled and invisible widgets

**Verification.** Headless test driving Tab across a synthetic panel

### `STUDIO-03009` — Event model and input routing

**Acceptance.** Mouse, keyboard, wheel and text events route to the right widget through hit-testing and capture

### `STUDIO-03010` — Mouse capture

**Acceptance.** A drag started in a widget keeps receiving events outside it, and releases correctly on button-up even outside the window

### `STUDIO-03011` — Hit-testing with nested clipping

**Acceptance.** A widget clipped by an ancestor is not hit outside the clip; nested scroll areas compose

### `STUDIO-03012` — Command routing from the UI to the command registry

**Acceptance.** A widget raises a command id; it does not contain the action logic

### `STUDIO-03013` — Accessibility metadata on every widget: role, name, value, state

**Acceptance.** Groundwork only — the metadata exists and is tested; platform accessibility APIs are Phase 32

### `STUDIO-03014` — Headless test renderer capturing draw data and widget geometry

**Acceptance.** A test can assert what was drawn, where, in what state and in what order, with no GPU

### `STUDIO-03015` — Frame lifecycle: build, layout, input, draw, retain

**Acceptance.** The phases are explicit and separable so layout can be tested without drawing

**How it was met.** `StudioFrame` sequences the five phases, and the UI is described **twice** per
frame — once to route input, once to draw — so hover, capture and focus are fully resolved before
any pixel is decided. An operation attempted in a phase that refuses it is counted and named rather
than silently tolerated, and a test asserts the count is zero for a well-formed frame and non-zero
for a malformed one. A layout test therefore runs `beginFrame` and `beginLayout`, asserts on
rectangles, and never constructs a draw list at all

**Verification.** `tests/StudioFrameTests.cpp`: phase ordering, refused operations, replay
equivalence between the two passes, identical ids across passes, and one state-store advance per
frame rather than per pass

### `STUDIO-03020` — Cursor shape requests from widgets

**Acceptance.** Resize, text, hand and default shapes requested by widgets and resolved once per frame

**How it was met.** A request is honoured only for the widget holding the mouse or — when nothing
does — the widget under the pointer, and only in the draw pass, when hover is final. "Last caller
wins" would make the shape depend on description order, and would hand the cursor back to the panel
underneath a splitter for the whole of a drag

**Verification.** `tests/StudioFrameTests.cpp`: the hovered widget's request wins over a
neighbour's, the capture holder keeps the cursor when the pointer leaves it, and the shape returns
to an arrow when nobody asks

### `STUDIO-03031` — Widget interaction helpers over `interact()`

**Acceptance.** Button, toggle, checkbox, tab, menu-bar title and menu item, each built on the one
`interact()` the router provides rather than on a parallel mechanism. Activation is reported only
in the input pass, so acting on it runs the action once per gesture however many times the UI is
described

**Verification.** `tests/StudioFrameTests.cpp`: one activation per click across both passes,
press-and-slide-off cancels, a disabled control neither activates nor hovers nor tab-stops but
still blocks the pointer, Space activates the focused control and is suppressed while a field takes
text, and a menu item commits on release so press-drag-release works

### `STUDIO-03032` — Text measurement seam

**Acceptance.** Extents, ascent, descent and line height are answered behind an interface the font
atlas implements later, with a font-free approximation until it exists. Widths count code points,
not bytes, and truncation cuts on code-point boundaries

### `STUDIO-03022` — Popup and modal layering with correct input blocking

**Acceptance.** A modal blocks input beneath it; popups stack and dismiss in order; Escape closes the topmost

**How it was met, in two pieces.** The *stacking* half came with nested submenus (`STUDIO-06017`):
popups are a chain, Escape closes the topmost, a press outside closes the lot, and a row is
highlighted only in the deepest popup holding the pointer — which is what stops two rows lighting up
where a flipped submenu overlaps its parent.

The *blocking* half came with the drop-down (`STUDIO-03036`), and it needed something the shell's
own menus did not: a popup opened inside a panel is not something the shell was told about, so the
frame raises its own blocking layer while one is open rather than being handed the answer by its
caller. Without it a click meant for a list row would also reach the control underneath — in a
property grid, that means editing the wrong property, which is the one failure a screenshot of a
drop-down can never show.

A *modal* window does not exist yet and has nothing to block; the layering it will use is here.

**Verification.** `tests/StudioSubmenuTests.cpp` for the chain, `tests/StudioDropdownTests.cpp`
for the blocking (`AnOpenListBlocksWhatItCovers` clicks a button under an open list, having first
proved that the same click works while it is closed)

### `STUDIO-03023` — Drag and drop: sources, targets, payload typing, visual feedback

**Acceptance.** A typed payload; a target that rejects a wrong type visibly; a cancelled drag that restores state

**The type is the whole point.** A target that swallowed anything would let a user drop a texture
onto a material slot and see nothing happen, which is indistinguishable from a drag that never
worked. A mismatched payload is *refused visibly* rather than ignored.

**A drop target cannot go through `interact()`.** Its capture rule — while a widget holds the mouse,
nothing else is hovered — is what stops a splitter drag being stolen by the panel it passes over,
and it is exactly wrong here: the source holds the mouse for the whole of a drag, and a drag is a
gesture whose purpose is to end somewhere else. Every target was invisible until this was separated
into `pointerOver`, which keeps the clip and layer rules and drops the capture one. Which of two
overlapping targets wins is still settled in the input pass and replayed in the draw pass, because
otherwise both would light up and the one that drew first would be the one that did not receive the
drop.

**The release frame is the drop frame, so the drag cannot be cleared before it.** Cancelling on the
button-up frame's `beginFrame` threw the payload away before the target under the pointer ever saw
the release, and every drop read as a cancellation. It ends at `endFrame` instead.

**And a gesture abandoned while the button is down stays abandoned.** Escape cancelled the drag and
the next frame started it again from the same press — a cancel the user could not make stick. Two
rules fixed it: no new drag while a cancelled press is still held, and no drag *starting* on the
frame the button comes up, because a widget still reports `held` there (that is what lets a click
resolve) and beginning a gesture on the frame it ended is how an abandoned drag resurrects itself.

**The threshold measures from where the press began**, not from the widget's centre. Pressing near
an edge would otherwise start a drag with the pointer having moved nothing at all. The router
records the press point for it, which the shell's dock drag wanted too.

**Rows declare what they carry and what they take.** `StudioTreeRow` gained `dragType`, `dragValue`
and `dropType`, so a tree is a source and a target without the caller keeping a second list in step
with its rows — which is how the Content Browser became draggable and the Problems panel became
droppable in a few lines each.

**Verification.** `tests/StudioDragDropTests.cpp`: the threshold and where it is measured from, a
matching target lighting up before the drop, a wrong type refused rather than ignored, delivery on
release, release over nothing cancelling, Escape abandoning it and *staying* abandoned, one payload
at a time, an untyped payload refused, and the label staying on screen at the far corner. Plus
`DroppingAnAssetOnABrokenRowAsksToRelinkItRatherThanClearIt` in the Problems panel's own tests

### `STUDIO-03025` — Clipboard integration through the platform seam

**Acceptance.** Degrades visibly when the platform has no clipboard (CNA gap G-02) rather than silently doing nothing

### `STUDIO-03026` — UTF-8 and Unicode correctness through the whole text path

**Acceptance.** Grapheme-aware cursor movement and selection; no byte-index bugs on multi-byte text

**Verification.** Tests over combining marks, CJK and emoji

**Done, in both halves.** The *rendering* half: a decoder that always advances — a decoder that can
stand still turns one corrupt byte into a hang — measurement and truncation on code-point
boundaries, and a visible replacement glyph where a face has no outline, because a silent gap reads
as a spacing bug while a box is something a user can report.

**The caret moves by grapheme cluster**, which is what a reader calls a character. It is not a code
point: `é` typed as `e` + U+0301 is two, a flag is two regional indicators, a family emoji is
several joined by U+200D, and Devanagari `कि` is a consonant and a vowel sign. Stepping by code
point puts the caret *inside* one rendered glyph, where there is nothing to draw a caret between,
and Backspace then takes the accent off a letter instead of removing the letter — which does not
read as a Unicode bug, it reads as Backspace having missed.

**The mouse had the same bug and needed the same fix.** A combining mark adds no width, so the
code-point boundary inside `e` + U+0301 sits at the same x as the one before it: clicking there put
the caret inside the glyph *invisibly*, and the damage appeared at the next keystroke. The field's
hit-test now snaps to cluster boundaries, so the mouse cannot reach a place the arrow keys refuse
to stop at. The test sweeps a click across the whole field and asserts that one Backspace always
removes exactly one whole cluster; with the old hit-test it reports `café` becoming `caf́`.

**And truncation cuts between characters too.** An ellipsis placed at a code-point boundary can
land between a letter and its accent — which, unlike cutting a multi-byte character in half, is
valid UTF-8, so nothing complains: it renders as a stray mark sitting on the ellipsis, and the only
person who finds out is the one whose name it happened to.

**What is implemented, and what is not.** The rules of UAX #29 that need no property tables: CRLF,
the Hangul jamo classes, combining marks, `ZWJ` sequences, variation selectors, emoji modifiers and
regional-indicator pairs. Those cover Latin with diacritics, Greek, Cyrillic, Hebrew and Arabic
marks, CJK, Hangul, the Indic matras and every emoji sequence in ordinary use. Not implemented:
`GB9c` (Indic conjunct breaks) and the extended-pictographic property, which need Unicode data
tables — a few hundred kilobytes of generated source and a version to keep current. The failure
that leaves is a caret stepping inside a rare cluster in a field holding a Unicode-report test
case; the failure it removes is a caret stepping inside `é`.

**Regional indicators are counted from the start of their run**, which is why walking backwards
re-walks forwards rather than reading leftwards from the caret. Pairing decided by looking only at
the two code points either side of a boundary joins every indicator to the one before it, so two
flags in a row become one cluster and the caret can never get between them.

**"Covers CJK" means the caret, not the glyph.** The cluster rules step over Chinese, Hangul and
emoji correctly, and the shipped IBM Plex faces have no outlines for any of them, so on screen they
are still a row of replacement boxes. That is `STUDIO-04019`, raised by getting this far: the model
being right and the glyph being absent are different failures, and it was worth putting both on
screen to tell them apart.

**IME composition is still `STUDIO-03027`**, and is a different problem: this is about text that
has already arrived.

### `STUDIO-03027` — IME support where the platform provides it

**Acceptance.** Composition text displayed and committed correctly; absence degrades to plain input

### `STUDIO-03028` — High-DPI scale factor threaded through layout and styling

**Acceptance.** 100/125/150/175/200% produce correctly proportioned layout with no fractional-pixel seams

**How it was met.** Scaling is applied in one place — on reading a metric — so no widget can forget
to apply it and none can apply it twice, and the authored logical value never changes. Thicknesses
that must stay visible are clamped to at least one physical pixel, because a hairline that rounds to
zero is a hairline that disappears. Text is rasterised at the size it is drawn at rather than scaled
from a master size.

The seams took the most care. A dock fraction of an arbitrary area is almost never an integer, so
panel edges land on fractional pixels and leave a partially covered column between neighbours —
faint at 100%, and at 150% landing differently on every splitter in the window. The **split** is
snapped to a whole pixel rather than each panel afterwards: rounding two neighbours independently
can leave a one-pixel gap of app background between them, or an overlap. Menu titles, toolbar
entries, tab widths and popup geometry are snapped for the same reason

**Verification.** `tests/StudioHighDpiTests.cpp` at 100/125/150/175/200%: every hairline stays at
least one pixel (checked down to 50% and up to 300%), chrome and controls scale, the minimum hit
target scales, every dock edge lands on a whole pixel, panels stay exactly adjacent across every
splitter, no panel collapses, menu geometry stays whole and on screen, each scale rasterises its own
glyphs, the frame commits no phase violations and stays under the draw-call bound — and clicking the
centre of a menu title opens it at every scale, which is the one that catches a UI that scaled its
drawing and not its hit testing. Eight golden-image resolutions and four `--shell-preview` CTest
cases cover the same set through the real binary

### `STUDIO-03030` — Restrained animation model: state transitions only, no decorative motion

**Acceptance.** Durations are tokens; animation can be disabled wholesale; nothing animates that a professional tool would not

### `STUDIO-03033` — Scrollable regions: wheel, draggable thumb, and row virtualisation

**Acceptance.** A region takes an area and a content size and gives back a viewport, a scroll
position and a clip. The wheel scrolls it while the pointer is over it; the thumb drags; both stop
at the ends rather than running past them. The bar appears only when the content does not fit — a
gutter reserved for a scrollbar that is not there is a column of wasted space on every panel that
does. The thumb's length is its share of the content but never smaller than a person can grab: all
the way proportional means a million-line log gets a one-pixel thumb, which is a scrollbar in name
only

**Virtualisation is part of the widget, not left to callers.** `visibleRows` answers "which rows are
worth describing", because the clip stops the *pixels* and only this stops the work of measuring and
laying out text that was never going to be seen. A hundred-thousand-line log has to cost what a
ten-line one costs, or the console stalls the editor exactly when somebody is reading it

**Following new content stops the moment the reader scrolls away.** Measured against how far the
content reached *last* frame, not this one: against the grown content the view is never already at
the end — that is why it grew — so comparing with the new extent would mean following never once
engaged. The opposite mistake, yanking the view back down while somebody is reading further up, is
the single most common complaint about log windows

**Verification.** `tests/StudioLogPanelTests.cpp` — the bar appearing only on overflow, the wheel
stopping at both ends, following engaging and then yielding to the reader, and `visibleRows` culling
a hundred thousand rows to a screenful and coming back empty past the end

### `STUDIO-03034` — Tree view: flattened rows, disclosure, indentation and selection

**Acceptance.** A scrolling list of rows carrying a depth, a disclosure triangle where a row has
children, hover and selection, and virtualisation. Clicking a triangle opens the row; clicking the
row selects it; those are different intentions and the widget keeps them apart — a tree that
conflated them would make it impossible to look inside a group without also selecting it

**A view over rows, not a walker over a data structure.** The outliner shows a scene graph and the
content browser will show a directory; a widget that knew about either would have to learn about
both. It takes a flat list with a depth per row, which is what a tree looks like once it has been
drawn, and the caller flattens

**Expansion is the caller's.** Not ceremony: the caller has to consult it anyway to decide which
rows to flatten, and a widget that owned it would mean asking the widget a question before it has
been called. Held as the set of *collapsed* ids, so the default is open and an unknown id needs no
entry — a tree that started collapsed would show one line and make the user work to discover that
their scene has anything in it

**Verification.** `tests/StudioOutlinerPanelTests.cpp` — hierarchy and depth, collapsing hiding only
its own children, the open default, selection marking, a click reaching the selection, and a
two-thousand-deep chain that neither exhausts the stack nor draws more than a screenful

### `STUDIO-03024` — Text selection model for text fields

**Acceptance.** A caret and a selection over a UTF-8 string, moving by whole code points, with the
selection running between an *anchor* and the caret in either direction — because dragging left from
the middle of a word selects leftwards, and Shift+Right then shrinks that selection from its left
edge, which a model storing only a begin and an end cannot express

**Separate from the widget, and tested without one.** Nearly everything that goes wrong with a text
field goes wrong here: a caret landing inside a multi-byte character, Backspace eating one byte of
three and leaving a string that is no longer UTF-8, an arrow with a selection moving past its edge
instead of to it. None of that needs a frame, a font or a window to reproduce, and none of it shows
in a screenshot until the damage is done

**What it deliberately is not.** Grapheme clusters. A flag emoji is one thing a reader sees and
several code points, so moving by code point steps inside some characters. That needs a grapheme
breaker and a table this repository does not have; code points are a real improvement on bytes, and
stopping here is a deliberate half-step recorded as `STUDIO-03026` rather than an oversight

**Verification.** `tests/StudioTextEditTests.cpp`, over an accented Latin character and a
four-byte emoji

### `STUDIO-03025` — Clipboard integration through the platform seam

**Acceptance.** The frame carries a clipboard the widgets use, and a host installs the platform's
into it. Until one is installed the frame keeps its own string, so a text field can be cut and
pasted in a headless test and in a build whose CNA has the Devices module switched off (CNA gap
G-02). That fallback is real but local, and the distinction is visible: `hasPlatformClipboard()`
says which one is in use

**Verification.** `TheClipboardWorksWithoutAPlatformAndDefersToOneWhenThereIs`

### `STUDIO-03035` — Editable single-line text field over the selection model

**Acceptance.** Click to place the caret, drag to select, Shift with the arrows and Home/End to
extend, Ctrl+A, Ctrl+C/X/V, Escape to abandon and Enter to commit. Committed on Enter or on losing
focus — never per keystroke, because a property bound to a field that wrote per character would put
one undo entry per letter and would parse a number while it is half-typed

**Two things it gets right that are easy to get wrong.** Focus arriving by *click* places the caret;
focus arriving by *keyboard* honours select-all-on-focus. The router grants focus on the frame after
a press, so a session that waited for focus would begin on a frame where nothing says the pointer was
involved — and a single click into a name field would then wipe it on the next keystroke. The press
starts the session instead. And losing focus with an uncommitted edit *commits* it: abandoning
somebody's typing because they clicked elsewhere is the behaviour every form gets wrong and nobody
forgives

**On the caret not blinking.** Studio has no animation model yet (`STUDIO-03030`), and a caret that
blinks off is a caret a golden image catches half the time — which would make every text screenshot
in the suite nondeterministic

**Verification.** `tests/StudioTextEditTests.cpp` — commit on Enter and not before, Escape leaving
the value alone, the router being told a key means text, and a click placing the caret where it
landed rather than at an end

### `STUDIO-03021` — Tooltip model with delay, placement and dismissal

**Acceptance.** A tooltip appears only after the pointer has *rested* on one control, names that
control and its shortcut, stays inside the window, and never covers what is being dragged

**It became necessary rather than nice when the toolbar went icon-only** (`STUDIO-04008`). An icon
toolbar with no tooltips is less discoverable than the row of words it replaced, so the tooltip is
part of that change rather than a later polish pass. The text is the label, the shortcut and the
action's description — assembled from the action registry, so a command whose shortcut is rebound
says so without anybody remembering to update a string.

**The delay is the whole feature, and it is a per-widget clock, not a per-pointer one.** Without a
delay, sweeping the pointer across a toolbar flashes six tooltips on the way to the seventh, and the
flicker is what the eye follows, so the one the user wanted is the one they do not read. The clock
therefore belongs to a *widget*: it resets whenever the hovered widget changes, including to
nothing, so coming back to a control waits again rather than snapping the tooltip open.

**Advanced at end of frame, and that is not an implementation detail.** Hover is decided during the
input pass, so asking which widget is hovered at the *start* of a frame answers with the previous
frame's. The first implementation did exactly that, and the off-by-one frame ate the delay: on the
frame the pointer crossed from one button to the next, the change was invisible, the clock never
reset, and the new button's tooltip appeared instantly. The delay worked only for the first control
the pointer ever touched — which no screenshot would have shown. `MovingToAnotherControlStartsTheWaitAgain`
is the test that caught it.

**Placement.** Below the control by default, flipped above when there is no room, and clamped
horizontally, so a tooltip on the last toolbar button stays on screen. Anchored to the widget's
rectangle rather than to the pointer, which is what keeps it off the thing it describes. Drawn in
its own layer above menus, and refused outright while anything is being dragged.

**It found a screenshot test that had been photographing nothing.** `--shell-pointer=40,40` is the
*gap* between two toolbar buttons at scale 1, so `CnaStudioShellPreviewPressedToolbar` had been
capturing a toolbar at rest and passing every run. Both that test and the new tooltip capture now
point at (20, 40), and `TheShellsFirstToolbarButtonIsUnderThePointTheScreenshotTestsUse` pins the
number so moving the toolbar fails loudly rather than quietly emptying those captures.

**Verification.** `tests/StudioTooltipTests.cpp` — the wait, the reset on moving to another control,
the reset on leaving and returning, one tooltip at a time with the anchor on the control, nothing
during a drag, the shell's toolbar carrying its shortcut, and staying inside the window. Plus
`CnaStudioShellPreviewTooltip`, which captures a real one through the rasterizer and fails the run
if none appeared, and `CnaStudioRejectsATooltipThatNeverAppears`, so resting somewhere that offers
none is an error rather than a picture of the shell at rest passing for a picture of a tooltip

### `STUDIO-03036` — Drop-down selection over a deferred popup

**Acceptance.** Click or Down to open, arrows to move, Enter to choose, Escape or a press elsewhere
to dismiss; the list escapes the panel it sits in, scrolls when it is long, and blocks what it
covers

**The hard part is not the list; it is where the list is allowed to be.** A widget cannot draw a
popup where it stands: it would be clipped by whatever panel it is in — a list clipped to a property
row shows one option — and painted under whatever is described after it. So the frame gained
*deferred popups*: a body handed to the frame and run at the end of both passes, against the
window's own clip and in a raised input layer. That facility is the reusable part; the drop-down is
its first user.

**A deferred body cannot answer the widget that queued it.** It runs after that call returned, so a
reference into the caller's stack frame would dangle — the kind of capture that works in every test
and fails on the one panel that builds its items inline. The answer goes through the control's own
retained state instead and is collected on the next pass that routes input: one frame of latency,
and the list closes on the same frame the user clicked.

**Two bugs the tests were written to catch, both found.** The pending-choice sentinel was `-1`,
and retained state holds *zero* the first time a widget is seen — so every drop-down selected its
first item the moment it was described. And the control consumed Enter as a keyboard activation
while its list was open, so Enter closed the list instead of choosing from it; while a list is open
the keyboard belongs to the list. A third was caught by a test that proved *itself* wrong first:
the keystroke that opens a list must not also move within it, or Down-then-Enter — the fastest way
to accept what is already selected — lands on the item after it.

**Verification.** `tests/StudioDropdownTests.cpp`: opening and closing, choosing, the change
reported exactly once (a control that reported it per frame would put one entry per frame in an
undo stack), dismissal by press and by Escape, flipping upwards near the bottom edge, an empty list
being disabled rather than opening on nothing, keyboard open/move/choose, the opening keystroke not
also moving, a two-hundred-item list bounded by its row limit, and no phase violations across
repeated frames

### `STUDIO-03040` — Modal dialogs: a window that owns the frame until it is answered

**Acceptance.** A dialog blocks the window beneath it, traps the keyboard, answers on Escape and on
Enter, and reports what the user chose — with the caller keeping the state, as everywhere else here.

**Everything that matters about it is what it prevents**, and none of it shows in a screenshot: a
click behind it reaching a button, Tab walking out into the panels it covers, Escape closing
something underneath instead of answering it. So the cases are those, each checked against the same
action plainly working while the dialog is closed — otherwise "the button did nothing" passes for
the wrong reason.

**The focus trap belongs to the router, not to the dialog.** `registerFocusable` now refuses a
widget whose layer is blocked, so while a modal is open its own controls *are* the whole Tab ring
and the traversal cannot leave. A dialog that kept its own focus index would have been a second
answer to a question the router already answers — and would have left the same bug open for menus
and popups, which had it too.

**Enter is taken only when no button claimed it.** A focused button activates on Enter itself, and
overwriting that with the default would make Enter on a focused Cancel mean Discard — the one
keystroke a confirmation dialog must never get wrong. A text field's `committed` is likewise *not*
what answers the dialog: a field reports it only when the value actually changed, which is right for
a property grid and wrong for a name prompt where Enter on an unedited name still means "that one".

**The scrim is not decoration.** A dialog over an undimmed workspace looks like a panel that happens
to be on top, and a user who does not know they are blocked reads the unresponsive editor as a hang.

**One shared layer axis.** The router's layers are a modal stack rather than a z-order —
`layerAcceptsInput()` is an equality test — and two vocabularies share it, the frame's popups and
modals and the shell's floats, menus and tooltips. The whole ordering is now written down once, at
`StudioFrame::kPopupLayer`. Getting it wrong was not hypothetical: floats and deferred popups shared
layer 1 for a commit, which let a drop-down opened inside a floating window be clicked *through* to
the panel holding it.

**Verification.** `tests/StudioDialogTests.cpp`: centred and inside even a 320x200 window, the modal
layer raised and lowered, a click behind reaching nothing, a button answering, Escape answering,
a dialog that refuses Escape, Tab unable to leave, a field taking the keyboard on open, Enter
carrying the text out, an empty required field refusing the affirmative button while Cancel still
works, a menu closing when a dialog opens, and About being a real dialog whose text the host sets

### `STUDIO-03041` — Make paint order separable from input order

**Acceptance.** A widget can be *described* before a surface and *drawn* after it without its author
splitting the code by hand, and the shape that used to be a trap is either impossible or fails a
test naming the widget.

**Not a preference. It has happened three times, and twice in one session.** A tree row is one
widget covering the whole line, so anything that has to win the click against the row must be
described *before* the row — and is therefore drawn before it, and painted over by it.

| | How it presented |
|---|---|
| The disclosure triangle | Expandable rows lost their triangle **on alternate lines only**, because the alternating fill is what covered it. Read as a data problem |
| The outliner's visibility toggle (`STUDIO-35060`) | As **nothing at all**. The click toggled, the tooltip appeared, two unit tests passed, and the eye was never once on screen |
| The Details panel's asset inspector (`STUDIO-07045`) | Written correctly, and covered only because the earlier two had made it a thing to check |

The second is the one that matters: a feature that works and cannot be seen is worse than a missing
one, because nothing reports it. It was found by reading a 1920×1080 capture while writing a handoff.

**The fix so far is a rule and two guards, which is one guard per *instance*.** `STUDIO-35063` asserts
the outliner's toggle ordering on emitted geometry, and `STUDIO-07045` added a rasterising one for
the asset inspector. Both are real and both are retrospective: they defend the two places somebody
already got wrong, and the next widget on a row starts from the same trap.

**What this task is for**, stated as a question rather than a design: the draw list is emitted in
call order and that is also description order, so the two are the same thing. Separating them is
probably a deferred-draw facility — a widget asks for a paint that happens at the end of the
enclosing row, the way a deferred popup escapes the panel it was opened in (`STUDIO-03033`) — and
that facility already exists for popups, which is the argument that this is a small change rather
than a renderer rewrite.

**Deliberately not attempted yet.** The obvious alternatives are worse: reordering the row widget so
the background is described first breaks input precedence, and asking every author to split their
widget by hand is what is happening now. This wants design rather than an edit, which is why it is
a task rather than a commit.

**A test that generalises is part of the acceptance.** Both existing guards name one widget. What is
missing is the one that fails for *any* interactive element drawn under an opaque fill that covers
it, without having to know which widgets exist — probably by rasterising a frame with every
interactive rectangle known to the frame, and requiring each hovered one to change some pixel.

### `STUDIO-03042` — A widget that needs its own id twice asks once

**Found by `STUDIO-09016`**, and pre-existing rather than caused by it. The hundred-thousand-asset
case was the first test to ask the Content Browser whether any widget id had been issued twice, and
the answer was one per draggable row and one per draggable card, every frame.

**Not a real collision, and that is the problem.** `WidgetIdStack::make` derives the id from the
scope and the key, so asking twice returns the same value — a row that called `make("row")` for its
`interact` and again where `studioDragSource` needed the same id was getting the right id both
times. But `make` also *records* the id, because issuing one twice is how two different widgets end
up sharing an identity, which presents as one control responding to a press somewhere else. So the
panel sat permanently above zero, and a genuine collision introduced next to it would have been
invisible in the noise. A detector nobody can assert against is a detector that is not running.

Fixed where it occurred — the tree's rows, the grid's cards, and the floating window's title bar and
resize grip, all of which fetched an id a second time rather than keeping the one they had — rather
than by adding a non-recording `peek`. A widget asking for its own id twice has the id already; the
call that would need `peek` is a caller asking about a widget it has not declared yet, and there
isn't one. (`STUDIO-09016` wanted exactly that and got a different shape instead, for reasons that
have nothing to do with identity: see its entry.)

**Asserted where it was missing.** `NoWidgetInTheBrowserIsGivenTheSameIdentityTwice` in
`tests/StudioContentBrowserTests.cpp` runs both views with the folder pane open, pointer away and
pointer over the listing, because a hover describes widgets an unpointed frame never does. The
hundred-thousand-asset cases assert it too, which is where it was found.
