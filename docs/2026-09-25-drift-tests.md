# Testing that the box and its record cannot drift apart

Written 2026-09-25, alongside the change that made a drifted turn detectable.
It says what can go wrong, what now catches each case, and how to make each
case happen on purpose so the catching can be watched rather than trusted.

## What "drift" means here

Within a session there are three pictures of one box:

1. the **authority** — a real container on the server, the only place the items
   exist;
2. the **order** (`OZ_StorageBox.OZS_RootOrder`) — this side's list of the
   roots, which a relative letter names by position in;
3. the **record** — the rows in the bridge's SQLite, which is the only picture
   that survives the session.

The letters are positional: "rewrite root 7", "drop root 3". That is cheap and
correct exactly as long as position k means the same root in (2) and (3). When
it stops meaning that, the letters keep being accepted and keep writing the
wrong roots — and the counts can still come out right afterwards, which is why
counting was not enough.

## What catches what

| Fault | Caught by | Where |
|---|---|---|
| the order and the record disagree about a position the letter names | the bridge refuses the letter, the session answers with an absolute rewrite | `storage-store.js` `applyOps`, `OZS_Commit` `expect` |
| the box and the record disagree about totals after a turn | the session rewrites the record from the container | `OZS_Session.OnCommitted` |
| a repair that does not help | at most `OZS_Const.MAX_REPAIRS` (3) per session, then it says so once and stops | `OZS_Session.OnCommitted` |
| a refused absolute rewrite | the session ends rather than looping | `OZS_OpReply.m_Whole` |
| anything that drifted and was never noticed | the closing write says what the box holds, absolutely | `OZS_Session.End` |

The identity check is the load-bearing one: it fires at the moment the wrong
write would happen, so nothing wrong is ever stored. The count check is a
backstop for a fault that changes totals without touching a named position, and
the closing write is the backstop for everything else.

## Making a fault happen on purpose

`oz_storage do=drift` (stand build only) removes one entry from the order and
touches nothing else — the items are fine, the box is fine, only this side's
idea of their numbering is wrong. That is the real fault in its smallest form.

```
world_exec oz_storage {"op":"proxy","do":"open","id":"<box id>"}
world_exec oz_storage {"op":"proxy","do":"drift","id":"<box id>","at":"2"}
world_exec oz_storage {"op":"proxy","do":"move","id":"<box id>","handle":"50","row":"40","col":"0"}
world_exec oz_storage {"op":"proxy","do":"status"}
```

## The runs, and what each one must show

### 1. Control — an ordinary turn is not disturbed by the checks

Open, combine two stacks of one calibre, read the status.

Must show: `sql` roots one lower than before, the box's tree one lower, no
`ERROR` line, and `storage.mjs diff <before> <after>` naming exactly the one
stack that was merged away.

Measured 2026-09-25: `sql v3943/119r/151e`, `tree 151`, diff `- Ammo_762x39 x1`.

### 2. A drifted turn is refused, not applied

Open, `drift at=2`, then any ordinary turn.

Must show, in the server log:

```
ERROR: ... the bridge refused a turn of box <id>: ... root 33 holds Ammo_9x19,
the letter says Ammo_22: the two sides have stopped agreeing about positions
... box <id>: the record is being set to what the box holds -- 118 root(s), 150 entity(ies)
```

and afterwards `status` with **no `flying`** — the wire is free.

Must NOT show: a version whose diff against the one before the session names
anything the player did not do. Without the identity check the same injection
wrote `- Ammo_556x45, - Mag_AKM_30Rnd, + Ammo_9x19` for a combine of two
`Ammo_9x19`, with the counts matching (measured 2026-09-25, before the fix).

### 3. The session still finishes after a refusal

Same as 2, then one more ordinary turn, then `do=shut` and wait out
`ProxyIdleSeconds` (20 s).

Must show: the second turn goes through (the order was rebuilt from the
container, so it is right again), then

```
... the record is being set to what the box holds -- 118 root(s), 150 entity(ies) (the session is ending)
... session <id> ended, 150 entity(ies) released
```

This is the run that catches a leaked flight: before `OnCommitRefused` existed,
a refusal never gave its flight back, `flying` stuck at 1, and the session
never closed, never drained its queue, and never released the box.

### 4. A close writes no phantom history

Open a box with many items, close it, read `storage.mjs history <id>`.

Must show: one row per thing that was actually done, and nothing else. The
engine calls `EECargoOut` once per item as the authority is torn down; before
`OZS_Releasing` that was audited as a player taking each one, and closing a
121-item box wrote 121 `take` rows.

### 5. The repair cap, and what the player is told

`do=drift` cannot reach this any more, by design — the identity check catches a
drifted position before the counts can disagree. `do=vanish` is the injector
for this path: it takes an item out of the authority without writing a letter,
so the totals disagree while every name the letters use still matches.

Run it with `OZS_Const.MAX_REPAIRS` temporarily set to 0, so the first
disagreement lands in the give-up branch (measured this way 2026-09-25).

Must show, in order:

```
ERROR: ... the box holds 117 root(s) and 149 entities, the record says 118 and 150
ERROR: ... has been put straight 0 time(s) and still disagrees; no more will be tried this session
```

in the client's log, the notice reaching the player:

```
WARNING: storage: proxy: box <id> refused #0 (#STR_OZS_DRIFT), the server is at v1
```

in `storage.mjs history <id>`, one row an admin will actually see:

```
drift  ... the box holds 117 root(s) and 149 entities, the record says 118 and 150; repairs gave up
```

and then the session **carrying on**: the next turn goes through, and the close
still writes the truth —

```
... the record is being set to what the box holds -- 117 root(s), 149 entity(ies) (the session is ending)
... session <id> ended, 149 entity(ies) released
```

That last part is the point of the branch. The box is not taken away from the
player: every item is in the authority and the authority is fine — what is in
doubt is the bookkeeping about it. Ending the session instead would discard the
authority WITH the items and write nothing (see `OZS_Session.Fail`), so a
player would lose real things over a disagreement about counting.

The cap governs the COUNT path only. The refusal path in `OZS_OpReply` repairs
whatever the count is, and must: a refused letter was never written, so without
the repair the record falls behind and stays behind, and everything done
afterwards is lost if the server goes down. A repair that is merely cosmetic
can be given up; one that is the only way back onto the wire cannot.

Put `MAX_REPAIRS` back to 3 afterwards.

## Before release

`do=drift` and `do=vanish`, like every other `oz_storage proxy` verb, are
stand-only and must not reach a published build.
