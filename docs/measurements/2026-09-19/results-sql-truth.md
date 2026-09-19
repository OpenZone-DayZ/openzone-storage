# SQL as the truth on the stand (2026-09-19, retail server, bridge beside it)

The stand of the earlier files, with `openzone-bridge` running on the same
machine (`STORAGE_XCHG_DIR` at the server profile's `OpenZone/Storage/xchg`).
Close = write the wire file at the 5 ms per frame budget, tell the bridge,
delete after its answer; open = ask the bridge, read the cache it names at 500
entities per second. "Work" is the job's own accounting of its steps, "wait"
the time from the close request to the bridge's answer, "wall" from the open
request to the last root standing.

## The same content as the earlier versions of the store

| box | roots / entities | close, write | close, bridge answer | close, delete | open, work | open, longest step | open, wall |
|---|---|---|---|---|---|---|---|
| Large, 200 flat Paper (boot close) | 200 / 200 | 45 ms, 9 frames | 57 ms | 20 ms, 4 frames | 9 ms | 3 ms | 0.4 s |
| Large, 200 Paper + 10 nested chains + AKM in a slot | 211 / 262 | 56 ms, 11 frames | 85 ms | 19 ms, 6 frames | 12 ms | 1 ms | 0.6 s |
| Large, 1272 roots (1261 Paper, 10 chains, AKM) | 1272 / 1323 | 305 ms, 57 frames | 103 ms | 78 ms, 27 frames | 48 ms | 2 ms | 2.7 s |
| Large, 100 nested chains (pouch, case, ammo box, 3 Paper) | 100 / 600 | 115 ms, 21 frames | 38 ms | 34 ms, 12 frames | 115 ms | 2 ms | 1.3 s |

For comparison, the single-file version 1 of 2026-09-17/19: 1443 flat roots
closed in 283 ms and opened with 20 ms of work; 100 nested chains of 500
entities closed in 78-83 ms and opened with 105-119 ms. Version 2 (one file
per root, rejected): 1108 ms and 196 ms. The wire of version 3 writes the
descriptor of every node in front of the bodies, which is the extra cost per
entity at a close; the open's work per entity is the same as before. The
longest step never exceeded 6 ms (a delete step) in any run.

## What survived the round trip

- `inspect` of 210 cargo items before the close and after the open through the
  bridge: hash -2127083125 both times.
- An AKM in the box's weapon slot with `Mag_AKM_30Rnd` at 17 rounds and a
  chambered `Ammo_762x39`: in SQL as `loc_type 2` with the slot id, ammo 17;
  back in the slot after the open (the probe's tree dump).
- 200 unchanged Paper roots between two closes: 411 root references, 211
  blobs -- the bridge deduplicated them by hash.

## The bridge away

- Killed during an open box's life: a close ends `no answer (code 7)`, the
  file is deleted, the box stays open with everything in it.
- A minute later (the core's client counts the bridge dead after 60 s without
  an answer): `close refused: #STR_OZ_ERR_NO_BRIDGE`, the gate.
- The bridge back: the client reconnected after 23 s of backoff, and the idle
  close that had been waiting went through by itself (`closing by auto-close`,
  version 3). No server restart.

## A class that vanished and came back

`OZ_ProbeToken`, an item only the stand's probe pbo declares, closed into the
box (version 7, 212 roots, row 35). Booted without the probe pbos: `1 stored
class(es) do not exist on this server: OZ_ProbeToken`, the bridge parked the
root (version 8, 211 roots), the box opened with 211 items. Booted with the
probe again: `the bridge parked 0 root(s) and returned 1`, version 10 with 212
roots, the token at row -1 / col -1 in SQL, the box opened with 212 items --
the engine found it a free cell. Earlier the same night the check had parked
the AKM and its magazine because `ConfigIsExisting` was asked about
`CfgVehicles` only; weapons live in `CfgWeapons`, magazines in `CfgMagazines`.

## The boot rules

- A box the engine saved OPEN with cargo boots as the engine's truth: closed
  into a new version at boot (`closing by boot`), in 57 ms + 129 ms of bridge
  answer for 212 roots.
- A kill during an open (rate lowered to 100/s for a 12 s window) rolled the
  world back to the last save, which is how a stand without players behaves
  (no autosave): the box came back OPEN with the 212 entities of that save
  while SQL held a newer version of 1272 roots. The engine won, as the rule
  says: the players' inventories of that save belong to the same moment, and
  the newer version stays in history for a rollback. On the live server the
  world autosaves every minute, so the engine's save is never far behind.
- The classes check waits for the boot closes (`the classes check waits for 1
  boot close(s)`): the bridge returns a parked root only into a closed box.

## Engine facts measured on the way

- `local` is a reserved word of Enforce Script: a class field named `local`
  is a syntax error at the class line; a field named `type` compiles.
- `GetYearMonthDayUTC` printed the year as one digit in the close file's name
  on this build while the same call inside the header stamp read 2026; the
  file stamp is now derived from the header stamp by dropping its punctuation.
- `GetPersistentID` is valid the frame `CreateObjectEx` returns and identical
  after a graceful stop and a boot; `GetID` changes every boot.
