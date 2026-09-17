# Ten boxes at once, with items moving (2026-09-17)

The owner asked for the load case the design was built to survive: ten boxes opening and
closing at the same time, items being moved at the same time, measured with the profiler.

## The stand

Retail server, one client connected and standing among five of the ten boxes (the other
five are elsewhere on the map, so their items cost the server everything except the
network sync). Ten boxes, **10683 items / 10768 entities** in all:

| Boxes | Items each | Where |
|---|---|---|
| 4 Large | 1410, 1410, 1410, 1443 | away from the player |
| 1 Medium | 10 | away from the player |
| 5 Large | 1000 | around the player |

Two tools were written for this run. `oz_storage open_all` / `close_all` put every box's
request in ONE frame, which ten separate bridge commands never do. `oz_probe churn` moves
items between two open boxes with the same server-side call an inventory action makes
(`TakeToDst` in SERVER mode), at a set rate, always taking from the fuller side so a long
run never empties one box.

Settings as shipped: 5 ms frame budget for both jobs, 500 items per second for opening,
50 deletes per frame. The budgets are per server, not per box: ten jobs divide them.

## Window 1: ten boxes opening at once

`open_all`, 10683 items restored from disk into the world.

| | |
|---|---|
| Wall time, all ten | 21.6 s |
| Longest single step of any job | 3 ms |
| Longest server frame (frame monitor, 187091 frames) | 44 ms |
| Frames over 100 ms | 0 |
| Profiler: stretches >= 150 ms | **none** |
| OpenZone_Storage share of the main thread | 3.87 % |
| Items missed or refused | 0 |

The profiler's verdict line: "nothing here would be felt as a freeze".

## Window 2: items moving, no open or close

4000 moves between two open 1000-item boxes at 400 moves per second, which is about
twenty times what a busy base does.

| | |
|---|---|
| Moves | 4000, none refused |
| Wall time | 13.1 s |
| Script work, all moves | 520.9 ms |
| Cost of one move | 0.13 ms |
| Longest batch of 20 moves | 4 ms |
| Longest server frame | 39 ms |
| Profiler: stretches >= 150 ms | **none** |
| OpenZone_Storage share | 0.90 % (the box's own cargo hooks) |

## Window 3: everything at once

A churn of 20000 moves running for 54 s, with `close_all` fired 5 s in and `open_all`
fired 30 s in. So items were being moved while ten boxes wrote themselves to disk, and
again while ten boxes filled themselves from disk.

| | |
|---|---|
| Moves accepted | 8300 |
| Moves refused | 11700 |
| Longest server frame (768465 frames over windows 2 and 3) | 44 ms |
| Frames over 100 ms | 0 |
| Profiler: stretches >= 150 ms | 1, of 168 ms |
| OpenZone_Storage share | 6.14 % |

**The 11700 refusals are the design working, not a fault.** The moment a box leaves the
OPEN state its `CanReleaseCargo` says no, so every move into or out of a closing box is
refused instead of racing the capture. A player would see the item simply not move.

**The 168 ms stretch is the profiler's known merge, not a frame.** Its own breakdown names
`Serializer.Write` 66 %, `OZS_Records.WriteListEntity` 16 %, `OZS_Records.ListLine` 6 %,
which is the close job writing the store. Ten close jobs write in consecutive frames with
the same function on the stack, and the 200 Hz sampler cannot see the frame boundaries
between them, so it reports them as one piece of work. The independent witness disagrees:
the in-engine frame monitor, which times every frame, saw a maximum of 44 ms across 768465
frames and not one frame over 100 ms. The same artefact was measured on 2026-09-16, when
fourteen back-to-back 20 ms frames were reported as one 232 ms freeze.

## Nothing was lost and nothing was duplicated

The sum of the items written by the final `close_all`, after all three windows:

```
10 + 1000 + 1000 + 1000 + 1000 + 1000 + 1410 + 1410 + 1443 + 1410 = 10683
```

Exactly the count the test started with. The two churned boxes ended with 1000 items each,
because the churn always takes from the fuller side.

## What this says about the live server

Ten boxes is more than the live server is likely to open in one second, and 400 moves per
second is far more than players generate. Under both, the mod's share of the main thread
stayed between 1 % and 6 %, no frame reached 100 ms, and the store stayed exact. The one
number worth watching is the 44 ms frame: it is the ceiling seen so far, it comes from the
engine's own entity creation and deletion rather than from the paced jobs, and it is well
under the 150 ms a player notices.
