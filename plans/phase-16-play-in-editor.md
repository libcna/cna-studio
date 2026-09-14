# Phase 16 — Play In Editor 2

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-16001` … `STUDIO-16999` and are never reused.

**Purpose.** Expand the separate-player architecture into a complete play-test workflow.

**Exit criteria.** Play, pause, step, stop, restart, live edits and crash isolation all work against a real game process.

**Progress:** 0 of 14 complete `░░░░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-16001` | Play, Pause, Step, Stop, Restart over the bridge | ⬜ | `STUDIO-06002` |
| `STUDIO-16002` | Game logs routed into the Console with source attribution | ⬜ | `STUDIO-07005` |
| `STUDIO-16003` | Crash reporting when the player dies, without taking Studio with it | ⬜ | `STUDIO-16001` |
| `STUDIO-16004` | Live asset reload into the running player | ⬜ | `STUDIO-16001` |
| `STUDIO-16005` | Live property edits into the running player | ⬜ | `STUDIO-15011` |
| `STUDIO-16006` | Scene reload | ⬜ | `STUDIO-16004` |
| `STUDIO-16007` | Selected-entity synchronisation where feasible | ⬜ | `STUDIO-16005` |
| `STUDIO-16008` | Simulation mode | ⬜ | `STUDIO-16001` |
| `STUDIO-16009` | Possession and eject workflow | ⬜ | `STUDIO-16008` |
| `STUDIO-16010` | Screenshot and capture from the player | ⬜ | `STUDIO-16001` |
| `STUDIO-16011` | Renderer preview selection among the installed player builds | ⬜ | `STUDIO-02041` |
| `STUDIO-16020` | Investigate displaying player output inside a Studio viewport | 🔬 | `STUDIO-16010` |
| `STUDIO-16021` | Decide the native code reload strategy | 🔬 | `STUDIO-15008` |
| `STUDIO-16022` | Implement the chosen reload strategy | ⬜ | `STUDIO-16021` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-16001` — Play, Pause, Step, Stop, Restart over the bridge

**Acceptance.** Carried forward from the prototype and retested through the Studio UI

### `STUDIO-16020` — Investigate displaying player output inside a Studio viewport

**Acceptance.** An efficient, process-safe mechanism, or a recorded decision not to. The separate-process architecture is not compromised to get an embedded image quickly

### `STUDIO-16021` — Decide the native code reload strategy

**Acceptance.** A recorded decision among restart-after-build, module reload in the player, and process replacement with state preservation. Reliability outweighs speed: the first shipped answer may simply be save, incremental compile, restart player, restore scene and camera context

