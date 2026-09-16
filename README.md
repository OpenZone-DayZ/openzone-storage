# OpenZone Storage

Virtual storage boxes for the OpenZone series: a box whose contents do not exist
in the game world while it is closed. Part of the [OpenZone](../README.md) family;
prefix `OZ`, runs with `OZ_Core` alone (and CF, which the core needs).

## Status: built and measured on the stand (2026-09-16), not published

Players get a box by deploying a **kit** from the hands (`OZ_StorageBoxKit_Small` /
`_Medium` / `_Large`): choose placement, turn the hologram with the wheel, hold the action
for ten seconds. An open box closes itself 120 s after it was opened (a plain timer that
waits while someone is looking). The inventory screen carries a **search bar** that shades
every item whose name does not contain the text, and a **Sort** button that lays the
open box out by name.

Three boxes -- `OZ_StorageBox_Small` (500 cells, 2 weapon slots, wooden crate model),
`OZ_StorageBox_Medium` (1000 cells, 4 slots, sea chest) and `OZ_StorageBox_Large`
(1500 cells, 6 slots, sea chest). Two verbs on the box, "Open the box (N)" and
"Close the box". While a box is open its cargo and slots are ordinary engine
inventory; a close writes everything into two files under
`$profile:OpenZone/Storage/<box id>/` and deletes the entities a few dozen per
frame; an open recreates them from the files at 250 per second, 5 ms of a frame
at most. Nothing is loaded on the main thread in one piece: the measured worst
frame of a full Large box cycle is 24 ms on the server and 67 ms on a client
standing next to four boxes opening at once.

The design and the decisions behind it: [docs/2026-09-16-storage-box-spec.md](docs/2026-09-16-storage-box-spec.md)
(section 13 is the code as built); the survey of existing implementations:
[docs/2026-09-16-virtual-storage-survey.md](docs/2026-09-16-virtual-storage-survey.md);
the numbers: [docs/measurements/2026-09-16/results-implementation.md](docs/measurements/2026-09-16/results-implementation.md).

| pbo | side | what |
|---|---|---|
| `OpenZone_Storage` | client + server | the boxes, the kits, the actions, the controller (jobs, viewers, auto-close, sort, boot rules), the store, the client viewer, the search bar |
| `OpenZone_Storage_Bridge` | server only, stand | the `oz_storage` verb for the MCP bridge: list, spawn, status, open, close, sort, files, slot, tune, lower |
| `OpenZone_StorageProbe` | client + server, stand | the measurement probe (crate, fill, inspect, blob round trip, frame monitors, the hologram diagnostic, the client control file) |
| `OpenZone_StorageProbe_Bridge` | server only, stand | the `oz_probe` verb |

Only the first pbo is meant for players; the other three are stand tooling.

## Files a server keeps

- `$profile:OpenZone/OZ_Storage.json` -- the settings, written with defaults on the first
  boot: `OpenFrameBudgetMs` 5, `OpenItemsPerSecond` 250, `CloseFrameBudgetMs` 5,
  `CloseDeletesPerFrame` 50, `AutoCloseSeconds` 120, `ViewerHeartbeatSeconds` 5, `ViewerTimeoutSeconds` 15, `ViewerMaxDistance` 5, `DebugLog`.
- `$profile:OpenZone/Storage/<box id>/items.bin` -- the store: one record per item with its
  `OnStoreSave` blob (every mod's state rides inside), children before parents, a trailer.
- `.../items.list` -- the same tree without blobs, one readable line per item; the fallback
  when the blob cannot be followed (a class removed by a mod update, a refused
  `OnStoreLoad`, a truncated file). Items restored from it keep type, place, health,
  quantity, liquid and magazine count and lose script state; the log says which.
- `.../items.bin.failed-<stamp>` -- a blob that broke, kept for a look.

The one-truth rule at boot: files present, they win; no files, the engine's own cargo of
the box is the truth and is written into files right away. A box that is open when the
server dies loses at most the last second (the engine autosaves every second while a
player is connected).

## Build and run

Through the `dayz` MCP server: `project_open` -> `mod_lint` -> `mod_build` -> `server_start`
-> `log_verdict` (ready line `storage loaded`) -> `world_ready` ->
`world_exec verb=oz_storage args={"op":"spawn","size":"large","pos":"x y z"}`.
The stand profile is `dayz-mcp.toml` plus the machine half `dayz-mcp.local.toml`
(retail server, so `tools/profiler/script-profile.ps1` from openzone-radio can read
the script VM). Both pbos are signed with the series key from `keys/`.

## Licence

CC BY-NC-SA 4.0 with the server-operator permission in `NOTICE`.
