// What a player's screen can ask of a box, and what the server does about it.
// Design: docs/specs/2026-09-24-storage-proxy-inventory-design.md §5, §6, §7.
//
// Every operation here is ATOMIC and SERVER-SIDE. The proxy has already shown
// the player its own guess; this is the word that counts, and it is told to
// every proxy of the box afterwards -- including the one that guessed, because
// a guess that happened to be right and the authority's word must not be two
// different things (§8.4).
//
// TWO RULES DECIDE THE ORDER OF EVERYTHING BELOW:
//
//   1. The proxy never decides last and never forbids (§5). A refusal here is
//      the only refusal there is.
//   2. The step that could create a duplicate goes LAST (§7). An item leaves a
//      place that is saved and arrives in one that is not; if the server dies
//      between the move and the write, the item is lost. The other order would
//      duplicate it. Losing is visible and an admin can fix it; a duplicate is
//      invisible and eats the economy.
class OZS_Ops
{
    static void Run(OZS_Session s, OZS_Watcher w, int op, int handle, int other, int netLow, int netHigh, int lt, int slot, int row, int col, int flip)
    {
        if (op == OZS_Const.OP_MOVE)
        {
            Move(s, w, handle, other, lt, slot, row, col, flip);
            return;
        }
        if (op == OZS_Const.OP_COMBINE)
        {
            Combine(s, w, handle, other);
            return;
        }
        if (op == OZS_Const.OP_SWAP)
        {
            Swap(s, w, handle, other);
            return;
        }
        if (op == OZS_Const.OP_OUT)
        {
            OZS_Boundary.Out(s, w, handle, netLow, netHigh, lt, slot, row, col, flip);
            return;
        }
        if (op == OZS_Const.OP_IN)
        {
            // Coming IN, the item is the player's own and announced, so it is
            // named by its NETWORK id; `other` is the container inside the box
            // it is going into, 0 being the box itself.
            OZS_Boundary.In(s, w, netLow, netHigh, other, lt, slot, row, col, flip);
            return;
        }
        w.No(handle, "unknown operation " + op.ToString(), s.m_Version);
    }

    // ---- inside the box --------------------------------------------------

    // A cell to a cell, or a cell to a slot, or into a container that is in
    // the box. `other` is the handle of the destination's parent: 0 is the box
    // itself.
    static void Move(OZS_Session s, OZS_Watcher w, int handle, int other, int lt, int slot, int row, int col, int flip)
    {
        EntityAI e = OZS_Authority.ByHandle(s.m_Auth, handle);
        if (!e)
        {
            w.No(handle, "no such item", s.m_Version);
            return;
        }
        EntityAI parent = s.m_Auth;
        if (other != 0)
        {
            parent = OZS_Authority.ByHandle(s.m_Auth, other);
            if (!parent)
            {
                w.No(handle, "no such container", s.m_Version);
                return;
            }
        }
        // The screen computes its own destination and so does this: a cell
        // outside the grid once made a proxy accept a move the authority
        // refused, and both calls returned true (measured 2026-09-24).
        if (!Fits(parent, lt, slot, row, col))
        {
            w.No(handle, "that is not a place in this box", s.m_Version);
            return;
        }
        // An item cannot be put inside itself, at any depth.
        if (parent == e || Holds(e, parent))
        {
            w.No(handle, "an item cannot go inside itself", s.m_Version);
            return;
        }
        // Which root it belonged to BEFORE the move: a move into or out of a
        // container inside the box changes two records, and afterwards the old
        // one is no longer reachable from the entity.
        int wasRoot = OZS_Commit.RootOf(s, e);
        InventoryLocation src = new InventoryLocation();
        e.GetInventory().GetCurrentInventoryLocation(src);
        InventoryLocation dst = new InventoryLocation();
        if (lt == InventoryLocationType.ATTACHMENT)
            dst.SetAttachment(parent, e, slot);
        else
            dst.SetCargo(parent, e, 0, row, col, flip == 1);
        if (!e.GetInventory().TakeToDst(InventoryMode.LOCAL, src, dst))
        {
            w.No(handle, "refused", s.m_Version);
            return;
        }
        s.Touch();
        OZS_Commit.Moved(s, e, wasRoot);
        // Where it ACTUALLY went, not where it was asked to go: the row is
        // read back off the entity.
        s.TellMoved(e);
    }

    // Two stacks into one. The engine decides, with every mod's CanBeCombined
    // in force -- which is the whole reason the authority is a real container
    // and not a table (§10, "what is NOT lost").
    static void Combine(OZS_Session s, OZS_Watcher w, int handle, int other)
    {
        ItemBase into = ItemBase.Cast(OZS_Authority.ByHandle(s.m_Auth, handle));
        ItemBase from = ItemBase.Cast(OZS_Authority.ByHandle(s.m_Auth, other));
        if (!into || !from || into == from)
        {
            w.No(handle, "no such pair", s.m_Version);
            return;
        }
        if (!into.CanBeCombined(from, false))
        {
            w.No(handle, "these do not stack", s.m_Version);
            return;
        }
        int fromRoot = OZS_Commit.RootOf(s, from);
        bool fromWasRoot = OZS_Commit.TopOf(s, from) == from;
        int goneHandle = OZS_Authority.Handle(s.m_Auth, from);
        into.CombineItems(from, true);
        s.Touch();
        // CombineItems moves quantity; it does not remove an emptied item.
        if (from.GetQuantity() <= 0)
        {
            GetGame().ObjectDelete(from);
            OZS_Commit.Left(s, fromRoot, fromWasRoot);
            s.TellGone(goneHandle);
        }
        else
        {
            OZS_Commit.Quantity(s, from);
            s.TellQuantity(from);
        }
        OZS_Commit.Quantity(s, into);
        s.TellQuantity(into);
    }

    // Two items exchange places. One call, so the box is never in a state
    // where both sit in the same cell.
    static void Swap(OZS_Session s, OZS_Watcher w, int handle, int other)
    {
        EntityAI a = OZS_Authority.ByHandle(s.m_Auth, handle);
        EntityAI b = OZS_Authority.ByHandle(s.m_Auth, other);
        if (!a || !b || a == b)
        {
            w.No(handle, "no such pair", s.m_Version);
            return;
        }
        InventoryLocation srcA = new InventoryLocation();
        InventoryLocation srcB = new InventoryLocation();
        a.GetInventory().GetCurrentInventoryLocation(srcA);
        b.GetInventory().GetCurrentInventoryLocation(srcB);
        InventoryLocation dstA = new InventoryLocation();
        InventoryLocation dstB = new InventoryLocation();
        dstA.Copy(srcB);
        dstA.SetItem(a);
        dstB.Copy(srcA);
        dstB.SetItem(b);
        if (!GameInventory.LocationSwap(srcA, srcB, dstA, dstB))
        {
            w.No(handle, "these two cannot change places", s.m_Version);
            return;
        }
        s.Touch();
        OZS_Commit.Moved(s, a, -2);
        OZS_Commit.Moved(s, b, -2);
        s.TellMoved(a);
        s.TellMoved(b);
    }

    // ---- the questions both halves ask -----------------------------------

    // Is this a place at all? Asked of the engine's own cargo and slot
    // declarations, never guessed from the config.
    static bool Fits(EntityAI parent, int lt, int slot, int row, int col)
    {
        if (!parent || !parent.GetInventory())
            return false;
        if (lt == InventoryLocationType.ATTACHMENT)
            return parent.GetInventory().HasInventorySlot(slot);
        CargoBase cargo = parent.GetInventory().GetCargo();
        if (!cargo)
            return false;
        if (row < 0 || col < 0)
            return false;
        if (row >= cargo.GetHeight() || col >= cargo.GetWidth())
            return false;
        return true;
    }

    // Does `holder` already contain `maybe`, at any depth? The engine refuses
    // a cycle too, but not before the move has been half made.
    static bool Holds(EntityAI holder, EntityAI maybe)
    {
        EntityAI up = maybe;
        while (up)
        {
            if (up == holder)
                return true;
            up = up.GetHierarchyParent();
        }
        return false;
    }
}
