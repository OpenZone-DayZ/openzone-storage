# Does ECE_NOPERSISTENCY_WORLD actually keep a container out of the save? (2026-09-24, stand)

The design for a server-side authoritative box leans on this flag: if the box and its
contents are never saved, a crash leaves nothing behind and no boot sweep is needed. That
was asserted from the constant's comment and not measured, which is not good enough for a
load-bearing assumption -- the engine autosaves continuously and a crate with 5000 items
has survived `taskkill` before.

```c
// 3_game/ce/centraleconomy.c
const int ECE_NOPERSISTENCY_WORLD = 8388608;   // do not save this object in world
const int ECE_LOCAL               = 1073741824; // create object locally
```

## The run

`oz_storage op=persist` (stand-only pbo) creates three `SeaChest`, two metres apart, each
filled with three `BandageDressing` through `CreateEntityInCargo` -- which takes no flags,
so this also answers what happens to the CHILDREN of a parent that is not saved.

Then `server_stop` (which saves) and `server_start`.

| Chest | Flags at creation | Network id | After save + restart |
|---|---|---|---|
| plain, 11527.81 / 3380.19 | `ECE_PLACE_ON_SURFACE` | 019587 | **still there** |
| nopersist, 11529.81 / 3380.19 | `+ ECE_NOPERSISTENCY_WORLD` | 019591 | **gone** |
| local+nopersist, 11531.81 / 3380.19 | `+ ECE_LOCAL + ECE_NOPERSISTENCY_WORLD` | **00** | **gone** |

The control matters: the plain chest came back, so the save really happened and the other
two were not lost to a failed save.

**No orphans.** A query of everything within 12 m of the spot after the restart found no
loose `BandageDressing` at all: the children of an unsaved parent are not saved either,
rather than being saved and dropped on the ground.

## Two facts worth keeping

- **`ECE_NOPERSISTENCY_WORLD` works, and it covers the whole tree.** Items created into an
  unsaved container with `CreateEntityInCargo` -- a call that takes no flags of its own --
  are not saved either.
- **`ECE_LOCAL` on the SERVER gives network id `00`.** The object is not registered on the
  network at all, rather than registered and withheld. That is why a client can never be
  told about it by accident, and why nothing about it can reach another player.

## What this settles for the design

A server-side authoritative box created with `ECE_LOCAL | ECE_NOPERSISTENCY_WORLD`:

- is invisible to every client, because it has no network identity;
- leaves nothing behind after a crash, because it is never written to the world save;
- needs no boot sweep for its contents, unlike today's boxes.

The truth of what was in it stays where it already is -- the bridge's SQL -- so a crash
costs whatever was not yet committed there, and nothing else.
