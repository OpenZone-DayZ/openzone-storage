# OpenZone Storage: the box (specification, draft 1)

Date: 2026-09-16. Status: **draft for the owner**. Decided by the owner the same day: model C
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
  or put into cargo (`CanPutInCargo`, `CanPutIntoHands` false) -- the another mod/ZMG anti-dupe rule.
- **Weapon slots** are attachment slots `OZ_Weapon_1..N` declared in `CfgSlots`
  (`class Slot_OZ_Weapon_1 { name = "OZ_Weapon_1"; displayName = "$STR_OZ_SLOT_WEAPON"; }`, the
  shape OpenZone_PDA already uses) and listed in the box's `attachments[]`; a
  `GUIInventoryAttachmentsProps` group shows them as one "Weapons" row. Vanilla weapons accept
  them through a config patch: `Rifle_Base` declares `inventorySlot[] = {"Shoulder","Melee"}`
  and `Pistol_Base` `{"Pistol"}` as ARRAYS (*measured*: `dz/config.bin` 4033 / 4285), so
  `inventorySlot[] += {"OZ_Weapon_1", ...}` on `Rifle_Base` and `Pistol_Base` works without the
  T148506 string trap. Modded weapons that redeclare `inventorySlot[]` themselves need the same
  patch in a glue pbo, or they simply do not fit the slots; both are acceptable.
- Closed box: cargo and slots are hidden and refuse everything --
  `GetInventory().LockInventory(HIDE_INV_FROM_SCRIPT)` on close, unlock on open (MMG's
  pattern), plus `CanReceiveItemIntoCargo`, `CanReceiveAttachment`, `CanDisplayCargo`,
  `CanDisplayAttachmentSlot` gated on the state. The only thing a closed box shows the player
  is the Open action with the count of stored items.
- State on the box: `m_OZ_State` in {CLOSED, OPENING, OPEN, CLOSING}, netsynced as an int so
  the client draws the lid and the action text. Persisted in the box's `OnStoreSave` together
  with the box id (section 6) -- the boot rules in section 7 read it.

## 2. Actions

Two `ActionInteractBase` actions on the box, target-only (`CCTObject`), vanilla interaction
distance (`UAMaxDistances.DEFAULT`), server logic in `OnExecuteServer` (details of the vanilla
open/close-barrel pair to be pinned from the source report, section 12):

- **Open** -- shown when the state is CLOSED. Text: `Open (N items)`; N comes from the store's
  header, which the box reads once at load. Starts an OPENING job (section 4).
- **Close** -- shown when the state is OPEN. Refused, with a message, while the box has viewers
  (section 3) or an OPENING job is still running. Starts CLOSING (section 5).

There is no third action; the auto-close of section 3 is insurance, not a verb.

## 3. Viewers and auto-close (item 1 of the brief)

The server has no notion of "player X is looking at container Y" (to be confirmed in the
source report; nothing in `GameInventory`/`CargoBase` names a viewer). The mod builds one:

- The client half sends `OZ_Storage.View(boxId, on)` when its inventory screen opens or
  closes with a box in the vicinity list (`VicinityItemManager`), and a heartbeat every 5 s
  while it stays open. The server keeps `map<box, set<playerId>>` with the last heartbeat.
- A viewer is dropped when: the client says so, the heartbeat is older than 15 s, the player is
  farther than 5 m from the box (server-side check on every heartbeat and on Close), the
  player disconnects or dies (`PlayerBase.EEKilled`, `MissionServer.InvokeOnDisconnect`).
- **Close is refused while viewers > 0.** The one who wants to close sees "someone is using
  the box".
- **Auto-close** (*owner*: numbers): every 10 s the server checks open boxes; a box with no
  viewers and no player within `AutoCloseRadius` (default 15 m) for `AutoCloseQuietSeconds`
  (default 120 s) closes itself. The opener dying or leaving is not a trigger by itself, only a
  reason the viewer set empties. `MissionServer.OnMissionFinish` closes every open box
  synchronously, without pacing.
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
  box, filled, then moved into its cell** with `FindFreeLocationFor` +
  `GameInventory.LocationSyncMoveEntity` -- the engine refuses children to a container that
  already sits in cargo (*measured*: 0 of 1000/500/500 with all three creation calls; 3 of 3
  cases restored with contents through the ground path). Backpacks with contents cannot enter
  a box at all: `Clothing_Base.CanPutInCargoClothingConditions` (vanilla, unchanged).
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
persistent id (`GetPersistentID(b1,b2,b3,b4)` as hex, stable across restarts, the key another mod
uses). Two files, written on every Close, read only on Open and at mission start:

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
- Import from the previous mod (item 6): a converter reads the live server's JSON into `items.list`
  records (their format has no blobs to convert) -- as soon as sample files are available.

## 7. One truth at a time (item 4 of the brief)

*Measured*: `ECE_DYNAMIC_PERSISTENCY` does nothing for items created in a container's cargo
(99 flagged and 100 unflagged items all came back after autosave + kill + restart), and there
is no script API that makes an existing entity non-persistent. So the engine WILL save an open
box's cargo, and the design has to live with two copies for a while:

- **OPEN**: the engine is the truth. The store files are kept until the box's next
  `OnStoreSave` after opening (the engine's next save), then deleted -- another mod's
  another container storage rule, so a crash between opening and the first autosave loses
  nothing.
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

## 12. Facts still to pin from the vanilla sources

The vanilla open/close action pair used as the template, the exact interaction distance
constant, the client-side hook for "inventory screen opened with these containers in
vicinity", and the `MissionServer` lifecycle order at a graceful shutdown. A source report
is being written into `docs/measurements/2026-09-16/agent-report-vanilla-facts.md`; this
section is replaced by its findings.
