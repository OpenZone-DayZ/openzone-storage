# The wire: stage B of the proxy design (2026-09-24, stand)

Stage B of [`2026-09-24-storage-proxy-inventory-design.md`](../../../../docs/specs/2026-09-24-storage-proxy-inventory-design.md)
§13: descriptors and handles to ONE player by addressed RPC, and the proxy built from them
on that client.

> *Готово =* заместитель совпадает с авторитетом по составу, замер времени сборки.

## What was measured

`OZ_StorageBox_Medium -1600660530-...`, six roots and ten entities: an AKM in a weapon slot
with a 17-round magazine, four loose bandages, a `FirstAidKit` with three bandages inside.

The CLIENT asked for the box (`OZS_Mirror.Ask`, the way a screen will), the server built the
authority, filled it from SQL and streamed it.

| Side | What it says |
|---|---|
| server | `v0 watchers 1 OPEN tree 10 \| 76561198014475380 10/10 whole` |
| client | `mirrors=1 \| ... v0 10/10 built in 13.0 ms netid 00` |

The proxy's contents, read off the client's own entities:

```
px -1600660530-... tree 10
  #1 AKM at slot -692829678 tree 2 netid 00
  #3 BandageDressing at 0,0    #4 at 0,1    #5 at 0,2    #6 at 0,3
  #7 FirstAidKit at 0,4 tree 4
```

Ten of ten, the same handles the authority gave out, the same cells, the same nesting, and
every entity unannounced. **13 ms of client work for a ten-item box**, which is the wrong end
of the scale to draw a line from -- stage G does that with a thousand.

## Then one drag, all the way through

`pxmove ... 3 4 7`: the client moved item #3 to cell 4,7 on its proxy and asked the server to
agree.

| Step | Result |
|---|---|
| the proxy, at once, `InventoryMode.LOCAL` | `true`, #3 is at 4,7 |
| the server's authority | moved |
| SQL | `root_idx 1 ... row 4 col 7` |
| the version | **3905, source `live`**, forked once from 3900; 3900 and 3899 still there |
| the proxy, after the server's word | **v1** |

That is stage E's commit working under stage C's operation: one drag rewrote **one root**, not
the box, and cut **one version for the session**, not one per drag.

## Two facts about server -> client RPCs, both measured the hard way

The first attempt sent the stream on the box entity, as the design's §2 sketch assumed. The
server logged every chunk; the client never heard one. Three measurements, in order:

**1. An RPC addressed to an object only reaches a side that HAS that object.** The box was
four kilometres from the player and not in their network bubble. Obvious in hindsight, silent
in practice: `ScriptRPC.Send` reports nothing.

**2. An id of 20260924 does not arrive at all.** Moved to the player's own entity, the stream
still never came. A tracer above `Object.OnRPC` -- on `DayZGame.OnRPC` itself -- showed the
client's game receiving nothing for id `20260924`, and receiving `20501`, `20502`, `20503`
immediately after the ids were made small. Nothing else changed between the two runs. Two
points, not a boundary: what the exact limit is was not established, only that a date-shaped
id is past it. Vanilla's own `ERPCs` end in the low hundreds and CF's entire framework rides
on `10042`.

> The same id travels client -> server perfectly well: `RPC_VIEW_ID = 20260916` has been in
> the shipped mod since 2026-09-16. The limit is on this direction only.

**3. A server -> client message arrives with its TARGET NULL.** With small ids the messages
arrived -- as `target=none`, although the server had addressed them to that player's own
entity and logged its network id doing so. With a null target the engine never calls
`Object.OnRPC`, so a handler on `PlayerBase` is never reached however correct it is.

### What the wire looks like because of them

| Direction | Rides on | Handled at |
|---|---|---|
| client -> server | the client's own player entity (target survives) | `PlayerBase.OnRPC` |
| server -> client | nothing; the target is lost | `DayZGame.Event_OnRPC` |

Every message carries the box id in its body, so the target was never load-bearing. Ids are
`20501..20508`.

**The instrument mattered more than the guesses.** Four rebuild-and-reconnect cycles were
spent on hypotheses that a single tracer one layer up answered in one: `OZ_ProbeGameRpc.c`
keeps it, off by default, behind `rpc on`.
