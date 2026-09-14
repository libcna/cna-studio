# Phase 3 — Studio UI core

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-03001` … `STUDIO-03999` and are never reused.

**Purpose.** Build the widget, state, layout, input and styling foundations of an original editor UI, CNA-free and headless-testable so that everything except the pixels is decided in CI.

**Exit criteria.** A panel can be described, laid out, hit-tested, focused, keyboard-navigated and driven to produce draw data, entirely without a GPU.

**Progress:** 0 of 26 complete `░░░░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-03001` | Create the `cna-studio-ui-core` module with no CNA dependency | ⬜ | `STUDIO-02001` |
| `STUDIO-03002` | Widget identity: stable ids derived from a scoped id stack | ⬜ | `STUDIO-03001` |
| `STUDIO-03003` | Persistent per-widget state store keyed by id | ⬜ | `STUDIO-03002` |
| `STUDIO-03004` | Design token model: the single source of visual truth | ⬜ | `STUDIO-03001` |
| `STUDIO-03005` | Theme system with a dark professional default | ⬜ | `STUDIO-03004` |
| `STUDIO-03006` | Semantic colour roles: normal, hover, pressed, selected, focused, disabled, warning, error, success | ⬜ | `STUDIO-03004` |
| `STUDIO-03007` | Focus model: focus ring, focus scopes, focus restoration | ⬜ | `STUDIO-03002` |
| `STUDIO-03008` | Tab navigation order | ⬜ | `STUDIO-03007` |
| `STUDIO-03009` | Event model and input routing | ⬜ | `STUDIO-03002` |
| `STUDIO-03010` | Mouse capture | ⬜ | `STUDIO-03009` |
| `STUDIO-03011` | Hit-testing with nested clipping | ⬜ | `STUDIO-03009` |
| `STUDIO-03012` | Command routing from the UI to the command registry | ⬜ | `STUDIO-03009` |
| `STUDIO-03013` | Accessibility metadata on every widget: role, name, value, state | ⬜ | `STUDIO-03002` |
| `STUDIO-03014` | Headless test renderer capturing draw data and widget geometry | ⬜ | `STUDIO-03009` |
| `STUDIO-03015` | Frame lifecycle: build, layout, input, draw, retain | ⬜ | `STUDIO-03003` |
| `STUDIO-03020` | Cursor shape requests from widgets | ⬜ | `STUDIO-03009` |
| `STUDIO-03021` | Tooltip model with delay, placement and dismissal | ⬜ | `STUDIO-03009` |
| `STUDIO-03022` | Popup and modal layering with correct input blocking | ⬜ | `STUDIO-03009` |
| `STUDIO-03023` | Drag and drop: sources, targets, payload typing, visual feedback | ⬜ | `STUDIO-03010` |
| `STUDIO-03024` | Text selection model for text fields | ⬜ | `STUDIO-03007` |
| `STUDIO-03025` | Clipboard integration through the platform seam | ⬜ | `STUDIO-03024` |
| `STUDIO-03026` | UTF-8 and Unicode correctness through the whole text path | ⬜ | `STUDIO-03024` |
| `STUDIO-03027` | IME support where the platform provides it | ⬜ | `STUDIO-03026` |
| `STUDIO-03028` | High-DPI scale factor threaded through layout and styling | ⬜ | `STUDIO-03004` |
| `STUDIO-03029` | Keyboard shortcut matching and chords | ⬜ | `STUDIO-03012` |
| `STUDIO-03030` | Restrained animation model: state transitions only, no decorative motion | ⬜ | `STUDIO-03004` |

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

### `STUDIO-03020` — Cursor shape requests from widgets

**Acceptance.** Resize, text, hand and default shapes requested by widgets and resolved once per frame

### `STUDIO-03022` — Popup and modal layering with correct input blocking

**Acceptance.** A modal blocks input beneath it; popups stack and dismiss in order; Escape closes the topmost

### `STUDIO-03023` — Drag and drop: sources, targets, payload typing, visual feedback

**Acceptance.** A typed payload; a target that rejects a wrong type visibly; a cancelled drag that restores state

### `STUDIO-03025` — Clipboard integration through the platform seam

**Acceptance.** Degrades visibly when the platform has no clipboard (CNA gap G-02) rather than silently doing nothing

### `STUDIO-03026` — UTF-8 and Unicode correctness through the whole text path

**Acceptance.** Grapheme-aware cursor movement and selection; no byte-index bugs on multi-byte text

**Verification.** Tests over combining marks, CJK and emoji

### `STUDIO-03027` — IME support where the platform provides it

**Acceptance.** Composition text displayed and committed correctly; absence degrades to plain input

### `STUDIO-03028` — High-DPI scale factor threaded through layout and styling

**Acceptance.** 100/125/150/175/200% produce correctly proportioned layout with no fractional-pixel seams

**Verification.** Headless layout tests at each scale

### `STUDIO-03030` — Restrained animation model: state transitions only, no decorative motion

**Acceptance.** Durations are tokens; animation can be disabled wholesale; nothing animates that a professional tool would not

