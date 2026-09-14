# CNA Editor File Formats

Every editor-authored file is JSON. That is a deliberate choice for the authoring formats, and it
is not a claim about the *runtime* formats — the content builder is free to emit something compact
and binary. Authoring files are optimised for a different thing: being read by a human, diffed in a
pull request, merged after a branch, and migrated across versions.

Three properties are maintained on purpose:

- **Order is stable.** Object members keep insertion order on read and write; component properties
  serialise alphabetically. Saving a file you did not change produces no diff.
- **Integers are written as integers.** `120`, never `120.0`. Coordinates, counts and layer indices
  stay readable in a diff.
- **Two relaxations for hand editing.** The parser accepts `//` line comments and trailing commas.
  The editor never writes either.

Every format carries a `formatVersion`. A file from the future is **rejected with a clear message**
rather than partially read; a file from the past is read and upgraded.

---

## `.cnaproject`

One per project, at the project root. Its directory *is* the project root; every other path in the
project is relative to it and uses forward slashes on all platforms.

```json
{
  "formatVersion": 1,
  "name": "MyGame",
  "kind": "CnaNative",
  "startupScene": "Scenes/MainMenu.cnascene",
  "assetDirectory": "Assets",
  "sceneDirectory": "Scenes",
  "defaultGraphicsBackend": "easygl",
  "layers": ["Background", "Default", "Foreground"],
  "gridSnap": 16.0,
  "targetPlatforms": ["linux-x64", "windows-x64"],
  "modules": ["cna-core", "cna-graphics-2d", "cna-audio"],
  "plugins": ["org.openeggbert.mc3"]
}
```

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `formatVersion` | int | — | Required. Currently `1` |
| `name` | string | `"Untitled"` | Display name |
| `kind` | enum | `"CnaNative"` | `CnaNative` or `XnaCompatible` — see below |
| `startupScene` | string | `""` | Project-relative. Ignored for `XnaCompatible` |
| `assetDirectory` | string | `"Assets"` | Scanned by the asset database |
| `sceneDirectory` | string | `"Scenes"` | Where new scenes are created |
| `defaultGraphicsBackend` | string | `"easygl"` | Which player build the Play button prefers |
| `layers` | string[] | `["Default"]` | Render layers, back to front. Never empty |
| `gridSnap` | number | `0` | World units a snapped drag rounds to. `0` means the viewport's visible grid. Written only when set |
| `targetPlatforms` | string[] | `["linux-x64"]` | Offered by the build dialog |
| `modules` | string[] | `["cna-core"]` | CNA modules the game links |
| `plugins` | string[] | `[]` | Plugin ids to load for this project |

### `gridSnap`

What a Ctrl-drag in the viewport rounds to, in world units. A project laid out on a 16-unit tile
grid says so once instead of having every user zoom until the drawn grid happens to agree.

Zero is **not** "no snapping" — Ctrl is what turns snapping on. It is "use the grid the viewport is
drawing", which is what the editor did before the setting existed and therefore what a project
written before it means. A separate enabled flag would have been a second way to say the same thing
and a second thing to get out of step.

Additive, like `layers`: no `formatVersion` bump, and the field is written only when it is set, so
the first save of an existing project is not a diff that says nothing. A negative value is refused
rather than clamped — it is not a smaller step, it is a value with no meaning.

### `layers`

Layers belong to the project, not to a scene: one named in one level and missing from the next would
make moving an entity between them silently change what it is. The **order is the meaning** — index
0 draws first — so this is a list, not a set, and moving an entry is a real edit.

It is never empty. A project with no layers has nothing for an entity to be on, so a file that omits
the field, or is hand-edited down to nothing or to blanks, gets `["Default"]` back. The field was
added without a `formatVersion` bump, which is what "additive changes need no migration" means in
practice; a project written before layers existed opens and gains the default.

The list drives `CNA.Layer`'s choices by re-registering that component's descriptor. Renaming a
layer therefore leaves entities holding a name that is no longer offered — and they are left that
way on purpose. Which of the remaining layers the user meant is their decision, not the editor's, so
the Validation panel reports it (`unknown-enum-value`) instead.

### `kind`

This field is how CNA avoids becoming a mandatory engine.

**`CnaNative`** opts into the editor's model: scenes, entities, components, the inspector, gizmos,
prefabs and the runtime bridge.

**`XnaCompatible`** does not. The editor offers the asset browser, importer settings, content
preview, backend configuration and Play — and stops there. The game keeps its own hand-written
`Game::Initialize`/`LoadContent`/`Update`/`Draw`, with no editor concepts in it. Declaring a
`startupScene` for such a project produces a warning, because the editor will not load it and the
mismatch is worth surfacing.

### `defaultGraphicsBackend`

The lower-case command-line name of a CNA backend (`cna-editor --list-backends` prints them). This
selects which **player build** the Play button launches, not anything about the editor: CNA fixes
its backend at compile time, so the editor binary is bound to whatever it was built against. An
unrecognised value loads with a warning rather than failing.

---

## `.cnascene`

One per scene. Lives under `sceneDirectory` by convention, but any path works.

```json
{
  "formatVersion": 1,
  "sceneId": "c486b3f0-2a41-4d6b-9f18-7e0c5a1b4d92",
  "name": "Level01",
  "entities": [
    {
      "id": "f392a1b2-c3d4-4e5f-a607-182930415263",
      "name": "Player",
      "components": {
        "CNA.Transform": {
          "position": [100, 220, 0],
          "rotation": [0, 0, 0, 1],
          "scale": [1, 1, 1]
        },
        "CNA.SpriteRenderer": {
          "layerDepth": 0.5,
          "origin": [0, 0],
          "sourceRectangle": [0, 0, 0, 0],
          "spriteEffects": "None",
          "texture": "67ecaf68-3d2a-4b1c-8e9f-0a1b2c3d4e5f",
          "tint": [255, 255, 255, 255]
        }
      },
      "editorState": {
        "expanded": true
      }
    },
    {
      "id": "a1b2c3d4-e5f6-4708-9a1b-2c3d4e5f6071",
      "name": "Weapon",
      "parent": "f392a1b2-c3d4-4e5f-a607-182930415263",
      "components": {
        "CNA.Transform": {
          "position": [24, 0, 0],
          "rotation": [0, 0, 0, 1],
          "scale": [1, 1, 1]
        }
      }
    }
  ]
}
```

### Entity fields

| Field | Type | Default | Meaning |
|-------|------|---------|---------|
| `id` | UUID string | generated | Stable identity. References point at this, never at the name |
| `name` | string | `"Entity"` | Display name; need not be unique |
| `parent` | UUID string | absent | Omitted for a root entity |
| `enabled` | bool | `true` | Omitted when true |
| `sortOrder` | int | `0` | Sibling ordering; omitted when zero |
| `components` | object | `{}` | Keyed by component type id |
| `editorState` | object | absent | Editor-only, never seen by the runtime |

`parent` is stored on the child rather than as a child list on the parent, because every operation
the editor performs — reparent, delete, "which entities are roots" — is cheaper and harder to
corrupt that way.

### `editorState`

Cosmetic, editor-only data: tree expansion, layer colour, notes, icon overrides. Keeping it in a
named sub-object means the runtime scene compiler drops it wholesale rather than needing to know
which of a component's fields are cosmetic. It is deliberately *not* a separate `.cnascene.user`
sidecar — that would double the file count and guarantee the two drift apart under version control.

### Property encoding

Types are **not** embedded. The `ComponentDescriptor` supplies them on load, which keeps scene files
small and readable.

| Property type | JSON form | Example |
|---------------|-----------|---------|
| `bool` | boolean | `true` |
| `int` | number | `42` |
| `float` | number | `0.5` |
| `string` | string | `"Player"` |
| `enum` | string (the option name) | `"FlipHorizontally"` |
| `color` | `[r, g, b, a]`, 0–255 | `[255, 0, 0, 128]` |
| `vector2` | `[x, y]` | `[10, 20]` |
| `vector3` | `[x, y, z]` | `[100, 220, 0]` |
| `vector4` | `[x, y, z, w]` | `[1, 0, 0, 1]` |
| `quaternion` | `[x, y, z, w]` | `[0, 0, 0, 1]` |
| `rectangle` | `[x, y, width, height]` | `[0, 0, 32, 32]` |
| `asset` | UUID string, or `null` | `"67ecaf68-…"` |
| `entity` | UUID string, or `null` | `"f392a1b2-…"` |
| `list` | array of the element encoding | `["ground", "solid"]` |

An absent or malformed **quaternion** defaults to identity `[0, 0, 0, 1]`, not to all-zero: an
all-zero quaternion is not a rotation and would collapse the transform.

An **empty rectangle** (`width` or `height` ≤ 0) means "the whole texture", matching XNA's own
`SpriteBatch::Draw` convention for a null source rectangle.

A **sprite animation**'s `frames` is one of these: `CNA.SpriteAnimation.frames` is a `list` of `int`,
each an index into the sheet. `frameDurations` is an optional parallel `list` of `float`, in seconds,
and is **ignored unless it is exactly as long as `frames`** — a clip that never needed a hold on one
frame should not carry a list of identical numbers, and a scene written before durations existed has
no such list at all, so absence has to keep working exactly as it did.

A **tilemap**'s grid is one of these: `CNA.Tilemap.tiles` is a `list` of `int`, row-major, `-1` for
an empty cell, sized by the component's own `columns` and `rows`. Empty is negative rather than zero
so that tile 0 — the first tile in every sheet anyone draws — stays usable. A stored list of the
wrong length is padded or truncated on load rather than rejected: a hand-edited scene one row short
should open and be fixable. There is no sparse form; it would scale better and diff worse, and is
not worth reaching for before a real map is measurably slow.

A **list** carries no per-element type tag. Its element type is declared once, on the
`PropertyDescriptor`, and never inferred from the contents — an empty list has no element to infer
from, and a list whose type followed its contents could never be edited back from empty. A tag per
element would be a second source of truth, and the first one to disagree would win by accident.
Lists do not nest: a list of lists is a table, and a table deserves its own type rather than a
widget that recurses.

Note that a **2-, 3- or 4-element array on an unregistered component** is still read as a vector,
because nothing declares otherwise and that guess has been the one the editor makes since Phase 0.
Any other length is read as a list, element by element, which is what makes a scene whose plugin is
missing save back byte-for-byte instead of losing the field.

### Loading is forgiving on purpose

An editor that refuses to open a slightly broken file is an editor you cannot use to *fix* a broken
file. A scene loads with warnings, never a partial failure:

| Problem | Behaviour |
|---------|-----------|
| Unknown component type | Data is preserved verbatim and round-trips through save; the inspector marks it unregistered. A missing plugin must not become missing data |
| Missing `parent` | The entity becomes a root; warning recorded |
| Parent cycle | The entity becomes a root; warning recorded |
| Missing or malformed `id` | A new id is generated; warning recorded |
| Duplicate `id` | The later entity is dropped; warning recorded |
| Property missing from the file | Filled from the descriptor's declared default |
| Property present but the wrong shape | Falls back to the type's zero value |
| `formatVersion` newer than this build | **Load fails** with an explicit message. This is the one hard failure |

---

## `.cnaprefab`

One reusable entity subtree. Its `entities` array uses **exactly** the entity encoding `.cnascene`
uses — the same code writes both — because an instantiated prefab and a hand-authored entity have to
be indistinguishable once they are in a scene. Two encodings of one thing would drift, and the
symptom (an override that appears out of nowhere) is the kind nobody sees until they diff two files
by hand.

```json
{
  "formatVersion": 1,
  "prefabId": "3c1e9a44-…",
  "name": "Enemy",
  "entities": [
    { "id": "…", "name": "Enemy",  "components": { "…": {} } },
    { "id": "…", "name": "Weapon", "parent": "…", "components": { "…": {} } }
  ]
}
```

The **first entity is the root**, and the rest are stored parents-before-children so an
instantiation can walk the array once without ever needing a parent it has not created yet. The
root has no `parent`: keeping the one it had where it was captured would make the file describe a
hierarchy that exists only there.

A file with no entities is **refused** — it would instantiate to nothing, and the user could not
tell that from an instantiation that silently failed. A child whose `parent` is not in the file is
attached to the root and reported, the same forgiving stance a scene takes toward a dangling parent.

### Overrides are computed, not stored

Nothing here, and nothing in a `.cnascene`, records "this instance has changed X". The scene holds
each instance's actual values the way it holds every other entity's, and "what has this instance
changed?" is answered by comparing it against the prefab.

A stored override list would be a second description of the same fact, free to disagree with the
first — and the way that disagreement surfaces is a property reverting to a value the user never
chose, which is the worst thing a prefab system can do. It also means prefabs added **no** new field
to the scene format: a scene written before they existed is still a valid scene.

What an instance does store is the link, in `editorState` because it is editor bookkeeping rather
than something the game runs (D-07):

| Key | On | Meaning |
|-----|----|---------|
| `prefabAsset` | the instance root only | Asset id of the `.cnaprefab` |
| `prefabEntity` | every entity of the instance | Id of the prefab entity it came from |

The per-entity link is what lets an override be found without depending on names or on sibling
order, both of which the user is free to change.

---

## `.cnaasset`

A sidecar named after the full source file name: `player.png` → `player.png.cnaasset`. Commit it
alongside the asset.

```json
{
  "formatVersion": 1,
  "id": "67ecaf68-3d2a-4b1c-8e9f-0a1b2c3d4e5f",
  "type": "Texture2D",
  "importer": "CNA.TextureImporter",
  "settings": {
    "generateMipmaps": true,
    "premultiplyAlpha": true
  },
  "dependencies": [],
  "sourceStamp": {
    "size": 20481,
    "modifiedTime": 1754209823
  }
}
```

| Field | Type | Meaning |
|-------|------|---------|
| `formatVersion` | int | Required |
| `id` | UUID string | **The reason this file exists.** Scenes reference this, never the path |
| `type` | enum | `Unknown`, `Texture2D`, `SpriteFont`, `SoundEffect`, `Song`, `Effect`, `Model`, `Scene`, `RawData` |
| `importer` | string | Importer type id; empty means "track but do not import" |
| `settings` | object | Importer-specific, written verbatim |
| `dependencies` | UUID string[] | Assets this one references; drives reimport ordering |
| `sourceStamp` | object | Size in bytes and modification time in **seconds** |

The sidecar holds the identity; the *path* is discovered by scanning. Moving or renaming a source
file is therefore free: the id follows the sidecar, the database updates the path, and no scene
changes.

### Why size and time rather than a content hash

Hashing every asset on every project open is how an editor comes to take thirty seconds to start.
Size plus modification time is enough for reimport decisions and costs a `stat`. A content hash can
be added later as an opt-in for pipelines that need the guarantee.

`modifiedTime` is in **seconds**, deliberately. The filesystem clock's native tick count is around
4.6 × 10¹⁸ nanoseconds, which is outside the range a `double` represents exactly — and JSON numbers
are doubles, so the native value round-tripped through this file *changed*, making every asset look
modified on every scan. That was a real bug, caught by
`AssetSidecarStampSurvivesAJsonRoundTrip`.

### A missing source file keeps its record

A file that is gone today may be one `git checkout` away from returning. Deleting the record would
permanently break every reference to it, so the database reports it as missing instead.

---

## `.cnarecovery`

Not an authoring format. One file per unsaved scene, under the user's *state* directory
(`$XDG_STATE_HOME/cna-editor/recovery`, or the platform equivalent), written every
`--autosave=SECONDS` while the open document differs from its file and deleted the moment it
matches again. Nothing in a project ever references one, and none is ever written beside a project.

```json
{
  "formatVersion": 1,
  "projectPath": "/home/me/MyGame/MyGame.cnaproject",
  "scenePath": "/home/me/MyGame/Scenes/Level01.cnascene",
  "sceneName": "Level01",
  "sceneId": "c486b3f0-2a41-4d6b-9f18-7e0c5a1b4d92",
  "savedAt": 1754236800,
  "scene": { "formatVersion": 1, "…": "the whole .cnascene document, verbatim" }
}
```

The file name is `<sceneId>.cnarecovery`, so a scene has one snapshot rather than an accumulating
pile. `projectPath` is what the editor matches on when a project is reopened: the scene someone was
editing when the process died is not necessarily the project's startup scene, and offering only the
latter would silently drop the work.

### Written by rename, never in place

A snapshot goes to `<sceneId>.cnarecovery.tmp` and is renamed over the previous one. A crash *during*
a snapshot therefore leaves the earlier snapshot intact. A half-written recovery file would be worse
than none at all — it would fail to load at the one moment it is needed, having already convinced
its owner their work was safe.

### There is no crash handler

The reliable half of crash recovery is the part that runs *before* the crash. A handler serialising
a document from inside `SIGSEGV` is calling `malloc` and the filesystem with a corrupted heap: the
situation in which it is least likely to work is exactly the one it exists for. A snapshot written
by ordinary code every few seconds is already on disk when the process dies and needs nothing from
the dying process at all.

### Recovery is offered, never applied

On reopening a project with a snapshot, the editor says so and puts two items in the File menu. It
does not load the snapshot. Replacing what someone opened with something whose provenance they
cannot see turns one loss into two. While the offer is outstanding, autosave for that scene is
suspended and says so — the current session's unsaved seconds are worth less than the previous
session's unsaved hours, and they share a file name.

---

## Editor ↔ player wire protocol

One JSON object per line over a stream socket. The framing is the newline, so a message body
contains exactly one and it is last.

```
{"type":"hello","payload":{"protocolVersion":1,"projectRoot":"/home/me/MyGame"}}
{"type":"ready","payload":{"backend":"SOFTWARE","protocolVersion":1}}
{"type":"loadScene","payload":{"scenePath":"Scenes/Level01.cnascene"}}
{"type":"setProperty","payload":{"entityId":"f392…","component":"CNA.Transform","property":"position","valueType":"vector3","value":[100,220,0]}}
{"type":"reloadAsset","payload":{"assetId":"8d9fb62c-7447-4c22-a190-036691308c8a"}}
{"type":"reportLog","payload":{"severity":"info","text":"Loaded 3 entities"}}
{"type":"screenshot","requestId":7,"payload":{"path":"/tmp/easygl-frame.png"}}
{"type":"screenshotReady","requestId":7,"payload":{"path":"/tmp/easygl-frame.png","written":true}}
```

| Field | Type | Meaning |
|-------|------|---------|
| `type` | string | Message kind |
| `requestId` | int | Correlates a reply with its request; omitted when unsolicited |
| `payload` | object | Type-specific fields |

### Messages

**Editor → player:** `hello`, `loadScene`, `reloadAsset`, `setProperty`, `pause`, `resume`,
`stepFrame`, `selectEntity`, `screenshot`, `quit`.

**Player → editor:** `ready`, `reportException`, `reportLog`, `reportFrameStats`,
`screenshotReady`.

### Three design points

**Unknown fields and unknown message types are ignored, never fatal.** Both ends must tolerate a
peer built from a different revision. A malformed line is counted and skipped; a peer from a newer
revision cannot kill a play session.

**`setProperty` carries its own `valueType`,** unlike a scene file. The player resolves component
schemas from its own registry, which may not match the editor's after a plugin reload, so the wire
has to be self-describing.

**`reloadAsset` names an asset by id, not by path.** Asset identity is a Uuid everywhere in the
editor (D-08), and a reload that named a path would miss a file renamed between the change and the
message. Both ends resolve the id through the database they each scanned, so there is one answer to
"what is this asset" on both sides.

**`screenshotReady` says whether the file was written, and is sent only once it has been.** The
player queues the request and answers it from the frame loop that owns the device, because that is
the only place a back buffer can be read; a reply sent when the message arrived would claim a file
existed before anything had been written to it. When the capture fails -- a build with no graphics,
a backend that cannot read its own back buffer -- the reply carries `written: false` and an `error`
string rather than nothing at all, since an editor waiting for a reply that never comes is worse
off than one told no. The asking side must not fall back to looking for the file: a stale one from
an earlier run answers a different question.

**The comparison mode is this protocol used several times at once.** ED-510 launches one player per
installed backend, waits for each `ready`, sends each the same `screenshot`, and compares the files
named in the `screenshotReady` replies. Nothing was added to the wire for it beyond `written`: what
made it possible was the reply becoming truthful.

**Reading goes through a stream decoder.** A stream socket delivers arbitrary chunks. A reader that
assumes one `recv()` equals one message works right up until a message straddles a packet boundary,
and then fails in a way that is very hard to reproduce.

### Why line-delimited JSON

The traffic is a handful of messages per second. Being able to watch a session with `nc` during
bring-up is worth far more than the bytes a binary format would save. The framing can be replaced
later without changing the message model.

---

## `plugin.json`

One per plugin directory.

```json
{
  "id": "org.openeggbert.mc3",
  "name": "MC3 Tools",
  "version": "0.1.0",
  "description": "Mesh-Craft import, export and primitive editing",
  "author": "OpenEggbert",
  "editorApiVersion": 1,
  "library": "libmc3-editor-plugin.so",
  "dependencies": []
}
```

| Field | Type | Meaning |
|-------|------|---------|
| `id` | string | Reverse-DNS, unique. Also the component type id prefix a plugin should use |
| `editorApiVersion` | int | Must match the editor's exactly, or the plugin is rejected unloaded |
| `library` | string | Shared library file name, relative to the manifest |
| `dependencies` | string[] | Plugin ids that must load first |

The manifest is read and validated **before** the library is opened. An ABI mismatch that reaches
`dlopen` is a crash, not an error message — so version checking, dependency ordering and library
existence are all settled first, and rejection is always reported rather than fatal.

---

## Format evolution

| Change | Requires a version bump? |
|--------|--------------------------|
| Adding an optional field with a default | No — older readers ignore it, newer readers default it |
| Adding a component type or property | No — the descriptor system handles it |
| Renaming a field | **Yes**, plus a migration |
| Changing a field's type or units | **Yes**, plus a migration |
| Changing the meaning of an existing value | **Yes**, plus a migration |

Both halves are implemented. Gating refuses a file from the future and one with no version at all;
the migration chain (`CNA/Editor/Core/FormatMigration.hpp`) upgrades one from the past.

A chain is a list of **single-version steps**: 3 becomes 4, then 4 becomes 5. No step knows about
more than one transition, which is what keeps the twelfth migration the same size as the first —
one function per `(from, to)` pair grows quadratically and is where migration frameworks go to die.
Steps run on the parsed JSON, before any of it reaches a document type, because by the time a
`SceneDocument` exists the fields the old file used are already gone.

Every format here is at version 1, so every chain is empty. That is the intended state: the
mechanism exists so the first real change is a small, tested, reviewable addition to a path that
already runs on every load, rather than a new path nobody has exercised. **Registering a step is
not a licence to bump a version** — formats stay backward compatible unless `plan.md` says
otherwise, so the practical rule remains: only make additive changes.

Two behaviours worth knowing:

- A migration that **cannot** run is a refusal, not a best-effort read. Reading a version-1 file
  with a version-3 reader substitutes defaults for fields that moved, and the substitution is
  written back on the next save — opening the file is how you lose part of it.
- A **sidecar** is the exception. One this build cannot upgrade keeps its `id`, loses only its
  importer settings, is reported, and is left on disk untouched. The id is what scenes reference
  (D-08); regenerating it would break every reference in the project, which is a far worse outcome
  than an importer setting reverting to its default.
