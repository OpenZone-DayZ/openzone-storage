# Review of the storage mod and its bridge side: leaks, holes, desyncs, duplicates

Read on 2026-09-26, every file of `OpenZone_Storage` (35 scripts, 12 965
lines), the bridge's `storage-*.js` (1 771 lines) and the core's
`OZ_BridgeClient` where the storage leans on it. Everything below is read out
of the code as it stands; where a claim was measured it says so, and where it
needs measuring it says that too.

The yardstick is the design's own (§7, §9): **losing is acceptable,
duplicating is not** -- and, from the proxy design (§8), the server is the
only arbiter. A finding is ranked by how close it comes to breaking one of
those two.

The findings of the 2026-09-25 review that are still open are listed at the
end rather than repeated.

---

## A. Holes -- what a client can do that it should not

### A1. Any box on the map opens for anyone who names its network id -- HIGH

`OZS_Proxies.OnWire` (RPC_PX_OPEN) takes an anchor by network id, finds it
with `GetObjectByNetworkId`, and opens the session. Nothing on the server
asks how far the player is from that anchor -- not at the open, not at any
operation after it. The distance check lives only in the client's action
condition (`CCTObject(UAMaxDistances.DEFAULT)`), which a crafted RPC does not
go through.

A box is an announced entity, so its id is known to any client that has ever
had it in its bubble. A modified client therefore opens any box it has seen
from anywhere on the map, and `Out` puts the contents into the player's own
inventory (`Asked`) or at their feet (`GROUND`). Cheat menus that send
arbitrary RPCs exist for this game.

Fix: a distance rule on the server. At `Open`/`Join`, refuse when the player
is farther from the anchor than `UAMaxDistances.DEFAULT` plus slack; in
`OZS_Session.OnFrame`, drop a watcher who has walked away (`No` + `Gone`,
the same pair a session end sends). The stash's `StashMaxDistance` is the
precedent, and the proxy stands still beside the player anyway, so a rule of
a few metres costs the honest player nothing.

### A2. A legacy physical stash opens its owner's record for whoever names it -- LOW

The same handler falls through `OZ_StashAnchor.Cast` to `OZ_StorageBox.Cast`,
and an `OZ_PersonalStash` entity is a storage box: naming one opens
`real.OZS_GetId()` -- somebody else's pair -- as a session. New stashes are
never made any more, but a world updated from an older build can still have
physical stashes standing in it until the boot sweep gets them, and the
vicinity filter that hides them is client-side only.

Fix: in `OnWire`, refuse `OZ_PersonalStash.Cast(real)` unless
`OZS_OwnerUid()` is the sender's uid.

### A3. A flood of operations makes the server restream the whole box -- LOW/MEDIUM

Three paths answer a client with `Restart()`: a stale version, a full queue
(`MAX_WAITING`) and a repeated RPC_PX_OPEN. A restart is a fresh snapshot
(see D1 for what that costs on one frame) and every row of the box again over
the wire. Nothing limits how often one client can trigger it, so a client that
sends sixteen operations a frame, or opens in a loop, has the server
re-serialising a thousand-item box continuously.

Fix: a minimum interval between restarts per watcher (a second is plenty);
past it, refuse without restarting.

## B. Duplication and loss -- the record against the world

### B1. With `WaitForRecord`, the hand-over is released by the FIRST reply, not its own -- MEDIUM

`OZS_Session.OnCommitted` calls `ReleaseHandover()` before anything else,
whatever letter the reply answers. Under the one-turn-at-a-time rule those
are the same thing -- except for the operations that post more than one
letter. `OZS_Boundary.Across` posts three (the step-aside move, the put-in,
the take-out) and holds its hand-over on the third while the first two are
still in flight. The reply to the FIRST letter releases it: the item goes to
the player before the `Left` that drops it from the record has landed. If
that third letter is then refused or lost, the item is in the player's
inventory and in the record -- the exact window `WaitForRecord` exists to
close.

Fix: release only when the flight that carried the hand-over has answered --
simplest, when `m_Flying` reaches zero after the decrement; or tag the
hand-over with the serial of its letter and match it in the reply.

### B2. A sort writes every re-celled root with `flip = 0`, against a layout planned with the flipped footprint -- MEDIUM

`OZS_Sorter.Plan` measures each root with `OZS_Ops.SizeOf`, which swaps
width and height for an item lying turned (that was the fix of 2026-09-26
for the rag). `OZS_Records.WriteDescriptor` then writes the planned cell and
forces `flip = 0` (line 175). The restore creates the item unturned in a
cell the planner reserved for it turned: a rag planned as three wide and one
tall comes back one wide and three tall, over two neighbours. The engine
refuses the cell, `Create` falls back to any free cell or to a stand-in, and
a stand-in root is parked as `no_room`.

Every sort with a turned item in the box loses that item to the parked list
(recoverable by an admin, invisible to the player). The stand test that
confirmed the sort had no turned items in it.

Fix, one line: keep `loc.GetFlip()` in the descriptor instead of forcing 0.
Alternatively plan with the unturned size -- either way both sides must
measure the same rectangle.

### B3. A split creates a NETWORKED entity inside the unannounced authority -- MEDIUM, needs measuring

`OZS_Ops.Split` calls vanilla's `SplitItemToInventoryLocation` /
`SplitIntoStackMaxToInventoryLocationEx` on the authority's item. All three
vanilla bodies (itembase.c:1985, :1872; magazine.c:206) create the new stack
with `GameInventory.LocationCreateEntity(dst, ..., ECE_IN_INVENTORY,
RF_DEFAULT)` -- a registered, announced entity -- as a child of a container
the network has never heard of. The mod's own rule, written in
`OZS_Records.ReadRoot`, is that everything built into an authority must be
LOCAL, "a networked child of an unannounced parent is a contradiction the
clients resolve badly". And when that stack later goes OUT, `Handover` calls
`RemoteObjectTreeCreate` on an entity that is already registered.

What the engine does with such a child is not known: the split was watched
through the proxy, which is told by rows and would not show a ghost. Measure
before deciding: log `made.GetNetworkIDString()` after a split. If it is not
`00`, do the split by hand -- `LocationCreateLocalEntity`,
`TransferItemProperties`, the quantity or the cartridge loop for a magazine
-- so the authority's rule holds.

### B4. The bridge going down mid-session (finding 1 of 2026-09-25) -- still open

`OZS_Commit.Ready` fails the session and `Fail` discards the authority with
everything in it. Items put IN since the last confirmed turn are lost; items
taken OUT are duplicated unless `WaitForRecord` is on. Two remarks beyond the
earlier review:

- `WaitForRecord` is measured at about 100 ms and the owner has felt it on
  the stand. Shipping with it ON closes the duplicate half for good; the
  default in `LoadDefaults` is still `false`.
- Discarding is the design's choice, not a necessity. The authority could be
  kept alive with its watchers dropped and its record written whole when the
  bridge answers again -- a crash costs one turn, a bridge restart would then
  cost nothing.

### B5. Two letters for one operation (finding 3 of 2026-09-25) -- still open, and now known to be concurrent

`OZ_BridgeClient.Fly` posts every call at once (`s_Ctx.POST` per call, an
`s_InFlight` list, no serialisation), so the two letters of `Combine` and of
`Swapped` are two concurrent HTTP requests and the bridge applies them in
arrival order. Each was numbered against a different picture of the record.
The `expect` check refuses a mismatch of class names; it cannot see the case
where the neighbouring root is of the same class, which is exactly the case
of two piles of the same ammunition.

### B6. Two servers on one bridge (finding 4 of 2026-09-25) -- still open.

## C. Desyncs -- what the player sees against what the authority holds

### C1. A container put into the box arrives on every screen EMPTY -- MEDIUM

`OZS_Boundary.In` moves the player's item, whole subtree and all, then tells
the watchers with `TellAdded(e)` -- which describes ONE entity
(`Describe` builds one row). The player's own client has just had the real
tree deleted by `RemoteObjectTreeDelete`, so the proxy builds a fresh, empty
backpack from the row; every other watcher does the same. The contents exist
in the authority and in the record, and appear on screen only after a
restream (a stale, a refusal, a reopen). A player who puts a full backpack
into the box and opens it there sees nothing in it. `Across` step 2 has the
same shape.

Fix: `TellAdded` sends the flattened subtree, parents before children, as one
`CH_ADDED` per node. `OZS_Mirror.Add` already resolves a row's parent by
handle, and `Change(CH_ADDED)` forgets-and-rebuilds each handle, so the
sequence is idempotent as it is.

### C2. One failed fill strands the player until they relog -- MEDIUM

When the open job fails after `Join` accepted the watcher ("too many
unreadable roots", the bridge refusing, a timeout), `OZS_OpenJob.Fail` leaves
the authority CLOSED and `OnOpenFailed` only notifies. The watcher stays; its
`OnFrame` waits for OPEN for ever; the client's `OZS_Mirrors.m_Waiting` is
never cleared because no BEGIN and no GONE ever come. The next press of the
button reaches `Join`, finds the watcher, calls `Restart()` and returns --
the branch that re-requests the open runs only for a NEW watcher. That box is
dead for that player until they disconnect (`DropPlayer`).

Fix: in `Join`, when the authority is CLOSED, request the open again for a
re-asking watcher as well; in `OnOpenFailed`, have the session tell its
watchers `No` and `Gone`, which also clears the client's wait.

### C3. A refused open tells the client nothing -- LOW

`OZS_Proxies.Open` returning false (the bridge down, the authority not
creatable) is logged and dropped; the client keeps `m_Waiting` and the player
sees a button that does nothing. Send RPC_PX_NO with `#STR_OZ_ERR_NO_BRIDGE`
even when there is no watcher to send it through -- the message carries the
id in its body and the client matches on that.

### C4. The closing write and `closed` race an admin -- LOW

`OZS_Session.End` posts the absolute rewrite (`/op replace`) and
`ROUTE_CLOSED` in the same frame; they are concurrent requests. If `closed`
lands first, an admin's `give`/`rollback` in the gap is accepted, and the
session's rewrite that follows makes a new `live` version over it -- the
"admin operations are overwritten by a session close" the owner reported,
narrowed to a millisecond window. Post `closed` from the reply of the closing
write, or let the replace letter carry `close: 1` and have the bridge mark
the box closed in the same transaction.

## D. Leaks and cost

### D1. Handles are a linear scan, and the snapshot asks for one per node -- MEDIUM

`OZS_Authority.Handle` walks `m_Items` to find the entity; `Index` calls it
for every node, and `Snapshot` calls it again for every node and every
parent. For a box of a thousand items that is on the order of a million
steps in Enforce, on ONE server frame, every time a watcher starts or
restarts its stream (and A3 lets a client trigger that at will). A frame
hitch of that size is felt by every player on the server. The owner's
verdict on the live server's lag was about exactly this kind of synchronous
work.

Fix: an O(1) handle on the entity itself -- an `int` field on a modded
`EntityAI`, assigned once by `Handle` -- or a map keyed by the handle for the
reverse direction. The parallel arrays were chosen because "an EntityAI is
not a hashable key"; the entity carrying its own number needs no key at all.

### D2. `OZS_Watcher.Player()` walks `GetPlayers()` on every message -- LOW

`Say` calls `Listening()`, then `Player()`, then `Who()` -- three walks with
an array allocation each -- per message, and `OnFrame` calls `Alive()` once
per watcher per frame. Resolve the `Man` once per `Say`, and once per frame
for the liveness test.

### D3. `FreeSpot` asks the engine about every cell -- LOW

Up to two orientations times the whole grid (a thousand `LocationCanAddEntity`
calls on a full large box), each a native occupancy scan. It runs for a
put-in with no cell named -- a drop on the container's header -- into a
nearly full box. Ask the occupancy map first (`Clear` over the rectangle) and
confirm only the candidate with the engine.

### D4. Statics that outlive the mission -- LOW

- `OZS_Authority.s_Live` is never reset. A session that reaches
  `EndAll` with a turn in flight returns early from `End` (`m_Closing`) and
  never discards, so its `OZS_AuthRec` survives the restart with a dangling
  box -- one dead row per such restart.
- On the client, `OZS_Mirrors.s_Inst` survives a disconnect; its mirrors then
  hold null containers. Harmless today, but nothing says so on purpose.

Add `OZS_Authority.Reset()` beside `OZS_Proxies.Reset()` in
`OnMissionFinish`, and a client-side reset in `MissionGameplay`.

### D5. A proxy keeps table rows for children deleted with their parent -- LOW

`OZS_Mirror.Forget(handle, true)` deletes the container; the engine takes its
children; their rows stay in `m_Handles`/`m_Items` reading null. `Forget`
could sweep nulls while it is there.

## E. Admin and lifecycle -- what the proxy left behind

### E1. The admin's live `close` is dead -- MEDIUM

`OZS_Controller.AdminCommand("close")` reads the PLACED box's state, which
under the proxy is always CLOSED, and answers "not open". An admin can no
longer end a session to free a box for a rollback, a give or an unpark --
and every one of those is refused while the box is `open` in SQL. Route the
command to the session: `OZS_Proxies.Find(id)`, tell the watchers `Gone`,
`End()`.

### E2. The admin's `remove` deletes a box that is in use -- MEDIUM

The same reading (placed box CLOSED) lets `remove` delete the anchor while a
session is live. The session goes on: players keep taking and putting through
a box that no longer exists, the turns keep writing into a record SQL has
marked `removed` (`applyOps` and `replaceRoots` never check the status), and
the final `closed` is refused as "unknown box". Nothing duplicates, but an
admin who removed a box would not expect it to keep trading. Refuse `remove`
while a session exists for the id, and have the bridge refuse turns on a
`removed` box.

### E3. `AutoCloseSeconds` no longer applies to anything -- MEDIUM

`AutoCloseTick` walks the controller's register, which holds placed boxes
only (an authority returns early from `EEInit`), and a placed box is never
OPEN. A session has no idle rule for its WATCHERS -- only for the moment the
last one leaves (`ProxyIdleSeconds`). A player who opens a box, leaves the
panel open and goes for dinner holds the box `open` in SQL until they
disconnect, and (with E1) no admin can do anything about it. Give the session
the idle rule the setting describes: `Touch` already stamps the time; when it
is older than `AutoCloseSeconds`, drop the watchers (`No` + `Gone`) and let
the session end.

### E4. Every crossing writes two audit rows, and a nested move writes a `take` -- LOW

`EECargoOut` on the authority fires for the take-out's own move, so each
take-out is a `take` and an `out`; each put-in a `put` and an `in`. A move
from the box's cargo into a container standing inside the box is an
`EECargoOut` of the box too, and is written as a `take` of an item that never
left. The history page reads as twice the traffic it had, with departures
that were rearrangements. Silence the cargo-event audit on authorities (the
explicit `Log("out")`/`Log("in")` already say it) and keep it for the placed
boxes of the boot reconciliation.

### E5. Consecutive sessions share one `live` version in SQL -- LOW

`applyOps` forks a version only when the current one's source is not
`live`; `markClosed` changes the box's status and nothing else. So the first
turn of the next session mutates the previous session's version in place,
and "one version per session" holds for the first session only. A rollback
to "before this session" is then impossible, which is the one thing an admin
reading the history wants. On `/v1/storage/closed`, retire the version:
`UPDATE storage_versions SET source = 'session' WHERE id = current AND
source = 'live'`.

## F. Housekeeping before this ships

- `FakePingMs` / `OZS_Late`: a held `ScriptRPC` does not survive the frame it
  was built in (measured 2026-09-25: the box stopped opening, clients
  re-requested in a loop). The setting is still there with a warning. Either
  delay the OPERATION rather than the built message, or refuse a non-zero
  value outside the diag build. A warning is not a guard.
- `DebugLog = 1` on the stand's profile; the per-drag `OZS_Say` lines in
  `OZS_Player` are behind it, so they cost nothing off.
- `OZS_Settings.Load`, line 225: `s = s;`.
- `OZS_Const`, the RPC block: "every one of these travels on the ANCHOR
  object" -- they ride on the player and on `DayZGame.Event_OnRPC` now.
- Stand-only verbs (`drift`, `vanish`, `persist`, `asother`) live in
  `OpenZone_Storage_Bridge`, which is `server_only` on the stand and absent
  from `packaging/` -- not shipped, as intended.
- Nothing is committed: 28 files in this repository, 3 in the bridge.

## G. Checked and sound

- File names on the bridge side are regex-guarded (`Xchg.closeName`,
  `opName`, `discard`); no path reaches the file system unchecked.
- A turn is one SQLite transaction; a refusal is proof nothing was written;
  the `expect` check refuses a drifted numbering before anything is applied.
- The closing write is sourced from the container, never from the order.
- `ByHandle` refuses an entity that has left the box; a handle is not a pass.
- A box cannot go into a box; an item cannot swallow its own box; `In` and
  `Across` take only what the player carries or can reach; `Out` puts only
  into the player's own hierarchy or at their feet.
- Scratch objects of the restore carry `ECE_NOPERSISTENCY_WORLD`.
- `OZS_Authority.Empty` lowers `Releasing` before the deferred deletions
  fire, but `Resort` sets the state to CLOSED in the same frame, so
  `OZS_Live()` is false when `EECargoOut` runs: no phantom takes. It holds by
  the state, not by the flag -- worth knowing if either moves.
- A session cut off by the mission finish leaves SQL `open`; the boot rule
  (`PostClosedAtBoot`) closes it, and the record is at the last confirmed
  turn.
- Changes queued during a stream are applied idempotently after it, whichever
  side of the snapshot they fell on.
- The bridge crashing AFTER committing a turn costs nothing: the record has
  the turn, an item out is with the player, an item in is rebuilt once.

## Order I would take them in

1. **A1** -- one distance check; the only finding a stranger can exploit.
2. **B2** -- one line, and it loses items to the parked list on every sort.
3. **C1**, **C2** -- small, and both are "the box does not work" to a player.
4. **B1** -- small; it is the hole `WaitForRecord` was turned on to close.
5. **E1**, **E2**, **E3** together -- the session's lifecycle from the
   admin's side, one batch.
6. **D1** -- the frame hitch, before the first big box on the live server.
7. **B3** -- measure first; the fix is a page if the measurement says so.
8. The rest, and the four still open from 2026-09-25.

---

## What was done the same day

Every fix was built, booted on the stand and exercised there through the
stand's own verbs; the bridge side has tests for its part (`test/storage-store.mjs`,
107 assertions; the whole suite 21/21).

| # | Status | How it was checked |
|---|---|---|
| A1 | fixed | `proxy do=open` from 66 m: "too far from the box"; from 1.4 m: opens; teleport 20 m away with the session open: "walked away … let go", watchers 0 |
| A2 | gone with the legacy stash | `OnWire` refuses any stash entity as an anchor; the physical stash and its lifecycle are deleted |
| A3 | fixed | a burst of 40 moves (24 over the queue) produced ONE restream, not 24 |
| B1 | fixed | cross-boundary swap: `flying 3`, the item reached the hands after the third reply |
| B2 | fixed | a bag moved turned, then a sort: "parked 0", the bag came back FLIPPED at its planned cell |
| B3 | measured, then fixed | vanilla's split made netid `019766` inside the authority; `SplitLocal` makes `00` (ammo 13→7+6, rag 6→3+3) |
| B4 | open (owner's decision) | `WaitForRecord` default; the authority discarded when the bridge dies mid-session |
| B5 | fixed | combine: one letter "rewrite 1 drop 1"; swap: "rewrite 2 drop 0" |
| B6 | fixed | `open_by` on the box: the holder re-opens, another server is refused and told `held_by` at boot |
| C1 | fixed | a bag with contents put in: the client's mirror tree equals the authority's (157/157), the bag shows `tree 2` |
| C2 | fixed | a failed fill tells the watchers `No`+`Gone`, a re-asking watcher requests the fill again |
| C3 | fixed | a refused open reaches the client as `RPC_PX_NO` with the box id; the wait ends |
| C4 | fixed | the closing letter carries `close: 1`; SQL closed in the same transaction |
| D1 | fixed | the handle lives on `ItemBase`; a 158-row snapshot takes 0.99 ms instead of 5.98 ms |
| D2 | fixed | `Player()` keeps and checks its last answer |
| D3 | fixed | `FreeSpot` walks our map first, the engine confirms; a rag with no cell landed at 19,9 |
| D4 | fixed | `OZS_Authority.Reset`, client `OZS_Mirrors.Reset` at mission finish |
| D5 | fixed | `Forget` drops null rows |
| E1 | fixed | admin `close` ends the session: "ok the session is ending; 1 watcher(s) were sent away" |
| E2 | fixed | admin `remove` refuses a box in use; `EEDelete` of a placed box ends its session |
| E3 | not a regression | the owner's rule was "the close waits while somebody is looking"; a session ends when its last watcher leaves |
| E4 | fixed | the session's history holds `in`/`out`/`open` only, no `take`/`put` |
| E5 | fixed | `closed` retires the `live` version; three sessions in a row made three versions (3977, 3983, 3984) |
| F | done | `OZS_Late` gone, the fake ping holds the operation (`tune ping=3000`: the move ran after the delay); settings v6 without the five dead knobs; `s = s;` gone; the RPC comment fixed |

Also found and fixed on the way: an item taken off a weapon slot into the
cargo was measured as 1x1 (`SizeOf` knows nothing outside a cargo), so an AKM
of 8x3 was accepted on row 48 of a fifty-row box; `SizeFor` now falls back
to the config size.

And the whole old scheme is gone, as the owner asked: the close job, the
auto-close, the boot close of a box saved with cargo, the physical stash
and its sweep, the stand verbs that opened or filled a placed box. A placed
box is always CLOSED and empty.

Two engine facts were measured and written into the `dayz-modding` skill:
`modded class EntityAI` is refused ("Engine class cannot be modded"), and
`Weapon_Base` descends from `ItemBase` (declaring the same field on both is
"Multiple declaration").

Not committed, not pushed.

### Found by the owner an hour later: the cross-boundary swap's three letters

Six canteen-for-rag swaps in a row through a hoodie, and the sixth answered
"the record did not take the turn". The log: `root 114 holds Rag, the letter
says Canteen`. `Across` posted its three steps as three letters, the core's
client sends every call at once, and the bridge applied the drop of position
114 before the rewrite of position 114 -- the identity check refused it, the
session repaired with an absolute rewrite (116 roots, 163 entities, nothing
lost or doubled), and the swap stopped half way. Before B1 the same race was
repaired silently, with the item already handed over.

Fix: `OZS_Session.m_Batch`. `Across` opens a batch, its three steps write
into one letter, and the letter is posted once at the end; `HoldHandover`
accepts a hold while a batch is open. The safety argument of §7 holds
unchanged: the letter goes out after the put-in's move (record last) and
before the take-out's hand-over (record first), so at every instant exactly
one item is unsaved and none is doubled. Verified: four swaps in a row, each
one letter `rewrite 1 drop 1 add 1`, no refusal, the item in the hands after
each.
