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
    //
    // NO LONGER ONLY A WITNESS (2026-09-26). It used to run behind the debug
    // flag and only say what it saw. A charge set off beside a placed box
    // ruined the authority standing in its coordinates, vanilla dropped all
    // 264 entities on the ground, this shouted 264 times -- and then the
    // session's closing write set the record to what the box held, nothing.
    // Now the stray is DELETED, since the record still describes it and the
    // next open builds it again exactly once, and the session is told it is
    // compromised, so nothing is written over the record on the way out.
    // That has to run on every server, not only on one with DebugLog on;
    // the walk below is a few pointer hops on an event, not a frame, and it
    // is skipped outright while no authority stands (OZS_Authority.Any).
    static void Moved(EntityAI e, EntityAI old_owner, EntityAI new_owner)
    {
        if (!GetGame() || !GetGame().IsServer() || !e || !old_owner)
            return;
        if (!OZS_Authority.Any())
            return;
        if (OZS_Controller.IsShuttingDown())
            return;
        OZ_StorageBox box = AuthorityOf(old_owner);
        if (!box)
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
        OZ_Log.Error("storage: proxy: " + e.GetType() + " LEFT the authority (" + old_owner.GetType() + ") and is now in " + now + "; it is deleted, the record keeps it");
        if (OZ_Log.IsDebug())
            DumpStack();
        OZS_Session s = OZS_Proxies.Get().Find(box.OZS_GetId());
        if (s)
            s.Compromised(e.GetType() + " left the box without an operation (now in " + now + ")");
        GetGame().ObjectDelete(e);
    }

    static bool InAuthority(EntityAI e)
    {
        return AuthorityOf(e) != null;
    }

    // The authority an entity stands in, by its hierarchy root; null when it
    // stands in none.
    static OZ_StorageBox AuthorityOf(EntityAI e)
    {
        EntityAI root = e;
        while (root.GetHierarchyParent())
            root = root.GetHierarchyParent();
        OZ_StorageBox box = OZ_StorageBox.Cast(root);
        if (box && box.OZS_IsAuthority())
            return box;
        return null;
    }
}
