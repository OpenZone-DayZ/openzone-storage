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
        if (OZS_Hide(actor_in_radius))
            return true;
        return super.ExcludeFromContainer_Phase1(actor_in_radius);
    }

    override bool ExcludeFromContainer_Phase2(Object object_in_radius)
    {
        if (OZS_Hide(object_in_radius))
            return true;
        return super.ExcludeFromContainer_Phase2(object_in_radius);
    }

    override bool ExcludeFromContainer_Phase3(Object object_in_cone)
    {
        if (OZS_Hide(object_in_cone))
            return true;
        return super.ExcludeFromContainer_Phase3(object_in_cone);
    }

    // Two reasons to keep a box out of the panel, and nothing else is touched.
    protected bool OZS_Hide(Object o)
    {
        if (OZS_NotMyStash(o))
            return true;
        return OZS_StandInFor(o);
    }

    // A PLACED BOX IS AN ANCHOR, NOT A CONTAINER, AND DOES NOT ANNOUNCE
    // ITSELF BY PROXIMITY (design 2026-09-24 §11; owner, 2026-09-24: "the box
    // still has its proximity").
    //
    // In this scheme the placed box never holds anything: the contents live in
    // an authority nobody is told about, and the player is shown a proxy. Left
    // in the vicinity list the box would appear next to its own proxy, same
    // class, same name, empty -- and the player would have no way to tell
    // which is which. It is a place with a button now, and the button is the
    // only way in.
    //
    // The exception is a box materialised THE OLD WAY -- by an admin command,
    // or by the boot reconciliation. Then it really does hold its items and
    // hiding it would hide them.
    protected bool OZS_StandInFor(Object o)
    {
        OZ_StorageBox box = OZ_StorageBox.Cast(o);
        if (!box)
            return false;
        // A personal stash is not an anchor: it is the container, and the
        // rule above it decides whether this player may see it.
        if (OZ_PersonalStash.Cast(box))
            return false;
        return box.OZS_GetState() == OZS_Const.STATE_CLOSED;
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
