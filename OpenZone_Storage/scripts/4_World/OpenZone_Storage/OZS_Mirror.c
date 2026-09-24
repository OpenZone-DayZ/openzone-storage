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

    string Status()
    {
        string s = "mirrors=" + m_Mirrors.Count();
        for (int i = 0; i < m_Mirrors.Count(); i++)
            s = s + " | " + m_Mirrors.Get(i).Status();
        return s;
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
        // Deep under the terrain: out of the vicinity panel's reach and out of
        // anything the player can look at.
        at[1] = at[1] - 500;
        Object made = GetGame().CreateObjectEx(m_Class, at, ECE_LOCAL | ECE_NOLIFETIME);
        m_Box = EntityAI.Cast(made);
        if (!m_Box)
        {
            OZ_Log.Error("storage: proxy: this client cannot create " + m_Class + " for box " + m_Id);
            return;
        }
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
        Send(OZS_Const.OP_MOVE, handle, into, lt, slot, row, col, flip);
    }

    void Out(int handle)
    {
        Send(OZS_Const.OP_OUT, handle, 0, InventoryLocationType.CARGO, -1, -1, -1, 0);
    }

    void Combine(int handle, int other)
    {
        Send(OZS_Const.OP_COMBINE, handle, other, InventoryLocationType.CARGO, -1, -1, -1, 0);
    }

    void Swap(int handle, int other)
    {
        Send(OZS_Const.OP_SWAP, handle, other, InventoryLocationType.CARGO, -1, -1, -1, 0);
    }

    // An item of the player's own goes in. It is named by its NETWORK id,
    // because on this side it is a real announced entity.
    void In(EntityAI mine, int lt, int slot, int row, int col, int flip)
    {
        if (!mine)
            return;
        int low;
        int high;
        mine.GetNetworkID(low, high);
        Send(OZS_Const.OP_IN, low, high, lt, slot, row, col, flip);
    }

    // EVERY message of ours leaves on this client's own player. Not on the
    // object a message arrived on: a server -> client message arrives with its
    // target NULL (measured), so there is nothing there to answer to.
    protected void Send(int op, int handle, int other, int lt, int slot, int row, int col, int flip)
    {
        Man me = GetGame().GetPlayer();
        if (!me)
            return;
        ScriptRPC rpc = new ScriptRPC();
        OZS_Wire.WriteOp(rpc, m_Id, op, handle, other, lt, slot, row, col, flip, m_Version);
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
    static void Ask(string id)
    {
        Man me = GetGame().GetPlayer();
        if (!me)
            return;
        ScriptRPC rpc = new ScriptRPC();
        rpc.Write(id);
        rpc.Send(me, OZS_Const.RPC_PX_OPEN, true, null);
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
