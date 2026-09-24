// The boundary between the box and the world: taking an item out, and putting
// one in.
// Design: docs/specs/2026-09-24-storage-proxy-inventory-design.md §6, §7.
//
// THE ENTITY MOVES. IT IS NOT COPIED. This turned out simpler than the design
// first assumed: the item in the authoritative container is a real entity, the
// item in a player's hands is the same real entity, and the only difference
// between the two is whether the network has been told about it. So the whole
// crossing is a server-side move plus one call:
//
//   box -> player   move it, then RemoteObjectTreeCreate  (announce it)
//   player -> box   RemoteObjectTreeDelete, then move it  (unannounce it)
//
// Blob, attachments, nested cargo cross for free, because nothing is captured
// and nothing is recreated. Neither direction can duplicate or lose an item
// BY ITSELF (measured: results-unpublish.md).
//
// WHAT CAN, IS THE COMMIT, AND ITS ORDER IS NOT A MATTER OF TASTE:
//
//   THE STEP THAT COULD CREATE A DUPLICATE GOES LAST.
//
//   · OUT: the item leaves SQL (which is saved) for a player's inventory
//     (which is also saved). Announce it after SQL has forgotten it, and a
//     crash in between loses the item. Do it the other way and the item is in
//     the player's save AND in the box: a duplicate.
//   · IN: the item leaves the player's inventory (saved) for the authority
//     (not saved). Write SQL after the move, and a crash in between loses it.
//     Write SQL first and a crash duplicates it.
//
// Losing is visible and an admin can put it right from the history. A
// duplicate is invisible and eats the economy. The choice is deliberate (§7).
class OZS_Boundary
{
    // ---- box -> player ---------------------------------------------------

    // The item named by `handle` goes WHERE THE PLAYER PUT IT. The client sends
    // the destination its screen computed -- the backpack, the vest pocket, the
    // hands, the cell -- and this honours it. Only when the destination cannot
    // be used does it fall back to "anywhere it fits": an item that lands in
    // the hands when the player dropped it into a backpack is a bug, not a
    // convenience (owner, 2026-09-24).
    static void Out(OZS_Session s, OZS_Watcher w, int handle, int netLow, int netHigh, int lt, int slot, int row, int col, int flip)
    {
        EntityAI e = OZS_Authority.ByHandle(s.m_Auth, handle);
        if (!e)
        {
            w.No(handle, "no such item", s.m_Version);
            return;
        }
        PlayerBase player = PlayerBase.Cast(w.Player());
        if (!player)
        {
            w.No(handle, "you are not here", s.m_Version);
            return;
        }
        InventoryLocation dst = new InventoryLocation();
        if (!Asked(player, e, netLow, netHigh, lt, slot, row, col, flip, dst) && !Somewhere(player, e, dst))
        {
            w.No(handle, "#STR_OZS_NO_ROOM", s.m_Version);
            return;
        }
        // Taken before anything moves: afterwards the entity belongs to the
        // player and its root in the record is unreachable from it.
        int wasRoot = OZS_Commit.RootOf(s, e);
        bool wasItself = OZS_Commit.TopOf(s, e) == e;
        if (wasRoot < 0)
        {
            w.No(handle, "this item is not in the record", s.m_Version);
            return;
        }

        // 1. SQL FIRST. Until this answers, the item is in the box and
        //    nowhere else; if the server dies now, nothing has happened.
        OZS_Commit.Left(s, wasRoot, wasItself);

        // 2. Then the move, and only then the announcement. Between these two
        //    the item is a server-side object nobody has been told about --
        //    which is what it has been all along.
        InventoryLocation src = new InventoryLocation();
        e.GetInventory().GetCurrentInventoryLocation(src);
        if (!e.GetInventory().TakeToDst(InventoryMode.LOCAL, src, dst))
        {
            // The record has already let it go. Rather than lose it, the item
            // is put back into the box and the record told again.
            OZ_Log.Error("storage: proxy: box " + s.m_Id + " could not hand #" + handle.ToString() + " to " + w.m_Uid + "; it stays in the box");
            OZS_Commit.Added(s, e);
            w.No(handle, "#STR_OZS_NO_ROOM", s.m_Version);
            return;
        }
        GetGame().RemoteObjectTreeCreate(e);
        OZS_Authority.Forget(s.m_Auth);
        s.Touch();
        s.TellGone(handle, w.m_Uid);
        OZS_Audit.Log("out", s.m_Id, w.m_Uid, w.Name(), e.GetType(), 0, -1, -1, "", "taken from the box");
    }

    // ---- player -> box ---------------------------------------------------

    // An item of the player's goes into the box. The client names it by its
    // NETWORK id, because on their side it is a real announced entity; that is
    // the one place in this design where a network id is the right name.
    static void In(OZS_Session s, OZS_Watcher w, int netLow, int netHigh, int into, int lt, int slot, int row, int col, int flip)
    {
        PlayerBase player = PlayerBase.Cast(w.Player());
        if (!player)
        {
            w.No(0, "you are not here", s.m_Version);
            return;
        }
        EntityAI e = EntityAI.Cast(GetGame().GetObjectByNetworkId(netLow, netHigh));
        if (!e)
        {
            w.No(0, "no such item", s.m_Version);
            return;
        }
        // It must be the player's own: an item on the ground, in somebody
        // else's hands or in another container is not theirs to put away.
        if (e.GetHierarchyRootPlayer() != player)
        {
            w.No(0, "that is not yours to put away", s.m_Version);
            return;
        }
        if (OZS_Ops.Holds(e, s.m_Auth))
        {
            w.No(0, "an item cannot swallow the box", s.m_Version);
            return;
        }
        // Into the box itself, or into a container that is in the box: the
        // player may have dropped it onto a backpack that lives inside.
        EntityAI holder = s.m_Auth;
        if (into != 0)
        {
            holder = OZS_Authority.ByHandle(s.m_Auth, into);
            if (!holder)
            {
                w.No(0, "no such container in the box", s.m_Version);
                return;
            }
        }
        InventoryLocation dst = new InventoryLocation();
        if (lt == InventoryLocationType.ATTACHMENT)
            dst.SetAttachment(holder, e, slot);
        else if (row >= 0 && OZS_Ops.Fits(holder, lt, slot, row, col))
            dst.SetCargo(holder, e, 0, row, col, flip == 1);
        else if (!holder.GetInventory().FindFirstFreeLocationForNewEntity(e.GetType(), FindInventoryLocationType.CARGO, dst))
        {
            w.No(0, "#STR_OZS_FULL", s.m_Version);
            return;
        }

        // 1. Off the network first: every client stops knowing this item,
        //    including the one whose player is holding it.
        GetGame().RemoteObjectTreeDelete(e);

        // 2. The move. Now it is in the box and in nobody's save.
        InventoryLocation src = new InventoryLocation();
        e.GetInventory().GetCurrentInventoryLocation(src);
        if (!e.GetInventory().TakeToDst(InventoryMode.LOCAL, src, dst))
        {
            // It is off the network and did not arrive: announce it again
            // where it was, which is where it still is.
            GetGame().RemoteObjectTreeCreate(e);
            w.No(0, "the box refused it", s.m_Version);
            return;
        }

        // 3. SQL LAST. A crash between the move and this loses the item; the
        //    other order would duplicate it (§7).
        OZS_Commit.Added(s, e);
        OZS_Authority.Index(s.m_Auth);
        s.Touch();
        s.TellAdded(e, w.m_Uid);
        OZS_Audit.Log("in", s.m_Id, w.m_Uid, w.Name(), e.GetType(), 0, -1, -1, "", "put into the box");
    }

    // The destination the player's own screen chose, if it can be used.
    //
    // THE CONTAINER MUST BE THE PLAYER'S OWN. It arrives as a network id from
    // the client, so it could be anything in the world: another player's vest,
    // a car five hundred metres away, the box itself. Only the player's own
    // hierarchy is accepted, and the engine is asked whether the place will
    // take the item -- a false here simply means the fallback runs.
    static bool Asked(PlayerBase player, EntityAI e, int netLow, int netHigh, int lt, int slot, int row, int col, int flip, out InventoryLocation dst)
    {
        if (lt == InventoryLocationType.HANDS)
        {
            if (player.GetHumanInventory().GetEntityInHands())
                return false;
            dst.SetHands(player, e);
            return player.GetInventory().LocationCanAddEntity(dst);
        }
        if (netLow == 0 && netHigh == 0)
            return false;
        EntityAI into = EntityAI.Cast(GetGame().GetObjectByNetworkId(netLow, netHigh));
        if (!into)
            return false;
        // Their own, and not something already inside the box.
        if (into != player && into.GetHierarchyRootPlayer() != player)
            return false;
        if (lt == InventoryLocationType.ATTACHMENT)
            dst.SetAttachment(into, e, slot);
        else if (row >= 0 && col >= 0)
            dst.SetCargo(into, e, 0, row, col, flip == 1);
        else
            return false;
        return player.GetInventory().LocationCanAddEntity(dst);
    }

    // Hands if they are free, otherwise anywhere the player's own inventory
    // will take it. Asked of the engine, so every mod's rules apply.
    static bool Somewhere(PlayerBase player, EntityAI e, out InventoryLocation dst)
    {
        HumanInventory hi = player.GetHumanInventory();
        if (hi && !hi.GetEntityInHands())
        {
            dst.SetHands(player, e);
            if (player.GetInventory().LocationCanAddEntity(dst))
                return true;
        }
        InventoryLocation any = new InventoryLocation();
        if (player.GetInventory().FindFreeLocationFor(e, FindInventoryLocationType.CARGO, any))
        {
            dst.Copy(any);
            return true;
        }
        if (player.GetInventory().FindFreeLocationFor(e, FindInventoryLocationType.ANY, any))
        {
            dst.Copy(any);
            return true;
        }
        return false;
    }
}
