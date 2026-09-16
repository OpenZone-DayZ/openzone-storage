// The measurement crates. Behaviour is SeaChest's; only the cargo grid differs
// (config.cpp). Every variant descends from OZ_ProbeCrate so one IsKindOf finds
// them all. The variants exist to measure the engine's cargo limits: a 10x500
// grid accepted only 256 rows (2560 one-slot items) on 2026-09-16.
class OZ_ProbeCrate: SeaChest
{
}

class OZ_ProbeCrateSmall: OZ_ProbeCrate
{
}

class OZ_ProbeCrateWide: OZ_ProbeCrate
{
}

class OZ_ProbeCrateHuge: OZ_ProbeCrate
{
}

class OZ_ProbeCrateSquare: OZ_ProbeCrate
{
}

// Entity lifecycle counters, incremented on BOTH sides. On the client they say
// how many item entities the network bubble delivered; on the server how many
// the probe (or anything else) created and deleted.
class OZ_ProbeCounters
{
    static int s_ItemInits;
    static int s_ItemDeletes;
}

modded class ItemBase
{
    override void EEInit()
    {
        super.EEInit();
        OZ_ProbeCounters.s_ItemInits++;
    }

    override void EEDelete(EntityAI parent)
    {
        super.EEDelete(parent);
        OZ_ProbeCounters.s_ItemDeletes++;
    }
}
