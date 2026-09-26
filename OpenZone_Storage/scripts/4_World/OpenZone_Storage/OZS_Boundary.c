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
        string wasType = e.GetType();
        if (wasRoot < 0)
        {
            w.No(handle, "this item is not in the record", s.m_Version);
            return;
        }

        // 1. SQL FIRST. Until this answers, the item is in the box and
        //    nowhere else; if the server dies now, nothing has happened.
        OZS_Commit.Left(s, wasRoot, wasItself, wasType);

        // AND, IF THE SERVER IS SET TO, WAIT FOR IT TO HAVE ANSWERED.
        //
        // "SQL first" above means the letter is on its way, not that it has
        // landed. The rest of this function runs in the same frame, so the
        // item is in the player's inventory -- and their save -- long before
        // the bridge replies. A session that then fails leaves the record on
        // its last confirmed version, which still holds this item: it is in
        // the player's save AND in the box, and the next open builds a second
        // one from the record (review 2026-09-25, finding 1).
        //
        // With WaitForRecord on, the hand-over is held until the answer comes
        // back. Nothing can be duplicated, because a turn that was never
        // written is a turn in which the item never left the box -- and if the
        // session fails instead, the item is still in the authority, the
        // record still describes it, and the next open produces exactly one.
        // The price is one round trip, which the session's status line
        // measures.
        if (OZS_Settings.Get().WaitForRecord)
        {
            if (s.HoldHandover(w, e, dst, handle))
                return;
            // No turn went out to wait for -- the letter was not sent, and
            // `Left` has already failed the session if the bridge is gone.
            // There is nothing to hold and nothing to hand over.
            return;
        }
        Handover(s, w, e, dst, handle);
    }

    // The second half of a take-out: the move, the announcement and the
    // bookkeeping. Split out because it either runs in the same frame as the
    // letter or one round trip later, and the two must be the same code.
    static void Handover(OZS_Session s, OZS_Watcher w, EntityAI e, InventoryLocation dst, int handle)
    {
        // 2. Then the move, and only then the announcement. Between these two
        //    the item is a server-side object nobody has been told about --
        //    which is what it has been all along.
        //
        // THE WATCHDOG IS TOLD THIS ONE IS LEAVING ON PURPOSE. It exists to
        // shout when an item leaves an authority with nobody having asked --
        // the symptom that started all this. A box a player empties by hand
        // is not that, and left unsaid it reported every ordinary take-out as
        // an error (measured 2026-09-25).
        OZS_Watchdog.Expect(e);
        InventoryLocation src = new InventoryLocation();
        e.GetInventory().GetCurrentInventoryLocation(src);
        bool moved = e.GetInventory().TakeToDst(InventoryMode.LOCAL, src, dst);
        // Everything below this line is unchanged from when it was the tail of
        // `Out`; the only difference is that it may now run a round trip late.
        // AND WHERE IT ACTUALLY IS, NOT WHAT THE CALL SAID.
        //
        // This is the one place in the whole design where an unchecked move
        // costs the ITEM. The record has already let it go one line above, on
        // purpose (§7); so a `TakeToDst` that answers true and leaves the item
        // with no location at all -- which this engine does, and which was
        // caught here by the departure watchdog on 2026-09-25 -- announces a
        // parentless entity to every client and the item is simply gone.
        // Nobody has it, and SQL has forgotten it.
        if (!moved || !OZS_Ops.Sits(e, dst))
        {
            OZ_Log.Error("storage: proxy: box " + s.m_Id + " could not hand #" + handle.ToString() + " to " + w.m_Uid + " (the move said " + moved.ToString() + "); it goes back into the box");
            bool back = OZS_Ops.Put(e, src);
            if (!back)
            {
                // The same cell-by-cell walk the inbound move uses, and for
                // the same reason: the engine's own finder proposes a cell
                // that is not one, and moving into it ends the process.
                InventoryLocation any = new InventoryLocation();
                if (OZS_Ops.FreeSpot(s.m_Auth, e, any))
                    back = OZS_Ops.Put(e, any);
            }
            if (back)
            {
                OZS_Commit.Added(s, e);
            }
            else
            {
                // Nothing can be done from here that would not be worse. The
                // item is left where the engine put it and said about, loudly,
                // so an admin can find it in the history.
                OZ_Log.Error("storage: proxy: box " + s.m_Id + ": " + e.GetType() + " could not be put back either; it is not in the box and not in the record");
            }
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
    // ONE END IN THE BOX AND ONE OUTSIDE, TRADING PLACES.
    //
    // Refused until now, and the reason written here was that a swap is TWO
    // crossings whose safe orders are opposite: out of the box the record
    // must let go FIRST, into the box it must be written LAST, and one
    // operation cannot do both without a moment where the record holds
    // neither item. That reasoning was right about the shape it described --
    // a single crossing that carries both items at once. It is not an
    // argument against the gesture, only against doing it in one crossing.
    //
    // THREE MOVES, AND THE RECORD IS NEVER SHORT. The one already in the box
    // steps aside WITHIN the box, which is not a crossing at all and changes
    // nothing in the record. That frees its cell; the player's item comes in
    // to exactly that cell, in the safe inbound order. Only then does the one
    // that stepped aside go out, in the safe outbound order, to exactly the
    // place the player's item came from. At no point is anything missing: the
    // record holds both for a moment, never neither.
    //
    // EACH ITEM IS AT RISK ALONE, which is what §9 already accepts for a
    // single move, and each step is visible on the screen when it lands. A
    // step that fails stops the rest, and the one before it is put back.
    //
    // Vanilla's own rule is kept: each item takes the other's exact place,
    // not a place the box chose (owner, 2026-09-25).
    static void Across(OZS_Session s, OZS_Watcher w, int handle, int netLow, int netHigh)
    {
        // THE THREE STEPS WRITE ONE LETTER. Posted as three they were three
        // concurrent requests, and the bridge applying the drop of a position
        // before the rewrite of the same position refused the turn -- see
        // OZS_Session.m_Batch. Whatever the steps below did, the letter goes
        // out once, at the end, on every path out of them.
        s.BeginBatch();
        AcrossSteps(s, w, handle, netLow, netHigh);
        s.EndBatch();
    }

    protected static void AcrossSteps(OZS_Session s, OZS_Watcher w, int handle, int netLow, int netHigh)
    {
        EntityAI inside = OZS_Authority.ByHandle(s.m_Auth, handle);
        if (!inside)
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
        EntityAI mine = EntityAI.Cast(GetGame().GetObjectByNetworkId(netLow, netHigh));
        if (!mine)
        {
            w.No(handle, "no such item", s.m_Version);
            return;
        }
        // The same two questions the inbound move asks, for the same reasons:
        // it must be theirs to put away, and it must not be a container the
        // box is already inside.
        if (!Reachable(player, mine))
        {
            w.No(handle, "that is not yours to put away", s.m_Version);
            return;
        }
        if (OZS_Ops.Holds(mine, s.m_Auth))
        {
            w.No(handle, "an item cannot swallow the box", s.m_Version);
            return;
        }

        // BOTH PLACES ARE READ BEFORE ANYTHING MOVES. Each step below makes
        // one of the two readings wrong, and the whole point of the gesture is
        // that each item ends up where the OTHER one started.
        InventoryLocation there = new InventoryLocation();
        if (!inside.GetInventory().GetCurrentInventoryLocation(there))
        {
            w.No(handle, "#STR_OZS_NO_SWAP", s.m_Version);
            return;
        }
        InventoryLocation here = new InventoryLocation();
        if (!mine.GetInventory().GetCurrentInventoryLocation(here))
        {
            w.No(handle, "#STR_OZS_NO_SWAP", s.m_Version);
            return;
        }
        EntityAI host = here.GetParent();
        if (!host)
        {
            w.No(handle, "#STR_OZS_NO_SWAP", s.m_Version);
            return;
        }
        // A weapon slot in the box has no cell to step aside from, and an item
        // in one is not in anybody's way: that pair is a plain move each way.
        if (there.GetType() != InventoryLocationType.CARGO)
        {
            w.No(handle, "#STR_OZS_NO_SWAP", s.m_Version);
            return;
        }

        // 1. THE BOX ITEM STEPS ASIDE, still inside the box.
        int tw;
        int th;
        if (!OZS_Ops.SizeOf(inside, tw, th))
        {
            w.No(handle, "#STR_OZS_NO_SWAP", s.m_Version);
            return;
        }
        InventoryLocation park = new InventoryLocation();
        if (!OZS_Ops.Somewhere(s.m_Auth, inside, there.GetRow(), there.GetCol(), tw, th, park))
        {
            w.No(handle, "#STR_OZS_NO_ROOM", s.m_Version);
            return;
        }
        int parkFlip = 0;
        if (park.GetFlip())
            parkFlip = 1;
        int thereFlip = 0;
        if (there.GetFlip())
            thereFlip = 1;
        OZS_Ops.Move(s, w, handle, 0, InventoryLocationType.CARGO, -1, park.GetRow(), park.GetCol(), parkFlip);
        if (!OZS_Ops.Sits(inside, park))
        {
            // It never left its cell, so there is nothing to undo.
            w.No(handle, "#STR_OZS_NO_SWAP", s.m_Version);
            return;
        }

        // 2. THE PLAYER'S ITEM COMES IN, to the cell just vacated.
        In(s, w, netLow, netHigh, 0, InventoryLocationType.CARGO, -1, there.GetRow(), there.GetCol(), thereFlip);
        if (mine.GetHierarchyParent() != s.m_Auth)
        {
            // The box would not take it, and `In` has already said why. The
            // one that stepped aside goes back to its own cell, which nothing
            // has taken in the meantime.
            OZS_Ops.Move(s, w, handle, 0, InventoryLocationType.CARGO, -1, there.GetRow(), there.GetCol(), thereFlip);
            return;
        }

        // 3. THE BOX ITEM GOES OUT, to exactly where the other one came from.
        //
        // INCLUDING THE HANDS, and the order is what makes that work rather
        // than luck. `Asked` refuses a hands destination while the hands hold
        // something -- and at this point they no longer do, because what they
        // held is the item that went into the box one step above. Reversing
        // the last two steps would break exactly this case, so they are not
        // to be reversed (owner asked whether hands follow the same scheme,
        // 2026-09-25).
        int low = 0;
        int high = 0;
        host.GetNetworkID(low, high);
        int hereFlip = 0;
        if (here.GetFlip())
            hereFlip = 1;
        Out(s, w, handle, low, high, here.GetType(), here.GetSlot(), here.GetRow(), here.GetCol(), hereFlip);
    }

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
        // WHAT IS GOING IN AND WHERE IT CAME FROM. Written before any of the
        // steps below run, because one of them once ended the process without
        // a crash dump and the log was the only witness left: the line that
        // named `OZ_StorageBox_Medium loose on the ground` is what found the
        // box-inside-a-box that the guard below now refuses (2026-09-25).
        string inWhere = "loose on the ground";
        if (e.GetHierarchyParent())
            inWhere = "in " + e.GetHierarchyParent().GetType();
        OZ_Log.Dbg("storage: proxy: taking in " + e.GetType() + ", " + inWhere);
        // THEIRS TO PUT AWAY: carried by them, or lying loose within reach.
        // An item on the ground in front of the player is as much theirs to
        // pick up as one in their pocket (owner, 2026-09-24); what is not is
        // something in somebody else's hands, or in a container across the
        // map that a crafted message could name.
        if (!Reachable(player, e))
        {
            w.No(0, "that is not yours to put away", s.m_Version);
            return;
        }
        if (OZS_Ops.Holds(e, s.m_Auth))
        {
            w.No(0, "an item cannot swallow the box", s.m_Version);
            return;
        }
        // A BOX DOES NOT GO INSIDE A BOX, AND THE ENGINE IS EMPHATIC ABOUT IT.
        //
        // Measured 2026-09-25: a storage box taken off the ground into an open
        // one ends the server process inside `TakeToDst` -- no crash dump, no
        // Windows event, the script log stops mid-operation. The guard above
        // does not catch it: it asks whether the item CONTAINS this box's
        // authority, and a placed box contains nothing -- the authority is a
        // separate, invisible entity. So the anchor of the very box being
        // filled sails through it.
        //
        // Nesting boxes was never wanted either: each one is its own record
        // with its own id, and a box inside a record would be a second truth
        // about the same items.
        if (OZ_StorageBox.Cast(e))
        {
            w.No(0, "#STR_OZS_NO_BOX_IN_BOX", s.m_Version);
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
        else if (!OZS_Ops.FreeSpot(holder, e, dst))
        {
            w.No(0, "#STR_OZS_FULL", s.m_Version);
            return;
        }
        // THE ENGINE MUST AGREE THE PLACE IS A PLACE, or the move that
        // follows ends the server process.
        //
        // Measured three times on 2026-09-25, each with a different item and
        // a different source -- a box off the ground, an ammo pile off the
        // ground, an ammo pile out of a shirt -- and every one of them was
        // heading for the same destination: cell 0,0, which was TAKEN. Every
        // inbound move that SUCCEEDED all night went to a cell the screen had
        // named. The difference is this branch: `FindFirstFreeLocationForNewEntity`
        // answers true and fills in a cell that is not free, and `TakeToDst`
        // into it kills the process -- no crash dump, no Windows event, the
        // script log stops mid-line.
        //
        // `LocationCanAddEntity` is the engine's own gate and it is asked
        // about EVERY branch here, not just this one: a named cell and an
        // attachment slot can be wrong for their own reasons, and none of
        // them is worth a dead server.
        if (!holder.GetInventory().LocationCanAddEntity(dst))
        {
            OZ_Log.Warn("storage: proxy: box " + s.m_Id + " will not take " + e.GetType() + " at " + OZS_Ops.Spot(dst) + "; the engine says that is not a place");
            w.No(0, "#STR_OZS_FULL", s.m_Version);
            return;
        }

        // 1. Off the network first: every client stops knowing this item,
        //    including the one whose player is holding it.
        GetGame().RemoteObjectTreeDelete(e);

        // 2. The move. Now it is in the box and in nobody's save.
        InventoryLocation src = new InventoryLocation();
        e.GetInventory().GetCurrentInventoryLocation(src);
        string inSrc = "type " + src.GetType().ToString();
        if (src.GetParent())
            inSrc = inSrc + " parent " + src.GetParent().GetType();
        else
            inSrc = inSrc + " parent NONE";
        inSrc = inSrc + " valid " + src.IsValid().ToString();
        // WHAT IS BEING ASKED, BEFORE IT IS ASKED. This line is the only
        // witness a move that ends the process leaves behind, and it has
        // earned its keep twice in one night (2026-09-25).
        OZ_Log.Dbg("storage: proxy: moving " + e.GetType() + " in from " + inSrc + " to " + OZS_Ops.Spot(dst));
        bool moved = e.GetInventory().TakeToDst(InventoryMode.LOCAL, src, dst);
        // WHERE IT ACTUALLY IS, NOT WHAT THE CALL SAID. A move that returns
        // true and leaves the item where it was has been seen before in this
        // engine, and here it costs more than a wrong cell: the item is
        // already off the network, so the player watches it vanish while it
        // sits in their inventory, and the commit that follows would write
        // THEM into the box's record as a root (owner, 2026-09-24).
        if (!moved || !OZS_Commit.TopOf(s, e))
        {
            GetGame().RemoteObjectTreeCreate(e);
            OZ_Log.Error("storage: proxy: box " + s.m_Id + " did not take " + e.GetType() + " (the move said " + moved.ToString() + "); it is announced again where it was");
            w.No(0, "the box refused it", s.m_Version);
            return;
        }

        // 3. SQL LAST. A crash between the move and this loses the item; the
        //    other order would duplicate it (§7).
        OZS_Commit.Added(s, e);
        OZS_Authority.Index(s.m_Auth);
        s.Touch();
        // The whole tree, not the top of it: what is inside a container
        // arrives with it (review 2026-09-26, C1).
        s.TellTree(e, w.m_Uid);
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
        // ONTO THE GROUND. Named by its type alone: the position is the
        // server's to compute, at the player's own feet.
        if (lt == InventoryLocationType.GROUND)
            return GameInventory.SetGroundPosByOwner(player, e, dst);
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

    // Carried by this player, or loose on the ground close enough to reach.
    // The distance is the engine's own action reach, so what the box accepts
    // is what the player could have picked up by hand.
    static bool Reachable(PlayerBase player, EntityAI e)
    {
        if (e.GetHierarchyRootPlayer() == player)
            return true;
        if (e.GetHierarchyParent())
            return false;
        return vector.Distance(e.GetPosition(), player.GetPosition()) <= UAMaxDistances.DEFAULT;
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
