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

    override bool PredictiveSwapEntities(notnull EntityAI item1, notnull EntityAI item2)
    {
        if (!OZS_Mirrors.None())
        {
            OZS_Mirror a = OZS_Mirrors.Of(item1);
            OZS_Mirror b = OZS_Mirrors.Of(item2);
            if (a && a == b)
                return a.DragSwap(item1, item2);
            if (a || b)
            {
                // One end in the box and one outside: two moves, not a swap.
                // The player can do it in two, and a half-done swap across the
                // boundary is exactly the kind of thing that loses an item.
                return false;
            }
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
                return false;
        }
        return super.PredictiveForceSwapEntities(item1, item2, item2_dst);
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
