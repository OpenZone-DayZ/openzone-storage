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
        units[] = {"OZ_StorageBox_Small", "OZ_StorageBox_Medium", "OZ_StorageBox_Large"};
        weapons[] = {};
        requiredVersion = 0.1;
        requiredAddons[] =
        {
            "DZ_Data",
            "DZ_Scripts",
            "DZ_Gear_Camping",
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
        version = "0.1.0";
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

    // 500 cells at the vanilla width of 10, two weapon slots.
    class OZ_StorageBox_Small: WoodenCrate
    {
        scope = 2;
        displayName = "$STR_OZS_BOX_SMALL";
        descriptionShort = "$STR_OZS_BOX_SMALL_DESC";
        attachments[] = {"OZ_Weapon_1", "OZ_Weapon_2"};
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
            itemsCargoSize[] = {10, 150};
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
