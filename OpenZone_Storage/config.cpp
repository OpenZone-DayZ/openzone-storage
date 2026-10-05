// OpenZone Storage -- boxes whose contents exist only while the box is open.
//
// Design: docs/2026-09-16-storage-box-spec.md. Three sizes, one script class
// (OZ_StorageBox) with a four-state machine, weapon slots as attachment slots
// on the box, vanilla cargo while open, files while closed.
//
// Depends HARD on OpenZone_Core (logger, config service) and, through it, on
// CF.
//
// THE SERIES' OWN ART SINCE 2026-10-05 (models, textures and animation by
// Crystal): every box and the locker wear a model of their own from
// OpenZone_Storage\models\<model>, built from the MLOD in models/<model>/ of
// the repository (models/README.md). The vanilla wooden crate and sea chest
// remain only as the config parents and as what a carried KIT looks like.
//
// What every box model shares:
//   - one whole-LOD selection, "camo". It is hidden selection 0, which is the
//     one the placement hologram repaints (Hologram.RefreshVisual asks for
//     "inventory" and falls back to index 0), and redeclaring the array
//     drops the parents' "camoGround" with the vanilla texture on it;
//   - two openings in one model.cfg, each on its own animation source: "_a"
//     smooth, "_b" snap. Phase 0 is closed, 1 open, animPeriod the whole
//     travel in seconds. The script drives ONE of them (OZ_StorageBox.
//     OZS_LidSource); the other costs nothing and stays for comparison.
//
// DamageSystem is inherited as it is, on purpose: the parents' healthLevels
// name the vanilla materials, which these models do not carry, so damage
// changes no look -- and a block of our own with only healthLevels in it
// would REPLACE the parents' class and lose their hitpoints and armour.

class CfgPatches
{
    class OpenZone_Storage
    {
        units[] =
        {
            "OZ_StorageBox_Small", "OZ_StorageBox_Medium", "OZ_StorageBox_Large", "OZ_StorageBox_Fridge",
            "OZ_StorageBoxKit_Small", "OZ_StorageBoxKit_Medium", "OZ_StorageBoxKit_Large", "OZ_StorageBoxKit_Fridge",
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

    // 250 cells at the vanilla width of 10, two weapon slots.
    class OZ_StorageBox_Small: WoodenCrate
    {
        scope = 2;
        displayName = "$STR_OZS_BOX_SMALL";
        descriptionShort = "$STR_OZS_BOX_SMALL_DESC";
        // A green plank weapons case with a hinged lid and a drop hasp,
        // 0.80 x 0.28 x 0.41 m (width, height, depth). Two colours ship and
        // the model is the same:
        //   ozs_case_small_co.paa          green-teal
        //   ozs_case_small_vanilla_co.paa  the paint of the vanilla
        //                                  StaticObj_ammoboxes_single
        model = "\OpenZone_Storage\models\small_case\ozs_case_small.p3d";
        hiddenSelections[] = {"camo"};
        hiddenSelectionsTextures[] = {"OpenZone_Storage\models\small_case\data\ozs_case_small_co.paa"};
        hiddenSelectionsMaterials[] = {"OpenZone_Storage\models\small_case\data\ozs_case_small.rvmat"};
        hologramMaterial = "ozs_case_small";
        hologramMaterialPath = "OpenZone_Storage\models\small_case\data";
        //   lid_a  smooth, 1.8 s: the hasp lowered, a pause, the lid rises
        //   lid_b  snap,   1.1 s: the hasp drops and swings, the lid is flung
        //                         open, bounces and settles
        class AnimationSources
        {
            class lid_a { source = "user"; initPhase = 0; animPeriod = 1.8; };
            class lid_b { source = "user"; initPhase = 0; animPeriod = 1.1; };
        };
        // THE WEAPON RACK AND NOTHING ELSE (owner, 2026-10-05): a box keeps
        // its weapon slots -- two, four and six by size -- and loses every
        // other. From 2026-09-27 a shared box also had the character's own
        // clothing slots; hanging a kit up is the personal stash's business
        // alone now. A record written before this may still name a hook the
        // box has lost: OZS_Records.Create lays such a root in the grid.
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

    // 500 cells, four weapon slots.
    class OZ_StorageBox_Medium: SeaChest
    {
        scope = 2;
        displayName = "$STR_OZS_BOX_MEDIUM";
        descriptionShort = "$STR_OZS_BOX_MEDIUM_DESC";
        // A painted plank crate with steel fittings and two hasp tongues,
        // 1.03 x 0.35 x 0.52 m. Three looks of the same model, each a texture
        // AND its material (own _nohq and _smdi), with hologram materials to
        // match (<material>_deployable.rvmat):
        //   ozs_crate_medium_co.paa        + ozs_crate_medium.rvmat        light wear
        //   ozs_crate_medium_worn_co.paa   + ozs_crate_medium_worn.rvmat   worn
        //   ozs_crate_medium_heavy_co.paa  + ozs_crate_medium_heavy.rvmat  beaten up
        model = "\OpenZone_Storage\models\medium_crate\ozs_crate_medium.p3d";
        hiddenSelections[] = {"camo"};
        hiddenSelectionsTextures[] = {"OpenZone_Storage\models\medium_crate\data\ozs_crate_medium_co.paa"};
        hiddenSelectionsMaterials[] = {"OpenZone_Storage\models\medium_crate\data\ozs_crate_medium.rvmat"};
        hologramMaterial = "ozs_crate_medium";
        hologramMaterialPath = "OpenZone_Storage\models\medium_crate\data";
        //   case_a  smooth, 2.4 s: the two tongues lifted off one by one, the
        //                          lid raised onto the hinge stop
        //   case_b  snap,   1.4 s: the tongues knocked off at once, the lid
        //                          flung open onto the stop
        class AnimationSources
        {
            class case_a { source = "user"; initPhase = 0; animPeriod = 2.4; };
            class case_b { source = "user"; initPhase = 0; animPeriod = 1.4; };
        };
        // The weapon rack and nothing else, as on the small box.
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

    // 1000 cells, six weapon slots. 100 rows is well inside the engine's
    // ceiling of 256 rows per cargo grid (measured 2026-09-16).
    class OZ_StorageBox_Large: SeaChest
    {
        scope = 2;
        displayName = "$STR_OZS_BOX_LARGE";
        descriptionShort = "$STR_OZS_BOX_LARGE_DESC";
        // A plastic military hard case, 128 cm: 1.27 x 0.47 x 0.64 m, four
        // draw latches that flip down before the lid opens on its back
        // hinges. (The same box in another shell is OZ_StorageBox_Fridge,
        // below.)
        model = "\OpenZone_Storage\models\hardcase_128\ozs_hardcase_128.p3d";
        hiddenSelections[] = {"camo"};
        hiddenSelectionsTextures[] = {"OpenZone_Storage\models\hardcase_128\data\ozs_hardcase_128_co.paa"};
        hiddenSelectionsMaterials[] = {"OpenZone_Storage\models\hardcase_128\data\ozs_hardcase_128.rvmat"};
        hologramMaterial = "ozs_hardcase_128";
        hologramMaterialPath = "OpenZone_Storage\models\hardcase_128\data";
        soundImpactType = "plastic";
        //   case_a  smooth, 2.8 s: the four latches undone one by one, the
        //                          lid lifted
        //   case_b  snap,   1.5 s: the latches torn down at once and
        //                          bouncing, the lid flung open
        class AnimationSources
        {
            class case_a { source = "user"; initPhase = 0; animPeriod = 2.8; };
            class case_b { source = "user"; initPhase = 0; animPeriod = 1.5; };
        };
        // The weapon rack and nothing else, as on the small box.
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
    // THE LARGE BOX IN ANOTHER SHELL (owner, 2026-10-05: "the fridge as an
    // alternative large box"): an old Soviet fridge lying on its back, door
    // up, 1.20 x 0.58 x 0.61 m. Everything a large box is -- the grid, the
    // slots, the script class behind it -- comes from OZ_StorageBox_Large;
    // only the look, the sound and the opening are its own, and it has a
    // kit of its own (OZ_StorageBoxKit_Fridge).
    //
    // _ca, not _co: the rusted-through holes low on the walls are cut out by
    // the texture's alpha, and the material carries AlphaTest128 for it --
    // the two belong together.
    class OZ_StorageBox_Fridge: OZ_StorageBox_Large
    {
        displayName = "$STR_OZS_BOX_FRIDGE";
        descriptionShort = "$STR_OZS_BOX_FRIDGE_DESC";
        model = "\OpenZone_Storage\models\fridge\ozs_fridge.p3d";
        hiddenSelections[] = {"camo"};
        hiddenSelectionsTextures[] = {"OpenZone_Storage\models\fridge\data\ozs_fridge_ca.paa"};
        hiddenSelectionsMaterials[] = {"OpenZone_Storage\models\fridge\data\ozs_fridge.rvmat"};
        hologramMaterial = "ozs_fridge";
        hologramMaterialPath = "OpenZone_Storage\models\fridge\data";
        soundImpactType = "metal";
        //   door_a  smooth, 2.0 s: the lever pulled and let go, the door rises
        //                          and rests at 100 degrees
        //   door_b  snap,   1.2 s: the lever yanked, the door flung open past
        //                          upright, it hits the stop at 110 degrees,
        //                          bounces and settles
        // Its own block on purpose: it replaces the hard case's sources,
        // which this model does not have.
        class AnimationSources
        {
            class door_a { source = "user"; initPhase = 0; animPeriod = 2.0; };
            class door_b { source = "user"; initPhase = 0; animPeriod = 1.2; };
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
    // (OZ_StashAnchor.EEOnAfterLoad).
    //
    // ITS OWN MODEL SINCE 2026-10-05: a steel cabinet with two mesh doors,
    // 1.00 x 1.90 x 0.52 m -- the height of the vanilla locker_closed_v1 it
    // wore until then. No hiddenSelections: an admin places the anchor, no
    // kit's hologram ever projects it, and the mesh doors carry a second,
    // tiled and alpha-cut texture that a whole-LOD "camo" would paint over.
    //
    // THE DOORS OPEN ON THE OPENING PLAYER'S CLIENT ONLY (OZ_StashAnchor.
    // OZS_ShowOpen, called by that client's own mirror): the server never
    // sets the phase, so everybody else keeps seeing the locker closed, and
    // nobody walks up to their locker to find somebody else's doors standing
    // open. The collision is one solid box of the closed cabinet for the
    // same reason -- a door only one client draws must not collide.
    //   locker_a  smooth, 2.0 s: the lever turned a quarter, the right door
    //                            opens to 105 degrees
    //   locker_b  snap,   1.2 s: the lever yanked, both doors flung open,
    //                            they hit the stop, bounce and settle
    //
    // Heavy, huge and hitpoint-rich on purpose: nothing carries it, nothing
    // pockets it, and a magazine emptied into it changes nothing.
    class OZ_StashAnchor: Inventory_Base
    {
        scope = 2;
        displayName = "$STR_OZS_STASH";
        descriptionShort = "$STR_OZS_STASH_DESC";
        model = "\OpenZone_Storage\models\locker\ozs_locker.p3d";
        soundImpactType = "metal";
        class AnimationSources
        {
            class locker_a { source = "user"; initPhase = 0; animPeriod = 2.0; };
            class locker_b { source = "user"; initPhase = 0; animPeriod = 1.2; };
        };
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
        // THE NAME AND THE DESCRIPTION ARE A WARNING TO ADMINS (owner,
        // 2026-09-27): an admin spawner listed this class under the same
        // name as the locker, and two of these were put into the world as
        // sea chests that nothing can open. Only the locker is placed; this
        // class exists as a session's authority and mirror, and the players'
        // screen keeps calling it a personal stash (OZ_PersonalStash.
        // GetDisplayName).
        displayName = "$STR_OZS_STASH_CONTAINER";
        descriptionShort = "$STR_OZS_STASH_CONTAINER_DESC";
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
        attachments[] = {"Headgear", "Mask", "Eyewear", "Body", "Vest", "Back", "Hips", "Legs", "Feet", "Gloves", "Armband", "OZ_Weapon_1", "OZ_Weapon_2", "OZ_Weapon_3", "OZ_Weapon_4"};
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
                attachmentSlots[] = {"OZ_Weapon_1", "OZ_Weapon_2", "OZ_Weapon_3", "OZ_Weapon_4"};
                icon = "set:dayz_inventory image:cat_common_cargo";
                view_index = 2;
            };
        };
    };

    // The kits a player carries and deploys into a box (OZS_Kit.c). Each has a
    // "<kit>Placing" twin: the class the hologram projects, with the box's model
    // and hologram material (Hologram reads them from the projection's class).
    // The CARRIED kit keeps a vanilla model -- a packed box is not the box --
    // and the twin wears the box's own, with its "camo" for the hologram to
    // repaint with <hologramMaterial>_deployable / _undeployable.rvmat.
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
        hologramMaterial = "ozs_case_small";
        hologramMaterialPath = "OpenZone_Storage\models\small_case\data";
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
        model = "\OpenZone_Storage\models\small_case\ozs_case_small.p3d";
        hiddenSelections[] = {"camo"};
    };
    class OZ_StorageBoxKit_Medium: OZ_StorageBoxKit_Small
    {
        displayName = "$STR_OZS_KIT_MEDIUM";
        descriptionShort = "$STR_OZS_KIT_MEDIUM_DESC";
        model = "\DZ\gear\camping\sea_chest.p3d";
        itemSize[] = {6, 5};
        weight = 7000;
        hologramMaterial = "ozs_crate_medium";
        hologramMaterialPath = "OpenZone_Storage\models\medium_crate\data";
    };
    class OZ_StorageBoxKit_MediumPlacing: OZ_StorageBoxKit_Medium
    {
        scope = 1;
        displayName = "This is a hologram";
        model = "\OpenZone_Storage\models\medium_crate\ozs_crate_medium.p3d";
        hiddenSelections[] = {"camo"};
    };
    class OZ_StorageBoxKit_Large: OZ_StorageBoxKit_Medium
    {
        displayName = "$STR_OZS_KIT_LARGE";
        descriptionShort = "$STR_OZS_KIT_LARGE_DESC";
        itemSize[] = {8, 5};
        weight = 9000;
        hologramMaterial = "ozs_hardcase_128";
        hologramMaterialPath = "OpenZone_Storage\models\hardcase_128\data";
    };
    class OZ_StorageBoxKit_LargePlacing: OZ_StorageBoxKit_Large
    {
        scope = 1;
        displayName = "This is a hologram";
        model = "\OpenZone_Storage\models\hardcase_128\ozs_hardcase_128.p3d";
        hiddenSelections[] = {"camo"};
    };
    // The large kit under another name: it becomes the fridge.
    class OZ_StorageBoxKit_Fridge: OZ_StorageBoxKit_Large
    {
        displayName = "$STR_OZS_KIT_FRIDGE";
        descriptionShort = "$STR_OZS_KIT_FRIDGE_DESC";
        hologramMaterial = "ozs_fridge";
        hologramMaterialPath = "OpenZone_Storage\models\fridge\data";
    };
    class OZ_StorageBoxKit_FridgePlacing: OZ_StorageBoxKit_Fridge
    {
        scope = 1;
        displayName = "This is a hologram";
        model = "\OpenZone_Storage\models\fridge\ozs_fridge.p3d";
        hiddenSelections[] = {"camo"};
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
