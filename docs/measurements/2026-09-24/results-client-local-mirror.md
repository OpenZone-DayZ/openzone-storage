# A container that exists only in one client's memory (2026-09-24, stand)

The question: can a storage box be shown to ONE player and to nobody else, by building it
on that client instead of replicating it?

DayZ has no per-player entity visibility. Everything in `3_game/global/game.c` that takes a
`PlayerIdentity` is a player-lifecycle call, a listing, or **RPC** -- `RemoteObjectCreate`,
`RemoteObjectTreeCreate` and their delete twins take an `Object` and nothing else. So the
only way to address one player is to send DATA and let that client build its own copy.

Whether such a copy is usable was not written down anywhere: vanilla makes client-local
entities in exactly one place (`scriptconsoleitemstab.c:695`, the script console's preview)
and never asks one for an inventory. So the engine was asked instead, through
`OZ_ProbeClientControl`'s new `mirror` and `shuffle` commands, on the stand with a live
client.

## What was asked, and what the engine answered

| # | Question | Answer |
|---|---|---|
| 1 | Does the CLIENT create a container with `ECE_LOCAL`? | **yes** -- `SeaChest`, network id `00` |
| 2 | Does that container have a `GameInventory` with a `CargoBase`? | **yes**, empty at first |
| 3 | Does `CreateEntityInCargo` put items in it, on the client? | **yes**, 3 of 3 `BandageDressing`, cargo reports 3 |
| 4 | Does the VANILLA inventory screen draw it? | **YES** -- `МАТРОССКИЙ СУНДУК (6/100)`, three bandages with their pictures, correct cell count (3 x 1x2) |
| 5 | Can an item be taken OUT of it into the player's hands, the way the screen does? | **no** -- the move is requested and nothing happens |
| 6 | Can an item be moved INSIDE it with `InventoryMode.LOCAL`? | **YES** -- `TakeToDst` returned true and the item really moved from cell 0,0 to 4,5 |
| 7 | The same move with `InventoryMode.PREDICTIVE`? | **no** -- returned false, the item stayed where LOCAL had put it |

Nothing was left broken by the failures: the refused predictive moves changed nothing and
produced no stranded entity.

## What this means

**The picture is free and the rules are free.** A mirror renders in the vanilla screen with
vanilla art, vanilla names and vanilla cell arithmetic, and the engine validates moves in
it -- `TakeToDst` answered true for a legal cell, which means it checked.

**The vanilla screen's own dragging will not work**, because it moves `PREDICTIVE`: that
mode applies the move here and asks the server to agree, and the server has no counterpart
to agree with -- the mirror's network id is `00`. Every drag the player makes with the
mouse in the vanilla panel takes that path.

**But `LOCAL` works.** So a screen of our own, issuing `LOCAL` moves on the mirror, gets
vanilla placement rules applied by the engine and needs no server round trip to rearrange
anything inside the box.

So the shape that survives the measurement:

| Side | What it is |
|---|---|
| Server | the real box, `ECE_LOCAL` so it is never announced, `ECE_NOPERSISTENCY_WORLD` so a crash leaves nothing. Every mod's own `CanReceiveItemIntoCargo`, `CanBeCombined`, slot and size rule applies, because the container is real |
| Wire | the contents as data to ONE player: `RPC(..., recipient)`, the only addressed call in the engine |
| Client | a mirror built with `ECE_LOCAL`: real container, real items, this client's memory only |
| Screen | ours, because the vanilla panel drags `PREDICTIVE`. It may still reuse `ItemPreviewWidget` and the vanilla container widgets for drawing |
| Rearranging inside | `LOCAL` moves on the mirror -- instant, engine-validated, no server trip |
| Crossing the boundary | the server does the real move on the authoritative box and re-syncs the mirror |

Nothing of the box reaches any other player at any point, because nothing is ever
announced. That is privacy by construction rather than by an honour-system filter.

## Left unmeasured

- Whether a mirror and the authoritative box can DISAGREE about a move (both are the same
  engine with the same classes, but the server's box may hold state the mirror lacks). The
  server is authoritative and its answer re-syncs, so a disagreement is a correction rather
  than a loss -- but the frequency is unknown.
- The cost of a mirror of a thousand items on the client.
- Whether the vanilla container widgets can be reused for drawing while our own handlers
  take the drag, or whether the whole screen has to be ours.

## The probe

`OZ_ProbeClientControl` (stand only, never published):

```
mirror <container> <item> <count> [n]   build a client-local container and report 1-4
shuffle <row> <col> [n]                 move its first item to that cell, LOCAL then PREDICTIVE
```

Answers go to `$profile:OpenZone_StorageProbe/scan.txt` and to the client's `.RPT` as
warnings, because `INFO` never reaches a retail client's log.
