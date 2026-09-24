# Is an entity still alive after RemoteObjectTreeDelete? (2026-09-24, stand)

The design's "put an item INTO the box" path stands on this call: the player's item is a
real, announced entity; it moves into the authoritative box and is then taken off the
network so no client knows about it any more. Vanilla's own header warns

```c
// 3_game/global/game.c:707
proto native void RemoteObjectTreeDelete(Object obj);
/// deletes only remote object tree (unregisters from network). do not use if not sure what you do
```

and this project has already been burnt once by a networking shortcut -- the undeletable
ghosts of 2026-09-18 -- so the question was asked rather than assumed.

## The run

`oz_ghost op=unpublish`: a `SeaChest` created ANNOUNCED (the real direction: an item that
was in the world), filled with two `BandageDressing`, then unannounced, then examined.

```
before: netid 019380, cargo 2
after:  alive, netid 00, cargo 2, accepts a new item: 1, cargo now 3,
        a move inside: true -> 5,5, after ObjectDelete: pending=true
```

| Check | Result |
|---|---|
| Is the entity still there? | **yes** |
| Its network id | **019380 -> 00** -- really unregistered, the exact inverse of publishing |
| Is its cargo intact? | **yes**, still 2 |
| Does it still accept an item? | **yes**, cargo 2 -> 3 |
| Does a move inside it still work? | **yes**, `TakeToDst` true, the item went to 5,5 |
| Does it delete cleanly afterwards? | **yes** -- a ghost-watcher scan of 25 m found **0 zombies** |

## What this settles

`RemoteObjectTreeCreate` and `RemoteObjectTreeDelete` are a clean inverse pair, and an
unpublished entity is a fully working server-side object: inventory, acceptance, moves and
deletion all behave. Nothing about it resembles the stuck state of 2026-09-18 (`netid 00,
loc=0, TODELETE PENDING SETDEL`, forever in the spatial index): there the entity was a
half-deleted corpse, here it is simply a live object nobody has been told about.

So the boundary works in both directions without serialising anything:

| Direction | How |
|---|---|
| box -> player | move the entity server-side, then `RemoteObjectTreeCreate` to announce it |
| player -> box | move the entity server-side, then `RemoteObjectTreeDelete` to take it off the network |

The item's whole state -- its blob, its attachments, its nested cargo -- crosses for free,
because it is the same object throughout. Nothing is captured and nothing is recreated, so
neither direction can duplicate or lose an item by itself.

The only window that remains is the SQL commit, and that is a separate decision recorded in
the design: destroy before create, lose rather than duplicate.
