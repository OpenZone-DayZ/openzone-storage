// The client half of the proxy design: the PROXY itself -- a real container in
// this one client's memory, holding a copy of what the authoritative container
// on the server holds.
// Design: docs/specs/2026-09-24-storage-proxy-inventory-design.md §2.
//
// It is a real entity on purpose. A table of records could not answer "does
// this fit here", and the screen needs a live item to draw an icon from. It is
// created with ECE_LOCAL, so it exists nowhere but in this process: no other
// client has heard of it, the server has not either, and nothing about it is
// ever saved.
//
// WHERE IT LIVES. Far under the terrain, because a container standing next to
// the player would show up in the vanilla vicinity panel and be rendered in the
// world. Nothing about a proxy needs a sensible position: nobody walks up to it
// and the screen draws its contents from the entities, not from the scene.
//
// WHAT IT IS NOT. It is never the last word. It may be more permissive than the
// authority but never stricter (§5), and where the two disagree -- which has
// been measured, on a negative cell both returned true and did different things
// -- the authority wins and the proxy is rebuilt.
class OZS_Mirrors
{
    protected static ref OZS_Mirrors s_Inst;
    protected ref array<ref OZS_Mirror> m_Mirrors;
    // A box was asked for and the panel should open when it is whole.
    protected bool m_Waiting;
    // The anchor the last ask went through. The client is never told a box's
    // id, so this is the only thing that pairs a proxy with the box in the
    // world it stands for.
    protected Object m_Asked;

    static OZS_Mirrors Get()
    {
        if (!s_Inst)
            s_Inst = new OZS_Mirrors();
        return s_Inst;
    }

    void OZS_Mirrors()
    {
        m_Mirrors = new array<ref OZS_Mirror>();
    }

    void Asking(Object anchor)
    {
        m_Asked = anchor;
    }

    // The box in the world a proxy stands for, paired at the moment of asking.
    Object Asked()
    {
        return m_Asked;
    }

    // True when this object in the world is the anchor of a proxy that is up.
    bool StandsInFor(Object o)
    {
        if (!o || m_Mirrors.Count() == 0)
            return false;
        for (int i = 0; i < m_Mirrors.Count(); i++)
        {
            if (m_Mirrors.Get(i).m_Anchor == o)
                return true;
        }
        return false;
    }

    OZS_Mirror Find(string id)
    {
        for (int i = 0; i < m_Mirrors.Count(); i++)
        {
            if (m_Mirrors.Get(i).m_Id == id)
                return m_Mirrors.Get(i);
        }
        return null;
    }

    array<ref OZS_Mirror> All()
    {
        return m_Mirrors;
    }

    // The one a stream has just built. A screen asks for a box before it knows
    // the box's id and binds to whatever arrives; only one screen is open at a
    // time, so "the newest" is the one that was asked for.
    OZS_Mirror Newest()
    {
        if (m_Mirrors.Count() == 0)
            return null;
        return m_Mirrors.Get(m_Mirrors.Count() - 1);
    }

    void Keep(OZS_Mirror m)
    {
        m_Mirrors.Insert(m);
    }

    void Drop(string id)
    {
        for (int i = m_Mirrors.Count() - 1; i >= 0; i--)
        {
            if (m_Mirrors.Get(i).m_Id != id)
                continue;
            m_Mirrors.Get(i).Destroy();
            m_Mirrors.RemoveOrdered(i);
        }
    }

    void DropAll()
    {
        for (int i = m_Mirrors.Count() - 1; i >= 0; i--)
            m_Mirrors.Get(i).Destroy();
        m_Mirrors.Clear();
    }

    // The ordinary inventory, opened when the box has arrived rather than
    // when it was asked for.
    void ShowWhenReady()
    {
        m_Waiting = true;
    }

    void Update(float timeslice)
    {
        if (!m_Waiting)
            return;
        OZS_Mirror m = Newest();
        if (!m || !m.m_Whole)
            return;
        m_Waiting = false;
#ifndef NO_GUI
        if (GetGame().GetMission())
            GetGame().GetMission().ShowInventory();
#endif
    }

    string Status()
    {
        string s = "mirrors=" + m_Mirrors.Count();
        for (int i = 0; i < m_Mirrors.Count(); i++)
            s = s + " | " + m_Mirrors.Get(i).Status();
        return s;
    }

    // ---- "is this about a box at all?" -----------------------------------
    //
    // THE FIRST QUESTION OF EVERY HOOK, AND IT MUST BE CHEAP AND EXACT.
    // The hooks sit on the player's own move methods, which run on every drag
    // the player makes anywhere -- their pockets, their backpack, a car, some
    // other mod's crate. None of that is ours. So:
    //
    //   1. no proxy open at all -> one integer test and straight to super;
    //   2. a proxy open -> compare the HIERARCHY ROOT of each end against the
    //      proxy ENTITIES we made ourselves, by pointer. Not by class, not by
    //      position: an ordinary crate of the same class must never match.
    //
    // Everything that is not a box goes to super untouched.
    static bool None()
    {
        if (!s_Inst)
            return true;
        return s_Inst.m_Mirrors.Count() == 0;
    }

    // The proxy this entity belongs to, or null. An item nested three
    // containers deep inside a proxy belongs to it; an item in the player's
    // backpack belongs to the player and answers null.
    static OZS_Mirror Of(EntityAI e)
    {
        if (None() || !e)
            return null;
        EntityAI root = e;
        while (root.GetHierarchyParent())
            root = root.GetHierarchyParent();
        array<ref OZS_Mirror> all = s_Inst.m_Mirrors;
        for (int i = 0; i < all.Count(); i++)
        {
            if (all.Get(i).m_Box == root)
                return all.Get(i);
        }
        return null;
    }

    // The proxy an inventory location is in: its parent's root. A location
    // with no parent -- the ground, the hands of nobody -- is never ours.
    static OZS_Mirror At(InventoryLocation il)
    {
        if (None() || !il)
            return null;
        return Of(il.GetParent());
    }

    // The one proxy a move touches, or null when it touches none. A move
    // between two different proxies is not a thing: a player has one box open.
    static OZS_Mirror Touching(InventoryLocation src, InventoryLocation dst)
    {
        OZS_Mirror from = At(src);
        if (from)
            return from;
        return At(dst);
    }

    // ---- the wire, client side -------------------------------------------

    // THE CLIENT LISTENS AT THE GAME, NOT AT ITS PLAYER (measured 2026-09-24).
    // A ScriptRPC sent server -> one client arrives with its TARGET NULL, even
    // when the server addressed it to that player's own entity and logged the
    // entity's network id doing so. With a null target the engine never calls
    // Object.OnRPC at all, so a handler on PlayerBase is never reached.
    // DayZGame.Event_OnRPC fires for every message, target or no target, and
    // every message of ours carries the box id in its body anyway.
    //
    // (The other direction is fine: client -> server keeps the target, which
    // is why the asking half still rides on the player.)
    static void Listen()
    {
        if (!GetGame() || !GetGame().IsClient())
            return;
        if (s_Ears)
            return;
        s_Ears = new OZS_MirrorEars();
        DayZGame.Event_OnRPC.Insert(s_Ears.OnGameRPC);
    }

    protected static ref OZS_MirrorEars s_Ears;

    // Returns true when the message was ours.
    static bool OnWire(int type, ParamsReadContext ctx)
    {
        if (!GetGame() || !GetGame().IsClient())
            return false;
        if (type == OZS_Const.RPC_PX_BEGIN)
        {
            string id;
            string cls;
            int total;
            int version;
            if (!ctx.Read(id) || !ctx.Read(cls) || !ctx.Read(total) || !ctx.Read(version))
                return true;
            OZS_Mirrors.Get().Drop(id);
            // `ref` on the local: a `new` held by a plain local can be
            // collected before it reaches the array that owns it.
            ref OZS_Mirror m = new OZS_Mirror(id, cls, total, version);
            m.m_Anchor = OZS_Mirrors.Get().Asked();
            OZS_Mirrors.Get().Keep(m);
            m.Begin();
            return true;
        }
        if (type == OZS_Const.RPC_PX_ROWS)
        {
            string rowsId;
            int n;
            if (!ctx.Read(rowsId) || !ctx.Read(n))
                return true;
            OZS_Mirror into = OZS_Mirrors.Get().Find(rowsId);
            for (int i = 0; i < n; i++)
            {
                OZS_Row r = new OZS_Row();
                if (!OZS_Wire.ReadRow(ctx, r))
                    return true;
                if (into)
                    into.Add(r);
            }
            return true;
        }
        if (type == OZS_Const.RPC_PX_END)
        {
            string endId;
            int endTotal;
            int endVersion;
            if (!ctx.Read(endId) || !ctx.Read(endTotal) || !ctx.Read(endVersion))
                return true;
            OZS_Mirror done = OZS_Mirrors.Get().Find(endId);
            if (done)
                done.End(endTotal, endVersion);
            return true;
        }
        if (type == OZS_Const.RPC_PX_CHANGE)
        {
            string chId;
            int what;
            int chVersion;
            OZS_Row cr = new OZS_Row();
            if (!ctx.Read(chId) || !ctx.Read(what) || !ctx.Read(chVersion))
                return true;
            if (!OZS_Wire.ReadRow(ctx, cr))
                return true;
            OZS_Mirror changed = OZS_Mirrors.Get().Find(chId);
            if (changed)
                changed.Change(what, cr, chVersion);
            return true;
        }
        if (type == OZS_Const.RPC_PX_NO)
        {
            string noId;
            int noHandle;
            int noVersion;
            string noWhy;
            if (!ctx.Read(noId) || !ctx.Read(noHandle) || !ctx.Read(noVersion) || !ctx.Read(noWhy))
                return true;
            OZS_Mirror said = OZS_Mirrors.Get().Find(noId);
            if (said)
                said.Refused(noHandle, noVersion, noWhy);
            return true;
        }
        return false;
    }
}

// The client's ear on DayZGame.Event_OnRPC. A ScriptInvoker wants a method of
// an object, so this is the object.
class OZS_MirrorEars
{
    void OnGameRPC(PlayerIdentity sender, Object target, int rpc_type, ParamsReadContext ctx)
    {
        OZS_Mirrors.OnWire(rpc_type, ctx);
    }
}

// ---------------------------------------------------------------------------

class OZS_Mirror
{
    string m_Id;
    string m_Class;
    // The box in the world this stands for. The client is never told a box's
    // id, so the pairing is made when the player asks through it.
    Object m_Anchor;

    EntityAI m_Box;
    ref array<int> m_Handles;
    ref array<EntityAI> m_Items;
    int m_Version;
    int m_Expected;
    int m_Missed;
    bool m_Whole;
    float m_Started;
    float m_Spent;
    // Wall time from the first chunk to the end: the stream is paced by the
    // server, so the work and the wait are two different numbers and both
    // matter.
    float m_Wall;
    // The screen hangs on this: it is told when the contents changed rather
    // than polling a thousand cells every frame.
    ref ScriptInvoker m_OnChanged;

    void OZS_Mirror(string id, string cls, int total, int version)
    {
        m_Id = id;
        m_Class = cls;
        m_Expected = total;
        m_Version = version;
        m_Handles = new array<int>();
        m_Items = new array<EntityAI>();
        m_OnChanged = new ScriptInvoker();
        m_Whole = false;
        m_Missed = 0;
    }

    ScriptInvoker OnChanged()
    {
        return m_OnChanged;
    }

    void Begin()
    {
        m_Started = GetGame().GetTickTime();
        m_Spent = 0;
        Man me = GetGame().GetPlayer();
        vector at = Vector(0, 0, 0);
        if (me)
            at = me.GetPosition();
        // BESIDE THE PLAYER, ON PURPOSE (owner, 2026-09-24). The vanilla
        // inventory finds containers by proximity, and a client-local
        // container standing next to the player is listed there like any
        // other -- with its grid, its slots, its stacking and its scrolling,
        // none of which we then have to write. The model is hidden: the player
        // should see the box they are standing at, not a second one at their
        // feet.
        at[0] = at[0] + OZS_Const.PROXY_ASIDE;
        Object made = GetGame().CreateObjectEx(m_Class, at, ECE_LOCAL | ECE_NOLIFETIME);
        m_Box = EntityAI.Cast(made);
        if (!m_Box)
        {
            OZ_Log.Error("storage: proxy: this client cannot create " + m_Class + " for box " + m_Id);
            return;
        }
        m_Box.SetInvisible(true);
        OZ_StorageBox asBox = OZ_StorageBox.Cast(m_Box);
        if (asBox)
        {
            // A proxy answers with the id of the box it shows, and its gates
            // must not refuse the items this fills it with.
            asBox.OZS_StandForId(m_Id);
            asBox.OZS_SetState(OZS_Const.STATE_OPEN);
            asBox.OZS_SetRestoring(true);
        }
    }

    // One row into the container. Parents always come before their children,
    // so the parent of a row is already built when the row arrives.
    void Add(OZS_Row r)
    {
        if (!m_Box)
            return;
        float t0 = GetGame().GetTickTime();
        EntityAI parent = m_Box;
        if (r.parent != 0)
        {
            parent = ByHandle(r.parent);
            if (!parent)
            {
                m_Missed++;
                return;
            }
        }
        EntityAI made = null;
        if (r.lt == InventoryLocationType.ATTACHMENT)
        {
            made = parent.GetInventory().CreateAttachmentEx(r.cls, r.slot);
        }
        else
        {
            bool flip = r.flip == 1;
            if (r.row >= 0)
                made = parent.GetInventory().CreateEntityInCargoEx(r.cls, 0, r.row, r.col, flip);
            if (!made)
                made = parent.GetInventory().CreateEntityInCargo(r.cls);
        }
        if (!made)
        {
            m_Missed++;
            m_Spent = m_Spent + (GetGame().GetTickTime() - t0);
            return;
        }
        Dress(made, r);
        m_Handles.Insert(r.handle);
        m_Items.Insert(made);
        m_Spent = m_Spent + (GetGame().GetTickTime() - t0);
    }

    // What of an item's state the proxy is given. Not the blob: it would buy
    // a correct field nobody on a client reads and pay with another mod's
    // OnStoreLoad running where it was never meant to (§10.2).
    static void Dress(EntityAI e, OZS_Row r)
    {
        if (r.health >= 0)
            e.SetHealth01("", "", r.health / 100.0);
        ItemBase item = ItemBase.Cast(e);
        if (item && r.qty >= 0)
            item.SetQuantity(r.qty, false, false);
    }

    void End(int total, int version)
    {
        m_Whole = true;
        m_Version = version;
        m_Expected = total;
        m_Wall = GetGame().GetTickTime() - m_Started;
        float wall = m_Wall * 1000;
        OZ_StorageBox asBox = OZ_StorageBox.Cast(m_Box);
        if (asBox)
            asBox.OZS_SetRestoring(false);
        string s = "storage: proxy: box " + m_Id + " built with " + m_Items.Count().ToString() + " of " + total.ToString() + " item(s)";
        s = s + " in " + (m_Spent * 1000).ToString() + " ms of work over " + wall.ToString() + " ms";
        if (m_Missed > 0)
            s = s + ", " + m_Missed.ToString() + " REFUSED BY THIS CLIENT";
        OZ_Log.Info(s);
        m_OnChanged.Invoke(this);
    }

    // ---- the server's word -----------------------------------------------

    void Change(int what, OZS_Row r, int version)
    {
        m_Version = version;
        if (what == OZS_Const.CH_GONE)
        {
            Forget(r.handle, true);
            m_OnChanged.Invoke(this);
            return;
        }
        if (what == OZS_Const.CH_ADDED)
        {
            // It may be here already: this client's own proxy guessed it, and
            // the server is confirming. Rebuilt from the authority's word
            // either way, so the two can never drift apart.
            Forget(r.handle, true);
            Add(r);
            m_OnChanged.Invoke(this);
            return;
        }
        if (what == OZS_Const.CH_MOVED)
        {
            EntityAI e = ByHandle(r.handle);
            if (!e)
            {
                Add(r);
                m_OnChanged.Invoke(this);
                return;
            }
            Place(e, r);
            m_OnChanged.Invoke(this);
            return;
        }
        if (what == OZS_Const.CH_QTY)
        {
            EntityAI q = ByHandle(r.handle);
            if (q)
                Dress(q, r);
            m_OnChanged.Invoke(this);
            return;
        }
    }

    // The authority says this item is here. The proxy is moved to match --
    // LOCAL, because PREDICTIVE does not work on a client-local container
    // (measured 2026-09-24).
    bool Place(EntityAI e, OZS_Row r)
    {
        if (!e || !m_Box)
            return false;
        EntityAI parent = m_Box;
        if (r.parent != 0)
        {
            parent = ByHandle(r.parent);
            if (!parent)
                return false;
        }
        InventoryLocation src = new InventoryLocation();
        e.GetInventory().GetCurrentInventoryLocation(src);
        InventoryLocation dst = new InventoryLocation();
        if (r.lt == InventoryLocationType.ATTACHMENT)
            dst.SetAttachment(parent, e, r.slot);
        else
            dst.SetCargo(parent, e, 0, r.row, r.col, r.flip == 1);
        return e.GetInventory().TakeToDst(InventoryMode.LOCAL, src, dst);
    }

    // ---- what the screen asks of the server ------------------------------

    // Move something inside the box. The proxy has already shown the player
    // its own guess; this is the word that will count.
    void Move(int handle, int into, int lt, int slot, int row, int col, int flip)
    {
        Send(OZS_Const.OP_MOVE, handle, into, 0, 0, lt, slot, row, col, flip);
    }

    // OUT CARRIES WHERE THE PLAYER PUT IT. The place they dropped it is the
    // whole point of the drag: a coat dragged into a backpack must land in
    // that backpack, not in the hands. The destination's container is one of
    // the player's own entities, so it travels by network id; lt/slot/row/col
    // say where inside it.
    void Out(int handle, EntityAI into, int lt, int slot, int row, int col, int flip)
    {
        int low = 0;
        int high = 0;
        if (into)
            into.GetNetworkID(low, high);
        Send(OZS_Const.OP_OUT, handle, 0, low, high, lt, slot, row, col, flip);
    }

    void Combine(int handle, int other)
    {
        Send(OZS_Const.OP_COMBINE, handle, other, 0, 0, InventoryLocationType.CARGO, -1, -1, -1, 0);
    }

    void Swap(int handle, int other)
    {
        Send(OZS_Const.OP_SWAP, handle, other, 0, 0, InventoryLocationType.CARGO, -1, -1, -1, 0);
    }

    // An item of the player's own goes in. It is named by its NETWORK id,
    // because on this side it is a real announced entity.
    void In(EntityAI mine, int into, int lt, int slot, int row, int col, int flip)
    {
        if (!mine)
            return;
        int low;
        int high;
        mine.GetNetworkID(low, high);
        Send(OZS_Const.OP_IN, 0, into, low, high, lt, slot, row, col, flip);
    }

    // EVERY message of ours leaves on this client's own player. Not on the
    // object a message arrived on: a server -> client message arrives with its
    // target NULL (measured), so there is nothing there to answer to.
    protected void Send(int op, int handle, int other, int netLow, int netHigh, int lt, int slot, int row, int col, int flip)
    {
        Man me = GetGame().GetPlayer();
        if (!me)
            return;
        ScriptRPC rpc = new ScriptRPC();
        OZS_Wire.WriteOp(rpc, m_Id, op, handle, other, netLow, netHigh, lt, slot, row, col, flip, m_Version);
        rpc.Send(me, OZS_Const.RPC_PX_OP, true, null);
    }

    void Shut()
    {
        Man me = GetGame().GetPlayer();
        if (!me)
            return;
        ScriptRPC rpc = new ScriptRPC();
        rpc.Write(m_Id);
        rpc.Send(me, OZS_Const.RPC_PX_SHUT, true, null);
    }

    // Ask the server to show a box. Static, because there is no mirror yet.
    //
    // THE ANCHOR IS NAMED BY ITS NETWORK ID, NOT THE BOX BY ITS OWN. A box id
    // is a server-side thing -- the engine's persistent id, or a stash's pair
    // -- and the client has never been told it. The anchor is an object both
    // sides have, and asking through it also means a player can only ask for a
    // box that is in front of them.
    static void Ask(Object anchor)
    {
        Man me = GetGame().GetPlayer();
        if (!me || !anchor)
            return;
        OZS_Mirrors.Get().Asking(anchor);
        int low;
        int high;
        anchor.GetNetworkID(low, high);
        ScriptRPC rpc = new ScriptRPC();
        rpc.Write(low);
        rpc.Write(high);
        rpc.Send(me, OZS_Const.RPC_PX_OPEN, true, null);
    }

    // ---- what a drag of the vanilla screen means -------------------------

    // One move the player made with the ordinary inventory screen, once it is
    // known to touch this proxy. Three cases, and they are not symmetrical:
    //
    //   inside the box   the proxy moves AT ONCE and the server is asked to
    //                    agree -- this is the responsive case, and the one
    //                    §5 is about;
    //   out of the box   nothing is done here. The real item is not in this
    //                    proxy, it is on the server; it appears in the
    //                    player's hands when the server has moved and
    //                    announced it. Guessing would mean showing a copy.
    //   into the box     likewise: the item is the player's real, announced
    //                    entity, and the server unannounces it. A local guess
    //                    would be a second one.
    bool Drag(InventoryLocation src, InventoryLocation dst)
    {
        EntityAI item = src.GetItem();
        if (!item)
            return false;
        bool fromMe = OZS_Mirrors.Of(item) == this;
        bool toMe = OZS_Mirrors.At(dst) == this;
        if (fromMe && toMe)
            return Inside(item, dst);
        if (fromMe)
            return TakeOut(item, dst);
        if (toMe)
            return PutIn(item, dst);
        return false;
    }

    protected bool Inside(EntityAI item, InventoryLocation dst)
    {
        int handle = HandleOf(item);
        if (handle == 0)
            return false;
        int into = 0;
        if (dst.GetParent() != m_Box)
            into = HandleOf(dst.GetParent());
        int flip = 0;
        if (dst.GetFlip())
            flip = 1;
        // The picture first, the word from the server after. The proxy may be
        // wrong here and the server will say so (§5).
        OZS_Row want = new OZS_Row();
        want.Set(handle, into, dst.GetType(), dst.GetSlot(), dst.GetRow(), dst.GetCol(), flip, item.GetType());
        Place(item, want);
        Move(handle, into, dst.GetType(), dst.GetSlot(), dst.GetRow(), dst.GetCol(), flip);
        return true;
    }

    // THE PLACE THE PLAYER DROPPED IT TRAVELS WITH THE OPERATION. `dst` is
    // already exactly what the vanilla screen computed -- the backpack they
    // aimed at, the vest pocket, the cell, the hands. Throwing it away and
    // letting the server find "somewhere" is how an item ends up in the hands
    // when the player put it into a backpack.
    protected bool TakeOut(EntityAI item, InventoryLocation dst)
    {
        int handle = HandleOf(item);
        if (handle == 0)
            return false;
        int flip = 0;
        if (dst.GetFlip())
            flip = 1;
        Out(handle, dst.GetParent(), dst.GetType(), dst.GetSlot(), dst.GetRow(), dst.GetCol(), flip);
        return true;
    }

    protected bool PutIn(EntityAI item, InventoryLocation dst)
    {
        int into = 0;
        if (dst.GetParent() != m_Box)
            into = HandleOf(dst.GetParent());
        int flip = 0;
        if (dst.GetFlip())
            flip = 1;
        In(item, into, dst.GetType(), dst.GetSlot(), dst.GetRow(), dst.GetCol(), flip);
        return true;
    }

    // The other shape the vanilla screen moves things in: "put this item into
    // that target", with the place either named or left to the engine. Every
    // PredictiveTakeEntityTo* method lands here, and from here it is the same
    // three cases as a drag.
    bool DragTo(EntityAI item, EntityAI target, int lt, int slot, int row, int col)
    {
        if (!item || !target)
            return false;
        InventoryLocation src = new InventoryLocation();
        if (!item.GetInventory().GetCurrentInventoryLocation(src))
            return false;
        InventoryLocation dst = new InventoryLocation();
        if (lt == InventoryLocationType.ATTACHMENT)
            dst.SetAttachment(target, item, slot);
        else if (row >= 0 && col >= 0)
            dst.SetCargo(target, item, 0, row, col, false);
        else if (!target.GetInventory().FindFreeLocationFor(item, FindInventoryLocationType.ANY, dst))
            return false;
        return Drag(src, dst);
    }

    // Two items changing places, both inside this proxy.
    bool DragSwap(EntityAI a, EntityAI b)
    {
        int ha = HandleOf(a);
        int hb = HandleOf(b);
        if (ha == 0 || hb == 0)
            return false;
        Swap(ha, hb);
        return true;
    }

    // Two stacks becoming one, both inside this proxy. Never instant: the
    // client's own CombineItemsClient names items by network id and a proxy's
    // items have none, so the engine on the server does it and tells us.
    bool DragCombine(EntityAI into, EntityAI from)
    {
        int hi = HandleOf(into);
        int hf = HandleOf(from);
        if (hi == 0 || hf == 0)
            return false;
        Combine(hi, hf);
        return true;
    }

    void Refused(int handle, int version, string why)
    {
        OZ_Log.Warn("storage: proxy: box " + m_Id + " refused #" + handle.ToString() + " (" + why + "), the server is at v" + version.ToString());
        m_OnChanged.Invoke(this);
    }

    // ---- handles ---------------------------------------------------------

    EntityAI ByHandle(int handle)
    {
        for (int i = 0; i < m_Handles.Count(); i++)
        {
            if (m_Handles.Get(i) == handle)
                return m_Items.Get(i);
        }
        return null;
    }

    int HandleOf(EntityAI e)
    {
        for (int i = 0; i < m_Items.Count(); i++)
        {
            if (m_Items.Get(i) == e)
                return m_Handles.Get(i);
        }
        return 0;
    }

    // `andDelete` also removes the entity: an item that left the box is not
    // just unnamed, it is not there.
    void Forget(int handle, bool andDelete)
    {
        for (int i = m_Handles.Count() - 1; i >= 0; i--)
        {
            if (m_Handles.Get(i) != handle)
                continue;
            EntityAI e = m_Items.Get(i);
            m_Handles.RemoveOrdered(i);
            m_Items.RemoveOrdered(i);
            if (andDelete && e)
                GetGame().ObjectDelete(e);
        }
    }

    // Everything directly in the box, in the order the screen draws it.
    int Roots(array<EntityAI> into)
    {
        if (!m_Box || !m_Box.GetInventory())
            return 0;
        GameInventory inv = m_Box.GetInventory();
        int n = 0;
        int ac = inv.AttachmentCount();
        for (int a = 0; a < ac; a++)
        {
            EntityAI att = inv.GetAttachmentFromIndex(a);
            if (att)
            {
                into.Insert(att);
                n++;
            }
        }
        CargoBase cargo = inv.GetCargo();
        if (!cargo)
            return n;
        int cc = cargo.GetItemCount();
        for (int c = 0; c < cc; c++)
        {
            EntityAI item = cargo.GetItem(c);
            if (item)
            {
                into.Insert(item);
                n++;
            }
        }
        return n;
    }

    void Destroy()
    {
        for (int i = m_Items.Count() - 1; i >= 0; i--)
        {
            if (m_Items.Get(i))
                GetGame().ObjectDelete(m_Items.Get(i));
        }
        m_Items.Clear();
        m_Handles.Clear();
        if (m_Box)
            GetGame().ObjectDelete(m_Box);
        m_Box = null;
    }

    string Status()
    {
        string s = m_Id + " v" + m_Version.ToString() + " " + m_Items.Count().ToString() + "/" + m_Expected.ToString();
        s = s + " built in " + (m_Spent * 1000).ToString() + " ms over " + (m_Wall * 1000).ToString() + " ms";
        if (!m_Whole)
            s = s + " streaming";
        if (m_Missed > 0)
            s = s + " missed " + m_Missed.ToString();
        if (!m_Box)
            return s + " NO CONTAINER";
        return s + " netid " + m_Box.GetNetworkIDString();
    }
}
