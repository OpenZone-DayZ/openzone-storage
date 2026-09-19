// OpenZone Storage Probe -- measurement tooling for the virtual-storage research.
//
// Not a gameplay mod and never published. It exists to put numbers behind the
// design decision "how should thousands of stored items be materialised":
//  * OZ_ProbeCrate: a SeaChest with a 10 x 500 cargo grid (5000 one-slot items);
//  * counters on ItemBase.EEInit / EEDelete on both sides of the wire;
//  * a per-frame monitor in MissionServer.OnUpdate and MissionGameplay.OnUpdate;
//  * batched jobs (fill / clear / capture / load / restore) driven through the
//    MCP bridge verb `oz_probe` (see OpenZone_StorageProbe_Bridge).
// Every result is one JSON line in $profile:OpenZone_StorageProbe/results.log.

class CfgPatches
{
    class OpenZone_StorageProbe
    {
        units[] = {"OZ_ProbeCrate", "OZ_ProbeCrateSmall", "OZ_ProbeCrateWide", "OZ_ProbeCrateHuge", "OZ_ProbeCrateSquare"};
        weapons[] = {};
        requiredVersion = 0.1;
        requiredAddons[] =
        {
            "DZ_Data",
            "DZ_Scripts",
            "DZ_Gear_Camping",
            "DZ_Gear_Consumables",
            "OpenZone_Storage"
        };
    };
};

class CfgMods
{
    class OpenZone_StorageProbe
    {
        dir = "OpenZone_StorageProbe";
        name = "OpenZone Storage Probe";
        author = "Zone Protocol";
        version = "0.1.0";
        type = "mod";

        dependencies[] = {"Game", "World", "Mission"};

        class defs
        {
            class gameScriptModule    { value = ""; files[] = {"OpenZone_StorageProbe/scripts/3_Game"}; };
            class worldScriptModule   { value = ""; files[] = {"OpenZone_StorageProbe/scripts/4_World"}; };
            class missionScriptModule { value = ""; files[] = {"OpenZone_StorageProbe/scripts/5_Mission"}; };
        };
    };
};

class CfgVehicles
{
    class SeaChest;
    class Paper;

    // An item only this stand pbo declares: closed into a box and then booted
    // without the probe, it is the "vanished mod" of the design (section 3.3).
    class OZ_ProbeToken: Paper
    {
        scope = 2;
        displayName = "Probe token";
    };

    // The nested Cargo class inherits everything from SeaChest's own Cargo and
    // overrides only the grid. 10 x 500 = 5000 cells for one-slot items.
    class OZ_ProbeCrate: SeaChest
    {
        scope = 2;
        displayName = "OZ Probe Crate 10x500";
        descriptionShort = "Measurement crate of the OpenZone storage probe. Not for play.";
        class Cargo
        {
            itemsCargoSize[] = {10, 500};
            openable = 0;
            allowOwnedCargoManipulation = 1;
        };
    };

    // Variants to find the engine's grid limits. Measured 2026-09-16: the
    // 10 x 500 grid reports 10x500 but creation fails from row 256 on, i.e.
    // 2560 one-slot cells is the real ceiling of one cargo of width 10.
    class OZ_ProbeCrateSmall: OZ_ProbeCrate
    {
        displayName = "OZ Probe Crate 10x100";
        class Cargo
        {
            itemsCargoSize[] = {10, 100};
        };
    };

    class OZ_ProbeCrateWide: OZ_ProbeCrate
    {
        displayName = "OZ Probe Crate 20x256";
        class Cargo
        {
            itemsCargoSize[] = {20, 256};
        };
    };

    class OZ_ProbeCrateHuge: OZ_ProbeCrate
    {
        displayName = "OZ Probe Crate 50x256";
        class Cargo
        {
            itemsCargoSize[] = {50, 256};
        };
    };

    class OZ_ProbeCrateSquare: OZ_ProbeCrate
    {
        displayName = "OZ Probe Crate 100x100";
        class Cargo
        {
            itemsCargoSize[] = {100, 100};
        };
    };
};
