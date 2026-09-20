# OpenZone Storage

[**Steam Workshop**](https://steamcommunity.com/sharedfiles/filedetails/?id=3803455084)

Virtual storage boxes for the OpenZone series: a box whose contents do not exist
in the game world while it is closed. Part of the OpenZone family of DayZ mods;
prefix `OZ`, runs with `OZ_Core` alone (and CF, which the core needs); the server
also needs the OpenZone bridge running -- see Requirements.

## Status: on the Steam Workshop, built and measured on the stand

Players get a box by deploying a **kit** from the hands (`OZ_StorageBoxKit_Small` /
`_Medium` / `_Large`): choose placement, turn the hologram with the wheel, hold the action
for ten seconds. Kits are not in the shipped economy --
[packaging/OpenZone_Storage/types-example.xml](packaging/OpenZone_Storage/types-example.xml)
lists all six classes at nominal 0 for a mission's own `types.xml`; an admin spawns or
grants a kit. An open box closes itself 120 s after it was opened (a plain timer that
waits while someone is looking). The inventory screen carries a **search bar** that shades
every item whose name does not contain the text and keeps a container lit if anything
inside it matches, however deep; a **Sort** button that lays the open box out by name; and
a **Count** button beside it that lists what the open box holds, total and by class.

Three boxes -- `OZ_StorageBox_Small` (250 cells, 2 weapon slots, wooden crate model),
`OZ_StorageBox_Medium` (500 cells, 4 slots, sea chest) and `OZ_StorageBox_Large`
(1000 cells, 6 slots, sea chest). Two verbs on the box, "Open the box (N)" and
"Close the box". While a box is open its cargo and slots are ordinary engine
inventory; a close writes everything into one file under
`$profile:OpenZone/Storage/xchg/` and deletes the entities a few dozen per
frame; an open reads them back at 500 per second, 5 ms of a frame at most (see
Where a closed box lives, below). Nothing is loaded on the main thread in one
piece: the measured worst server frame across these jobs is 44 ms, with ten
boxes (10768 entities) opening at once and a client standing among them.

The design and the decisions behind it: [docs/2026-09-16-storage-box-spec.md](docs/2026-09-16-storage-box-spec.md)
(section 13 is the code as first built, section 24 the move to the bridge's SQL as the
truth of a closed box); the survey of existing implementations:
[docs/2026-09-16-virtual-storage-survey.md](docs/2026-09-16-virtual-storage-survey.md);
the numbers: [docs/measurements/2026-09-16/results-implementation.md](docs/measurements/2026-09-16/results-implementation.md)
and the later runs in [docs/measurements/](docs/measurements/), by date.

| pbo | side | what |
|---|---|---|
| `OpenZone_Storage` | client + server | the boxes, the kits, the actions, the controller (jobs, viewers, auto-close, sort, boot rules), the store, the client viewer, the search bar |
| `OpenZone_Storage_Bridge` | server only, stand | the `oz_storage` verb for the MCP bridge: list, spawn, status, open, close, open_all, close_all, sort, files, slot, tune, lower |
| `OpenZone_StorageProbe` | client + server, stand | the measurement probe (crate, fill, inspect, blob round trip, frame monitors, the hologram diagnostic, the client control file) |
| `OpenZone_StorageProbe_Bridge` | server only, stand | the `oz_probe` verb |

Only the first pbo is meant for players; the other three are stand tooling.

## Where a closed box lives

A **closed** box's contents are not on this server's disk -- the truth is the OpenZone
bridge's SQLite ([openzone-bridge](https://github.com/covalschi/openzone-bridge), a
separate Node.js process the server must run; not the MCP bridge in the table above). An
**open** box's truth is its ordinary engine cargo, the same as any container.

- `$profile:OpenZone/OZ_Storage.json` -- the settings, written with defaults on the first
  boot: `OpenFrameBudgetMs` 5, `OpenItemsPerSecond` 500, `CloseFrameBudgetMs` 5,
  `CloseDeletesPerFrame` 50, `AutoCloseSeconds` 120, `ViewerHeartbeatSeconds` 5, `ViewerTimeoutSeconds` 15, `ViewerMaxDistance` 5, `DebugLog`.
- `$profile:OpenZone/Storage/xchg/<box id>-<stamp>.bin` -- the wire: one file per close,
  handed to the bridge by name and folded into a per-box cache, `<box id>.bin` in the same
  folder, that the bridge alone writes and deletes. The engine only reads that cache, on an
  open; neither file is a record of anything once the bridge has answered.

**The bridge is mandatory.** Open, Close and Sort are refused (`#STR_OZ_ERR_NO_BRIDGE`)
while it is unreachable or before the boot check has answered; idle auto-close waits for it
instead of firing, and the server never closes a box on its own shutdown (there is no
round trip to wait in) -- an open box that goes down with the server loses at most the
last second, the same as any container (the engine autosaves every second while a player
is connected). At boot the engine and the bridge reconcile: SQL wins over a half-finished
transition, an open box the engine still has cargo for is the engine's truth and closes
into a new version, and a class the bridge remembers that no longer exists in
`CfgVehicles`/`CfgWeapons`/`CfgMagazines` gets its root parked until the class comes back.
Full protocol: section 24 of [the spec](docs/2026-09-16-storage-box-spec.md).

Admins also get a web page from the bridge itself: box lists, contents, history and a
rollback, all read from SQL or edited in it for the box's next open; three live commands
to the running game -- close, remove, report. Sign-in is optional, Discord OAuth if the
bridge sets `ADMIN_URL`. See openzone-bridge's own README.

## Requirements

- [Community Framework](https://steamcommunity.com/sharedfiles/filedetails/?id=1559212036)
- [OpenZone Core](https://steamcommunity.com/sharedfiles/filedetails/?id=3798432022)
- **On the server, as a separate process (not a PBO):**
  [openzone-bridge](https://github.com/covalschi/openzone-bridge), reachable and
  configured with `STORAGE_XCHG_DIR` pointed at this server's
  `profiles/OpenZone/Storage/xchg` (this repo's `$profile:OpenZone/Storage/xchg`).

## Build and run

Through the `dayz` MCP server: `project_open` -> `mod_lint` -> `mod_build` -> `server_start`
-> `log_verdict` (ready line `storage loaded`) -> `world_ready` ->
`world_exec verb=oz_storage args={"op":"spawn","size":"large","pos":"x y z"}`.
The stand profile is `dayz-mcp.toml` plus the machine half `dayz-mcp.local.toml`
(retail server, so `tools/profiler/script-profile.ps1` from openzone-radio can read
the script VM). All four pbos are signed with the series key from `keys/` during
`mod_build`.

To publish: `mod_build`, then `.\package.ps1` assembles each `@Mod` folder from
`packaging/` (`mod.cpp`, `meta.cpp`, the signing key; `-Check` reports what is stale
without changing anything), then the Workshop.

## Licence

CC BY-NC-SA 4.0 with the server-operator permission in `NOTICE`.
