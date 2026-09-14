# Phase 3 — Studio UI core

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-03001` … `STUDIO-03999` and are never reused.

**Purpose.** Build the widget, state, layout, input and styling foundations of an original editor UI, CNA-free and headless-testable so that everything except the pixels is decided in CI.

**Exit criteria.** A panel can be described, laid out, hit-tested, focused, keyboard-navigated and driven to produce draw data, entirely without a GPU.

**Progress:** 19 of 28 complete `████████░░░░`

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
| `STUDIO-03021` | Tooltip model with delay, placement and dismissal | ⬜ | `STUDIO-03009` |
| `STUDIO-03022` | Popup and modal layering with correct input blocking | 🔄 | `STUDIO-03009` |
| `STUDIO-03023` | Drag and drop: sources, targets, payload typing, visual feedback | ⬜ | `STUDIO-03010` |
| `STUDIO-03024` | Text selection model for text fields | ⬜ | `STUDIO-03007` |
| `STUDIO-03025` | Clipboard integration through the platform seam | ⬜ | `STUDIO-03024` |
| `STUDIO-03026` | UTF-8 and Unicode correctness through the whole text path | 🔄 | `STUDIO-03024` |
| `STUDIO-03027` | IME support where the platform provides it | ⬜ | `STUDIO-03026` |
| `STUDIO-03028` | High-DPI scale factor threaded through layout and styling | ✅ | `STUDIO-03004` |
| `STUDIO-03029` | Keyboard shortcut matching and chords | ✅ | `STUDIO-03012` |
| `STUDIO-03030` | Restrained animation model: state transitions only, no decorative motion | ⬜ | `STUDIO-03004` |
| `STUDIO-03031` | Widget interaction helpers over `interact()`: button, toggle, checkbox, tab, menu item | ✅ | `STUDIO-03015` |
| `STUDIO-03032` | Text measurement seam: code-point-correct extents, baselines and truncation | ✅ | `STUDIO-03015` |

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

### `STUDIO-03023` — Drag and drop: sources, targets, payload typing, visual feedback

**Acceptance.** A typed payload; a target that rejects a wrong type visibly; a cancelled drag that restores state

### `STUDIO-03025` — Clipboard integration through the platform seam

**Acceptance.** Degrades visibly when the platform has no clipboard (CNA gap G-02) rather than silently doing nothing

### `STUDIO-03026` — UTF-8 and Unicode correctness through the whole text path

**Acceptance.** Grapheme-aware cursor movement and selection; no byte-index bugs on multi-byte text

**Verification.** Tests over combining marks, CJK and emoji

**In progress.** The *rendering* half is done: a decoder that always advances — a decoder that can
stand still turns one corrupt byte into a hang — measurement and truncation on code-point
boundaries, and a visible replacement glyph where a face has no outline, because a silent gap reads
as a spacing bug while a box is something a user can report. Cursor movement and selection wait on
the text fields of `STUDIO-03024`, and grapheme clustering — where a combining mark or an emoji
sequence is one thing to a reader and several code points to a decoder — waits with them

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

