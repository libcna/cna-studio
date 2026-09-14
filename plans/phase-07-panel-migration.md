# Phase 7 — Existing-panel migration

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-07001` … `STUDIO-07999` and are never reused.

**Purpose.** Port every prototype panel onto the Studio UI and retire the Dear ImGui presentation.

**Exit criteria.** Feature, input, docking and visual parity, proven panel by panel against the Phase 0 inventory — then ImGui is removed deliberately.

**Progress:** 3 of 23 complete `█░░░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-07001` | Compatibility adapter so unported panels keep working during the migration | 🔄 | `STUDIO-06015` |
| `STUDIO-07002` | Port the main menu bar | ⬜ | `STUDIO-06003` |
| `STUDIO-07003` | Port the toolbar | ⬜ | `STUDIO-06006` |
| `STUDIO-07004` | Port the status bar | ⬜ | `STUDIO-06007` |
| `STUDIO-07005` | Port the Console / Output Log | ✅ | `STUDIO-07001` |
| `STUDIO-07006` | Port the Hierarchy panel | ⬜ | `STUDIO-07001` |
| `STUDIO-07007` | Port the Inspector panel | ⬜ | `STUDIO-07001` |
| `STUDIO-07008` | Port the Content Browser | ⬜ | `STUDIO-07001` |
| `STUDIO-07009` | Port the viewport container | ⬜ | `STUDIO-04012` |
| `STUDIO-07010` | Port the Build panel | ⬜ | `STUDIO-07001` |
| `STUDIO-07011` | Port the Diagnostics panel | ⬜ | `STUDIO-07001` |
| `STUDIO-07012` | Port the Validation panel | ⬜ | `STUDIO-07001` |
| `STUDIO-07013` | Port the History panel | ⬜ | `STUDIO-07001` |
| `STUDIO-07014` | Port the Comparison panel | ⬜ | `STUDIO-07001` |
| `STUDIO-07015` | One log model, read by both consoles | ✅ | — |
| `STUDIO-07016` | Panel content seam: the shell hosts a ported panel's content | ✅ | `STUDIO-06018` |
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

**What holds today.** The seam exists and one panel has gone through it: `StudioShell::setPanelContent`
hosts a ported panel's content, an unported panel is the empty surface it always was, and both
consoles read one `StudioLog`. What does not hold yet is the words *one running Studio*: the two
presentations are still two entry points, `--ui=imgui` and `--ui=studio`, rather than one process
showing ported and unported panels side by side. That is the remaining half, and it is what
`STUDIO-06015` is waiting on

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

