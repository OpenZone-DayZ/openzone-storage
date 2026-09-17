# Virtual storage boxes: requirements and stand measurements

Date: 2026-09-16. Status: **research, item 0 of the brief** -- the box model is the owner's
decision; nothing of the mod itself exists yet. Everything below is either a fact from the
vanilla scripts, a fact from a profile of a running server, or a number from the stand;
guesses are marked as such.

*(Sections 1 and 2 of the original draft compared other people's storage mods and quoted
their sources. They were removed before this repository was published; the numbering of the
later sections is unchanged so the cross-references still hold.)*

## 0. Summary

- "Virtual storage" means one thing: **while a container is closed its contents do not exist
  as entities**; they live in a file and are re-created when the container is opened. The
  motivation is the network bubble: everything within about a kilometre of a player is
  replicated to that player, cargo included, so a base holding thousands of items floods
  every client that walks up to it.
- The owner's model -- two actions, Open creates the items in the box's real cargo, Close
  captures and deletes them -- is viable on the numbers, with two constraints the stand
  imposed:
  1. **One cargo grid holds at most 256 rows.** A 10-wide grid caps at 2560 one-slot items;
     a 20-wide grid at 5120. A "5000-item box" needs a 20-wide grid (unusual in the vanilla
     UI) or two boxes.
  2. **Materialisation must be paced.** Creating 5000 items in one server frame costs the
     server 0.36 s alone and 0.82 s with a client next to it; spread over frames it costs
     nothing visible. The client received 5000 items created beside it in about 3 s at
     1000--1700 items/s without frame drops in three of four attempts, and **once stood
     still for 39.2 s** in a single frame. Pacing at 250 items/s (the engine's own network
     bubble streams at ~310/s) produced no stall in any run.
- Recommendation: **model A with a capacity cap and paced materialisation** (called C in
  section 5), full item state through `OnStoreSave`/`OnStoreLoad`, nested containers filled
  before they enter the box, and a rule that refuses to restore a box twice. No native DLL
  and no SQL sidecar are needed for the measured costs; both stay as fallbacks (section 7).
- Open decisions for the owner are listed in section 6.

## 1. Why the storage in use had to be replaced

An eight-hour profile of the running server (2026-09-15/16) found 1462 of its 1585
main-thread freezes inside synchronous JSON loads made by the storage mod it used then:
entered from the per-frame update 1324 times, from the engine's save hook 93 times and from
the player command handler 45 times. A file was being read on the main thread every few
seconds rather than on the player's open action. The freezes ran 0.15 to 2.5 s, with 365 s
of freeze in the worst hour.

Reading itself is not slow: on the stand `JsonFileLoader.LoadFile` of a 5000-item, 870 KB
file takes 9--14 ms (section 4). Freezes of that length therefore mean files far larger than
one box, or many files per frame, or both. The detail matters less than the lesson the design
follows: **never read or write a store on the main thread in one piece, and never on a timer
-- only on the player's action, and spread over frames.**

## 3. Engine facts established on the stand

Retail `DayZServer_x64.exe` (2026-08-13 build), CF + MCP bridge + the probe, Chernarus,
no other mods; client = retail `DayZ_x64.exe` windowed 1600x900 in the background
(pauseMode 2, steady 52 ms frames = 19 fps).

| Fact | Number |
|---|---|
| Cargo grid ceiling | `itemsCargoSize[] = {10, 500}` is reported by `GetCargo().GetHeight()` as 500, but creation fails from **row 256**: 2560 cells at width 10. `{20, 256}` works: 5000 items created. |
| Server main loop | idle: 254 025 frames in 96 s (0.1--0.4 ms per frame); with one player still ~0.2 ms. There is no frame cap. |
| `MissionServer.OnUpdate(timeslice)` | **clamped at 0.3 s** by the engine; a frame with 724 ms of script work arrived as 300 ms. Frame lengths must be taken from `GetTickTime()` deltas. |
| `GetTickTime()` | advances inside a frame with sub-millisecond resolution; `TickCount(prev)` counts at 10 MHz (QPC). |
| Containers in cargo | `Container_Base.CanReceiveItemIntoCargo` returns false when `!GetInventory().AreChildrenAccessible()`; **all three creation calls** (`CreateEntityInCargo`, `CreateEntityInCargoEx`, `LocationCreateEntity` + `SetCargo`) returned null for items aimed at a bag lying in the crate's cargo: 0 of 1000, 0 of 500, 0 of 500. A nested container must be filled before it is placed into the box. |
| Weapons and magazines | live in `CfgWeapons` / `CfgMagazines`; `ConfigIsExisting("CfgVehicles AKM")` is false. |
| Network bubble entry | a client teleported to 4.6 m from a crate with 5000 items received them at **~310 items/s, 17.8--17.9 s** for all, client frames <= 77 ms, server frames <= 28 ms. Leaving the bubble deleted 5000 client entities in ~2 s. |
| Engine autosave | with a player connected the server rewrites `storage_1/data/dynamic_00N.bin` round-robin, one file per second, continuously. A crate with 5000 items created at runtime survived `taskkill` + restart with all 5000 items (they were autosaved). `dynamic_005.bin` grew to 1.16 MB against 0.1--0.36 MB for the others. |
| Vanilla login | one server frame of 1.8 s and one client frame of 1.7 s when a player connects -- the reference for "what a freeze looks like". |
| Profiler caveat | `script-profile.ps1` reports "engine, no script on the stack" stretches >= 150 ms on a near-idle stand that the frame monitor does not see (idle loop 91 % engine share makes 30 consecutive engine-only samples likely). Its script-attributed stretches match the frame monitor exactly. |

## 4. Measurements

Probe: `OpenZone_StorageProbe` (this repository). Items are `Paper` (1x1) unless stated.
"Script ms" is `GetTickTime()` around the creating loop; "frame ms" is the longest frame
during the job and 3 s after it, from `GetTickTime()` deltas in `MissionServer.OnUpdate`.
Every row is a JSON line in `docs/measurements/2026-09-16/results-run{1,2,3,4}.jsonl`; the
client's per-second lines are in `client-run{3,4}.txt` there, the profiler reports in
`profiler-*.txt`.

### 4.1 Server, everything in one frame

| Operation | n | Script ms | Frame ms | Note |
|---|---|---|---|---|
| create, explicit cell (`LocationCreateEntity`) | 100 | 13 | 36 | 10-wide crate |
| | 1000 | 114--128 | 114--128 | two crates, three runs |
| | 2500 | 380 | 381 | client 4.6 m away |
| | 5000 | 362 | 364 | no client |
| | 5000 | 816 | 837 | client 4.6 m away |
| create, engine finds the cell (`CreateEntityInCargo`) | 1000 | 109 | 110 | no penalty for the search |
| create AKM + Mag_AKM_30Rnd + AK_WoodBttstck + AK_WoodHndgrd | 150 rifles = 600 entities | 237 | 243 | 1.6 ms per rifle |
| create TaloonBag_Blue (children refused) | 50 / 100 | 79--86 / 170 | 79--88 / 171 | |
| delete (`ObjectDelete`) | 100 | 6 | 12 | |
| | 1000 | 39--64 | 48--74 | |
| | 2500--2560 | 155--177 | 181--203 | |
| | 5000 | 259 | 283 | no client |
| | 5000 | 379--383 | 440--445 | client 4.6 m away |
| | 600 (150 rifles) | 35 | 64 | |
| capture type/cell/health/quantity | 1000 / 2560 / 5000 | 2 / 3 / 19 | 11 / 10 / 43 | |
| `JsonSerializer.WriteToString` | 5000 | 7 | | 360 KB compact |
| `JsonFileLoader.SaveFile` | 5000 | 10 | | 870 KB on disk |
| compact list `type\|row\|col\|hp\|qty;` | 5000 | 5 | | 90 KB = what a list UI would send |
| `JsonFileLoader.LoadFile` | 2560 (444 KB) / 5000 (870 KB) | 8 / 9--14 | 10 / 11--16 | the call that freezes the live server |

The profiler saw the 5000-in-one-frame creation as one stretch of 364 ms: "OZ_Probe.FillOne
39 %, Object.ConfigGetInt 9 %, EntityAI.EntityAI 6 %". Per item the engine spends
0.07--0.15 ms creating a one-slot item and 0.4 ms per entity of a rifle with attachments;
a second nearby client roughly doubles the creation cost (replication registration).

### 4.2 Server, spread over frames

| Operation | n | Batch | Gap | Total script ms | Longest batch | Longest frame | Wall |
|---|---|---|---|---|---|---|---|
| restore (create + `SetHealth01` + `SetQuantity`) | 5000 | 50 per frame | 0 | 806--916 | 61--98 ms | 63--98 ms | ~1 s |
| create | 5000 | 50 per frame | 0 | 903 | 51 | 69 | ~1 s |
| create, paced | 5000 | 25 | 100 ms | 834 | 59 | 60 | 23 s |
| delete | 5000 | 50 per frame | 0 | 353--463 | 6--8 | 7--13 | ~0.5 s |

Nothing above 100 ms in any frame once the work is split; 50 one-slot items per frame is
already the upper end for the acceptance criterion (<= 150 ms) on a server with 80 mods,
where the per-item cost will be higher than on this clean stand. A time budget per frame
(e.g. stop the batch after 20 ms) is the robust form.

### 4.3 Client

| Event | Items | Duration | Rate | Client longest frame | Server longest frame |
|---|---|---|---|---|---|
| enter the bubble at a full crate (teleport to 4.6 m) | 5000 | 17.8--17.9 s | ~310/s | 73--77 ms | 26--28 ms |
| leave the bubble | 5000 deletes | ~2 s | ~2500/s | 62 ms | -- |
| items created in one server frame beside the client | 1000 | 1.1 s | ~900/s | 66 ms | 167 ms |
| | 2500 | 2.2 s | ~1100/s | 63 ms | 381 ms |
| | 5000 | 3.0 s | ~1700/s | 64 ms | 837 ms |
| restore 50 per frame beside the client, 1st attempt | 5000 | **one frame of 39.2 s**, all 5000 arrived after it | -- | **39 187 ms** | 88 ms |
| restore 50 per frame beside the client, 2nd attempt | 5000 | 3.2 s | ~1500/s | 65 ms | 63 ms |
| create paced 25 per 100 ms beside the client | 5000 | 25 s | 250/s | 76 ms | 60 ms |
| delete 50 per frame beside the client | 5000 | 2 s | | 61 ms | 13 ms |

The 39 s stall did not reproduce in two further runs of equal or bigger bursts; the client
RPT logged `[ErrorModuleHandler] :: Error thrown: 0x00040004` a minute after it and
nothing else. The cause is not established (a lost reliable packet inside a 5000-message
burst is the plausible guess). It is reported as observed: one stall in four bursts of
2500--5000 creations, none in paced or bubble-streamed delivery.

### 4.4 Memory and persistence

- Server private bytes with one player and 5000 probe items: 5.11 GB. No same-process
  baseline without the items was taken; 5000 `Paper` entities are not what moves this number.
- The engine autosaved the probe crate's 5000 cargo items while the player was connected
  and restored them on the next boot; boot time was unchanged (22.7 s against 18--25 s for
  the other boots of the day). Item 4 of the brief (double truth of an open box) is
  therefore real and starts within a minute of opening.

## 5. Models compared

| | A. Open = all items in the real cargo (owner's hypothesis) | B. Own list window, only the withdrawn item is created | C. A with a capacity cap and paced materialisation | D. Native DLL patch of the engine |
|---|---|---|---|---|
| Looks like a normal box | yes | no: a custom window, every interaction re-implemented | yes | yes |
| Server, open a 5000-item box | 0.36 s in one frame (0.82 s with a player near) -- a visible freeze | ~0: one list RPC of 90 KB in ~90 chunks | 0.8--0.9 s of work spread over frames, each <= 60--100 ms; 3--25 s wall | -- |
| Client, open | 3 s of streaming, once a 39 s stall | negligible | 25 s at 250/s, no stall in any run; ~3 s for a 2560-cell box at 1000/s | -- |
| Approaching a closed base | nothing to send | nothing | nothing | filter cargo of closed boxes in the replication path -- entities still exist |
| Server save / memory while closed | none: items are not entities | none | none | items stay in `storage_1` and in memory unless the DLL also patches the save |
| Composite and nested items | attachments fine; nested containers must be filled before insertion | list must render trees; withdraw of a filled bag = tree creation | same as A | vanilla behaviour |
| Persistence hazards | engine autosaves the open box (double truth), same as every mod in section 2 | none from the engine | same as A, with the dupe rules below | -- |
| Item state fidelity | `OnStoreSave` blob: full, mods included | same blob, but the list shows only what the descriptor carries | same as A | native |
| Box capacity | 2560 cells at width 10, 5120 at width 20 | unlimited | same as A | vanilla |
| Work | medium: actions, store, journal, restore, boot rules | high: UI + drag/drop/split/attach/hands re-done | medium, plus pacing and lid animation on completion | weeks of reverse engineering; re-done after every game update; the frequency proxy (`hid.dll`) shows it is possible |

## 6. Recommendation and the decisions that are the owner's

**Build C**: the owner's two-action box, real cargo, with these rules.

1. **Capacity.** One box = one cargo grid <= 256 rows. At the vanilla UI width of 10 that is
   2560 one-slot items (a rifle takes 24 cells, so a full box of rifles is ~100 rifles with
   attachments = ~400 entities). "Thousands of items on a base" = several boxes, each opened
   on its own. A 20-wide grid (5120 cells) is possible but untested in the inventory UI.
2. **Paced open.** Create items over frames with a time budget (<= 20 ms per frame) and a
   rate cap towards nearby clients (250/s to start with, configurable); the lid animates
   open when the last item exists. A full 2560-cell box opens in ~10 s at 250/s; a typical
   box of a few hundred items in under 2 s.
3. **State.** Capture each entity in full: location, weapon chambers, `OnStoreSave`
   blob with `g_Game.SaveVersion()`, cartridges, health per damage zone -- into a
   `FileSerializer` stream, not field-by-field JSON. That is what carries CF_ModStorage and
   the radio's frequency. Fallback when `OnStoreLoad` fails after a game update: keep the
   type and location, drop the blob, log it (item 3 of the brief).
4. **Nested containers.** Restore order is children-first *for containers in cargo*: create
   the bag at a temporary location, fill it, then move it into its cell. Or forbid bags with
   contents in boxes -- the owner's call (below).
5. **Double truth (item 4).** The engine autosaves an open box within a minute. Rule: while
   open, the engine is the truth; Close writes the store and deletes the entities; on boot a
   box that comes back open with items is closed into the store *at mission start*, and a
   box that has both a store record and items in cargo keeps the cargo and discards the
   record (the dupe rule). Proven by a restart test once the code
   exists.
6. **Disk.** File writes are cheap (10 ms for 5000 items); the live server's freezes come
   from reading per frame, not from file size. One file per box, snapshot on close plus an
   append journal for changes while open, `.new` + `CopyFile` for atomicity, read only on
   the Open action and at mission start. The SQL sidecar (phase 2) is not needed for these
   numbers.
7. **No DLL now** -- section 7.

Decisions for the owner:

- Grid width 10 (vanilla look, 2560 cells) or 20 (5120 cells, UI to be checked)?
- Pacing target: 250 items/s (safe, 10 s for a full box) or ~1000/s (3 s, one stall seen)?
- Nested containers with contents inside boxes: supported (children-first restore) or refused?
- Boot rule for an open box found in engine storage: close it into the store (proposed) or
  leave it open?

**Decided by the owner 2026-09-16:** model C, three boxes of 500 / 1000 / 1500 cells at
width 10, weapon slots on the box. The remaining choices moved to
`2026-09-16-storage-box-spec.md`, section 11.

## 7. The native DLL option (owner's question)

A DLL cannot be *called* from Enforce Script; there is no FFI. The frequency proxy works by
patching engine bytes at load time, and that is the only shape a storage DLL could take:

- **D1, replication filter.** Hook the code that enumerates an entity's children for a
  client's bubble and skip the cargo of a closed box. Items keep existing on the server, so
  the engine's save, memory and CE stay as they are; only the approach burst disappears.
  Needs the replication path located in `DayZServer_x64.exe` (not done; the profiler work
  located VoN and the script VM, not networking), a per-entity flag reachable from native
  code, and a re-read after every game update.
- **D2, storage backend.** Hook a rarely used script native (a file function on a magic
  path) so script can hand a blob to native code that writes SQLite on its own thread and
  answers on a later frame. `RestApi` already gives asynchronous GET/POST to a sidecar
  without any patch, so D2 buys nothing.
- **D3, engine save patch.** Move `dynamic_*.bin` writes off the main thread. The stand
  shows no save cost worth it at this scale; the profile attributes the freezes to the
  previous mod's JSON reads, not to the engine save.

None of the measured costs needs D1--D3. D1 is the one to revisit if live tests show the
client stall of 4.3 recurring with paced materialisation, or if the owner wants boxes far
above 2560 items. BattlEye is not the obstacle (server-side only); the reverse engineering
and the per-update maintenance are.

## 8. Composite items and containers with their own storage (owner's note)

- Attachments (magazine, stock, handguard, optics) are created on the parent after it exists
  (`CreateAttachment`); 150 rifles with three attachments each cost 237 ms in one frame,
  0.4 ms per entity. Magazines carry cartridges (`GetCartridgeAtIndex`, `ServerStoreCartridge`)
  and weapons carry chambers (`GetCartridgeInfo`, `PushCartridgeToChamber`); run-length
  encoding of cartridges is the compact form.
- Containers inside the box (bags, cases, ammo boxes) cannot receive children while they
  lie in cargo -- section 3. The store must be a tree and the restore must fill a container
  before placing it. Restoring through `CreateEntityInCargoEx` with a retry after
  `UnlockInventory(HIDE_INV_FROM_SCRIPT)` addresses locked inventories, not this rule.
- Every child is an entity and counts against the pacing budget: a box of 100 filled bags
  with 10 items each is 1100 entities to create, not 100.

## 9. How to repeat

```
project_open E:/openzone/openzone-storage   -> mod_build -> server_start -> world_ready
world_exec oz_probe {"op":"crate","pos":"7000 0 7000","size":"wide"}     # 20x256 grid
world_exec oz_probe {"op":"fill","item":"Paper","n":"5000","batch":"25","gap_ms":"100"}
world_exec oz_probe {"op":"status"}       # or read $profile:OpenZone_StorageProbe/results.log
world_exec oz_probe {"op":"capture"}  {"op":"load"}  {"op":"restore","batch":"50"}  {"op":"clear","batch":"0"}
```

`size` = big (10x500), small (10x100), wide (20x256), huge (50x256), square (100x100);
`mode` = loc | find | ex; `attach` and `cargo` take comma-separated class names,
`cargo_mode` = find | ex | loc. The client writes
`clientprofile/OpenZone_StorageProbe/client.log` (per-second item counts and frame
statistics, burst summaries). The profiler:
`powershell -File openzone-radio/tools/profiler/script-profile.ps1 -ProcessIdToSample <pid> -Seconds 40 -ThreadsHz 0 -Out <prefix> -NoElevate`.

## 10. Sources

- The eight-hour profile of the running server, 2026-09-15/16, taken with
  `openzone-radio/tools/profiler/script-profile.ps1`.
- Vanilla scripts of the 2026-08-12 build (`dta/scripts.pbo`): `inventory.c`,
  `inventorylocation.c`, `cargo.c`, `container_base.c`, `entityai.c`, `centraleconomy.c`.
- The stand runs of this survey: `docs/measurements/2026-09-16/`.
