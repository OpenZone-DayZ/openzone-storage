# Review of the proxy inventory: edge cases and ways to duplicate

Read on 2026-09-25, after the drift work. Everything below is read out of the
code as it stands; where a claim was measured this session it says so.

The design's own rule is the yardstick: **losing is acceptable, duplicating is
not** (§7, §9). A finding is ranked by how close it comes to breaking that.

---

## 1. A failed session duplicates everything it handed out — HIGH

`OZS_Boundary.Out` writes SQL first and moves second, which is the right order
for a crash. But "writes SQL" means **the letter is posted**, not that the
bridge has answered: the move and `RemoteObjectTreeCreate` run in the same
frame, long before the reply.

If the reply never comes — the bridge went down, or a turn was refused and the
absolute rewrite was refused too — `OZS_Session.Fail` runs. It silences the
closing write (`m_Wrote = true`) and discards the authority. The record is left
on the last version the bridge confirmed, and that version **still holds every
item handed out since**. Those items are in the players' inventories and will
be saved there.

Open the box again and they are in both places.

The window is not a frame. It is the whole rest of the session: the bridge dies
at 10:00, the player keeps taking things out until 10:20, every one of them is
duplicated.

The same shape with the server crashing instead is much narrower — the letter
is usually already in flight and the player's save has to land as well — and
that one §9 already accepts.

Fixes, in increasing cost:

- **Journal the unconfirmed turns to disk.** `Fail` writes the roots it could
  not confirm into the profile directory; the boot exchange replays them when
  the bridge is back. Keeps the current latency and closes the hole.
- **Do not hand the item over until SQL has answered.** Correct and simple to
  describe, and it puts a round trip inside every drag.
- **Accept it and say so**, as §9 accepts the crash case. Not the same thing:
  a crash is rare and narrow, a bridge restart is neither.

## 2. A handle outlives the box, and every operation still honours it — HIGH

`OZS_Authority.ByHandle` looks the entity up in the session's table and returns
it. `Forget` only drops entries whose entity is **deleted**; an item that left
the box is alive, so its handle keeps working for the rest of the session.

`OZS_Boundary.Out` is safe by accident — `RootOf` returns -1 and it refuses —
but nothing else checks:

- `OZS_Ops.Move` will happily `Put` that entity into the box. It is in a
  player's inventory, announced to every client, and the move is `LOCAL` into
  an unannounced container: that is exactly the ghost this project spent a week
  on (see `persistence-networking.md`). The player watches it stay in their
  inventory while the server has taken it.
- If the item is in **another** player's inventory — A took it out, B still has
  the handle — B pulls it out of A's pockets.
- `Combine` and `Split` reach it the same way.

A client can send any handle it likes, so this needs no race: take an item out,
then send `OP_MOVE` naming it.

Fix, one place: `ByHandle` returns the entity only while it is still under this
box.

```c
// AND IT MUST STILL BE IN THE BOX. A handle is not a claim on an entity
// for the rest of the session -- an item handed out to a player is alive,
// so its entry survives Forget, and every operation that names it would
// act on something the box no longer holds.
```

## 3. Two letters for one operation — MEDIUM

`OZS_Commit.Split` states the rule in its own comment: *"both halves go in one
letter. Written separately they would be two turns, and a server that died
between them would leave the record holding a stack that had already given its
contents away — the rounds counted twice or not at all."*

Two operations break that rule:

- **`OZS_Ops.Combine`** posts `Left(from)` and then `Quantity(into)`. Measured
  this session, the log shows the pair: `rewrite 0 drop 1` then
  `rewrite 1 drop 0`. If the drop lands and the rewrite does not, the rounds
  are lost; if the rewrite lands and the drop does not, **the rounds exist
  twice**.
- **`OZS_Ops.Swapped`** posts `Moved(first)` and `Moved(second)`. Half of that
  leaves the record with two roots claiming one cell — not a duplicate, but a
  record whose next open parks something.

Both are one letter's worth of work: a letter already carries several rewrites
and drops.

`OZS_Boundary.Across` posts up to four and cannot be made atomic — it is three
crossings by construction, and the file says so. That one is a documented
trade, not an oversight.

## 4. Nothing stops two servers opening one box — MEDIUM (HIGH for sharding)

`/v1/storage/open` logs `open of <id>, which SQL believed open already; the
engine knows better` and hands the contents over regardless. For one server
restarting, that comment is right — the engine does know better.

For two servers on one bridge it is a duplicator: both materialise the same
contents, both write. `storage_boxes` has no column saying which server holds a
box open, so the bridge cannot tell the two cases apart.

This matters now because of the location-sharding plan
(`docs/specs`, 2026-09-22): several servers, one bridge.

Fix: carry a server id on `/open` and store it. Same id re-opening is the
restart case and is allowed; a different id while the box is open is refused.

## 5. A queued operation is never re-checked for staleness — LOW/MEDIUM

`OZS_Session.Operate` checks `version < w.m_OthersAt` when the operation
arrives, then may park it in `m_Waiting` for up to 16 turns. `NextWaiting` runs
it without asking again, so an operation admitted against one picture of the
box can execute against a later one. It checks that the watcher is still there
and nothing else.

Nothing duplicates — every operation re-reads the box — but an item can land
somewhere the player did not aim at, which reads as "the box moved my things".

Fix: store the version with the waiting entry and re-test it on the way out,
refusing with `stale` exactly as the front door does.

## 6. `OZS_Watchdog.s_Expected` is never cleared on a failed step — LOW

`Expect(e)` is set before a departure the mod intends. If that step then fails,
the flag stays set and the next genuine departure of that same entity is
swallowed. Also, `Moved` returns on `!OZ_Log.IsDebug()` **before** clearing it,
so outside debug the flag is only ever set.

Harmless today (the watchdog is diagnostic, and the reference is weak), but it
makes the one tool for this class of bug unreliable exactly when it is needed.

## 7. What was checked and is sound

- **Handles are never reused.** `m_Next` only grows, so a stale handle names a
  dead entry rather than somebody else's item. (The problem is 2, which is the
  opposite: an entry that is too much alive.)
- **`Out`'s rollback cannot duplicate.** `Put` verifies with `Sits`, and
  `Added` after a successful put-back re-adds the root the `Left` removed.
- **`In` cannot take what is not the player's.** `Reachable` refuses anything
  with a parent that is not this player, and loose items only within the
  engine's own action reach.
- **A box cannot be put inside a box**, and an item cannot swallow the box it
  is going into.
- **`Swap` never leaves one item moved and the other not** — every step is read
  back with `Sits`, and a step that fails puts the previous one back.
- **Death and disconnect** drop the watchers only; an item already handed out is
  in the player's save or their corpse, which is correct either way.
- **The closing write** takes the container's real contents, so any drift that
  went unnoticed during a session is corrected on the way out (this session).
- **A drifted position is refused before it can be written** (this session).

---

## Order I would take them in

1. **2** — one function, closes a hole a client can walk through on purpose.
2. **3** — mechanical, and the rule is already written down in the file.
3. **4** — cheap now, and sharding makes it urgent later.
4. **1** — the real one, and the only one that needs a decision about cost.
5. **5**, **6** — tidy-ups.
