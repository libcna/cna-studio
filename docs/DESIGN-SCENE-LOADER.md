# How a game consumes a scene — ED-250 / Q-02

> The question `ANALYSIS.md` flagged as *the decision most likely to pull the editor back toward
> being an engine*, and the last thing blocking Phase 1 from closing.

## The question

The editor writes `.cnascene`. Something has to turn that file into entities a game can draw and
update. Whatever does it becomes, in effect, a runtime — so the shape of it decides whether this
project stays an editor or grows into an engine beside CNA.

`ANALYSIS.md` listed three candidates. This document records which was chosen, why, and what was
given up.

## The options

### A — A loader shipped from `cna-editor`

A header the game includes, which reads the scene through CNA's public API. The game gains a
dependency on this repository; CNA gains nothing.

### B — A new optional module inside CNA

`CNA::Scene`, or similar, able to load a `.cnascene`. One dependency for the game instead of two,
and the format becomes part of CNA's own surface.

### C — Code generation

The editor emits C++ that constructs the scene with ordinary calls. No parsing at run time, no
loader at all, and the fastest possible start-up.

## The decision

**Option A.** The loader ships from this repository, as a header.

### Why

**It keeps CNA untouched, which is the boundary the whole project rests on.** Decision D-01 says
the editor is a consumer of CNA's public API and nothing else; D-03 makes that checkable by
confining CNA to a single module. Option B breaks the first and weakens the second: the moment CNA
knows what a `.cnascene` is, the editor's file format is CNA's compatibility burden, and every
format change becomes a change to a library other people ship in their games.

**It is reversible.** Nothing about option A prevents option B later. The format does not change,
the loader's code does not change, and moving it into CNA is a relocation rather than a rewrite. The
reverse is not true: a format that has been part of CNA's public surface for a release cannot be
taken back out of it.

**Option C is fast and wrong for now.** Generated code cannot load a scene the player did not
compile, which kills the play-mode loop the editor already has — `cna-player` takes a scene path on
the command line and loads it at run time (D-15). It also puts generated source in the game's
repository and a code generator in its build, which is a large thing to ask before anyone has
complained that parsing is slow. It stays available: the loader's output is a plain structure, so a
generator emitting the same structure is an optimisation, not a redesign.

### What this costs

A game using scenes gains a dependency on `cna-editor` — specifically on `cna-editor-core`, five
CNA-free files under the same MS-PL licence as everything else here. The owner accepted this cost
explicitly when choosing option A.

## Shape

```
include/CNA/Editor/Runtime/SceneLoader.hpp     the whole loader, header-only
```

The game writes:

```cpp
#include "CNA/Editor/Runtime/SceneLoader.hpp"

namespace Runtime = CNA::Editor::Runtime;

// Once, after the graphics device exists.
Runtime::SceneLoadResult loaded = Runtime::loadScene("Scenes/Level01.cnascene", getGraphicsDeviceProperty(), ".");
if (!loaded.succeeded) { /* loaded.errorMessage says why */ }

// Every frame.
spriteBatch.Begin(SpriteSortMode::BackToFront, BlendState::NonPremultiplied);
loaded.scene.draw(spriteBatch);
spriteBatch.End();
```

### Why one header and not a header-only *everything*

The loader parses with the editor's own `JsonValue`, which lives in `cna-editor-core` and is
compiled, not inlined. Writing a second JSON reader so that the header could stand entirely alone
was considered and rejected: the editor's writer and the game's reader would then be two
implementations of one format, free to drift, and *a scene that loads in the editor and not in the
game* is the worst failure this design can produce. One reader, one writer, one format.

That is also why the loader deliberately does **not** re-derive world transforms. It composes them
the same way `SceneTransform.hpp` does, because that code is already tested against the editor's own
viewport — and a game whose sprites sit somewhere other than where the editor drew them would be a
bug nobody could see until they compared two screenshots.

### What the loader is not

- **Not an entity-component system.** It hands back a flat vector of plain structures with their
  world transforms resolved. What the game does with them is the game's business. The moment this
  grows an update loop, a message bus or a component registry, the editor has become an engine.
- **Not an asset pipeline.** It resolves texture references through the ids the editor wrote and
  loads them with `Texture2D`. Import settings are honoured only where CNA's own API exposes them.
- **Not a substitute for the player.** `cna-player` remains the process that runs a scene during
  play mode. The loader is what a *shipped game* uses.

## Open items

- **Components beyond `Transform` and `SpriteRenderer` are carried, not interpreted.** Their
  properties are preserved in `SceneEntity::components` so a game can read its own component types,
  but the loader does not act on them. `Camera` is read for its clear colour and projection because
  a scene without one cannot be framed; `Light`, `AudioSource` and `ModelRenderer` are data only,
  and will stay that way until the corresponding editor features exist (ED-402, ED-404).
- **No hot-reload path.** ED-306 will add asset reload into a *running player* over the bridge;
  that is a different mechanism and does not belong in a shipped game's loader.
