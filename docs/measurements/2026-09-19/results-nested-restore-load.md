# The local-tree restore under a box full of nested items (2026-09-19, stand)

Question from the owner: how much load does the fix of section 21 add? Answer: none --
the new restore does less script work than the old one on the same content, because a
LOCAL move serialises no SYNC_MOVE and one `RemoteObjectTreeCreate` per root replaces the
create-plus-move traffic of every entity under it.

## Content

`OZ_StorageBox_Large` (10 x 150 cells), filled by the probe with `chain n=100`: 100 x
`PlateCarrierPouches[SmallProtectorCase[FirstAidKit(2 BandageDressing)]]` = 100 roots,
500 entities, 300 nested containers to move plus 100 roots into the box. A pouch takes 15
cells, so 100 is the box's capacity (150 asked, 50 stayed on the ground). Retail server
2026-08-13, `@CF @VPPAdminTools @OpenZone_Core @OpenZone_PDA @OpenZone_Radio
@OpenZone_Storage @OpenZone_StorageProbe`, the probe bridge pbos on the server. The same
files were restored by both versions of the code; the "old" run is commit `d948877`'s
`OZS_Records.c`, `OZS_Jobs.c`, `OZS_ListFallback.c` checked out over the new tree and
built, the "new" runs are `8ee56e9`.

## The open (restore of 100 roots / 500 entities, rate 500 entities per second)

| run | clients | frames | script work | longest step | wall | server frame max during the open | missed |
|---|---|---|---|---|---|---|---|
| old (`TakeToDst SERVER` in the frame of creation) | 0 | 1962 | 138.1 ms | 4 ms | 1 s | 29 ms | 0 |
| new (local tree, moves next frame, published once) | 0 | 2553 | 105 ms | 3 ms | 1 s | 29 ms | 0 |
| new, a client standing at the box | 1 | 1534 | 119 ms | 2 ms | 1 s | 22 ms | 0 |

The frame counts are the token pacing at an idle server's ~1 ms frames (0.5 tokens per
frame) and mean nothing for a live server; with the client connected the frames are
~20 ms and the job spans the same second. "Script work" is the sum of the job's own
steps as `OZS_OpenJob` accounts them; "server frame max" is the probe's in-engine
frame monitor over the whole open (`oz_probe baseline`), reset before each run. The
profiler (`script-profile.ps1 -Seconds 60`) put `OpenZone_Storage` at 0.90 % of the
main thread in the old window, 1.17 % in the new window with the client standing at the
box (the client's viewer traffic included), and the open job's functions at 0.24-0.28 %
inclusive in both; its ">= 150 ms stretches" were engine-only with no script on the stack in both
windows, the idle-server merge artefact known from 2026-09-16.

The client that stood at the box received the published trees whole: its own tree walk
after the open lists the box with 501 lines, 100 pouches, 100 cases, 100 kits, 200
bandages, nothing with location 0.

## The close (unchanged code, for the record)

100 items (500 entities) written in 14-15 frames, 78-83 ms, commit 3-4 ms; deleted 500
entities in 10 frames, 26-34 ms, longest step 6 ms. No zombie after any close.

## Side finding: a box spawned on a stand without players does not survive a restart

`oz_storage spawn` creates the box, but the server saves the world only with players on
it (or on a clean shutdown), and `server_stop` is a kill: the first two Large boxes of
this session were gone after the restart, their store directories left behind as orphans
(`90918-220635-7-4599`, `90918-221108-7-1990`). The Small box of 2026-09-18 survived
because the owner's client had been connected while it existed. Nothing to fix in the
mod; a note for the stand.
