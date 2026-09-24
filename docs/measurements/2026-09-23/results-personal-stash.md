# Personal stash: slots, the per-player filter, the anchor (2026-09-23, stand)

Design: `E:/openzone/docs/specs/2026-09-23-storage-personal-stash-design.md`.
Stand: retail server, one connected client, bridge running. Everything below was seen in
the game, not reasoned about.

## M1 -- vanilla character slots on a plain container

A throwaway class `OZ_StashSlotProbe : Container_Base` was given the eight vanilla
character slots -- `Headgear`, `Body`, `Vest`, `Back`, `Legs`, `Feet`, `Gloves`,
`Armband` -- and put on the ground beside the player.

**All eight are accepted on a plain container and the engine draws its own ghost icons
for them**, seen in the game as the `Спорядження` group on the stash's panel. Nothing is
added to the clothing side, so the trap of T148506 (a vanilla item that declares
`inventorySlot` as a string, which silently drops a mod's `inventorySlot[] +=`) never
arises here.

A first draft of this file carried a table naming one vanilla item per slot. It was
written from memory rather than from the run and at least one of its class names
(`TTSKOJacket`) does not exist in any loaded config -- the real one is
`TTsKOJacket_Camo`. The table is gone; what stands above is what was actually seen.

## Why items in the stash would not hold anything

The owner reported on 2026-09-23: clothing kept in the stash could be EMPTIED but not
FILLED. That asymmetry is the signature of `CanReceiveItemIntoCargo`, which gates putting
in and not taking out. There turned out to be two separate gates, and only the second one
was shutting the pocket.

### Gate one: cargo depth -- real, and it passes for a slot

`EntityAI.AreChildrenAccessible()` (`3_game/entities/entityai.c:1662`) walks UP the
hierarchy and returns false the moment an ancestor's location is `CARGO` or `PROXYCARGO`,
while `ATTACHMENT` and `HANDS` only increment a counter checked against
`GameConstants.INVENTORY_MAX_REACHABLE_DEPTH_ATT`, which is **2**.

So a jacket dropped into the stash's CARGO GRID is unreachable -- and so is one in a plain
vanilla sea chest; that half is vanilla and we do not change it. A jacket in an ATTACHMENT
SLOT costs one step of two and passes this gate.

### Gate two: clothing refuses any parent that is not a person -- this was the fault

```
// 4_world/entities/itembase/clothing_base.c:66
bool CanReceiveItemIntoCargoClothingConditions(EntityAI item)
{
    EntityAI hierarchyParent = GetHierarchyParent();
    return !hierarchyParent || hierarchyParent.IsMan() || SmershException(hierarchyParent);
}
```

Loose, or worn by a Man -- nothing else. A container is not a Man, so a jacket hung in the
stash's `Body` slot passed the depth gate and was refused here.

Vanilla makes exactly this exception for its own personal stash, in the sibling method of
the same file (line 93): `if (parent && parent.IsInherited(UndergroundStash)) return true;`.
`OZS_ClothingCargo.c` says the same for `OZ_PersonalStash`, in both methods, and falls
through to vanilla for every other parent. **Deliberately not extended to the published
storage boxes.**

### Measured after the change

| Host | Where the clothing is | Move an item into its cargo |
|---|---|---|
| `OZ_PersonalStash` | `Body` slot | **accepted** -- "both levels accepted" |
| `SeaChest` (vanilla control) | cargo grid | refused, unchanged |

The first draft of this file blamed the probe (`oz_storage op=nest`) for measuring its own
artefacts, on the grounds that it refused on every container including vanilla. That was
wrong: the probe was reporting the clothing gate correctly all along, and it flipped to
"accepted" the moment the gate was widened. The control shows it is still honest.

## B, C and D: the key, the opening, the exits (evening)

Owner's word: "продолжай работу, одним заходом". Stand: retail server, the Discord bridge
live on 8787, one client.

### The shape this took

**A stash IS a storage box.** `OZ_PersonalStash` now inherits `OZ_StorageBox`, so the
four-state machine, the paced open that reads SQL, the paced close that writes it, the
audit hooks, the idle clock, the viewer list and the boot reconcile are the SAME CODE,
not a second copy. Two things differ and only two:

- **The key is a pair.** A box is keyed by the engine's persistent id; a stash by
  `s_<anchor>_<uid>` -- which locker, and whose. It is one string because the bridge's
  `box_id` is opaque text, so nothing in the schema had to change. `OZS_Const.StashId`
  spells it on the game side and `Xchg.splitId` is its mirror on the bridge's.
- **It exists only while it is being used.** A box stands where it was placed; a stash is
  created at the player's feet on the verb and removed when the close has written it.

### B -- the key and the bridge

| Check | Result |
|---|---|
| The engine composes the pair | `storage: stash s_11542x3377_76561198014475380 created for Survivor` |
| The bridge accepts it as a box id and as a close file name | yes; `test/storage-xchg.mjs` has seven new cases, including that a stash name is refused for a box and a path is refused in either |
| The admin lists it beside the boxes with its owner | `s_11542x3377_76561198014475380  OZ_PersonalStash  closed  v3370` in `storage.mjs boxes`; the web listing has a `Whose` column and a Boxes/Stashes filter |
| The whole bridge suite | 21 files, 0 failures |

### C -- opening

| Check | Result |
|---|---|
| The verb creates the stash and opens it | `opening by Survivor (0 stored)` -> `opened ... 0 items` |
| A second press does not cost the contents | the standing stash is reused and brought to the player's feet, never deleted |
| The contents come back from SQL | `opened by Survivor: 1 items (2 entities) ... missed 0` after a close that wrote version 3372 |
| An admin rollback reaches the game | rolled back to 3369 from the console; the next open restored that content |

### D -- the four exits

Every one of them ends in the same two steps, in this order: the contents go to SQL, then
the entity goes. `OZS_Stashes.Drop` is the only place a stash is removed and it refuses to
remove one that is not CLOSED or still holds entities.

| Exit | Hook | Seen |
|---|---|---|
| The window is shut | `OZS_Controller.OnView(viewing=false)` | `stops looking` -> `closing by stash: 1 items (2 entities) written ... version 3372` -> `removed from the world (stash)` |
| Away or idle | `AutoCloseTick` -> `OZS_Stashes.ShouldClose` (6 m, 60 s) | closed after a teleport 50 m away. NOTE: the viewer is dropped on distance first, so in practice the WINDOW exit fires before the distance one -- both end in the same close, and nothing was lost |
| Disconnect | `PlayerBase.OnDisconnect` -> `OnPlayerLeft` -> `CloseAllOf(uid, "left")` | client killed with the stash open; after the logout timer, version 3374, 1 item / 2 entities, `removed from the world` |
| Death | `PlayerBase.EEKilled` -> the same | health set to 0 with the stash open: version 3375, 1 item / 2 entities, removed |
| A crash (not one of the four) | the boot rule, then `OZS_Stashes.Sweep()` | a stash left from the previous session: `boot: box s_... CLOSING with 0 entities, bridge says closed -> closed` then `removed from the world (boot)` |

A stash that survived a restart WITHOUT ITS KEY is left standing and shouted about rather
than deleted with somebody's kit inside -- seen twice while the class was being changed
under it: `a stash survived the restart with no key, holding 3 entity(ies) at ...; it is
left standing for an admin`.

### A defect in the restore, older than the stash, found by it

The first round trip logged `missed 1` and

```
storage: TTsKOJacket_Camo with 1 items could not be moved into OZ_PersonalStash at -1,-1;
it stays where it was built
```

and the next shutdown then wrote an EMPTY version over a full one, because the jacket was
standing on the ground rather than in the stash. The cause: `OZS_Records.Create` builds any
item that HAS CARGO CHILDREN on the ground first and moves it into place afterwards
(the local-tree rule from the ghost fix, 2026-09-18), and `ApplyMoves` only ever knew how
to move into CARGO -- `SetCargo`, `TakeEntityToCargoEx`, `TakeEntityToCargo`, and nothing
for a slot.

So **any attachment with contents failed to return to its slot**. The boxes rarely showed
it because a weapon's own parts are attachments rather than cargo; the stash shows it at
once, since every garment in it is a slotted item with pockets.

`OZS_Move` now carries the slot and `ApplyMoves` tries `SetAttachment` + `TakeToDst`, then
`TakeEntityAsAttachmentEx`, before the cargo ladder -- and a slotted item DELIBERATELY does
not fall back into cargo: it would look restored, be in the wrong place, and the next close
would write that wrong place down as the truth. Measured after: `missed 0`, no warning, and
the jacket came back in its `Body` slot with its nail inside.

### What SQL holds at the end of the run

```
items of the current version (3375):
root_idx  node_idx  parent  type              loc_type  slot     row  col  quantity
0         0         -1      TTsKOJacket_Camo  2         5119774  -1   -1   0
0         1         0       Nail              3         -1       0    0    70
```

`loc_type 2` is ATTACHMENT and the slot is the `Body` id, so the garment is recorded as
SLOTTED and its nail as its cargo -- the shape the restore now reads back. Every close
after the fix wrote 1 root / 2 entities; versions 3372, 3373, 3374 and 3375 are the four
exits in order, none of them duplicated an item and none lost one.

## How deep does it go? (the owner's question, evening)

"А если их будет четыре? Зачем мы хардкодим какие-то уровни?"

**Nothing in the mod counts levels.** A record is a parent-indexed tree: every node names
its parent by index, and the restore walks whatever the capture found, at any depth.
`OZS_ClothingCargo` no longer counts steps either -- it asks the engine for
`GetHierarchyRoot()` and checks whether a stash is at the top, so there is no number
there to get wrong and none to raise later. (It briefly had a guard of six; that was a
cycle guard, and the owner was right that it had no business being a magic number.)

**The one depth limit belongs to the engine**, and it is the same on a player's own body:
`AreChildrenAccessible()` spends a budget of `INVENTORY_MAX_REACHABLE_DEPTH_ATT = 2`
attachment steps, and any ancestor in CARGO cuts the chain outright at any depth.

Measured with a chain probe that takes `Class@Slot,...` of any length
(`oz_storage op=chain` in the stand-only pbo):

| Chain | Links attach? | Cargo of the last link |
|---|---|---|
| stash <- `TTsKOJacket_Camo@Body` | yes | reachable, `missed 0` after a round trip |
| stash <- `PlateCarrierVest@Vest` <- `PlateCarrierPouches@VestPouch` | yes | **reachable**: "every link accepted", `AreChildrenAccessible=true` |
| stash <- `PlateCarrierVest@Vest` <- `PlateCarrierHolster@VestHolster` <- `Glock19@Pistol` | **yes, all three** | **`AreChildrenAccessible=false`** -- the budget of two is spent |

So a fourth level can be BUILT and STORED; what the engine refuses is REACHING into it to
move things, and it refuses that on a character too. Storing and restoring are not capped:
the three-link tree above hangs together and comes back.

The two-link tree survives the round trip intact -- SQL after a close:

```
root_idx  node_idx  parent  type                 loc_type  slot
0         0         -1      PlateCarrierVest     2         6119694
0         1         0       PlateCarrierPouches  2         -230399667
0         2         1       Nail                 3         -1
```

### A second defect in the restore, and why the first fix was not enough

The two-link tree first came back BROKEN: `PlateCarrierPouches with 1 items could not be
moved into PlateCarrierVest at cell -1,-1`. The slot fix of that morning carried the slot
id into the move but used `slot >= 0` as the test for "this goes into a slot" --

**and a slot id can be negative.** `VestPouch` is `-230399667`; `Body` is `5119774`. So the
jacket went home and the pouches went on the ground, and the sign of a hash decided which.
`OZS_Move` now carries an explicit `toSlot` flag beside the id. Measured after: `missed 0`,
no warning, and the tree above is what a close writes back.

## The slot list, and the slots a nested item does not get

Two findings from the owner's second pass over the stash, 2026-09-23.

### The stash was missing half the character's slots -- fixed

`SurvivorBase.attachments[]` (characters_data.pbo) is, in order:

```
Head, Shoulder, Melee, Headgear, Mask, Eyewear, Hands, LeftHand, Gloves,
Armband, Vest, Body, Back, Hips, Legs, Feet, Splint_Right
```

The stash had carried eight of them. `Mask` (face), `Eyewear`, `Hips` (the belt, and with
it a holster), `Shoulder` and `Melee` were simply left out -- there is no rule behind it,
it was an incomplete list. All five added; `Head`, `Hands` and `LeftHand` are the body
rather than storage and `Splint_Right` is a dressing, so those four stay out. Seen in the
game: `Спорядження` now draws eleven ghost icons over two rows and `Зброя` six.

### An item in a slot shows its CARGO but never its own SLOTS -- vanilla, not ours

A mountain bag held in the hands draws its three slots (`Chemlight`, `WalkieTalkie`,
`Backpack_1`); the same bag in the stash's `Back` slot draws only its 7x8 grid. The reason
is one branch in the inventory UI:

| Where the item hangs | Widget the UI builds |
|---|---|
| on the PLAYER (`playercontainer.c:144-161`) | `if (item.GetSlotsCountCorrect() > 0)` -> **`ContainerWithCargoAndAttachments`**, else `ContainerWithCargo` |
| in any other container's slot row (`attachmentcategoriesrow.c:703-705`) | `if (item.GetInventory().GetCargo())` -> **`ContainerWithCargo`**, and there is no other branch |

So only the player's own gear panel ever builds the widget that can draw a nested item's
slots. Every other container in the game -- a car, a vanilla chest, ours -- shows cargo
only. Nothing in our config or script reaches this.

Fixing it means `modded class AttachmentCategoriesRow` carrying a copy of `RefreshSlot`
(107 lines, `attachmentcategoriesrow.c:655-761`) with the player's branch grafted in. That
is a vanilla widget class used by every container in the game, and a copied method of that
size breaks on the next DayZ patch and collides with any other mod that touches it. Left
undone pending the owner's word; the workaround is to take the bag out, arrange it, put it
back -- its contents ride along either way.

## M2 -- the vicinity filter

Two `OZ_PersonalStash` entities were placed 1 m and 1.5 m from the player, tagged with two
different owner uids.

| Case | Expected | Seen |
|---|---|---|
| Stash owned by the local player | in the vicinity panel, cargo readable | listed as `ОСОБИСТИЙ СХРОН (0/500)` with the `Спорядження` and `Зброя` groups |
| Stash owned by somebody else | absent from the panel | absent |
| A non-stash container at the same distance | untouched by the override | listed |

The filter is `VicinityItemManager.ExcludeFromContainer_Phase1/2/3`. It is honest only on an
honest client -- see the note at the head of `OZ_PersonalStash.c`; the owner accepted that
trade on 2026-09-23.

Open cosmetic defect: the collapsed vicinity row for a stash draws three crossed-out slot
icons above the category rows. To be fixed before task C.

## A -- the anchor

`OZ_StashAnchor` inherits the vanilla grey closed locker
`StaticObj_Furniture_locker_closed_v1` (config `structures_furniture.pbo`, model
`DZ\structures\Furniture\Cases\locker\locker_closed_v1.p3d`, parent `HouseNoDestruct`).

| Check | Result |
|---|---|
| The locker stands in the world | yes, grey, from the vanilla model, spawned at a given position |
| The verb is offered by looking at it | yes: `F  Відкрити особистий схрон`, at 1.3 m and at 2.2 m |
| The verb creates a stash | yes: `storage: stash opened for 76561198014475380 at the anchor` |
| Four opens in a row leave one stash | yes: 1 of 1 `OZ_PersonalStash` within 40 m -- **and that was a defect, not a pass: see below** |
| The inventory opens by itself after the verb | yes, half a second later |
| The anchor is in nobody's vicinity panel | yes -- and for free: `Building.IsInventoryVisible()` already returns false (`3_game/entities/building.c:264`) |

### The defect in the first cut of the anchor

The first `OnExecuteServer` DELETED any stash this player already had before making a new
one, and this file first reported the "exactly one stash" that produced as a passing check.
The owner spotted what that meant: pressing the verb a second time destroyed everything in
the stash, because until tasks B-E give it a home in SQL the entity in the world is the
only copy of its contents. Corrected on the same day -- an existing stash is now REUSED and
brought to the player's feet, and nothing in this increment deletes one. Closing a stash at
all is task D.

### Two engine facts this cost

- **A building keeps its own action map.** `BuildingBase.InitializeActions()` builds one per
  type and hands it out through `GetActions()`, so a verb on a `House` is attached in that
  class's `SetActions()`. Registering the action in `ActionConstructor` is necessary but not
  sufficient -- with only the registration the verb never appears, and nothing is logged.
- **`ActionInteractBase` has no `OnExecuteServer` of its own; `AnimatedActionBase` does**
  (`animatedactionbase.c:175`), together with `OnExecuteClient` (`:179`). Both are
  `protected` and are called from the animation event, which is why the client twin is the
  right place to open a window.

`CCTCursor` rather than `CCTObject` for the target: a locker is two metres tall and its
origin sits on the floor, so `CCTObject` (which compares `GetPosition()`) measures from the
base while the player is looking at the top shelf. Vanilla's own underground lever makes the
same choice. `IsLockTargetOnUse()` is false so that two players at one locker do not queue.

`world_action(OZS_ActionOpenStash)` from the MCP is refused by the action's own `Can()`: that
path builds a synthetic `ActionTarget` whose cursor hit position is the zero vector, which no
`CCTCursor` can accept. Not a fault in the action -- the same action passes with a real
cursor, as the table above shows. Use the gamepad interact button to exercise it from here.
