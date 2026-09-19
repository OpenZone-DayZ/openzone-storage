# Edge cases and load, 2026-09-19 evening

Stand: retail server, the bridge on the same machine (`STORAGE_XCHG_DIR` in the server profile),
no players unless said. Box 1 = `OZ_StorageBox_Large` `-1265842135--247164804--150452033-1044523530`
(214 roots / 265 entities after a rollback). The mixed box = `OZ_StorageBox_Large`
`-187857958-2017803604--1687751745--222091886`, filled by the probe with 18 classes of small
items (Apple, Pear, Plum, Matchbox, Battery9V, Chemlight_White, TunaCan, SodaCan_Cola, Rag,
BandageDressing, Nail, Ammo_762x39, Whetstone, Compass, Ammo_9x19, Paper, DuctTape; 60 of each
until the grid was full): **960 roots, 960 entities**, no nesting.

## Edge cases

| # | Scenario | Expected (design §3, §9, §13) | Seen |
|---|---|---|---|
| 1 | Rollback from the web while the box's cache file is held open by another process (the engine reading it) | the rollback lands; the cache drop fails silently; the next open rebuilds the cache | rollback -> version 31 in SQL, the cache file stayed; the open during the lock was refused in words (`the bridge refused: the exchange directory is not writable`), the box went back to CLOSED; after the lock the open gave the rolled-back content (214 roots) |
| 2 | Kill the server while the box is OPEN and the engine's save has it OPEN (an autosave with a player connected) | the engine's cargo is the truth: a boot close into a new version, no duplicate | boot: `OPEN with 214 entities, bridge says open -> closing it into a new version`; version 32 `boot`, 214 roots / 265 entities = the same content |
| 3 | Kill the server right after an open, the engine's save older than the open (no player, no autosave) | SQL wins, no empty version, the box CLOSED at the SQL version | boot: `CLOSED with 0 entities, bridge says open -> the engine's save predates the open; SQL wins, 214 stored`; no new version. **Defect found:** SQL kept the box `open` after the boot (the web refused every change with "the box is open"). Fixed in f22953a: the boot rule posts the `closed` the crash never sent; verified on the next boot (`v1/storage/closed ok`, SQL `closed`) |
| 4 | Bridge killed while a box is OPEN, then a close | the close is refused in words, the box stays OPEN, nothing deleted | `v1/storage/close failed, code 7 -> could not be closed: the bridge did not take the close: no answer (code 7); nothing deleted`; status OPEN, 214 entities intact |
| 5 | Bridge back after 4 | the gate reopens on the next successful call; the close then lands | the first close after the restart was refused by the gate (`#STR_OZ_ERR_NO_BRIDGE`, the poll had not succeeded yet); ten seconds later the close made version 33 |
| 6 | A close command arriving while the box is in a transition (auto-close in flight) | refused in words | `close refused: #STR_OZS_OPENING` (the key names OPENING for a CLOSING box too -- a wording nit) |
| 7 | Bridge not yet answering at boot | boxes unavailable, asked every 5 s, everything alive once it answers | every boot logged `the bridge is down ... asking every 5 s` once and answered on the next try |

Not exercised today: a kill during the close's own write (the window is ~150 ms; the rule is the
engine's save either way), a marker desync (covered by the unit tests of the bridge and the A2
stand run).

## Load, the mixed box (960 roots, 18 classes)

| Transition | Engine | Bridge |
|---|---|---|
| auto-close | written in 27 frames, 138 ms; deleted 960 in 20 frames, 52.9 ms; longest step 6 ms | ingest 111 ms |
| open | work 74.7 ms, longest step 3 ms, wall 2 s (500/s pace), missed 0, parked 0 | cache valid, no write |
| close (server) | written in 36 frames, 186 ms; deleted in 20 frames, 46 ms; longest step 6 ms | 63 ms |
| open | work 39.9 ms, longest step 2 ms, wall 2 s | -- |
| close (server) | written in 38 frames, 194 ms; deleted in 20 frames, 45 ms; longest step 6 ms | 62 ms |
| open | work 19.9 ms, longest step 2 ms, wall 2 s | -- |

Frame monitor (the probe's `baseline`) over one clean close + open: 305 010 frames, average
0.1 ms, **max 10 ms**, none over 100 ms. (Since the boot, including the probe's fills of 60
creates per frame: max 688 ms -- the fills, not the mod.)

Sampling profiler (`openzone-radio/tools/profiler/script-profile.ps1 -NoElevate -Seconds 90`,
`profiler-mixed-960.txt` beside this file) over the auto-close, an open, a close and an open:
main thread 17 199 samples, 73.0 % engine only, 20.6 % interpreting script; script time by mod:
vanilla 21.1 %, **OpenZone_Storage 2.59 %**, the probe 1.7 %, CF 0.7 %, VPP 0.3 %. Hottest of ours:
`OZS_Controller.OnFrame` 2.62 % inclusive, `OZS_OpenJob.Tick` 0.78 %, `OZS_Records.ReadRoot`
0.76 %, `OZS_Audit.Flush` 0.62 %, `OZS_CloseJob.Tick` 0.47 %. One stretch >= 150 ms reported:
191 ms of `Serializer.Write` -- the profiler's known merge of the close's consecutive 5 ms write
frames into one stretch (see `results-ten-boxes.md` of 09-17); the engine's own longest step in
that close was 6 ms and the frame monitor saw no frame over 10 ms.
