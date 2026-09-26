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
grants a kit. A box is shown to whoever presses its one action, and the session ends when
the last of them closes the screen, walks off, leaves or dies -- nothing about it is on a
timer. The inventory screen carries a **search bar** that shades
every item whose name does not contain the text and keeps a container lit if anything
inside it matches, however deep; a **Sort** button that lays the open box out by name; and
a **Count** button beside it that lists what the open box holds, total and by class. Two
stacks either side of the box's edge merge the way vanilla's do (drag one onto the other,
from a pocket or off the ground); a loose item dropped on an item in the box changes places
with it; and **Alt+click** moves an item from the box into the inventory, or from the
inventory (the hands included) into the box.

### The personal stash

An admin places a **locker** (`OZ_StashAnchor`) -- a placed item like a box, saved with the
world and renewed to 45 days of lifetime on every boot, wearing the vanilla locker model.
A player presses F on it and is shown a
record of their own, keyed by which locker and whose -- the same player at two lockers has
two stashes, and one locker holds one per player. It carries the character's own slots
(headgear, mask, eyewear, body, vest, back, hips, legs, feet, gloves, armband, shoulder,
melee) plus four of the box's weapon slots, so a whole kit can be hung up rather than
piled into a grid. Clothing kept in those slots keeps working pockets, which vanilla
otherwise refuses to anything that is not a person.

Nothing stands in the world for it: the record goes to that one player's screen and
nowhere else, and the pairing of locker and player is made on the server from who sent
the message, so another player at the same locker cannot name it. What the player does
with it is written to the database turn by turn, like a box.

Still to come: the ten-at-once concurrency run.

Three boxes -- `OZ_StorageBox_Small` (250 cells, 2 weapon slots, wooden crate model),
`OZ_StorageBox_Medium` (500 cells, 4 slots, sea chest) and `OZ_StorageBox_Large`
(1000 cells, 6 slots, sea chest). One verb on the box, "Show the box (N)": the record is
read into an unannounced container of the same class at 500 entities per second, 5 ms of a
frame at most, and streamed to the player's screen in chunks; every drag is one operation
the server performs and writes to the database before the next (see Where a closed box
lives, below). Nothing is loaded on the main thread in one piece: the measured worst
server frame across these jobs is 44 ms, with ten boxes (10768 entities) filling at once
and a client standing among them.

The design and the decisions behind it: [docs/2026-09-16-storage-box-spec.md](docs/2026-09-16-storage-box-spec.md)
(section 13 is the code as first built, section 24 the move to the bridge's SQL as the
truth of a closed box); the survey of existing implementations:
[docs/2026-09-16-virtual-storage-survey.md](docs/2026-09-16-virtual-storage-survey.md);
the numbers: [docs/measurements/2026-09-16/results-implementation.md](docs/measurements/2026-09-16/results-implementation.md)
and the later runs in [docs/measurements/](docs/measurements/), by date.

| pbo | side | what |
|---|---|---|
| `OpenZone_Storage` | client + server | the boxes, the kits, the one action, the proxy (authority, sessions, mirrors, operations, per-turn commits), the fill job, the boot rules, the search bar |
| `OpenZone_Storage_Bridge` | server only, stand | the `oz_storage` verb for the MCP bridge: list, spawn, status, files, tune, lower, and `proxy`/`auth` -- a session driven without a screen |
| `OpenZone_StorageProbe` | client + server, stand | the measurement probe (crate, fill, inspect, blob round trip, frame monitors, the hologram diagnostic, the client control file) |
| `OpenZone_StorageProbe_Bridge` | server only, stand | the `oz_probe` verb |

Only the first pbo is meant for players; the other three are stand tooling.

## Where a closed box lives

A box's contents are never on this server's disk and never in the placed box -- the truth
is the OpenZone bridge's SQLite ([openzone-bridge](https://github.com/covalschi/openzone-bridge),
a separate Node.js process the server must run; not the MCP bridge in the table above).
While somebody looks into a box its contents stand in an **authority**: a real container of
the same class that nobody is told about and nothing saves, written to SQL turn by turn.
The player's screen shows a client-local **proxy** of it, and every drag is an operation
the server performs and answers. The placed box is the anchor a player walks up to; it is
always closed and empty.

- `$profile:OpenZone/OZ_Storage.json` -- the settings, written with defaults on the first
  boot: `OpenFrameBudgetMs` 5, `OpenItemsPerSecond` 500, `ProxyRowsPerMessage` 40,
  `ProxyMessagesPerFrame` 4, `ProxyIdleSeconds` 20, `WaitForRecord` (off: the item is
  handed over before the bridge has confirmed the turn; on: one round trip per take-out
  and nothing can be duplicated), `FakePingMs` (stand only), `DebugLog`.
- `$profile:OpenZone/Storage/xchg/<box id>-<stamp>-<serial>.bin` -- the wire: one file per
  turn, handed to the bridge by name and read into SQL; and a per-box cache, `<box id>.bin`
  in the same folder, that the bridge alone writes and deletes and the engine reads when
  it fills a box. Neither file is a record of anything once the bridge has answered.

**The bridge is mandatory.** A box cannot be shown (`#STR_OZ_ERR_NO_BRIDGE`) while the
bridge is unreachable or before the boot check has answered, and a session whose bridge
goes away ends: the record stands at the last confirmed turn, which is also what a crash
costs. A session is held to the player -- within six metres of the box, or it ends as if
the screen had been closed. At boot the engine and the bridge reconcile: a box SQL still
believes open is a session the last run never ended and is closed on its record, a box
held open by another server on the same bridge is refused here, and a class the bridge
remembers that no longer exists in `CfgVehicles`/`CfgWeapons`/`CfgMagazines` gets its
root parked until the class comes back.
Full protocol: section 24 of [the spec](docs/2026-09-16-storage-box-spec.md).

Admins also get a web page from the bridge itself: box lists, contents, history and a
rollback, all read from SQL or edited in it for the box's next open; three live commands
to the running game -- close, remove, report. Sign-in is optional, Discord OAuth if the
bridge sets `ADMIN_URL`. See openzone-bridge's own README.

## Requirements

- [Community Framework](https://steamcommunity.com/sharedfiles/filedetails/?id=1559212036)
- [OpenZone Core](https://steamcommunity.com/sharedfiles/filedetails/?id=3798432022)
- **On the server, as a separate process (not a PBO):**
  [openzone-bridge](https://github.com/OpenZone-DayZ/openzone-bridge) **0.5.0 or newer**,
  reachable and configured with `STORAGE_XCHG_DIR` pointed at this server's
  `profiles/OpenZone/Storage/xchg` (this repo's `$profile:OpenZone/Storage/xchg`).
  An older bridge does not know the personal stash's key and turns every stash away with
  `bad box id`; the boxes themselves keep working.

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
