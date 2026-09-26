// A player leaving the world: every proxy session they were in lets them
// go. OnDisconnect runs after the logout timer and before the character is
// saved (CF's measured ordering); EEKilled runs before the corpse exists.
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
            OZS_Proxies.Get().DropPlayer(GetIdentity(), "disconnected");
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
        OZ_Log.Dbg("storage: client: vanilla asks MOVE of [" + OZS_Say(dst.GetItem()) + "] to " + dst.GetRow().ToString() + "," + dst.GetCol().ToString() + " flip " + dst.GetFlip().ToString());
        if (!OZS_Mirrors.None())
        {
            OZS_Mirror m = OZS_Mirrors.Touching(src, dst);
            if (m)
            {
                OZS_Mirrors.s_Via = "TakeToDst";
                return m.Drag(src, dst);
            }
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
        {
            OZS_Mirrors.s_Via = "ToTargetCargo";
            return m.DragTo(item, target, InventoryLocationType.CARGO, -1, -1, -1);
        }
        return super.PredictiveTakeEntityToTargetCargo(target, item);
    }

    override bool PredictiveTakeEntityToTargetCargoEx(notnull CargoBase cargo, notnull EntityAI item, int row, int col)
    {
        if (!OZS_Mirrors.None() && cargo)
        {
            EntityAI holder = cargo.GetCargoOwner();
            OZS_Mirror m = OZS_Box(holder, item);
            if (m)
            {
                OZS_Mirrors.s_Via = "ToTargetCargoEx";
                return m.DragTo(item, holder, InventoryLocationType.CARGO, -1, row, col);
            }
        }
        return super.PredictiveTakeEntityToTargetCargoEx(cargo, item, row, col);
    }

    override bool PredictiveTakeEntityToTargetAttachmentEx(notnull EntityAI target, notnull EntityAI item, int slot)
    {
        OZS_Mirror m = OZS_Box(target, item);
        if (m)
        {
            OZS_Mirrors.s_Via = "ToTargetAttachmentEx";
            return m.DragTo(item, target, InventoryLocationType.ATTACHMENT, slot, -1, -1);
        }
        return super.PredictiveTakeEntityToTargetAttachmentEx(target, item, slot);
    }

    override bool PredictiveTakeEntityToTargetAttachment(notnull EntityAI target, notnull EntityAI item)
    {
        OZS_Mirror m = OZS_Box(target, item);
        if (m)
        {
            OZS_Mirrors.s_Via = "ToTargetAttachment";
            // THE SENTINEL, NOT A NEGATIVE NUMBER. Slot ids are negative
            // hashes, so -1 is only "no slot" by accident and the receiving
            // side has no way to tell one from the other.
            return m.DragTo(item, target, InventoryLocationType.ATTACHMENT, InventorySlots.INVALID, -1, -1);
        }
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
                OZS_Mirrors.s_Via = "ToHands";
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
            {
                OZS_Mirrors.s_Via = "DropEntity";
                return m.DropOut(item);
            }
        }
        return super.PredictiveDropEntity(item);
    }

    override bool PredictiveTakeEntityAsAttachment(notnull EntityAI item)
    {
        OZS_Mirror m = OZS_Box(this, item);
        if (m)
        {
            OZS_Mirrors.s_Via = "AsAttachment";
            return m.DragTo(item, this, InventoryLocationType.ATTACHMENT, InventorySlots.INVALID, -1, -1);
        }
        return super.PredictiveTakeEntityAsAttachment(item);
    }

    override bool PredictiveTakeEntityAsAttachmentEx(notnull EntityAI item, int slot)
    {
        OZS_Mirror m = OZS_Box(this, item);
        if (m)
        {
            OZS_Mirrors.s_Via = "AsAttachmentEx";
            return m.DragTo(item, this, InventoryLocationType.ATTACHMENT, slot, -1, -1);
        }
        return super.PredictiveTakeEntityAsAttachmentEx(item, slot);
    }

    // "INTO MY INVENTORY, WHEREVER IT FITS" -- the screen's quick take
    // (cargocontainer.c:829, attachments.c:279) and what Alt+click on a box
    // item asks (owner, 2026-09-26). Vanilla's PREDICTIVE call would look
    // for the place itself and fail on a local container; the proxy proposes
    // one the same way and the server checks it. See OZS_Mirror.TakeInto.
    override bool PredictiveTakeEntityToInventory(FindInventoryLocationType flags, notnull EntityAI item)
    {
        if (!OZS_Mirrors.None())
        {
            OZS_Mirror m = OZS_Mirrors.Of(item);
            if (m)
            {
                OZS_Mirrors.s_Via = "ToInventory";
                return m.TakeInto(item, this, flags);
            }
        }
        return super.PredictiveTakeEntityToInventory(flags, item);
    }

    override bool PredictiveTakeEntityToTargetInventory(notnull EntityAI target, FindInventoryLocationType flags, notnull EntityAI item)
    {
        OZS_Mirror m = OZS_Box(target, item);
        if (m)
        {
            OZS_Mirrors.s_Via = "ToTargetInventory";
            if (OZS_Mirrors.Of(item) == m)
                return m.TakeInto(item, target, flags);
            return m.DragTo(item, target, InventoryLocationType.CARGO, -1, -1, -1);
        }
        return super.PredictiveTakeEntityToTargetInventory(target, flags, item);
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

    // WHAT THE CLIENT ITSELF BELIEVES ABOUT AN ITEM, written where the client
    // can be read. Three gaps have been guessed at and missed; this prints the
    // numbers vanilla is actually deciding on (owner, 2026-09-26).
    protected static string OZS_Say(EntityAI e)
    {
        if (!e)
            return "nothing";
        string s = e.GetType();
        InventoryLocation il = new InventoryLocation();
        if (e.GetInventory() && e.GetInventory().GetCurrentInventoryLocation(il))
        {
            s = s + " lt " + il.GetType().ToString() + " at " + il.GetRow().ToString() + "," + il.GetCol().ToString();
            s = s + " locFlip " + il.GetFlip().ToString();
        }
        if (e.GetInventory())
            s = s + " itemFlip " + e.GetInventory().GetFlipCargo().ToString();
        int cw;
        int ch;
        GetGame().GetInventoryItemSize(InventoryItem.Cast(e), cw, ch);
        s = s + " cfg " + cw.ToString() + "x" + ch.ToString();
        return s;
    }

    override bool PredictiveSwapEntities(notnull EntityAI item1, notnull EntityAI item2)
    {
        OZ_Log.Dbg("storage: client: vanilla asks SWAP of [" + OZS_Say(item1) + "] with [" + OZS_Say(item2) + "]");
        if (!OZS_Mirrors.None())
        {
            OZS_Mirror a = OZS_Mirrors.Of(item1);
            OZS_Mirror b = OZS_Mirrors.Of(item2);
            // Named only once the pair is known to be ours: set before that,
            // it labelled the next operation of ours with the route of a
            // vanilla swap this mod never touched.
            if (a && a == b)
            {
                OZS_Mirrors.s_Via = "SwapEntities";
                return a.DragSwap(item1, item2);
            }
            if (a || b)
            {
                OZS_Mirrors.s_Via = "SwapEntities";
                return CrossBoundarySwap(item1, item2);
            }
        }
        return super.PredictiveSwapEntities(item1, item2);
    }

    override bool PredictiveForceSwapEntities(notnull EntityAI item1, notnull EntityAI item2, notnull InventoryLocation item2_dst)
    {
        OZ_Log.Dbg("storage: client: vanilla asks FORCE SWAP of [" + OZS_Say(item1) + "] with [" + OZS_Say(item2) + "], displaced to " + item2_dst.GetRow().ToString() + "," + item2_dst.GetCol().ToString() + " flip " + item2_dst.GetFlip().ToString());
        if (!OZS_Mirrors.None())
        {
            OZS_Mirror a = OZS_Mirrors.Of(item1);
            OZS_Mirror b = OZS_Mirrors.Of(item2);
            if (a && a == b)
            {
                // THE THIRD ARGUMENT IS THE POINT OF THIS METHOD. The screen
                // has already worked out where the displaced item goes, and it
                // used to be dropped on the floor here, leaving the server to
                // choose -- which it could only do from the two root cells.
                OZS_Mirrors.s_Via = "ForceSwapEntities";
                return a.DragForceSwap(item1, item2, item2_dst);
            }
            if (a || b)
            {
                OZS_Mirrors.s_Via = "ForceSwapEntities";
                return CrossBoundarySwap(item1, item2);
            }
        }
        return super.PredictiveForceSwapEntities(item1, item2, item2_dst);
    }

    // ONE END IN THE BOX AND ONE OUTSIDE.
    //
    // Refused for a while, and the note that stood here said why: a swap is
    // two crossings whose safe orders are opposite, so one crossing carrying
    // both items would leave a moment with neither in the record. That is
    // still true of one crossing -- and it is not how this is done. The
    // server takes three ordinary steps instead, never short of either item;
    // see `OZS_Boundary.Across`.
    //
    // This side only has to say WHICH is which. The item in the box is named
    // by its handle, the player's own by its network id, and nothing else
    // travels: both places are read on the server, where they cannot be stale
    // by the time they are used.
    protected bool CrossBoundarySwap(EntityAI item1, EntityAI item2)
    {
        OZS_Mirror a = OZS_Mirrors.Of(item1);
        OZS_Mirror b = OZS_Mirrors.Of(item2);
        // TWO DIFFERENT BOXES IS NOT THIS. Each half would be a crossing of
        // its own box's boundary, with two records to keep and two orderings
        // in one gesture; two moves do it with one item at risk at a time.
        if (a && b)
        {
            OZ_Log.Warn("storage: proxy: " + item1.GetType() + " and " + item2.GetType() + " are in two different boxes; move them one at a time");
            return false;
        }
        OZS_Mirror box = a;
        EntityAI inside = item1;
        EntityAI mine = item2;
        if (!box)
        {
            box = b;
            inside = item2;
            mine = item1;
        }
        if (!box)
            return false;
        int handle = box.HandleOf(inside);
        if (handle == 0)
            return false;
        box.Across(mine, handle);
        return true;
    }

    override void EEKilled(Object killer)
    {
        if (GetGame() && GetGame().IsServer())
            OZS_Proxies.Get().DropPlayer(GetIdentity(), "died");
        super.EEKilled(killer);
    }
}
