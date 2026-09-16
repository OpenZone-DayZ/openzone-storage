// Server-side glue between the storage mod and the MCP bridge: the
// `oz_storage` verb for world_exec (spawn a box, open, close, status). A
// development tool for the stand, never published. Requires @DZMCP_Bridge,
// which the stand loads via -serverMod; this pbo goes there too.

class CfgPatches
{
    class OpenZone_Storage_Bridge
    {
        units[] = {};
        weapons[] = {};
        requiredVersion = 0.1;
        requiredAddons[] =
        {
            "DZ_Data",
            "DZ_Scripts",
            "DZMCP_Bridge",
            "OpenZone_Storage"
        };
    };
};

class CfgMods
{
    class OpenZone_Storage_Bridge
    {
        dir = "OpenZone_Storage_Bridge";
        name = "OpenZone Storage Bridge";
        author = "Zone Protocol";
        version = "0.1.0";
        type = "mod";

        dependencies[] = {"Game", "World", "Mission"};

        class defs
        {
            class missionScriptModule { value = ""; files[] = {"OpenZone_Storage_Bridge/scripts/5_Mission"}; };
        };
    };
};
