# The mirror: what it costs, and whether it agrees with the authority (2026-09-24, stand)

Two questions left open by `results-client-local-mirror.md`, both asked before writing the
spec because either answer could change its shape.

## 1. Does a mirror judge a move the same way the authoritative box would?

The same battery of seven moves, run on a CLIENT mirror (`OZ_ProbeClientControl moves`) and
on a SERVER box built with the design's own flags, `ECE_LOCAL | ECE_NOPERSISTENCY_WORLD`
(`oz_ghost op=moves`). Both boxes: `SeaChest`, three `BandageDressing` in the first cells.

| # | Destination | What it exercises | Mirror (client) | Authority (server) |
|---|---|---|---|---|
| 1 | 4,5 | a free cell well inside | true, 0,0 -> 4,5 | true, 0,0 -> 4,5 |
| 2 | 0,0 | back where it came from | true, 4,5 -> 0,0 | true, 4,5 -> 0,0 |
| 3 | 9,9 | the far corner | true, 0,0 -> 9,9 | true, 0,0 -> 9,9 |
| 4 | 40,0 | a row far outside the grid | true, stayed 9,9 | true, stayed 9,9 |
| 5 | 0,40 | a column far outside | true, stayed 9,9 | true, stayed 9,9 |
| **6** | **-1,0** | **a negative row** | **true, 9,9 -> -1,-1** | **true, stayed 9,9** |
| 7 | onto another item's cell | two items meet | true, stayed -1,-1 | (the answer was cut by the wire's length limit) |

**They diverge, and the divergence is silent.** Case 6: both calls returned `true`, but the
mirror moved the item to `-1,-1` -- out of the cargo, into no valid location -- while the
authority left it where it was. The mirror was then **stuck**: case 7 could no longer move
that item at all.

### What follows from this

- **`TakeToDst` returning true does not mean the same thing on both sides.** A return value
  cannot be used as the client's verdict.
- **The screen must compute and validate its own destinations** rather than offering a cell
  to the engine and believing the answer. Our own grid would never produce `-1`, but a bug
  or a hostile client would, and the mirror would take it.
- **A resync path is mandatory, not a nicety.** A mirror can reach a state the authority
  would never produce, and nothing in the engine will correct it.
- The authority behaved correctly in every case, which is the reassuring half: the server
  is a sound arbiter.

## 2. What does a thousand-item mirror cost the client?

`OZ_StorageBox_Large` (10x100 cells) filled with 1000 `Paper` (1x1), created **all in one
frame** on the client -- the burst case on purpose.

| Measure | Value |
|---|---|
| The container itself | 0 ms |
| 1000 items | **47 ms**, about 0.047 ms each |
| The client's own frame monitor over the burst | 1001 inits, 634 frames, avg **6.3 ms**, max **55 ms** |
| Frames over 100 ms | **0** |

**One 55 ms hitch, not a stall.** For comparison, the closest earlier measurement
(2026-09-17) is of a NETWORKED burst: 5000 entities created next to a client in one server
frame once stalled that client for **39.2 s**. A mirror has no network step at all, and is
some three orders of magnitude cheaper.

So pacing the mirror build is not required at this size. It is still worth keeping for much
larger boxes, and the machinery exists (`OZS_OpenJob` paces at 500/s within a 5 ms budget);
but a thousand items can simply be built.

And the blast radius shrinks: today's open streams to every client nearby, while a mirror
costs one frame to the one player who opened the box and nothing to anybody else.

### Not measured

The cost of the VANILLA panel drawing a thousand cells. The big mirror stayed collapsed in
the vicinity list during the run, so only the entity construction was timed. It matters
less than it looks: the screen is ours and will draw only the visible page.
