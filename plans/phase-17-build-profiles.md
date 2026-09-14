# Phase 17 — Build profiles

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-17001` … `STUDIO-17999` and are never reused.

**Purpose.** Model the target matrix properly: platform, architecture, renderer, configuration and feature profile.

**Exit criteria.** A user picks a named profile, and Studio offers only combinations that can actually be built.

**Progress:** 0 of 11 complete `░░░░░░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-17001` | Target profile data model | ⬜ | `STUDIO-02040` |
| `STUDIO-17002` | Profile editing UI | ⬜ | `STUDIO-17001` |
| `STUDIO-17003` | Target OS and platform implementation selection | ⬜ | `STUDIO-17001` |
| `STUDIO-17004` | CPU architecture selection | ⬜ | `STUDIO-17001` |
| `STUDIO-17005` | Renderer selection, validated by capability rather than by name | ⬜ | `STUDIO-17001`, `STUDIO-02030` |
| `STUDIO-17006` | Build configuration: Debug, Development, Release, Shipping | ⬜ | `STUDIO-17001` |
| `STUDIO-17007` | Feature profile: what the game requires of a renderer | ⬜ | `STUDIO-17005` |
| `STUDIO-17008` | The GUI offers only meaningful combinations | ⬜ | `STUDIO-17007` |
| `STUDIO-17009` | Studio drives the project's own CMake, showing the exact commands | ⬜ | `STUDIO-17001` |
| `STUDIO-17010` | Build output parsed for navigation, with the original log always retained | ⬜ | `STUDIO-17009` |
| `STUDIO-17011` | Clean and incremental build | ⬜ | `STUDIO-17009` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-17006` — Build configuration: Debug, Development, Release, Shipping

**Acceptance.** Mapped onto real CMake and toolchain behaviour; Studio does not invent semantics CNA lacks

### `STUDIO-17007` — Feature profile: what the game requires of a renderer

**Acceptance.** A project needing modern CNAEXT rendering says so, and incompatible targets are reported before the build starts rather than after it fails

### `STUDIO-17009` — Studio drives the project's own CMake, showing the exact commands

**Acceptance.** The commands are visible and runnable by hand; the generated CMake stays editable

### `STUDIO-17010` — Build output parsed for navigation, with the original log always retained

**Acceptance.** Compiler errors are never hidden behind "Build failed"

