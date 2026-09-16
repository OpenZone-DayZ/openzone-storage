// Server-side glue between the storage probe and the MCP bridge: adds the
// `oz_probe` verb to DZMCP_BridgeCore so world_exec can drive the probe.
// Requires @DZMCP_Bridge, which the stand loads via -serverMod; this pbo goes
// there too (mods.server_only in dayz-mcp.local.toml).

class CfgPatches
{
    class OpenZone_StorageProbe_Bridge
    {
        units[] = {};
        weapons[] = {};
        requiredVersion = 0.1;
        requiredAddons[] =
        {
            "DZ_Data",
            "DZ_Scripts",
            "DZMCP_Bridge",
            "OpenZone_StorageProbe"
        };
    };
};

class CfgMods
{
    class OpenZone_StorageProbe_Bridge
    {
        dir = "OpenZone_StorageProbe_Bridge";
        name = "OpenZone Storage Probe Bridge";
        author = "Zone Protocol";
        version = "0.1.0";
        type = "mod";

        dependencies[] = {"Game", "World", "Mission"};

        class defs
        {
            class missionScriptModule { value = ""; files[] = {"OpenZone_StorageProbe_Bridge/scripts/5_Mission"}; };
        };
    };
};
