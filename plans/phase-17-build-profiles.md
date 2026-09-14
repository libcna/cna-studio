# Phase 17 — Build profiles

> Part of the [CNA Studio master plan](../plan.md). Ids in this phase are `STUDIO-17001` … `STUDIO-17999` and are never reused.

**Purpose.** Model the target matrix properly: platform, architecture, renderer, configuration and feature profile.

**Exit criteria.** A user picks a named profile, and Studio offers only combinations that can actually be built.

**Progress:** 5 of 12 complete `█████░░░░░░░`

| Id | Task | Status | Depends on |
|----|------|:------:|------------|
| `STUDIO-17001` | Target profile data model | ✅ | `STUDIO-02040` |
| `STUDIO-17002` | Profile editing UI | ⬜ | `STUDIO-17001` |
| `STUDIO-17003` | Target OS and platform implementation selection | ✅ | `STUDIO-17001` |
| `STUDIO-17004` | CPU architecture selection | ✅ | `STUDIO-17001` |
| `STUDIO-17005` | Renderer selection, validated against what CNA can build | ✅ | `STUDIO-17001`, `STUDIO-02030` |
| `STUDIO-17006` | Build configuration: Debug, Release, RelWithDebInfo, MinSizeRel | ✅ | `STUDIO-17001` |
| `STUDIO-17007` | Feature profile: what the game requires of a renderer | ⬜ | `STUDIO-17005` |
| `STUDIO-17008` | The GUI offers only meaningful combinations | ⬜ | `STUDIO-17007` |
| `STUDIO-17009` | Studio drives the project's own CMake, showing the exact commands | ⬜ | `STUDIO-17001` |
| `STUDIO-17010` | Build output parsed for navigation, with the original log always retained | ⬜ | `STUDIO-17009` |
| `STUDIO-17011` | Clean and incremental build | ⬜ | `STUDIO-17009` |
| `STUDIO-17012` | Features that are tri-state in CNA are tri-state in the profile | ⬜ | `STUDIO-17001` |

## Acceptance and verification

Tasks whose completion condition is not obvious from the title.

### `STUDIO-17001` — Target profile data model

**Delivered by `STUDIO-02040`.** `CNA/Studio/Project/TargetProfile.hpp`: six axes as a value, with a
project carrying as many profiles as it ships on and none of them privileged

### `STUDIO-17003` — Target OS and platform implementation selection

**Acceptance.** Operating system and CNA platform implementation are separate choices, and a
platform CNA reserves but has not implemented is refused rather than quietly replaced by the default

**Also fixed here, and it was a real defect.** Studio's build runner passed
`-DCNA_GRAPHICS_BACKEND` when configuring a *user's game*. Current CNA does not define that variable
at all, so every game Studio configured silently took CNA's default renderer instead of the one the
user chose — and the build **succeeded**, which is exactly why it survived. It now passes
`CNA_GRAPHICS_RENDERER` and `CNA_PLATFORM`, and a test asserts the old name appears nowhere in the
configure command

### `STUDIO-17005` — Renderer selection, validated against what CNA can build

**Acceptance.** A profile naming a renderer CNA cannot build for its operating system is refused
before the build, with CNA's own reason. Note the scope: this is *buildability*, not capability —
whether the chosen renderer can do what the game needs is `STUDIO-17007`, and whether it can host
Studio is a different question again that must never be asked here

### `STUDIO-17006` — Build configuration

**Acceptance.** CMake's four: Debug, Release, RelWithDebInfo, MinSizeRel. The task originally said
"Debug, Development, Release, Shipping", which is another engine's vocabulary; Studio drives the
project's own CMake, so it uses CMake's

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

### `STUDIO-17012` — Features that are tri-state in CNA are tri-state in the profile

**Acceptance.** A profile can say *off*, *use it if the machine has it*, or *require it* for any CNA
option that has three states, and the Build panel offers all three. Today the model is a boolean per
feature, with a per-feature spelling of "on" as the escape hatch

**Why it is not just tidiness.** `CNA_ENABLE_VIDEO` takes `OFF`, `AUTO` or `ON`, and `ON` *requires*
FFmpeg: it fails the configure on a machine without it. Studio's boolean mapped "on" to `ON`, which
made every exported project require FFmpeg to build — found by `STUDIO-02051` building an exported
game rather than reading it. The stop-gap maps "on" to `AUTO`, which is right for a project that
merely wants video and wrong for one that cannot ship without it; that project currently has to
override `CNA_ENABLE_VIDEO` by hand

**Verification.** `TurningVideoOnAsksCnaToUseFfmpegIfPresentRatherThanToRequireIt` pins the current
behaviour and will need rewriting when this lands, which is the intent
