// OpenZone Storage -- boxes whose contents exist only while the box is open.
//
// Design: docs/2026-09-16-storage-box-spec.md. Three sizes, one script class
// (OZ_StorageBox) with a four-state machine, weapon slots as attachment slots
// on the box, vanilla cargo while open, files while closed.
//
// Depends HARD on OpenZone_Core (logger, config service) and, through it, on
// CF. The donor models are the vanilla wooden crate and the sea chest until
// the series has its own art.

class CfgPatches
{
    class OpenZone_Storage
    {
        units[] =
        {
            "OZ_StorageBox_Small", "OZ_StorageBox_Medium", "OZ_StorageBox_Large",
            "OZ_StorageBoxKit_Small", "OZ_StorageBoxKit_Medium", "OZ_StorageBoxKit_Large",
            "OZ_StashAnchor", "OZ_PersonalStash"
        };
        weapons[] = {};
        requiredVersion = 0.1;
        requiredAddons[] =
        {
            "DZ_Data",
            "DZ_Scripts",
            "DZ_Gear_Camping",
            "DZ_Structures_Furniture",
            "JM_CF_Scripts",
            "OpenZone_Core"
        };
    };
};

class CfgMods
{
    class OpenZone_Storage
    {
        dir = "OpenZone_Storage";
        name = "OpenZone Storage";
        author = "Zone Protocol";
        version = "0.3.0";
        type = "mod";

        // > 0 keeps CF's ModStorage happy for every ItemBase; the box itself
        // persists its state through vanilla OnStoreSave (see OZ_StorageBox).
        storageVersion = 1;

        dependencies[] = {"Game", "World", "Mission"};
        defines[] = {"OPENZONE_STORAGE"};

        class defs
        {
            class gameScriptModule    { value = ""; files[] = {"OpenZone_Storage/scripts/3_Game"}; };
            class worldScriptModule   { value = ""; files[] = {"OpenZone_Storage/scripts/4_World"}; };
            class missionScriptModule { value = ""; files[] = {"OpenZone_Storage/scripts/5_Mission"}; };
        };
    };
};

// Weapon slots on the box. The engine resolves a slot by the CLASS name
// Slot_<name>; `name` is what attachments[] and inventorySlot[] match on.
// No ghostIcon: the shipped imageset carries no icon that means "weapon on a
// rack", and a name it does not carry draws nothing.
class CfgSlots
{
    class Slot_OZ_Weapon_1 { name = "OZ_Weapon_1"; displayName = "$STR_OZS_SLOT_WEAPON"; };
    class Slot_OZ_Weapon_2 { name = "OZ_Weapon_2"; displayName = "$STR_OZS_SLOT_WEAPON"; };
    class Slot_OZ_Weapon_3 { name = "OZ_Weapon_3"; displayName = "$STR_OZS_SLOT_WEAPON"; };
    class Slot_OZ_Weapon_4 { name = "OZ_Weapon_4"; displayName = "$STR_OZS_SLOT_WEAPON"; };
    class Slot_OZ_Weapon_5 { name = "OZ_Weapon_5"; displayName = "$STR_OZS_SLOT_WEAPON"; };
    class Slot_OZ_Weapon_6 { name = "OZ_Weapon_6"; displayName = "$STR_OZS_SLOT_WEAPON"; };
};

class CfgVehicles
{
    class WoodenCrate;
    class SeaChest;
    class Inventory_Base;
    class Container_Base;
    class StaticObj_Furniture_locker_closed_v1;

    // 500 cells at the vanilla width of 10, two weapon slots.
    class OZ_StorageBox_Small: WoodenCrate
    {
        scope = 2;
        displayName = "$STR_OZS_BOX_SMALL";
        descriptionShort = "$STR_OZS_BOX_SMALL_DESC";
        attachments[] = {"OZ_Weapon_1", "OZ_Weapon_2"};
        class Cargo
        {
            itemsCargoSize[] = {10, 25};
            openable = 0;
            allowOwnedCargoManipulation = 1;
        };
        class GUIInventoryAttachmentsProps
        {
            class Weapons
            {
                name = "$STR_OZS_SLOTS_WEAPONS";
                description = "";
                attachmentSlots[] = {"OZ_Weapon_1", "OZ_Weapon_2"};
                icon = "set:dayz_inventory image:cat_common_cargo";
                view_index = 1;
            };
        };
    };

    // 1000 cells, four weapon slots.
    class OZ_StorageBox_Medium: SeaChest
    {
        scope = 2;
        displayName = "$STR_OZS_BOX_MEDIUM";
        descriptionShort = "$STR_OZS_BOX_MEDIUM_DESC";
        attachments[] = {"OZ_Weapon_1", "OZ_Weapon_2", "OZ_Weapon_3", "OZ_Weapon_4"};
        class Cargo
        {
            itemsCargoSize[] = {10, 50};
            openable = 0;
            allowOwnedCargoManipulation = 1;
        };
        class GUIInventoryAttachmentsProps
        {
            class Weapons
            {
                name = "$STR_OZS_SLOTS_WEAPONS";
                description = "";
                attachmentSlots[] = {"OZ_Weapon_1", "OZ_Weapon_2", "OZ_Weapon_3", "OZ_Weapon_4"};
                icon = "set:dayz_inventory image:cat_common_cargo";
                view_index = 1;
            };
        };
    };

    // 1500 cells, six weapon slots. 150 rows is well inside the engine's
    // ceiling of 256 rows per cargo grid (measured 2026-09-16).
    class OZ_StorageBox_Large: SeaChest
    {
        scope = 2;
        displayName = "$STR_OZS_BOX_LARGE";
        descriptionShort = "$STR_OZS_BOX_LARGE_DESC";
        attachments[] = {"OZ_Weapon_1", "OZ_Weapon_2", "OZ_Weapon_3", "OZ_Weapon_4", "OZ_Weapon_5", "OZ_Weapon_6"};
        class Cargo
        {
            itemsCargoSize[] = {10, 100};
            openable = 0;
            allowOwnedCargoManipulation = 1;
        };
        class GUIInventoryAttachmentsProps
        {
            class Weapons
            {
                name = "$STR_OZS_SLOTS_WEAPONS";
                description = "";
                attachmentSlots[] = {"OZ_Weapon_1", "OZ_Weapon_2", "OZ_Weapon_3", "OZ_Weapon_4", "OZ_Weapon_5", "OZ_Weapon_6"};
                icon = "set:dayz_inventory image:cat_common_cargo";
                view_index = 1;
            };
        };
    };
    // The anchor: the locker players walk up to.
    //
    // A PLACED THING, NOT A PIECE OF THE MAP (owner, 2026-09-26 evening: "give
    // it a model that does not vanish by lifetime"). It used to BE the vanilla
    // static locker, StaticObj_Furniture_locker_closed_v1 -- a House, and a
    // House made by script is never saved: the world save holds items, not
    // buildings, so every restart lost the anchor and with it the way to every
    // stash keyed at that spot. It is an item now, exactly like a placed box:
    // saved with the world, its lifetime renewed to 45 days on every boot
    // (OZ_StashAnchor.EEOnAfterLoad), and it wears the same grey locker by the
    // model's path, because the model lives in CfgVehicles only under a House
    // class this can no longer inherit. _v2/_v3 and the blue set are one word
    // away in that path.
    //
    // Heavy, huge and hitpoint-rich on purpose: nothing carries it, nothing
    // pockets it, and a magazine emptied into it changes nothing.
    class OZ_StashAnchor: Inventory_Base
    {
        scope = 2;
        displayName = "$STR_OZS_STASH";
        descriptionShort = "$STR_OZS_STASH_DESC";
        model = "\DZ\structures\Furniture\Cases\locker\locker_closed_v1.p3d";
        weight = 60000;
        itemSize[] = {10, 10};
        physLayer = "item_large";
        carveNavmesh = 1;
        canBeDigged = 0;
        rotationFlags = 2;
        class DamageSystem
        {
            class GlobalHealth
            {
                class Health
                {
                    hitpoints = 1000000;
                };
            };
        };
    };

    // The personal stash (spec 2026-09-23). One per owner, created at that
    // player's feet on opening and deleted when the session ends, so the model
    // is never seen -- the anchor is what players look at.
    //
    // The character slots are the VANILLA ones, measured 2026-09-23: the engine
    // accepted all eight on a plain container and drew their own ghost icons,
    // so every vanilla clothing item fits with no edit to its class and the
    // T148506 trap never arises. The weapon slots are the box's own, reused.
    //
    // THE SLOTS ARE NOT DECORATION -- they are the only way clothing kept
    // here stays usable. EntityAI.AreChildrenAccessible() (3_game/entities/
    // entityai.c:1662) walks up the hierarchy and returns FALSE the moment any
    // ancestor sits in CARGO, while an ATTACHMENT only costs one step of a
    // budget of INVENTORY_MAX_REACHABLE_DEPTH_ATT = 2. So a jacket dropped into
    // this grid can never have its own pockets filled -- that is vanilla, and a
    // vanilla sea chest does the same -- but a jacket hung in the Body slot
    // can. Measured on the stand 2026-09-23.
    class OZ_PersonalStash: SeaChest
    {
        scope = 2;
        displayName = "$STR_OZS_STASH";
        descriptionShort = "$STR_OZS_STASH_DESC";
        // SeaChest like the boxes, because the script class beside this
        // one is OZ_StorageBox: a stash IS a box, with a pair for a key.
        // The script side must not skip DeployableContainer_Base, which is
        // SeaChest's own script class and the box's parent.
        //
        // Turned off on purpose: this one is invisible, stands under a
        // player's feet and lives for one session.
        carveNavmesh = 0;
        canBeDigged = 0;
        weight = 0;
        attachments[] = {"Headgear", "Mask", "Eyewear", "Body", "Vest", "Back", "Hips", "Legs", "Feet", "Gloves", "Armband", "Shoulder", "Melee", "OZ_Weapon_1", "OZ_Weapon_2", "OZ_Weapon_3", "OZ_Weapon_4"};
        class Cargo
        {
            itemsCargoSize[] = {10, 50};
            openable = 0;
            allowOwnedCargoManipulation = 1;
        };
        class GUIInventoryAttachmentsProps
        {
            class Gear
            {
                name = "$STR_OZS_SLOTS_GEAR";
                description = "";
                attachmentSlots[] = {"Headgear", "Mask", "Eyewear", "Body", "Vest", "Back", "Hips", "Legs", "Feet", "Gloves", "Armband"};
                icon = "set:dayz_inventory image:cat_common_cargo";
                view_index = 1;
            };
            class Weapons
            {
                name = "$STR_OZS_SLOTS_WEAPONS";
                description = "";
                attachmentSlots[] = {"Shoulder", "Melee", "OZ_Weapon_1", "OZ_Weapon_2", "OZ_Weapon_3", "OZ_Weapon_4"};
                icon = "set:dayz_inventory image:cat_common_cargo";
                view_index = 2;
            };
        };
    };

    // The kits a player carries and deploys into a box (OZS_Kit.c). Each has a
    // "<kit>Placing" twin: the class the hologram projects, with the box's model
    // and hologram material (Hologram reads them from the projection's class).
    class OZ_StorageBoxKit_Small: Inventory_Base
    {
        scope = 2;
        displayName = "$STR_OZS_KIT_SMALL";
        descriptionShort = "$STR_OZS_KIT_SMALL_DESC";
        model = "\DZ\gear\camping\wooden_case.p3d";
        rotationFlags = 2;
        itemSize[] = {5, 4};
        weight = 5000;
        itemBehaviour = 0;
        hologramMaterial = "wooden_case";
        hologramMaterialPath = "dz\gear\camping\data";
        class DamageSystem
        {
            class GlobalHealth
            {
                class Health
                {
                    hitpoints = 500;
                };
            };
        };
    };
    class OZ_StorageBoxKit_SmallPlacing: OZ_StorageBoxKit_Small
    {
        scope = 1;
        displayName = "This is a hologram";
    };
    class OZ_StorageBoxKit_Medium: OZ_StorageBoxKit_Small
    {
        displayName = "$STR_OZS_KIT_MEDIUM";
        descriptionShort = "$STR_OZS_KIT_MEDIUM_DESC";
        model = "\DZ\gear\camping\sea_chest.p3d";
        itemSize[] = {6, 5};
        weight = 7000;
        hologramMaterial = "sea_chest";
    };
    class OZ_StorageBoxKit_MediumPlacing: OZ_StorageBoxKit_Medium
    {
        scope = 1;
        displayName = "This is a hologram";
    };
    class OZ_StorageBoxKit_Large: OZ_StorageBoxKit_Medium
    {
        displayName = "$STR_OZS_KIT_LARGE";
        descriptionShort = "$STR_OZS_KIT_LARGE_DESC";
        itemSize[] = {8, 5};
        weight = 9000;
    };
    class OZ_StorageBoxKit_LargePlacing: OZ_StorageBoxKit_Large
    {
        scope = 1;
        displayName = "This is a hologram";
    };
};

// Vanilla weapons learn the box slots. Both bases declare inventorySlot[] as
// an ARRAY in DZ/config.bin (Rifle_Base {"Shoulder","Melee"}, Pistol_Base
// {"Pistol"}), so += extends rather than replaces; the concrete rifles and
// pistols inherit without redeclaring (measured 2026-09-16).
class CfgWeapons
{
    class RifleCore;
    class PistolCore;

    class Rifle_Base: RifleCore
    {
        inventorySlot[] += {"OZ_Weapon_1", "OZ_Weapon_2", "OZ_Weapon_3", "OZ_Weapon_4", "OZ_Weapon_5", "OZ_Weapon_6"};
    };

    class Pistol_Base: PistolCore
    {
        inventorySlot[] += {"OZ_Weapon_1", "OZ_Weapon_2", "OZ_Weapon_3", "OZ_Weapon_4", "OZ_Weapon_5", "OZ_Weapon_6"};
    };
};
