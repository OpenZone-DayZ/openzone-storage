# Vanilla containers full of items: what a client and the server pay as a player walks up

Measured 2026-09-30 on the storage stand: retail `DayZServer_x64.exe` (build 2026-08-13),
CF + VPP + OpenZone Core/PDA + OpenZone Storage + the probe pbos, one retail client windowed
in the BACKGROUND (the engine draws it at 30 fps there), nobody else online. The owner's
question: how much do full vanilla containers cost the server and a client, and at what
distance from them does it start.

## Setup

- **Containers.** The request was "barrels of 500 slots". A vanilla barrel is 10x15 = 150
  cells (`Barrel_ColorBase`), so the vanilla 500-cell container was used instead: the car
  tent, `CarTent`, 10x50. A packed tent refuses every script creation into its cargo (500
  of 500 failed), so each tent was pitched first (`TentBase.Pitch(true, true)`, probe op
  `open`), then filled with 500 `Paper` by `GameInventory.LocationCreateEntity` into
  explicit cells, 500 in one frame (probe op `fill`).
- **Run 1, four tents** in one row at z=10240, x=4740/4747/4754/4761 (2000 items). The
  player was teleported in steps along x=4724 from 200 m down to 11 m, then into the
  middle of the row.
- **Run 2, eight tents**: the same row plus a second one 10 m behind it (z=10230), 4000
  items. A fresh client (a relog, so it held no cargo from run 1) was teleported in steps
  along x=4750.5 from 100 m to 5 m from the front row.
- **Witnesses.** The server: the probe's frame monitor, read at every step (`baseline`:
  frames, mean and longest frame since the last read). The client: the probe's client
  monitor, one line per second (`frames on`): items created (`ItemBase.EEInit`), frame
  count, mean and longest frame. The server's main thread: openzone-radio
  `tools/profiler/script-profile.ps1`, 360 s over run 1 and 480 s over run 2, 200 Hz.
- **Distances** below are horizontal, from the player to the tent's origin.

## Results

**The cargo of a vanilla container reaches a client only within about 20 m of it.**
Every tent's contents arrived at 11.2, 14.9, 18.3 or 19.1 m; none arrived at 20.3, 20.6 or
20.8 m, at 22 m or anywhere further out (100, 150, 200 m included). The container itself is
on the client from far away; its items are not. Each container crosses the line on its own,
so eight tents arrived as four separate batches as the player walked.

**The server streams them at about 300 items per second per client.** Every batch of 1000
items (two tents) took 3 to 4 s: 290, 290, 300, 120 per second.

| | client frames while 1000 items arrive | client frames idle | server frames while sending | server frames idle |
|---|---|---|---|---|
| run 1, 4 tents (2 batches) | mean 33.3 ms, longest 61-62 ms | mean 33.3 ms, longest 60-63 ms | mean 0.2 ms, longest 11 ms | mean 0.2 ms, longest 11-33 ms |
| run 2, 8 tents (4 batches) | mean 33.2-33.3 ms, longest 62-64 ms | same | mean 0.2 ms, longest 19-21 ms | mean 0.2 ms, longest 15-51 ms |

No frame on either side went over 100 ms while items arrived, in either run.

**The server's main thread shows no stall from it.** Run 1's profile: no stretch of 150 ms
or more in one piece of work, 65.9 % engine / 27.9 % interpreting / 6.2 % natives from
script, the top script functions the vanilla player modifiers and plugin manager. Run 2's
profile names four stretches, and each lines up with something else: 887 + 903 ms at the
respawned client's login (the known yardstick: a vanilla login costs about 1.8 s on this
stand), 183 ms still inside that login, 214 ms when the new character was teleported 7 km
across the map. Nothing during the four deliveries.

**What the client keeps.** After run 1 the player was moved 300 m away: the client deleted
66 entities, not the 2000 items. Received cargo stays on the client while the container
itself is still within its network range; only the relog cleared it.

**What does cost something: making the items.** 500 one-slot items created in one frame
took 65-82 ms of server work, the same order as measured 2026-09-16 (1000 -> 114-128 ms).

## What this does not show

- **The client ran capped at 30 fps in the background.** Its frames have about 33 ms of
  room, and work smaller than the headroom does not lengthen a frame. The result reads "no
  frame got longer", not "the client did no work"; a foreground, uncapped client is the
  measurement for the cost per item on the client.
- **One client, empty server.** Sixty players near sixty stashes each get their own
  stream; the per-client rate says nothing about the sum.
- **Teleport steps, not walking.** The distance of each arrival is bracketed by the step
  before it, which is why the line is given as 19.1-20.3 m.
- **The stand character died in run 2**: a teleport to y=339.1 at 4750.5/10259.5 put its
  feet under the runway surface (339.4 there), it fell through the world and died. The
  corpse lies at 4750.5 339.5 10259.5 with the stand gear; the run went on with a fresh
  character, teleported 1 m above the surface from then on.

## Files

- `client-arrivals.log`: the client's seconds with items arriving, and every batch summary.
- `profile-4-tents.txt`, `profile-8-tents.txt`: the server profiler's reports.
