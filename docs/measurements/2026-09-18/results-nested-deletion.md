# Nested containers: the stuck deletion (2026-09-18, stand)

Stand: retail server `E:\dayzmod\dayzserver-retail`, `@CF @VPPAdminTools @OpenZone_Core
@OpenZone_PDA @OpenZone_Radio @OpenZone_Storage @OpenZone_StorageProbe` plus the two bridge
pbos on the server. Box `OZ_StorageBox_Small` id `90918-165558-11-5860` at `4903 9.48 2460`.
Watcher: `oz_ghost watch pos="4903.5 9.5 2460.5" radius=6 types=PlateCarrierPouches,
SmallProtectorCase,FirstAidKit,BandageDressing`, every 0.5 s into
`$profile:OpenZone_StorageProbe/ghost.txt`; the client's view through
`tree` in `clientprofile/OpenZone_StorageProbe/control.txt`.

A zombie line reads `ZOMBIE PlateCarrierPouches #00 loc=0 W TODELETE PENDING SETDEL`:
network id 0, location UNKNOWN, still in the world's spatial index, `ToDelete()`,
`IsPendingDeletion()` and `IsSetForDeletion()` true, no juncture, no inventory lock.

## Runs, in order

| # | recipe | server | client |
|---|---|---|---|
| 1 | client builds pouch>case>kit>2 bandages from spawned parts, stashes, close | clean | clean |
| 2 | restore (old code), client grab, stash, close | clean | clean |
| 3 | restore (old code), client grab, drop, `world_delete` | **2 zombies** (pouch, case) | ghost |
| 4 | `redelete` bottomup / delete / remote on those | unchanged | -- |
| 5 | client-built chain, take, drop, `world_delete` | clean | clean |
| 6 | spawned empty pouch, server `put` into the box, grab, drop, delete | clean | clean |
| 7 | probe `chain` (same-frame build, root into the box), grab, drop, delete | **zombie** | ghost |
| 8 | probe `chain ground=1` (never in the box), take, drop, delete | clean | clean |
| 9 | probe `chain` two levels (pouch > empty case), grab, drop, delete | **zombie** | ghost |
| 10 | run 9 then server `put` back, close | **zombie** | ghost |
| 11 | probe `chain mode=tree` | **zombie** | -- |
| 12 | probe `chain mode=incargo` | refused by the engine at level 1 | -- |
| 13 | probe `chain flags=inventory` | **zombie** | -- |
| 14 | no client at all: probe `chain`, server `out`, delete | **zombie** | -- |
| 15 | all-client: nest, stash, grab, drop, delete | clean | clean |
| 16 | probe `chain ground=1`, client takes the **case** out of the pouch, drop, delete | **zombie** | -- |
| 17 | spawned case, server `put` into the box, grab, drop, delete | clean | -- |
| 18 | spawned pouch and case 20 s earlier, server `put` case into pouch, grab, drop, delete | clean | -- |
| 19 | `chain types=FirstAidKit leaves=2 ground=1`, client takes a bandage out, drop, delete | clean | -- |
| 20 | probe `chain mode=deferred delay=1`, `out`, delete | clean | -- |
| 21 | probe `chain mode=sync` on the same boot, `out`, delete | **2 zombies** | -- |
| 22 | run 21's tree, `deltree` deepest first | case gone, root **zombie** | -- |
| 23 | restore (frame-deferred moves), `out`, delete | clean | -- |
| 24 | restore (frame-deferred), client grab, stash, grab, drop, server `put`, close | clean | **ghost** |
| 25 | restore (frame-deferred), client grab, drop, delete | clean | **ghost** |
| 26 | probe `chain mode=deferred delay=10`, grab, drop, delete | clean | **ghost** |
| 27 | restore (local tree, published once), `out`, delete | clean | -- |
| 28 | restore (local tree), client grab, drop, delete | clean | clean |
| 29 | restore (local tree), grab, stash, grab, drop, take, stash, close | clean | clean |

Runs 3 and 14 are the two halves of the proof that neither the client nor the box is
needed; runs 16-18 pin the server-side trigger to a script move into a container created
in the same frame; run 19 clears `CreateEntityInCargo`; runs 20-21 give the one-frame cure
on the server; runs 24-26 show the client's own trigger, the SYNC_MOVE of a tree; runs
27-29 are the fix.

## Side facts

- A client joining the server freed every pending zombie at once (22:58:48); a restart
  persisted them as real items, the top two levels of each tree.
- The ghost copy on the client sits at the item's last ground position with location 0;
  a relog clears it; `NearestLoose` of the probe's client control targets it, so a test
  that follows a ghost run has to move the player or relog.
- `EntityAI.IsSetForDeletion()` is what makes such an item unpickable: `CanBeActionTarget`
  refuses it and the juncture is denied (`junctures.c`).
- An empty retail server runs its frames in about 1 ms, so a rate of 500 entities per
  second gives half a token per frame and an open of 5 entities takes ~40 frames; with a
  player connected it is 1-2 frames. Unchanged by the fix.
