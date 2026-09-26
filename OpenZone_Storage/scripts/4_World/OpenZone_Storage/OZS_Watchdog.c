// Who takes an item out of an authoritative box.
//
// A box opened with nine items reported eight before any operation had run,
// and this mod's own `move` had not logged a line. The first witness watched
// DELETIONS and never fired once, which settles half the question: the item is
// not destroyed. It LEAVES -- it stops having the box as its parent and goes
// on existing somewhere the count cannot see (owner, 2026-09-25: the rifle,
// then the cans).
//
// So the event to watch is the parting, not the funeral. `EEParentedFrom` is
// not it either -- it never fired once -- but `OnItemLocationChanged` does,
// and the stack under it names whoever asked. One pointer walk on an item
// that is changing hands anyway, and it says nothing unless the parent was an
// AUTHORITY: a container that exists only while somebody is looking into a
// box, and only for departures the mod did not ask for itself.
class OZS_Watchdog
{
    // THE ONE ITEM THAT IS ALLOWED TO LEAVE RIGHT NOW. Set by an operation
    // just before it parts with something on purpose -- a stack emptied by
    // combining, an item handed out to a player -- and good for that one act
    // alone. Without it the ordinary use of a box is reported as the thing
    // this watches for, which buries the departures nobody asked for.
    protected static EntityAI s_Expected;

    static void Expect(EntityAI e)
    {
        s_Expected = e;
    }

    // An item changed owner. The interesting case is the one that costs the
    // box an item: it WAS in an authority and now is not.
    static void Moved(EntityAI e, EntityAI old_owner, EntityAI new_owner)
    {
        // BEHIND THE DEBUG FLAG, AND FIRST. This runs from `ItemBase.
        // OnItemLocationChanged`, which fires for EVERY item that changes
        // hands anywhere in the world -- not only in a box. The walk up the
        // hierarchy below is cheap, but cheap times everything is a cost the
        // owner should be able to switch off (owner, 2026-09-25). The flag is
        // the mod's own `DebugLog`, the one that already decides whether Dbg
        // lines are written at all.
        if (!OZ_Log.IsDebug())
            return;
        if (!GetGame() || !GetGame().IsServer() || !e || !old_owner)
            return;
        if (OZS_Controller.IsShuttingDown())
            return;
        if (!InAuthority(old_owner))
            return;
        if (e == s_Expected)
        {
            s_Expected = null;
            return;
        }
        if (new_owner && InAuthority(new_owner))
            return;
        string now = "nowhere";
        if (new_owner)
            now = new_owner.GetType();
        OZ_Log.Error("storage: proxy: " + e.GetType() + " LEFT the authority (" + old_owner.GetType() + ") and is now in " + now);
        DumpStack();
    }

    static bool InAuthority(EntityAI e)
    {
        EntityAI root = e;
        while (root.GetHierarchyParent())
            root = root.GetHierarchyParent();
        OZ_StorageBox box = OZ_StorageBox.Cast(root);
        return box && box.OZS_IsAuthority();
    }
}
