// Client only. Keeps other players' stashes out of the vicinity panel.
//
// The panel is built entirely on the client: RefreshVicinityItems scans the
// geometry around the local player and hands the result to the container
// widgets, which keep an entity only if IsInventoryVisible() lets them
// (5_mission/gui/inventorynew/inherited/vicinitycontainer.c:383). The three
// ExcludeFromContainer phases are the sanctioned place to drop an entity from
// that scan; at least six unrelated mods override them, and vanilla's own
// underground stash hides itself with the same pair of display gates
// (4_world/entities/undergroundstash.c:24, :54).
//
// This is a filter, not a lock. See the note in OZ_PersonalStash.c: the entity
// is still replicated to everyone nearby, and the server does not check the
// owner on a move. The owner chose this trade on 2026-09-23.
modded class VicinityItemManager
{
    override bool ExcludeFromContainer_Phase1(Object actor_in_radius)
    {
        if (OZS_NotMyStash(actor_in_radius))
            return true;
        return super.ExcludeFromContainer_Phase1(actor_in_radius);
    }

    override bool ExcludeFromContainer_Phase2(Object object_in_radius)
    {
        if (OZS_NotMyStash(object_in_radius))
            return true;
        return super.ExcludeFromContainer_Phase2(object_in_radius);
    }

    override bool ExcludeFromContainer_Phase3(Object object_in_cone)
    {
        if (OZS_NotMyStash(object_in_cone))
            return true;
        return super.ExcludeFromContainer_Phase3(object_in_cone);
    }

    // True only for a stash that belongs to someone else. Anything that is not
    // a stash falls through untouched -- this override must never decide the
    // fate of an object it was not written for.
    protected bool OZS_NotMyStash(Object o)
    {
        OZ_PersonalStash stash = OZ_PersonalStash.Cast(o);
        if (!stash)
            return false;
        return !stash.OZS_IsMine();
    }
}
