// A player leaving the world: their viewer entries go; the box itself is
// left to the auto-close timer (owner 2026-09-16). OnDisconnect runs after
// the logout timer and before the character is saved (CF's measured
// ordering); EEKilled runs before the corpse exists.
modded class PlayerBase
{
    // The identity is gone by the time OnDisconnect runs; the name is kept
    // from the connect for the log line.
    protected string m_OZS_Name;

    override void OnConnect()
    {
        super.OnConnect();
        if (GetIdentity())
            m_OZS_Name = GetIdentity().GetName();
    }

    string OZS_GetName()
    {
        if (GetIdentity())
            return GetIdentity().GetName();
        return m_OZS_Name;
    }

    override void OnDisconnect()
    {
        if (GetGame() && GetGame().IsServer())
        {
            OZS_Controller.Get().OnPlayerLeft(this);
            OZS_Proxies.Get().DropPlayer(GetIdentity(), "disconnected");
        }
        super.OnDisconnect();
    }

    // THE ASKING HALF OF THE PROXY WIRE. A client's message rides on its own
    // player entity -- not on the box, because an RPC addressed to an Object
    // only reaches a side that has that object, and a box four kilometres away
    // is not streamed in (measured 2026-09-24, the first try sent a whole
    // session on the box and the client never heard a word of it).
    //
    // The ANSWERING half cannot ride on anything: a server -> client message
    // arrives with its target null whatever the server addressed it to, so the
    // client listens at DayZGame.Event_OnRPC instead. See OZS_Mirrors.Listen.
    override void OnRPC(PlayerIdentity sender, int rpc_type, ParamsReadContext ctx)
    {
        if (OZS_Proxies.OnWire(sender, rpc_type, ctx))
            return;
        super.OnRPC(sender, rpc_type, ctx);
    }

    // ---- the vanilla inventory screen, over a box ------------------------
    //
    // THE THREE METHODS THE SCREEN MOVES THINGS WITH, AND NOTHING ELSE.
    // Every drag the vanilla inventory makes is one of these three calls on
    // the PLAYER -- all 25 call sites in 5_mission/gui/inventorynew are
    // `player.Predictive...`, none go through the container. That is why the
    // hook is here and not on GameInventory, which is the base of every
    // inventory in the game and would see every crate and car as well.
    //
    // WHAT THEY DO FOR AN ORDINARY DRAG: nothing at all. `OZS_Mirrors.None()`
    // is an integer test, and with no box open it goes straight to super.
    // With a box open, the two ends of the move are compared BY POINTER
    // against the proxy entities this client made. A rearrangement inside the
    // player's own pockets touches neither, and is passed on untouched.
    //
    // WHY THEY HAVE TO BE INTERCEPTED AT ALL: `InventoryMode.PREDICTIVE`, which
    // is what these three use, does not work on a client-local container
    // (measured 2026-09-24: LOCAL true, PREDICTIVE false, every time). The
    // move is applied locally instead, and the server is asked in our own
    // words.
    override bool PredictiveTakeToDst(notnull InventoryLocation src, notnull InventoryLocation dst)
    {
        if (!OZS_Mirrors.None())
        {
            OZS_Mirror m = OZS_Mirrors.Touching(src, dst);
            if (m)
                return m.Drag(src, dst);
        }
        return super.PredictiveTakeToDst(src, dst);
    }

    // THE OTHER FOUR WAYS THE SCREEN PUTS SOMETHING SOMEWHERE. A drop onto a
    // container's header, or onto a slot, does not go through
    // PredictiveTakeToDst at all: SplitItemUtils and the container widgets
    // reach for these instead (splititemutils.c:17, :23,
    // containerwithcargo.c:476). Missing them is why an item could be moved
    // INSIDE the box but not put INTO it -- the call that would have put it
    // there was a PREDICTIVE one nobody had intercepted, and PREDICTIVE does
    // nothing on a local container (owner's report, 2026-09-24).
    override bool PredictiveTakeEntityToTargetCargo(notnull EntityAI target, notnull EntityAI item)
    {
        OZS_Mirror m = OZS_Box(target, item);
        if (m)
            return m.DragTo(item, target, InventoryLocationType.CARGO, -1, -1, -1);
        return super.PredictiveTakeEntityToTargetCargo(target, item);
    }

    override bool PredictiveTakeEntityToTargetCargoEx(notnull CargoBase cargo, notnull EntityAI item, int row, int col)
    {
        if (!OZS_Mirrors.None() && cargo)
        {
            EntityAI holder = cargo.GetCargoOwner();
            OZS_Mirror m = OZS_Box(holder, item);
            if (m)
                return m.DragTo(item, holder, InventoryLocationType.CARGO, -1, row, col);
        }
        return super.PredictiveTakeEntityToTargetCargoEx(cargo, item, row, col);
    }

    override bool PredictiveTakeEntityToTargetAttachmentEx(notnull EntityAI target, notnull EntityAI item, int slot)
    {
        OZS_Mirror m = OZS_Box(target, item);
        if (m)
            return m.DragTo(item, target, InventoryLocationType.ATTACHMENT, slot, -1, -1);
        return super.PredictiveTakeEntityToTargetAttachmentEx(target, item, slot);
    }

    override bool PredictiveTakeEntityToTargetAttachment(notnull EntityAI target, notnull EntityAI item)
    {
        OZS_Mirror m = OZS_Box(target, item);
        if (m)
            return m.DragTo(item, target, InventoryLocationType.ATTACHMENT, -1, -1, -1);
        return super.PredictiveTakeEntityToTargetAttachment(target, item);
    }

    // INTO THE HANDS -- THE ONE THE OWNER FOUND BY DOUBLE-CLICKING.
    // This does not go through an InventoryLocation at all: it runs the hand
    // FSM, which serialises THE ENTITY into ScriptInputUserData -- by network
    // id. A proxy's item has none, so the server read a null and threw
    //
    //   NULL pointer to instance. Variable 'itemSrc'
    //   DayZPlayerInventory::ValidateHandEvent
    //
    // and rolled the client's guess back. That is what "the items went back to
    // their old places and some vanished" was (owner, 2026-09-24): not the
    // box's doing at all, but the hand pipeline refusing an item it cannot
    // name. Routed like any other crossing instead.
    override void PredictiveTakeEntityToHands(EntityAI item)
    {
        if (!OZS_Mirrors.None() && item)
        {
            OZS_Mirror m = OZS_Mirrors.Of(item);
            if (m)
            {
                InventoryLocation src = new InventoryLocation();
                if (!item.GetInventory().GetCurrentInventoryLocation(src))
                    return;
                InventoryLocation hands = new InventoryLocation();
                hands.SetHands(this, item);
                m.Drag(src, hands);
                return;
            }
        }
        super.PredictiveTakeEntityToHands(item);
    }

    // OUT OF THE BOX STRAIGHT ONTO THE GROUND, in one operation. It used to
    // come to the hands instead, which is not what the player asked for
    // (owner, 2026-09-24). The server does the whole thing: the item leaves
    // the box, lands at the player's feet and is announced there.
    override bool PredictiveDropEntity(notnull EntityAI item)
    {
        if (!OZS_Mirrors.None())
        {
            OZS_Mirror m = OZS_Mirrors.Of(item);
            if (m)
                return m.DropOut(item);
        }
        return super.PredictiveDropEntity(item);
    }

    override bool PredictiveTakeEntityAsAttachment(notnull EntityAI item)
    {
        OZS_Mirror m = OZS_Box(this, item);
        if (m)
            return m.DragTo(item, this, InventoryLocationType.ATTACHMENT, -1, -1, -1);
        return super.PredictiveTakeEntityAsAttachment(item);
    }

    override bool PredictiveTakeEntityAsAttachmentEx(notnull EntityAI item, int slot)
    {
        OZS_Mirror m = OZS_Box(this, item);
        if (m)
            return m.DragTo(item, this, InventoryLocationType.ATTACHMENT, slot, -1, -1);
        return super.PredictiveTakeEntityAsAttachmentEx(item, slot);
    }

    // The proxy either end of this move belongs to, or null when neither does.
    // One integer test away from null when no box is open.
    protected OZS_Mirror OZS_Box(EntityAI target, EntityAI item)
    {
        if (OZS_Mirrors.None())
            return null;
        OZS_Mirror m = OZS_Mirrors.Of(target);
        if (m)
            return m;
        return OZS_Mirrors.Of(item);
    }

    override bool PredictiveSwapEntities(notnull EntityAI item1, notnull EntityAI item2)
    {
        if (!OZS_Mirrors.None())
        {
            OZS_Mirror a = OZS_Mirrors.Of(item1);
            OZS_Mirror b = OZS_Mirrors.Of(item2);
            if (a && a == b)
                return a.DragSwap(item1, item2);
            if (a || b)
                return CrossBoundarySwap(item1, item2);
        }
        return super.PredictiveSwapEntities(item1, item2);
    }

    override bool PredictiveForceSwapEntities(notnull EntityAI item1, notnull EntityAI item2, notnull InventoryLocation item2_dst)
    {
        if (!OZS_Mirrors.None())
        {
            OZS_Mirror a = OZS_Mirrors.Of(item1);
            OZS_Mirror b = OZS_Mirrors.Of(item2);
            if (a && a == b)
                return a.DragSwap(item1, item2);
            if (a || b)
                return CrossBoundarySwap(item1, item2);
        }
        return super.PredictiveForceSwapEntities(item1, item2, item2_dst);
    }

    // ONE END IN THE BOX AND ONE OUTSIDE: REFUSED, AND HERE IS THE REAL REASON.
    //
    // A swap across the boundary is TWO crossings, and the two want opposite
    // orders. Design §7 puts the step that could duplicate last, which means:
    //
    //   out of the box   delete from SQL FIRST, then move and announce
    //   into the box     move first, write to SQL LAST
    //
    // One operation cannot satisfy both. Whichever order is chosen, one half
    // runs in its unsafe order -- and in between, the record is missing BOTH
    // items at once. A plain move risks one item for one turn, which §9
    // accepts; a swap would risk two, and the player could not tell afterwards
    // which half had gone through.
    //
    // There is also no undo. Each crossing is its own letter to the bridge; if
    // the second is refused -- the box is full, a mod's rule says no, the cell
    // is taken -- the first has already been written and putting it back is a
    // THIRD operation that can fail in turn.
    //
    // Two ordinary moves do the same thing with one item at risk at a time and
    // a visible result after each. If this is ever wanted as one gesture, it
    // belongs on the client as two operations in sequence, not as one here.
    protected bool CrossBoundarySwap(EntityAI item1, EntityAI item2)
    {
        // A refusal the player cannot see is worse than a refusal: this is the
        // only trace of why nothing happened.
        OZ_Log.Warn("storage: proxy: a swap between " + item1.GetType() + " and " + item2.GetType() + " crosses the box's boundary and is refused; move them one at a time");
        return false;
    }

    override void EEKilled(Object killer)
    {
        if (GetGame() && GetGame().IsServer())
        {
            OZS_Controller.Get().OnPlayerLeft(this);
            OZS_Proxies.Get().DropPlayer(GetIdentity(), "died");
        }
        super.EEKilled(killer);
    }
}
