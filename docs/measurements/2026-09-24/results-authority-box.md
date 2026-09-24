# The authoritative box: stage A of the proxy design (2026-09-24, stand)

Stage A of [`2026-09-24-storage-proxy-inventory-design.md`](../../../../docs/specs/2026-09-24-storage-proxy-inventory-design.md)
§13: a real container created with `ECE_LOCAL | ECE_NOPERSISTENCY_WORLD`, filled from SQL by
the ordinary open, handing its items handles.

> *Готово =* ящик наполняется, в мире его не видно ни одному клиенту, после рестарта следов нет.

All three were run against a live bridge and a connected client.

## The subject

`OZ_StorageBox_Medium -1600660530--699927733--556575823--336523421` at 7500 7500, closed,
holding in SQL a deliberately awkward tree:

| Root | What it exercises |
|---|---|
| AKM in `OZ_Weapon_1`, with `Mag_AKM_30Rnd` (17 rounds) and a chambered `Bullet_762x39` | an ATTACHMENT root with an attachment of its own and a blob that matters |
| 4 x `BandageDressing` in cargo | plain cargo roots |
| `FirstAidKit` with 3 `BandageDressing` inside | a container root: the restore builds these on the ground and moves them in a frame later |

Six roots, ten entities.

## 1. It fills

```
auth do=make   -> authority OZ_StorageBox_Medium for -1600660530-... netid=00 authority=true
                  at 7500 313.32 7500; boxes registered 4
auth do=open   -> open accepted, state now OPENING
auth do=status -> OPEN netid 00 roots 6 tree 10 handles 0
auth do=index  -> handles for 10 entity(ies)
                  | #1 AKM netid 00 | #3..#6 BandageDressing netid 00 | #7 FirstAidKit netid 00
auth do=peek handle=2 -> #2 Mag_AKM_30Rnd netid 00
auth do=peek handle=8 -> #8 BandageDressing netid 00
```

- **The ordinary open path needed no changes.** `RequestOpenAs` was given the authority
  instead of a box in the world; it asked the bridge for the id the authority stands for and
  restored into it.
- **Every entity in it has netid `00`**, the container included. `OZS_Records` now seeds
  `parentLocal` from the box, so the top-level items are built with
  `LocationCreateLocalEntity` like the nested ones, and no move is published.
- **`boxes registered 4` before and after**: an authority does not join the controller's
  register, so `FindById` cannot confuse it with the real box of the same id.
- Handles number the tree depth-first: #1 AKM, #2 its magazine, #3–#6 the loose bandages,
  #7 the kit, #8–#10 its contents.

## 2. No client sees it

The client's own view of the same spot (`OZ_ProbeClientControl`, `tree 14`, which runs
`GetObjectsAtPosition` on the client):

```
=== tree at 7502.74 313.99 7502.84 r=14
OZ_StorageBox_Medium #08128 parent=- loc=1 W at 7500.00 313.31 7500.00
total roots 1, lines 1, indexed-with-parent 0
```

The server's list of the same spot at the same moment:

```
OZ_StorageBox_Medium #08128 loc=1 W box=-1600660530-... CLOSED      <- the real box
ZOMBIE OZ_StorageBox_Medium #00 loc=1 W box=-1600660530-... OPEN    <- the authority
ZOMBIE AKM #00 loc=2 in=OZ_StorageBox_Medium#00
ZOMBIE Mag_AKM_30Rnd #00 loc=2 in=AKM#00
ZOMBIE FirstAidKit #00 loc=3 in=OZ_StorageBox_Medium#00
3x ZOMBIE BandageDressing #00 loc=3 in=FirstAidKit#00
4x ZOMBIE BandageDressing #00 loc=3 in=OZ_StorageBox_Medium#00
total 18, zombies 11
```

**Server 2 containers and 10 items; client 1 container and 0 items.**

The watcher's word "ZOMBIE" means only "netid 00" -- its heuristic for the stuck entities of
2026-09-18. Here netid 00 is the whole point, and the two cases are opposites: a ghost is
something the CLIENT sees and the server cannot delete; this is something the server holds
and no client has ever heard of.

## 3. After a restart there is no trace

The authority was left **OPEN and full**, the client disconnected, and the server stopped the
ordinary way -- `server_stop` **saves first**, so this is the engine writing its world save
with ten unannounced entities standing in it.

After the boot, the same spot:

```
=== scan at 7500.00 313.30 7500.00 r=20
OZ_StorageBox_Medium #09082 loc=1 W box=-1600660530-... CLOSED
total 1, zombies 0
```

Nothing came back: not the container, not one of the ten items. `log_verdict`: 0 errors, 0
crashes; the two warnings are the boot's usual "the bridge is down" before the bridge answers
and three missing classes from another mod's boxes.

And SQL was not harmed by the box being open when the server went:

```
storage: boot: box -1600660530-... CLOSED with 0 entities, bridge says open
  -> the engine's save predates the open; SQL wins, 6 stored
```

That is `ApplyBootRule`'s last branch, written for the crash case, doing exactly the right
thing here: the world box is empty, SQL's version is the truth, and `PostClosedAtBoot` puts
the status back to closed. Version 3900 survived whole.

## 4. The round trip is faithful

Before the authority existed, SQL held version 3899 (an ordinary close of the real box).
`auth do=close` then wrote version 3900 **from the authority**, and the two versions are the
same row for row: 6 roots, 10 entities, the same cells, the same health, the same
`Mag_AKM_30Rnd` with `ammo 17`. A blob written by a local, unannounced entity reads back
exactly like one written by an announced entity.

This is also a regression check on the ordinary path: version 3899 was written by the real
box **after** the `OZS_Records` change, and matches what came before it.

## 5. Discard leaves nothing

`auth do=discard` deletes the contents deepest-first and then the container, writing nothing
and asking the bridge nothing. The scan right afterwards: `7 entities, 0 zombies` -- the
player's own clothing and the real box, and not one netid-00 object left standing.

## What is deliberately not done yet

- **Nobody closes an authority on its own.** It is outside the controller's register, so the
  auto-close tick and the shutdown `CloseAll` never see it. Its life will be governed by the
  proxies that hold it (stages B and F).
- **An open authority marks the box open in SQL**, because it uses the ordinary open. Per-
  operation commit (stage E) replaces that.
- Nothing here measures cost. A thousand items on an authority is stage G.
