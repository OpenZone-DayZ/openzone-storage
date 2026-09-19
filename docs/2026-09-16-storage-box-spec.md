# OpenZone Storage: the box (specification, draft 2)

Date: 2026-09-16. Status: **built and measured; draft 2 for the owner**. Sections 1--12 are the
research and the design as decided; section 13 is the delta between that design and the code
as built and measured the same evening (`docs/measurements/2026-09-16/results-implementation.md`). Decided by the owner the same day: model C
of the survey (real cargo, items exist only while the box is open, materialisation paced),
three box sizes of 500 / 1000 / 1500 cells, weapon slots on the box. Everything marked
*owner* below is still theirs to decide; everything marked *measured* is a stand result
(`docs/measurements/2026-09-16/`, runs 1--5) or a source fact.

## 1. Boxes

| Class | Grid (width x rows) | Cells | Weapon slots (*owner*, proposal) | Donor model until own art |
|---|---|---|---|---|
| `OZ_StorageBox_Small`  | 10 x 50  | 500  | 2 | `WoodenCrate` |
| `OZ_StorageBox_Medium` | 10 x 100 | 1000 | 4 | `SeaChest` |
| `OZ_StorageBox_Large`  | 10 x 150 | 1500 | 6 | `SeaChest` (scaled art later) |

- Width 10 is the vanilla inventory width; the engine caps a grid at 256 rows (*measured*),
  so all three fit with room to spare. Bigger boxes are more boxes.
- All three extend one script class `OZ_StorageBox` (`Container_Base` lineage like SeaChest,
  `DeployableContainer_Base` if placement by kit is wanted later). A box cannot be picked up
  or put into cargo (`CanPutInCargo`, `CanPutIntoHands` false), which is what stops a box
  from being duplicated with its contents.
- **Weapon slots** are attachment slots `OZ_Weapon_1..N` declared in `CfgSlots`
  (`class Slot_OZ_Weapon_1 { name = "OZ_Weapon_1"; displayName = "$STR_OZ_SLOT_WEAPON"; }`, the
  shape OpenZone_PDA already uses) and listed in the box's `attachments[]`; a
  `GUIInventoryAttachmentsProps` group shows them as one "Weapons" row, and
  `CanDisplayAttachmentCategory` hides the row while the box is closed (entityai.c:1781). The
  engine resolves a slot by the CLASS name `Slot_<name>` (inventoryslots.c:23-42) and
  pre-populates constants only for the first 32 vanilla slots, so the mod always resolves its
  ids at runtime with `InventorySlots.GetSlotIdFromString("OZ_Weapon_1")`. Vanilla weapons
  accept the slots through a config patch: `Rifle_Base` declares
  `inventorySlot[] = {"Shoulder","Melee"}` and `Pistol_Base` `{"Pistol"}` as ARRAYS
  (*measured*: `dz/config.bin` 4033 / 4285) and the concrete rifles (AKM, M4A1, Mosin) inherit
  without redeclaring, so `inventorySlot[] += {"OZ_Weapon_1", ...}` on `Rifle_Base` and
  `Pistol_Base` works without the T148506 string trap. Four vanilla attachment classes declare
  `inventorySlot` as a string (AUG optic, SCAR-H front sight, SSG82 optic, splint) -- irrelevant
  for the weapon slots, but the reason the patch targets the two bases only. Modded weapons that
  redeclare `inventorySlot[]` themselves need the same patch in a glue pbo, or they simply do
  not fit the slots; both are acceptable.
- Closed box: cargo and slots are hidden and refuse everything --
  `GetInventory().LockInventory(HIDE_INV_FROM_SCRIPT)` on close, unlock on open (MMG's
  pattern), plus `CanReceiveItemIntoCargo`, `CanReceiveAttachment`, `CanDisplayCargo`,
  `CanDisplayAttachmentSlot` gated on the state. The only thing a closed box shows the player
  is the Open action with the count of stored items.
- State on the box: `m_OZ_State` in {CLOSED, OPENING, OPEN, CLOSING}, netsynced as an int so
  the client draws the lid and the action text. Persisted in the box's `OnStoreSave` together
  with the box id (section 6) -- the boot rules in section 7 read it.

## 2. Actions

The vanilla template is the barrel (`Barrel_ColorBase`, `4_world/entities/itembase/barrel_colorbase.c`,
and `ActionOpenBarrel` / `ActionCloseBarrel`, `.../actions/interact/actionopenbarrel.c`), read
through the knowledge base:

- The barrel keeps an `OpenableBehaviour m_Openable` and netsyncs the bool by path:
  `RegisterNetSyncVariableBool("m_Openable.m_IsOpened")` (barrel_colorbase.c:21). `Open()` /
  `Close()` flip it, call `SetTakeable(false/true)` and `UpdateVisualState()`, which drives the
  lid with `SetAnimationPhase("Lid", 1 or 0)`; `OpenLoad()` / `CloseLoad()` are the same plus
  `SetSynchDirty()`, used from `OnStoreLoad`; `OnStoreSave` writes `m_Openable.IsOpened()`; the
  client redraws the lid from `OnVariablesSynchronized` (lines 158-163) and that is the whole
  replication path. Sounds go through `StartItemSoundServer(id)` + `ItemSoundHandler`, a synced
  int; `SoundSynchRemote` is a deprecated stub in this build (itembase.c:4857-4876).
- Gates: `ItemBase.CanDisplayCargo()` is generic and returns `IsOpen()` (itembase.c:4148-4156),
  so a box that overrides `IsOpen()` hides its cargo in every client UI for free;
  `CanReceiveItemIntoCargo` and `CanReleaseCargo` return false unless `IsOpen()`;
  `CanPutInCargo` / `CanPutIntoHands` only when empty and closed (barrel_colorbase.c:490-523).
  **`CanLoadItemIntoCargo` must stay ungated**: it runs at server start when the storage is
  loaded (entityai.c:1567-1576), and gating it on "closed" would drop a box's cargo at boot --
  vanilla removed a ruined-check from the receive gate for exactly that reason
  (itembase.c:4193).
- `ActionOpenBarrel: ActionInteractBase` sets `m_CommandUID = CMD_ACTIONMOD_INTERACTONCE`,
  `m_StanceMask = ERECT | CROUCH`, `m_Text = "#open"`; `ActionCondition` casts the target and
  returns `!IsLocked() && !IsOpen()`; `OnExecuteServer` calls `ntarget.Open()` and plays the
  soundset. 39 lines in total. `ActionCloseBarrel` calls `DetermineAction(player)` instead,
  a server-side hook that may consume or mutate cargo before `Close()` -- the slot our capture
  takes. `ActionInteractBase.CreateConditionComponents` is `CCINone` + `CCTObject(UAMaxDistances.DEFAULT)`,
  `DEFAULT = 2.0` (actionconstants.c:109-119). `OnStartServer` fires when the action is
  accepted, `OnExecuteServer` on the animation's event frame (animatedactionbase.c:175-190).

The box has the same two actions, `OZ_ActionOpenBox` and `OZ_ActionCloseBox`, with three
differences: the condition reads the box's four-state `m_OZ_State` (Open only from CLOSED, Close
only from OPEN), the text carries the stored count (`Open (N items)`, N from the store header
read once at load), and `OnExecuteServer` does not flip the state itself but hands the box to
`OZ_BoxController` which runs the OPENING / CLOSING jobs of sections 4 and 5 and flips the
state when they finish. Close is refused with a message while the box has viewers (section 3)
or an OPENING job runs. There is no third action; the auto-close is insurance, not a verb.

## 3. Viewers and auto-close (item 1 of the brief)

Facts from the sources: the server enforces reach on every inventory move --
`GameInventory.c_MaxItemDistanceRadius = 2.5` with `CheckRequestSrc / CheckTakeItemRequest /
CheckMoveToDstRequest(requestingPlayer, src, dst, radius)` (inventory.c:815-819) -- but it
keeps no record of who is looking at a container; nothing in `GameInventory` or `CargoBase`
names a viewer. The client does know: `Inventory: LayoutHolder` has `OnShow()` / `OnHide()`
(5_mission/gui/inventorynew/inventory.c:1234, 1266) and `VicinityItemManager.GetInstance()`
exposes `GetVicinityItems()` / `GetVicinityCargos()`, refreshed every 0.25 s within 0.5 m for
items, 2 m for actors, 3 m for large actors (vicinityitemmanager.c:3-8). The mod builds the
server-side notion on top:

- The client half sends `OZ_Storage.View(boxId, on)` from `PlayerBase.OnInventoryMenuOpen()` /
  `OnInventoryMenuClose()` -- empty vanilla declarations (playerbase.c:6012-6013) that
  `MissionGameplay.ShowInventory` / `HideInventory` call on the client only
  (missiongameplay.c:1153-1184) -- for every `OZ_StorageBox` in `GetVicinityItems()`, re-sends
  it when the vicinity list changes while the screen is open, and a heartbeat every 5 s. The
  server keeps `map<box, map<playerId, lastSeen>>`. The server's own reach check adds the
  root's collision radius to the 2.5 m (`PlayerCheckRequestSrc`, dayzplayerinventory.c:2848-2853).
- A viewer is dropped when the client says so, when the heartbeat is older than 15 s, when the
  player is farther than 5 m from the box (checked on every heartbeat and on Close -- twice the
  engine's 2.5 m reach, so a player who can still move items is never dropped), when the
  player dies (`PlayerBase.EEKilled`) or disconnects. The disconnect hook is
  `PlayerBase.OnDisconnect()` (playerbase.c:7320-7339), which `MissionServer.PlayerDisconnected`
  calls after the logout timer and right before `player.Save()`; `OnClientDisconnectedEvent` is
  too early because the logout can still be cancelled (missionserver.c:628-711, and CF's
  measured order in cf_modstoragemodule.c:53-61).
- **Close is refused while viewers > 0.** The one who wants to close sees "someone is using
  the box".
- **Auto-close** (*owner*: numbers): every 10 s the server checks open boxes; a box with no
  viewers and no player within `AutoCloseRadius` (default 15 m) for `AutoCloseQuietSeconds`
  (default 120 s) closes itself. The opener dying or leaving is not a trigger by itself, only a
  reason the viewer set empties.
- `MissionServer.OnMissionFinish` (declared on `Mission`, gameplay.c:702, not overridden by
  vanilla `MissionServer` but by CF's modded one, cf missionserver.c:40-45) runs on a graceful
  shutdown or `#shutdown`, not on a killed process; it closes every open box synchronously,
  without pacing. A kill has no script hook at all (CF detects one only by a `.lock` file left
  behind, cf_modstoragemodule.c:191-196); it is covered by the boot rules of section 7.
- A client without the mod cannot join (the pbo is in `-mod`), so the RPC is always present.

## 4. Open: paced materialisation

*Measured* on the stand: creating one-slot items costs 0.07--0.15 ms each on the server and a
rifle with three attachments 1.6 ms; 5000 in one frame = 362 ms (816 ms with a client next
to the box); 50 per frame keeps every frame under 100 ms; the client receives items created
beside it at 900--1700/s without frame drops, but one 5000-item burst stalled a client for
39 s; paced at 250/s nothing stalled.

- OPENING is a per-frame job on `MissionServer.OnUpdate` with two limits from config:
  `OpenFrameBudgetMs` (default 20) and `OpenItemsPerSecond` (default 250). Each frame creates
  entities until either limit is hit. A full 1500-cell box opens in 6 s at 250/s; a typical
  box with a few hundred items in 1--2 s. The lid animates and the state becomes OPEN when the
  last entity exists; until then the cargo stays locked.
- Creation order per record: the entity first, then its attachments (recursively), then its
  cargo children. A container that has cargo children is created **on the ground beside the
  box, filled, then moved into its cell** with
  `box.GetInventory().TakeEntityToCargoEx(InventoryMode.SERVER, item, idx, row, col)` -- the
  SERVER mode does `LocationSyncMoveEntity` and also sends the `SYNC_MOVE` inventory command
  to clients (inventory.c:1050-1073), which the bare `LocationSyncMoveEntity` the probe used
  does not. The engine refuses children to a container that already sits in cargo
  (*measured*: 0 of 1000/500/500 with all three creation calls; 3 of 3 cases restored with
  contents through the ground path). Two more vanilla vetoes on the move: a container never
  enters a container of its own type (`Container_Base.CanPutInCargo`, container_base.c:8-17),
  and backpacks with contents cannot enter a box at all
  (`Clothing_Base.CanPutInCargoClothingConditions`). Both unchanged.
- The move runs the script gates (`CanPutInCargo`, `CanReceiveItemIntoCargo`) inside the
  native, so the box must count as open for its own restore: the OPENING state answers
  `IsOpen()` true to the gates and false to the actions.
- Cells and slots come from the record: `CreateEntityInCargoEx(type, idx, row, col, flip)` for
  cargo (the only call that restores `flip`), `LocationCreateEntity(SetAttachment(parent, null,
  slot), type, ECE_IN_INVENTORY, RF_DEFAULT)` for weapon slots and attachments.
- If a record's class no longer exists (`ConfigIsExisting` on CfgVehicles / CfgWeapons /
  CfgMagazines), the record is skipped and logged; its blob is skipped by the list file
  (section 6), so the rest of the box still opens.

## 5. Close: capture and delete

- CLOSING locks the inventory first (nobody can add anything from this frame on), walks the
  weapon slots and the cargo recursively, writes the two files of section 6 atomically, then
  deletes the entities in batches of 50 per frame (*measured*: 5000 deletes = 259 ms in one
  frame, 6--8 ms per batch of 50). The state becomes CLOSED when the last entity is gone.
- Capture is cheap: 19 ms for 5000 items, 7 ms to serialise, 10 ms to write (*measured*), so
  it runs in the frame of the action. A box of 1500 is well under the 150 ms criterion.

## 6. Store (item 5 of the brief)

One directory per box under `$profile:OpenZone/Storage/<boxid>/`, box id = the box's
persistent id (`GetPersistentID(b1,b2,b3,b4)` as hex, stable across restarts). Two files, written on every Close, read only on Open and at mission start:

| File | Content | Purpose |
|---|---|---|
| `items.bin` | `FileSerializer`: format version, `g_Game.SaveVersion()`, timestamp, box class, grid; then one record per root item: type, location (type, slot, row, col, flip), children records, weapon chambers, **`OnStoreSave` blob**, magazine cartridges, health per zone, lifetime | full fidelity: quantity, wetness, temperature, liquid, agents, energy, colour, quickbar, the radio's frequency, CF_ModStorage of every mod -- all ride inside `OnStoreSave` (*measured*: exact round trip of AKM + loaded magazine + chambered round, Rag, Canteen, Battery9V, `OZ_Radio_100m` at index 3, `OZ_DataCarrier_Chip` with two CF notes) |
| `items.list` | the same tree without blobs: type, location, global and per-zone health, quantity, liquid, magazine count, chamber -- fixed-layout, one record per line via `FPrintln` | the **fallback** of item 3: readable no matter what `OnStoreLoad` does |

- `OnStoreLoad` returning false leaves the stream position undefined (*measured on the stand
  code, by construction*), so a refused blob ends the readable part of `items.bin`. Rule: if
  `SaveVersion` in the header differs from the running game's, or any `OnStoreLoad` refuses,
  the rest of the box is materialised from `items.list` (type, place, native layer) and the
  loss of script state is written to the log per item, with the box id. The files are kept
  as `items.bin.failed-<time>` for a manual look.
- Atomic write: `items.bin.new` + `items.list.new`, then `CopyFile` over the live names and
  `DeleteFile` of the `.new` (FileSerializer has no rename; CopyFile exists).
- No per-change journal (*owner*). While the box is open the engine autosaves its cargo
  every minute anyway (*measured*: `dynamic_00N.bin` round-robin, one file per second, with
  a player connected), so the engine's own save is the journal and the boot rules below
  turn it back into a store. The owner's journal from the brief can be added later if a
  restart test finds a gap; the SQLite sidecar (phase 2) stays unnecessary for these sizes.
- **No import** (*owner*, 2026-09-17). Item 6 of the brief is dropped. Nothing but logs
  will ever leave the live server, and the owner decided the boxes are not replaced in
  place: players carry their things over by hand. So there is no converter, and the
  previous mod's pbo stays unopened.

## 7. One truth at a time (item 4 of the brief)

*Measured*: neither `ECE_DYNAMIC_PERSISTENCY` (run 5) nor `ECE_NOPERSISTENCY_WORLD` (run 6,
"do not save this object in world") does anything for items created in a container's cargo --
99 flagged and 100 / 97 unflagged items came back after autosave + kill + restart both times,
and a flagged item dropped on the ground came back too. The ECE persistency flags govern
objects the mod puts into the world itself; a child of a persistent container is saved with
its parent. There is no script API that makes an existing entity non-persistent
(measured on the stand, 2026-09-16). So the engine WILL save an open box's cargo, and the
design has to live with two copies for a while:

- **OPEN**: the engine is the truth. The store files are kept until the box's next
  `OnStoreSave` after opening (the engine's next save), then deleted, so a crash between
  opening and the first autosave loses nothing.
- **CLOSING**: the files are written first, then the entities go. The state is persisted with
  the box.
- **Boot** (`MissionServer.OnMissionStart`, before players): for every box found in the world,
  by persisted state:
  - CLOSED, files present, cargo empty: nothing.
  - CLOSED or CLOSING or OPENING with files present and cargo not empty: **files win** -- the
    cargo is deleted (a save caught a half-done transition).
  - OPEN, no files: the box was open at the kill; close it now, synchronously, from the
    engine's own cargo (write files, delete entities, CLOSED).
  - OPEN, files present: the engine had not saved since the opening began; files win, cargo
    deleted, state CLOSED.
- `OnMissionFinish`: every open box is closed synchronously (files written, state CLOSING
  persisted); whether the engine saves afterwards or not, the boot rule resolves it.

To prove with the probe once the code exists: restart with an open box, with a closed box,
kill during OPENING, kill during CLOSING -- expected result each time: zero lost, zero
duplicated, and a log line saying which rule fired.

## 8. Composite items

Attachments recurse (a rifle in a weapon slot with a magazine with rounds and an optic with
a battery is one record tree); magazine cartridges and chambers are captured explicitly
because they are engine-native, not script variables. Containers inside the box (protector
cases, ammo boxes, first-aid kits) work through the ground path of section 4; backpacks and
vests must be empty to enter, as in vanilla. Every child counts for the pacing budget.

## 9. Configuration

`$profile:OpenZone/OZ_Storage.json` through the core's config service: `OpenFrameBudgetMs`
(20), `OpenItemsPerSecond` (250), `AutoCloseRadius` (15), `AutoCloseQuietSeconds` (120),
`ViewerHeartbeatSeconds` (5), `ViewerTimeoutSeconds` (15), `ViewerMaxDistance` (5),
`LogLevel`. Capacities and slot counts are config.cpp facts, not JSON.

## 10. Acceptance (from the brief, adjusted to the sizes)

- No main-thread stretch >= 150 ms attributed to the mod, measured with
  `script-profile.ps1`, while opening and closing a full Large box (1500 one-slot items) and
  while opening four Large boxes in a row; the probe's frame monitor as the second witness.
- Client next to the box: no frame above 100 ms during an Open at the default pacing.
- Restart with a box open and a box closed: item count and every field of `inspect`
  identical before and after (hash equal except environmental temperature).
- Kill during OPENING and during CLOSING: at most the last operation lost, never a
  duplicate.
- The radio's frequency, a CF_ModStorage carrier's data and a loaded rifle survive a
  close/open cycle -- already shown by the probe, to be repeated with the mod's own code.

## 11. Decisions for the owner

1. Weapon slots per size: 2 / 4 / 6 as above, or another split.
2. Auto-close numbers: 15 m and 120 s quiet, or other.
3. No per-change journal while open (engine autosave is the journal) -- agree, or keep the
   brief's journal from day one.
4. Pacing defaults 20 ms per frame and 250 items/s -- agree, or faster for small boxes.
5. Donor models until own art (WoodenCrate / SeaChest), or wait for dayz-3d.

## 12. Sources

- Vanilla scripts, build of 2026-08-12, read through the `dayz` MCP knowledge base (core
  layer, 131 697 declarations): `barrel_colorbase.c`, `actionopenbarrel.c`,
  `openablebehaviour.c`, `inventory.c` (reach checks 815-819, move natives 553-592),
  `inventorylocation.c`, `cargo.c`, `container_base.c` (children of a container in cargo),
  `clothing_base.c:38-56` (clothing enters cargo only empty), `vicinityitemmanager.c`,
  `inventory.c:1234/1266` (`OnShow` / `OnHide`), `centraleconomy.c` (ECE flags),
  `objectspawner.c:48` (the object spawner's use of `ECE_DYNAMIC_PERSISTENCY`),
  `transmitterbase.c:10-33` (the radio's frequency in `OnStoreSave`), `serializer.c:103`
  (`FileSerializer`), `game.c:434` (`SaveVersion`).
- `Addons/dz.pbo` `config.bin` converted with CfgConvert: `Rifle_Base` 4033, `Pistol_Base` 4285
  (`inventorySlot[]` arrays); `OpenZone_PDA/config.cpp:23-70` for the `CfgSlots` shape.
- Stand runs of 2026-09-16, `docs/measurements/2026-09-16/`: results-run1..5, client-run3/4,
  the profiler reports; the survey `2026-09-16-virtual-storage-survey.md`.
- Names: `OZ_StorageBox` with `OZ_StorageBox_Small / _Medium / _Large`, `OZS_ActionOpenBox` /
  `OZS_ActionCloseBox`, `OZS_Controller`, `OZS_CloseJob` / `OZS_OpenJob`, `OZS_Store` /
  `OZS_StoreWriter`, `OZS_Records`, `OZS_ListFallback`, `OZS_ClientViewer`, `OZS_Player`,
  `OZS_ActionRegister`, `OZS_Settings`, `OZS_Const`.
- Actions (section 2): registered in `ActionConstructor.RegisterActions` -- without that
  `ItemBase.AddAction` drops an action silently (the error goes to the .RPT only); the stored
  count is a netsynced int on the box (`m_OZS_StoredCount`), so the Open text reads no file;
  the state `m_OZS_State` is netsynced 0..3. Both actions were run by a connected player on
  the stand (`world_action`): "opening by Survivor (1443 stored)", "closing by Survivor".
- Viewers (section 3): the RPC rides on the box entity
  (`Object.RPCSingleParam(OZS_Const.RPC_VIEW_ID)` -> `OZ_StorageBox.OnRPC`), no CF RPC and no
  box id on the wire; the client scans `VicinityItemManager.GetVicinityItems()` twice a second
  while `InventoryMenu.IsOpened()` (*measured*: `FindMenu(MENU_INVENTORY)` answers the hidden
  menu too, so the first cut reported "looking" from a closed screen), sends "looking" when a
  box enters the list and every 4 s, "gone" when it leaves or the screen closes; the server
  prunes by 15 s silence, 5 m distance and presence. Auto-close ticks every 5 s. Extension of
  the draft: a player's disconnect (`PlayerBase.OnDisconnect`) or death (`EEKilled`) closes an
  OPEN box within `AutoCloseRadius` of them at once when nobody else is within the radius or
  looking -- the draft made leaving only a reason for the viewer set to empty. The requester's
  own stale entry never blocks their Close (actions run from the world view, not the screen).
- Open (section 4): a token bucket of `OpenItemsPerSecond` and a frame budget of
  `OpenFrameBudgetMs`, both shared by every box opening at the same time (four boxes at once
  cost the frame the same as one); a class that no longer exists is read by a LOCAL stand-in
  that consumes its blob and is deleted (the probe's method) instead of `ConfigIsExisting` +
  skipping through the list; a broken stream (an `OnStoreLoad` that refuses, a truncated file)
  drops the half-built root, keeps the blob as `items.bin.failed-<stamp>` and continues from
  `items.list`; a `SaveVersion` difference is NOT a fallback trigger -- the file's version is
  handed to `OnStoreLoad`, as the engine does with older saves.
- Close (section 5): the capture is paced too, `CloseFrameBudgetMs` per frame (*measured*:
  0.17 ms per entity in the `FileSerializer`, 1430 entities = 238 ms in one frame), through an
  `OZS_StoreWriter` that keeps both files open across frames; the OLD live files are deleted
  when the writer opens, so a close in flight has no store and a crash inside it falls under
  the "no files" boot rule; the lock (state CLOSING, gates shut both ways) comes first.
- Store (section 6): `items.bin` header = format version, game `SaveVersion`, UTC stamp, box
  class, box id, roots, entities; a trailer `BIN_END` proves the file was written to the end;
  `items.list` header `OZS-LIST|1|saveVer|stamp|class|id|roots|entities`, one line per
  entity `depth|type|loctype|slot|row|col|flip|health|quantity|liquid|ammo|chambers|zones`;
  the fallback restores the native layer, a battery's energy through its energy manager. The
  box id is `<yyyymmdd-hhmmss>-<serial>-<random>` from `GetYearMonthDayUTC` (whose year reads
  as one digit on this build; uniqueness does not depend on it), not the engine's persistent id.
- One truth (section 7), `OZS_Controller.Reconcile` per loaded box: files present -> files
  win (a half-done transition's cargo is deleted, the stored count comes from the list header);
  no files -> the engine's cargo is the truth and is closed into files synchronously at boot;
  no files and no cargo -> CLOSED and empty (a warning when items were recorded). The files of
  an opened box are released 3 s after the engine has saved it (`OnStoreSave` runs while that
  save is still being written). `OnMissionFinish` closes open boxes synchronously, cancels
  opening boxes (files stay, half-restored items go) and flushes closing ones. The world's
  persistent entities load a few frames after `OnMissionStart`, so the boot summary waits 15 s.
- Configuration (section 9), `$profile:OpenZone/OZ_Storage.json`: `OpenFrameBudgetMs` **5**,
  `OpenItemsPerSecond` 250, `CloseFrameBudgetMs` **5**, `CloseDeletesPerFrame` 50,
  `AutoCloseRadius` 15, `AutoCloseQuietSeconds` 120, `ViewerHeartbeatSeconds` 5 (server-side
  arithmetic; the client repeats every 4 s), `ViewerTimeoutSeconds` 15, `ViewerMaxDistance` 5,
  `DebugLog`. Five milliseconds rather than the draft's twenty because the sampling profiler
  merges back-to-back frames of 20 ms into one "freeze" (a 232 ms stretch for a 14-frame
  capture) and resolves frames of 5 ms; the jobs need far less per frame anyway.
- Acceptance (section 10): met -- server frames max 24 ms (one full Large box) and 33 ms (four
  Large boxes at once), no stretch >= 150 ms from the mod at the shipped budget, client frames
  max 57 / 67 ms next to the boxes, four restart scenarios without a loss or a duplicate, the
  cargo dump identical before and after, the radio's frequency, the CF carrier's notes and the
  loaded rifles through the mod's own code. Numbers and log lines:
  `docs/measurements/2026-09-16/results-implementation.md`.

## 14. Open points after draft 2

1. The defaults of section 11 are in the code (2 / 4 / 6 slots, 15 m / 120 s, no journal,
   5 ms + 250/s, donor models) -- each still one number away for the owner.
2. Disconnect or death closing the box next to the leaving player at once: keep, or leave it
   to the quiet period only.
3. How players get boxes: today a box is spawned by an admin (the stand verb or a spawner);
   there is no recipe, no `types.xml` entry, and the box is never takeable. Craftable or
   placeable boxes and the economy entry are a separate task.
4. Import of the live server's existing stores (item 6 of the brief): waits for sample files.
   **Closed 2026-09-17**: no files will be given and no boxes are replaced in place (section 17).
5. A box that the "no files" rule closes at boot is closed synchronously (1468 entities =
   about 0.4 s, once) -- fine for a few boxes; a server with hundreds of open boxes at a crash
   would pay that once at boot.

## 15. Additions of the same night (owner decisions 2026-09-16, late)

The owner answered section 14 the same night: the auto-close is a **plain timer**, players
get boxes through a **kit deployed from the hands**, and the inventory screen gets a **Sort
button** and a **live search**. Built and measured:

- **Auto-close**: `AutoCloseSeconds` (120) after the box was opened; no distance, no player
  events. While somebody is still looking (section 3 viewers) the close waits and the 5 s
  tick tries again. Disconnect and death only drop the leaving player's viewer entries.
  `AutoCloseRadius` and the "close on leaving" extension of section 13 are gone.
- **Kits** `OZ_StorageBoxKit_Small / _Medium / _Large` (`OZS_Kit.c`): `ItemBase` with
  `IsBasebuildingKit`, `IsDeployable`, `GetDeployTime` = 10 s, the vanilla
  `ActionTogglePlaceObject` + `ActionDeployObject`. The vanilla placement path does the rest:
  a tap of the use input shows the hologram, the next/previous-action input (the wheel, the
  action-menu keys) turns it by 15 degrees, a ten-second hold deploys, the action deletes the
  kit, and `OnPlacementComplete` creates the box at the hologram's position and orientation
  (log: "<player> placed OZ_StorageBox_Small id=... at ..."). The hologram projects the
  `<kit>Placing` twin class, which carries the box's model and the vanilla hologram materials
  (`wooden_case` / `sea_chest`), so the player sees the box they are about to place. The kit
  itself uses the box's model, heavy behaviour (carried in front), 5 x 4 / 6 x 5 / 8 x 5 cells,
  5 / 7 / 9 kg. *Measured*: the hologram counts as "floating" (deploy refused) when its
  contact point is closer than 1 m or farther than 2 m from the player -- the camera must
  aim at the ground about a metre and a half ahead; bushes and walls refuse it too, like any
  vanilla kit. No recipe and no economy entry yet (`types-example.xml` in the repo root).
- **Sort** (`OZS_Sorter.c`): a close whose records carry a new layout -- cargo roots ordered
  by display name, class and quantity (fullest first), packed row by row with the items'
  own sizes -- followed by a reopen; the requester keeps their screen open and watches the
  items come back in order. Once per `SORT_COOLDOWN` (10 s) per box, refused while anyone
  else is looking, through the box's entity RPC from the client's button or the stand verb.
  The order key folds case through `OZS_Case` (tables for ASCII and Cyrillic; the engine's
  `ToLower` turns non-ASCII into spaces). The open job takes any free cell when a record's
  cell is taken, so a layout that did not fit never loses an item. *Measured*: 1443 items
  sorted in one close (57 frames) and one reopen (5.9 s); the grid came back with the
  battery, rifle, canteen and chip first, then the papers, then the rag, radio and cases.
- **Search bar** (`OZS_Search.c`, `gui/layouts/ozs_search.layout`): created inside the
  inventory root on `InventoryMenu.OnShow`, top right above the equipment column: a label, an
  edit box and the Sort button. The query is kept in four spellings (typed, lower,
  Capitalized, UPPER) and an icon matches when its localized display name contains any of
  them; non-matching icons get a dark overlay (`ozs_shade.layout`, a colorable panel created
  inside the icon at priority 500 above the item render) -- both cargo icons and attachment
  slots. *Measured*: the icon's own "Color" panel sits behind the render and cannot shade it,
  the `ItemPreviewWidget` ignores its widget colour, and a panel with `style blank` paints
  nothing -- `style rover_sim_colorable` does. The Sort button asks the first open box in the
  vicinity list.
- **Stand tooling**: the probe's `modded class Hologram` prints every collision check of a kit
  hologram to the client's .RPT (`ErrorEx`; `Print` reaches no file on the retail client), and
  `$profile:OpenZone_StorageProbe/control.txt` on the client (`search <text>`, `clear`,
  `sort n`, `inventory n`) drives the screen without a mouse. Verb ops `sort`, `lower`,
  `tune autoclose=`.

## 16. Open points after the night

1. A placed box cannot be taken back: no dismantle action, no kit returned. Decide whether an
   empty box may be dismantled into its kit (and by whom).
2. The Sort button has no background of its own (style Empty); the bar's placement above the
   equipment column may need a nudge on the owner's 3840 x 1600 screen.
3. The search was measured with an English client; the Cyrillic folding tables are in the code
   but not yet seen on a Ukrainian client.
4. The search field takes focus by a click only; no key opens it.
5. Kits have no recipe and no spawn; `types-example.xml` lists them with nominal 0.

## 17. Additions of 2026-09-17 (owner decisions during his own test)

The owner ran the stand himself with his own client. Four of the five changes below come
from what he saw there; the fifth from a box of his that vanished overnight.

- **Restored items now reach the clients.** The open job moved each restored root with the
  bare `LocationSyncMoveEntity`, which succeeds on the server and tells no client
  (`inventory.c:1056-1073`). Protective cases restored into a box therefore stayed drawn
  where the client had last seen them, on the ground around the box, and vanished when the
  box closed; on the server they had been inside it all along. Fixed by
  `TakeToDst(InventoryMode.SERVER, ...)`, which does the same move and sends the SYNC_MOVE
  command, with `TakeEntityToCargoEx(InventoryMode.SERVER, ...)` as the fallback and
  `CreateEntityInCargo` as the last resort when the recorded cell is taken. Section 4 of
  this spec had named that exact trap before the code was written, and the code took the
  other branch anyway -- the same fix went into `OZS_ListFallback.c`.
- **The auto-close counts idle, not age** (*owner*). The 120 s restart on every item that
  goes into, out of or across the box: `EECargoIn`, `EECargoOut`, `EECargoMove`,
  `EEItemAttached`, `EEItemDetached` all call `OZS_Touch`. The paced jobs do not count,
  because OPENING is not OPEN and the restore sets `m_OZS_Restoring`. Before this an
  opened box closed itself under a player who was still standing at it, and the contents
  read as lost although they were in the store the whole time.
- **The screen opens at once, locked** (*owner*). `CanDisplayCargo` and the two attachment
  gates answer yes from the first frame of OPENING, so the grid is drawn while it fills
  instead of after; `CanReleaseCargo` and `CanReleaseAttachment` stay on `IsOpen`, so
  nothing can be taken out of a box that is still filling. While OPENING the bar shows a
  loading line with the count in place of the field and the button.
- **The search bar belongs to the box** (*owner*). It is built in `SetEntity` of
  `ContainerWithCargo` and `ContainerWithCargoAndAttachments`, inside that container's own
  main widget with `SetSort(0)`, so it rides the head of the box's panel and not the
  inventory root. `InventoryMenu.OnHide` clears the query, so a closed screen never leaves
  items shaded.
- **A placed box no longer disappears.** `OZS_Kit.OnPlacementComplete` created the box with
  `ECE_PLACE_ON_SURFACE` and no `ECE_NOLIFETIME`, and our classes are not in `types.xml`,
  so the central economy removed the owner's box overnight. Now `OZS_Const.BOX_LIFETIME`
  (45 days) is set at placement and again in `EEInit` on every boot, and `EEDelete` writes
  a warning naming the box, its state, its contents and the fact that its files are kept.
  An admin can still override the lifetime through `types.xml`; this is the floor that
  works without one.

### Measured on 2026-09-17

Default `OpenItemsPerSecond` raised from 250 to 500. The Large box, 1443 roots and 1468
entities, no client connected:

| Step | Wall | Script work | Longest step |
|---|---|---|---|
| Open at 500/s | 2.9 s | 20 ms total | 2 ms |
| Close, write | 283 ms | -- | within the 5 ms budget |
| Close, commit | 2 ms | -- | -- |
| Close, delete 1468 entities | 72 ms | -- | 6 ms |

Nothing refused, nothing missed, `items.list` back to 1468 lines. The longest delete step
of 6 ms overruns the 5 ms budget because the budget is checked before a step and not
inside it: one entity's deletion is the grain.

`blobtime` (probe op) times the engine's own serialization of the same 1443 entities:

| What | Total | Per item |
|---|---|---|
| `OnStoreSave` into memory | 9 ms | 0.01 ms |
| `OnStoreLoad` from memory | 2 ms | 0 ms |
| `OnStoreSave` into a file | 86 ms | 0.06 ms |

The 1351 load refusals are expected: a blob written by one entity is read back by a fresh
one only when the classes match, and the probe feeds them all to one reader. The point of
the run was the cost, and the cost says the blob is not what makes a close take 283 ms --
the file does.

## 18. Open points after 2026-09-17

Carried over from section 16, still undecided:

1. A placed box cannot be taken back: no dismantle action, no kit returned. Decide whether
   an empty box may be dismantled into its kit, and by whom.
2. The Sort button has no background of its own (style Empty).
3. The search was measured with an English client; the Cyrillic folding tables are in the
   code but have not been seen on a Ukrainian client.
4. The search field takes focus by a click only; no key opens it.
5. Kits have no recipe and no spawn; `types-example.xml` lists them with nominal 0.

New:

6. When a box is destroyed its store directory stays on disk as an orphan (the `EEDelete`
   warning is the only trace). Decide: delete it, move it to a grave folder, or keep it.
   **Half closed 2026-09-17**: it is now marked with `removed.txt` and kept (section 19);
   what to do with a marked directory afterwards is still the owner's call.
7. The half-commit hole: `items.bin` and `items.list` carry the same stamp, and nothing
   compares them at load. A crash between the two copies would leave a new `items.bin`
   beside an old `items.list` and the fallback would restore the wrong contents. The check
   is about ten lines and is not written yet. **Closed 2026-09-17** (section 19).
8. Vanilla destroys Cyrillic in container headers (`ToUpper` turns non-ASCII into spaces).
   Our `OZS_Case` tables fix it for the search; a `modded class Header` fix was tried,
   blanked the header for a reason never found, and was reverted.

**Closed 2026-09-17**: item 6 of the brief, the import of the live server's stores. Nothing
but logs will leave that server, and the owner decided the boxes are not replaced in place:
players move their things across by hand.

## 19. The stamp check and the orphan mark (2026-09-17, owner)

Two points of section 18 are closed.

### The half-commit hole (point 7)

Both files of a store are written in one `OZS_StoreWriter` pass and carry the SAME stamp,
so a pair whose stamps differ is not a pair. It can only arise when the delete of the old
live file fails and a new blob lands beside a survivor of an older commit. Splicing such a
list onto a blob that broke at root N restores the wrong items from N on, and where the two
disagree about the first N roots it duplicates them.

`OZS_OpenJob` now keeps the blob's stamp (`m_BinStamp`) and `OpenList` compares it with
`OZS_Store.ListStamp`. On a mismatch it refuses the fallback, names both stamps and copies
the list aside as `items.list.failed-<stamp>`, next to the `items.bin.failed-<stamp>` the
blob failure already leaves. Without that copy the refusal would save nothing: the box goes
on with the roots the blob delivered, and its next close overwrites the very list the
refusal was protecting. That was watched happening before the copy was added. When the blob
cannot be opened at all its stamp is unknown, nothing is compared, and the list is used as
before: a store whose blob is unreadable has only the list to offer.

*Measured*, two Large boxes of 1410 roots, each with `items.bin` truncated to 60000 bytes
so it breaks at root 639:

| Box | List stamp | Result |
|---|---|---|
| Matching pair | same as the blob | fallback taken at root 639, all 1410 items back, 0 missed |
| Mismatched pair | aged by 15 hours | fallback refused, box opened with the 639 roots the blob held, both files copied aside |

### Orphan stores (point 6)

A box leaving the world now writes `removed.txt` into its own store directory: the UTC
stamp, the class, the state it was in, its entity and stored counts, and its position. The
directory and its files are untouched, so an admin can tell an orphan from a closed box by
listing the folder instead of searching the log, and `oz_storage files` reports
`removed=true` and the list's stamp.

The trap is the mission teardown: the engine deletes every entity when the world goes down,
and `EEDelete` cannot tell that from a box somebody blew up. Unguarded, a clean restart
would mark every store as orphaned. `OZS_Controller` therefore carries a static
`s_Shutdown`, set in `MissionServer.OnMissionFinish` before `CloseAll` and cleared in
`OnMissionStarted`, because statics survive a mission restart. *Measured*: one box deleted
in a running world left exactly one `removed.txt`; a full stop of the server with five boxes
standing added none.

What the owner has still not decided is what to do with a marked directory afterwards. It is
kept, which is the safe default; deleting it or moving it to a grave folder is a policy
choice, and the mark is what makes either possible later.

## 20. Ten boxes at once (2026-09-17, owner)

The load case the pacing was built for, measured: ten boxes opening and closing in the same
frame while items move between them. Full report and the three profiler windows in
`docs/measurements/2026-09-17/`.

Two tools were added for it and are worth keeping. `oz_storage open_all` and `close_all`
put every box's request in one frame, which ten separate bridge commands never do.
`oz_probe churn` moves items between two open boxes with `TakeToDst` in SERVER mode, the
same call an inventory action makes, at a set rate, always taking from the fuller side so a
long run never empties one box and starts failing for the wrong reason.

Ten boxes, 10683 items, one client standing among five of them:

| Window | Longest frame | Stretches >= 150 ms | Mod share of the main thread |
|---|---|---|---|
| Ten opening at once, 21.6 s | 44 ms | none | 3.87 % |
| 4000 moves at 400/s, no jobs | 39 ms | none | 0.90 % |
| Churn + ten closing + ten opening | 44 ms | one of 168 ms, see below | 6.14 % |

One move costs 0.13 ms of script on the server. No job step exceeded 3 ms against its 5 ms
budget, because the budgets divide between jobs: ten boxes cost the frame what one does.

**The refusals are the design, not a fault.** While the churn ran through a `close_all`,
11700 of its moves were refused: a box that is no longer OPEN answers no in
`CanReleaseCargo`, so an item cannot be moved out from under the capture. A player would
see the item not move, which is the correct outcome.

**The 168 ms stretch is the sampler merging frames, not a frame.** Its breakdown names
`Serializer.Write` and `OZS_Records.WriteListEntity`, the close job writing its store. Ten
close jobs run in consecutive frames with the same function on the stack and the 200 Hz
sampler cannot see the boundaries between them. The in-engine frame monitor, which times
every frame, saw 44 ms at worst over 768465 frames and nothing above 100 ms. The same
artefact was measured on 2026-09-16 (fourteen 20 ms frames reported as one 232 ms freeze),
and it is the reason the frame budget is 5 ms rather than 20.

**Nothing was lost or duplicated.** The final `close_all`, after ten opens, ten closes, ten
more opens and 8300 successful moves through all of it, wrote 10683 items: exactly the
count the test started with.

## 21. Ghost items: the stuck deletion of nested containers (2026-09-18, owner's video)

The owner filmed the live server's phantom items and then reproduced them on the stand
(https://www.youtube.com/watch?v=CWDBvLZji04): a pouch holding a protective case holding a
first aid kit, restored by a box, taken out and dropped, and later drawn on the ground on the
client where nobody could pick it up; a restart turned it into a real item again, with the
case inside and the kit gone. The hunt below took one evening with the stand's own client
and a new verb; every line is a measurement, not a reading of the engine's source.

### What a ghost is

On the server it is an entity whose deletion stopped halfway: `ToDelete()` and
`IsPendingDeletion()` answer yes, the network id is 0, the inventory location is UNKNOWN,
and the entity is still in the spatial index (`GetObjectsAtPosition` returns it). A second
`ObjectDelete`, `EntityAI.Delete()` or `RemoteObjectTreeDelete` plus `ObjectDelete` change
nothing; a player joining the server frees every pending one (measured to the second); a
restart writes them into the persistence as real items -- the top two levels of the tree,
which is the dupe the players saw. On the client the copy stays behind with location 0 at
the spot where the item last lay -- that is the picture on the ground nobody can pick up --
and a relog clears it.

### The recipe (every step needed)

1. A script moves an entity into a container **created in the same frame**
   (`TakeToDst(InventoryMode.SERVER)`, i.e. `LocationSyncMoveEntity`). The restore did
   exactly that for every container with cargo: create it, fill it, move it into its parent,
   all in one frame. An entity *created* into such a container with `CreateEntityInCargo`
   is fine; a player's juncture move is fine; the same move into a container created 20 s
   earlier is fine; one frame between the creation and the move is enough on the server.
2. The entity, or the tree holding it, leaves that cargo for the ground -- a player's
   take-out and drop, or a script move (`TakeToDst` to a ground location).
3. Any `ObjectDelete` afterwards: an admin tool on the ground, or our own close after the
   player put the item back into the box.

Variants measured on the way, all with a two- or three-level chain unless noted:

| what | result |
|---|---|
| chain built by the probe in one frame, moved into the box, closed untouched | clean |
| the same, client takes it out and stashes it back, closed | clean |
| the same, client takes it out and drops it, deleted | **zombie** |
| the same, client drops it, the server puts it back, closed | **zombie** (the owner's case) |
| the same without any client: server moves it out, deleted | **zombie** |
| an empty pouch through the box and out | clean |
| an empty case moved by script into a pouch created in the same frame, taken out, deleted | **zombie** |
| the same with the pouch created 20 s earlier | clean |
| a chain the client built with its own moves, through the box and out | clean |
| a probe chain built on the ground, never in a box, taken and dropped | clean |
| a bandage created by `CreateEntityInCargo` in a same-frame kit, taken out, deleted | clean |
| probe `chain mode=deferred delay=1` (moves in the next frame), out, deleted | clean |
| creation flags `ECE_IN_INVENTORY` instead of `ECE_PLACE_ON_SURFACE` | zombie |
| `RemoteObjectTreeDelete`, local move, `RemoteObjectTreeCreate` around the root's move | zombie |
| every level created inside its parent, no move at all | impossible: the engine refuses a cargo-resident parent (`CreateEntityInCargo` null, `FindFirstFreeLocationForNewEntity` no cell) |
| deleting a tainted tree deepest first | the children go, the root still sticks |

### The client has its own trigger

With the server-side delay in place (even ten frames), a client that watched the restore
still kept a ghost after taking the tree out, dropping it and having it deleted. On the
client the trigger is the SYNC_MOVE itself: a container with children moved into a cargo by
a command from the server, whatever the timing. A client that joins after the tree is in
place gets it as one creation and never has the problem.

### The fix

The restore builds each root's tree out of **local** entities the clients never see:
`CreateObjectEx(..., ECE_LOCAL | ECE_PLACE_ON_SURFACE | ECE_NOLIFETIME)` for a container
with cargo, `LocationCreateLocalEntity` for everything under it, the recorded cell first and
any free cell second. The moves are not run where the records are read: each is queued as an
`OZS_Move` (innermost first, the order of reading) and the open job runs the queue at the
start of its **next** frame's Tick in `InventoryMode.LOCAL`, which keeps the server one
frame away from the taint. The move into the box is followed by `RemoteObjectTreeCreate`
of the root, placed or not, so every client receives the finished tree once, in its final
place -- the pattern of the game's own `ReplaceItemWithNewLambdaBase`. The job lasts one
frame more than the reading; `Cancel` and the abandon path delete the containers still
waiting on the ground; `OZS_ListFallback.Make` does the same. Nothing is drawn on the
ground during the restore any more, because local entities are invisible until published.

Verified on the stand: restore, server moves the tree out, delete -- 0 zombies (2 before);
restore, client takes it out and drops it, delete -- server clean, client clean (a client
ghost before); the owner's own sequence, hands, box, hands, ground, hands, box, close --
clean on both sides, five entities deleted.

### What stays

- Nested containers that **other** mods build the old way (a storage mod's own restore, an
  admin tool spawning a filled bag) carry the same taint, and our close cannot delete them
  cleanly once a player has moved them; deleting deepest first removes their children but
  not the root. Nothing to do on our side but know it when a report comes in.
- Attachments created under a local container go through `LocationCreateLocalEntity`; the
  taint was never measured for a creation, only for moves, and the box's own weapon slots
  keep the networked path.
- The stand's tools stay in the probe: the `oz_ghost` verb (`watch`, `scan`, `redelete`),
  the probe ops `chain mode=sync|local|tree|move|incargo|inbox|deferred flags=...
  delay=...`, `put`, `out`, `deltree`, `tree`, and the client control commands `grab`,
  `drop`, `take`, `into`, `onto`, `stash`, `tree`.

### Load (2026-09-19, owner asked)

The same 100 nested roots (500 entities) restored by both versions of the code: old
138 ms of script work with a longest step of 4 ms, new 105 ms and 3 ms; with a client
at the box 119 ms and 2 ms, server frames at most 22 ms, and the client's own walk of
the box shows all 500 entities in place. The fix costs nothing; the LOCAL moves skip the
SYNC_MOVE serialisation. Table in `docs/measurements/2026-09-19/`.

## 22. An item whose mod left the server (2026-09-19, owner asked)

Measured by renaming the case's class in a stored box to one that does not exist
(`SmallProtectorCase` -> `SmallProtectorCasX` in both files, the stamps untouched). The
open warns `cannot create SmallProtectorCasX in PlateCarrierPouches`, gives up on the
blob (`items.bin cannot be followed at root 0`, the blob kept as `items.bin.failed-<stamp>`)
and restores the box from `items.list`: the pouch comes back **empty**, the case and
everything inside it -- the kit, the bandages -- are gone, `missed 1`. So today an item of a
vanished mod takes its contents with it, and every root behind it in the same box comes
back through the list, i.e. with health, quantity and ammunition but without the script
state of its class (a radio's frequency, a battery's charge through the energy manager
excepted). The files are never destroyed: the mod back on the server and the `.failed`
blob renamed into place restore everything.

Two things the owner may want, neither done:

- **The contents of a vanished container fall into the container above it.** The list
  fallback knows the subtree; on a failed creation it could go on with the children under
  the parent, any free cell, instead of skipping the subtree. Small change, list-level
  state only.
- **A length-prefixed record in `items.bin`**, so the reader skips one unreadable record
  and keeps the full state of every other root. A format change (`BIN_VERSION` 2, the old
  blob still readable).

## 23. The owner's decisions of 2026-09-19, and what became of them

1. **A placed box is never dismantled** (*owner*: no). Closed, nothing to build.
2. **The Sort button looks like the PDA's** (*owner*). The button in `ozs_search.layout`
   draws itself through children the way `oz_pda_menu.layout` does: an edge panel
   (0.22 0.3 0.38), a face (0.135 0.18 0.225) that lights up to the edge colour under the
   mouse (`OnMouseEnter`/`OnMouseLeave` in `OZS_BoxBar`), and the text in the PDA's blue
   (0.31 0.71 0.91), 13 px, centred. `SetText` goes to the `SortText` child.
3. **The Cyrillic search** the owner checks himself on his own client.
4. **The search field opens by a click only** (*owner*: fine as it is).
5. **Kits are handed out by admins** (*owner*); no recipe, no spawn, `types-example.xml`
   stays at nominal 0.
6. **A removed box's store goes to an archive** (*owner*). `OZS_Store.Archive` moves every
   file of `<id>/` and `<id>/roots/` into `Storage/removed/<id>/`, writes `removed.txt`
   there and deletes the emptied directories (`DeleteFile` removes an empty directory --
   measured). `EEDelete` calls it in place of the old mark; the `files` verb reports
   `archived=`. Verified on the stand with a version 2 store: index, list, the `.failed`
   copy and `roots/0000.bin` all moved, the live directory gone.
7. **Vanilla's uppercase in container headers** (*owner*: look at how the PDA does it). The
   PDA never uppercases; the inventory screen does, in four places before the name reaches
   a header (`CargoContainer.UpdateHeaderText`, `Attachments.InitAttachmentGrid`,
   `HandsPreview.CreateNewIcon`, `ContainerWithCargoAndAttachments.SetEntity`) and in the
   headers themselves (`Header.SetName`, `ClosableHeader.SetName`) -- which is why the fix
   of 2026-09-17 in the header alone found only spaces to fix. `OZS_Headers.c` reopens all
   of them to fold case through `OZS_Case`; the closable header of a box without a cargo
   grid is named again after `super.SetEntity`. Compiles on the client; not yet seen on a
   Ukrainian client (the stand's own client could not connect this night, see below).
8. **The record with a length in `items.bin`** (*owner*, section 22's second option) turned
   out impossible as written: `FileSerializer` is `Open`, `Close`, `Read`, `Write` -- no
   position, no seek, no raw bytes -- so a typed stream cannot step over a body it cannot
   parse. The equivalent that the engine allows is **one file per root**: `items.bin` is
   now the header alone (version 2, the same fields), every root's record sits in
   `roots/NNNN.bin` with the store's stamp and its own index inside, and the open reads
   root by root: a file that is missing, of another commit, or broken inside costs that
   root and no other -- it comes from `items.list`, the file is kept as `.failed-<stamp>`,
   and the next root is read from its own file. The writer deletes the old files at Open as
   before, writes the index whole, the root files straight into place and the list line by
   line, and commits index and list at the end; a crash before the commit leaves no index,
   and the engine's cargo is the truth as it always was. A version 1 store (the boxes
   closed before this) opens through `items.list` and is rewritten as version 2 at its next
   close. Measured on the stand: root 7 of 100 with its class renamed to one that does not
   exist -- `root 7 of 100 cannot be read (the record cannot be followed); kept as
   roots\0007.bin.failed-...; it comes from items.list`, the other 99 from their files,
   500 entities in the box, no zombie. The price is the file count: 100 nested roots
   closed in 205 ms (78-83 ms as one file) and opened with 142 ms of script work
   (105-119 ms); a flat box of 1400 items closed in 1108 ms of paced writing (283 ms for
   1443 as one file) and opened with 196 ms of work (20 ms), longest step 8-10 ms, wall
   2.8 s at the 500-per-second rate either way. Table in
   `docs/measurements/2026-09-19/results-store-v2.md`. If the file count ever matters,
   roots can be grouped into files of N without touching the reader's logic.

Stand notes of the night: the stand's own client stopped connecting after 02:30
(`0x00010001`, the server unavailable to it while it answered the query port and the
bridge); Steam was up, the BattlEye client service was not. The owner tests the client
side himself.

## 24. SQL as the truth, a file as the wire (2026-09-19, owner)

The owner rejected version 2 of the store (section 23.8) for its file cost and
redirected the whole persistence: the truth of a CLOSED box is the bridge's
SQLite, the truth of an OPEN box is the engine's cargo, and a file is only the
wire between them, because an `OnStoreSave` body leaves the script VM through
`FileSerializer` and no other way. The design lives in the series hub,
`docs/specs/2026-09-19-storage-sql-truth-design.md`; sections 6, 7, 19, 22 and
23.6-23.8 of this document no longer describe the code. What replaced them:

- one file per close in `$profile:OpenZone/Storage/xchg/`, version 3 of the
  wire (header with a random marker, per root the descriptor of the subtree and
  then the bodies, the marker after every root, `BIN_END`); the bridge keeps
  every root byte for byte, deduplicated by hash, and answers an open with a
  cache file it validates or rebuilds from SQL; the engine never writes at an
  open and never deletes the cache;
- the boot exchange instead of the file rules: SQL wins over a half-done
  transition, an open box with cargo is the engine's truth and closes into a
  new version, the classes SQL holds are checked in `CfgVehicles`, `CfgWeapons`
  and `CfgMagazines` and the missing ones parked by the bridge, byte for byte,
  to come back unplaced once the class exists again; the check waits for the
  boot closes, because the bridge returns a root only into a closed box;
- a root the engine cannot read (a refused `OnStoreLoad`, a marker out of
  step, no room for a returned root) is deleted with everything the open
  created, parked, and the open asked again -- a degraded state is never
  written back;
- the bridge is mandatory: Open, Close and Sort are refused with the core's
  `#STR_OZ_ERR_NO_BRIDGE` while it is down or the boot exchange is unanswered,
  the idle close waits, mission finish never closes a box (a close needs the
  bridge's answer and there is no frame loop to wait in);
- events (placed, removed, open, close, put, take, sort, park reasons,
  unavailable) go by HTTP in batches of up to 200 once a second;
- the box id is the engine's persistent id, `b1-b2-b3-b4`, valid the frame the
  box is created and the same after every boot (measured).

Measured on the stand 2026-09-19 (`docs/measurements/2026-09-19/results-sql-truth.md`):
1272 roots close in 305 ms of paced writing and open with 48 ms of work;
100 nested chains (600 entities) close in 115 ms and open with 115 ms; the
bridge answers a close in 40-130 ms; the AKM with its magazine and chambered
round, ten nested chains and an item of a stand-only class survived the vanish
and return of their pbo. Not pushed; commits ff790c9..HEAD of this repository
and 39d17cf..57bff93 of `openzone-bridge`.
