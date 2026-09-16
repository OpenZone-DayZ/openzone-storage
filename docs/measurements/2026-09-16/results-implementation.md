# Implementation runs, 2026-09-16 (evening)

The mod itself (`OpenZone_Storage`) on the retail stand: DayZServer_x64.exe 1.29
(build 2026-08-13), `verifySignatures = 2`, mods CF + OpenZone_Core + OpenZone_PDA +
OpenZone_Radio + OpenZone_StorageProbe + the two bridge glues; one retail client in a
1600x900 window standing next to the box. Every number below is from the server's
script log (`storage:` lines), the probe's frame monitor (`oz_probe baseline`), the
probe's client monitor (`clientprofile/OpenZone_StorageProbe/client.log`) or
`script-profile.ps1` (200 Hz sampler on the main thread).

## Close: capture and delete

| Load | Capture | Delete |
|---|---|---|
| 1410 items, 1430 entities, one frame (first cut) | 240 ms in ONE frame: count 1 ms, `items.bin` 238 ms, `items.list` 35 ms, commit 2 ms | 29 frames, 110 ms |
| same, paced at 20 ms per frame | 13 frames, 265 ms total + commit 3 ms; longest step 21 ms | 29 frames, 83 ms, 50 entities per frame |
| 1443 items, 1468 entities, 6 loaded rifles, client next to the box, 20 ms | 14 frames, 280 ms + commit 4 ms | 30 frames, 107 ms; longest step 21 ms |
| same, paced at 5 ms per frame (the shipped default) | 61 frames, 313 ms + commit 3 ms | 30 frames, 134 ms; longest step 6 ms |

The `FileSerializer` costs about **0.17 ms per entity** (the `OnStoreSave` blob and one
`Write` per field, unbuffered), which is why the capture is paced. The old files are
deleted when the writer opens, so a crash anywhere inside a close leaves the engine's
cargo as the only truth (see the boot rules).

## Open: paced materialisation

| Load | Wall | Script work | Longest frame step |
|---|---|---|---|
| 1410 items, 1430 entities, nobody connected | 5.7 s at 250/s | 23 ms over 23 561 frames | 2 ms |
| same, client next to the box | 5.7 s | 73--207 ms | 2 ms |
| 1443 items, 1468 entities, 6 rifles, client next to the box | 5.9 s | 37 ms | 2 ms |
| four Large boxes at once (5759 entities), client next to them | 14.8--18.4 s each | 462 / 184 / 53 / 58 ms | 2 ms |

Server frames during the single-box cycle (probe monitor): 166 378 frames, avg 0.2 ms,
**max 24 ms**, none over 100 ms. During the four-box open: 304 070 frames, **max 33 ms**,
none over 100 ms.

Client next to the box: 255 entities per second arrive during an open, 1468 deletes in
one second during a close; client frames stayed at their idle 51 ms average, **max 57 ms**
(single box) and **max 67 ms** (four boxes), none over 100 ms.

## Round trip and the fallback

- `inspect` of 1409 cargo items before the close and after the reopen: identical except a
  canteen's ambient temperature (9.1 -> 9.8); the second cycle with 1437 items and 6
  rifles: identical, hash `-109834866` both times. Every rifle came back in its slot with
  its magazine (17 or 30 rounds) and its chambered round.
- `items.bin` truncated to 60 000 bytes by hand: the blob broke at root 639 of 1410, was
  kept as `items.bin.failed-<stamp>`, roots 639..1409 came from `items.list`; all 1410
  items and 1430 entities were restored. The six stateful stock items were among the list
  roots and lost exactly their script state (rag wetness, canteen temperature, battery
  energy, radio frequency, chip notes, rifle chamber), as designed and logged.

## Viewers, auto-close, players leaving

- Client inventory screen showing the box: `viewers=1`, `close` refused with
  `#STR_OZS_BUSY`; screen closed: `stops looking`, `close` accepted one second later.
- `FindMenu(MENU_INVENTORY)` answers the inventory menu while it is HIDDEN: the first cut
  reported "looking" whenever the player merely stood near the box. Fixed by asking
  `InventoryMenu.IsOpened()`.
- Auto-close with the quiet period tuned to 20 s and the player 60 m away: fired at the
  next 5 s tick.
- Client process killed with the box open: the server's disconnect handling closed the
  box 16 s later ("closing by server (disconnect)"); the identity is already null in
  `PlayerBase.OnDisconnect`, so the name is now kept from `OnConnect`.

## Restart scenarios (hard kills with `Stop-Process -Force`, client connected so the
engine autosaves every second)

| Scenario | Persisted state at boot | Rule | Reopen |
|---|---|---|---|
| S1 restart with a CLOSED box | CLOSED, 0 entities, files | files win (nothing to do) | 10 items |
| S2 kill with an OPEN box whose files were already released | OPEN, 10 entities, no files | closed from the engine's cargo | 10 items |
| S3 kill during OPENING (1410 items) | OPENING, 1408 entities, files | files win, 1408 stale items removed | 1410 items |
| S4 kill during the delete phase of CLOSING | OPEN, 1410 entities, files (committed) | files win, 1410 stale items removed | 1410 items, rifle in its slot |

Nothing lost, nothing duplicated in any of the four. The world's persistent entities load
a few frames AFTER `MissionServer.OnMissionStart`, so the summary line waits 15 s.

## Profiler

`script-profile.ps1 -Seconds 60` windows, reports `profiler-cycle1-close-open-20ms.txt`,
`profiler-cycle2-four-boxes.txt` and `profiler-cycle3-close-5ms.txt` in this folder:

| Window | OpenZone_Storage share of the main thread | Stretches >= 150 ms |
|---|---|---|
| 1: close + open of 1468 entities, 20 ms budget | 1.79 % | one of 232 ms in `Serializer.Write` -- the 14 back-to-back capture frames of 20 ms merged by the 5 ms sampler (the frame monitor saw max 24 ms); one of 693 ms in the probe's own `inspect` |
| 2: four boxes opening at once | 2.69 % | none ("nothing here would be felt as a freeze") |
| 3: close with the 5 ms budget | 1.91 % | none |

So the shipped default budget is 5 ms per frame for both jobs: the profiler resolves such
frames, the jobs need far less anyway, and the wall time of a close grows from 0.3 s to
0.4 s.

## Defect found on the way

`ItemBase.AddAction` only accepts action classes registered by
`ActionConstructor.RegisterActions`; the box's Open/Close actions were not, so players
would never have seen the prompt (the error goes to the .RPT only). Fixed by
`OZS_ActionRegister.c`; the skill's actions reference now says so first.
