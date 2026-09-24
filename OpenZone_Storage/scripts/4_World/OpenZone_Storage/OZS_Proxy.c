// The server half of the proxy design: who is looking into which box, what
// their proxies have been told, and the streaming of a box's contents to one
// player at a time.
// Design: docs/specs/2026-09-24-storage-proxy-inventory-design.md §2, §5, §8.
//
// A SESSION is one box being looked at. It owns the authoritative container
// (OZS_Authority) and a list of watchers. There is no lock: several players
// may be in one box at once (owner's decision), so the server is the only
// arbiter and it needs the four things §8 asks for -- stable handles, an
// optimistic version, one operation at a time, and a change sent to every
// proxy of that box.
//
// A WATCHER is one player's proxy. It is streamed the box once, in chunks
// paced over frames, and then kept up to date by changes. Changes that happen
// WHILE it is still streaming are queued and sent after the stream's end: the
// snapshot was taken before them, so applying them earlier would be applying
// them twice.
class OZS_Proxies
{
    protected static ref OZS_Proxies s_Inst;
    protected ref array<ref OZS_Session> m_Sessions;

    static OZS_Proxies Get()
    {
        if (!s_Inst)
            s_Inst = new OZS_Proxies();
        return s_Inst;
    }

    static void Reset()
    {
        s_Inst = null;
    }

    void OZS_Proxies()
    {
        m_Sessions = new array<ref OZS_Session>();
    }

    array<ref OZS_Session> Sessions()
    {
        return m_Sessions;
    }

    OZS_Session Find(string id)
    {
        for (int i = 0; i < m_Sessions.Count(); i++)
        {
            if (m_Sessions.Get(i).m_Id == id)
                return m_Sessions.Get(i);
        }
        return null;
    }

    // ---- a player asks to see a box --------------------------------------

    // `spot` is only a valid position for the authoritative container to stand
    // at -- the anchor's, so a ground-built container of the restore has real
    // ground under it. Nobody ever looks at it.
    bool Open(string id, string cls, vector spot, PlayerIdentity who, out string why)
    {
        if (id == "" || cls == "" || !who)
        {
            why = "an open needs an id, a class and a player";
            return false;
        }
        OZS_Session s = Find(id);
        if (!s)
        {
            s = new OZS_Session(id, cls, spot);
            m_Sessions.Insert(s);
        }
        return s.Join(who, why);
    }

    void Shut(string id, PlayerIdentity who)
    {
        OZS_Session s = Find(id);
        if (s)
            s.Leave(who, "closed the screen");
    }

    // Every session this player is in: a disconnect, a death, a teleport.
    void DropPlayer(PlayerIdentity who, string cause)
    {
        if (!who)
            return;
        for (int i = m_Sessions.Count() - 1; i >= 0; i--)
            m_Sessions.Get(i).Leave(who, cause);
    }

    void OnFrame(float timeslice)
    {
        for (int i = m_Sessions.Count() - 1; i >= 0; i--)
        {
            OZS_Session s = m_Sessions.Get(i);
            s.OnFrame(timeslice);
            if (s.IsDone())
            {
                s.End();
                m_Sessions.RemoveOrdered(i);
            }
        }
    }

    // The mission is coming down: let every authority go without writing, the
    // way a crash would. Whatever reached SQL is what there is (§9).
    void EndAll()
    {
        for (int i = m_Sessions.Count() - 1; i >= 0; i--)
            m_Sessions.Get(i).End();
        m_Sessions.Clear();
    }

    string Status()
    {
        string s = "sessions=" + m_Sessions.Count();
        for (int i = 0; i < m_Sessions.Count(); i++)
            s = s + " | " + m_Sessions.Get(i).Status();
        return s;
    }

    // ---- the anchor's RPCs -----------------------------------------------

    // Called from the player's OnRPC: a client's message rides on its own
    // player entity, and that direction keeps its target. Returns true when
    // the message was ours.
    static bool OnWire(PlayerIdentity sender, int type, ParamsReadContext ctx)
    {
        if (!GetGame() || !GetGame().IsServer())
            return false;
        if (type == OZS_Const.RPC_PX_OPEN)
        {
            string wantId;
            if (!ctx.Read(wantId))
                return true;
            // The client names the box; the class and the place are the
            // server's to decide. A client that could name the class could
            // ask for a grid the record does not fit.
            OZ_StorageBox real = OZS_Controller.Get().FindById(wantId);
            if (!real)
            {
                OZ_Log.Warn("storage: proxy: " + OZS_Controller.UidOfIdentity(sender) + " asked for " + wantId + ", which is not a box in this world");
                return true;
            }
            string why;
            if (!OZS_Proxies.Get().Open(wantId, real.GetType(), real.GetPosition(), sender, why))
                OZ_Log.Warn("storage: proxy: " + OZS_Controller.UidOfIdentity(sender) + " cannot open " + wantId + ": " + why);
            return true;
        }
        if (type == OZS_Const.RPC_PX_SHUT)
        {
            string shutId;
            if (!ctx.Read(shutId))
                return true;
            OZS_Proxies.Get().Shut(shutId, sender);
            return true;
        }
        if (type == OZS_Const.RPC_PX_OP)
        {
            string opId;
            int op;
            int handle;
            int other;
            int lt;
            int slot;
            int row;
            int col;
            int flip;
            int version;
            if (!OZS_Wire.ReadOp(ctx, opId, op, handle, other, lt, slot, row, col, flip, version))
                return true;
            OZS_Session s = OZS_Proxies.Get().Find(opId);
            if (!s)
                return true;
            s.Operate(sender, op, handle, other, lt, slot, row, col, flip, version);
            return true;
        }
        return false;
    }
}

// ---------------------------------------------------------------------------

class OZS_Session
{
    string m_Id;
    string m_Class;
    vector m_Spot;
    OZ_StorageBox m_Auth;
    ref array<ref OZS_Watcher> m_Watchers;
    // Bumped by every accepted operation. A client names the version it acted
    // on; one that has fallen behind is refused and resynchronised (§8.2).
    int m_Version;
    // Seconds with nobody looking. An authority costs memory, not secrecy, so
    // the wait can be short (§11).
    float m_Empty;
    bool m_Ended;
    // Turns posted to the bridge that have not answered yet, and what the
    // bridge last said about the record. A session with turns in flight is not
    // finished, however quiet it has gone.
    int m_Flying;
    int m_SqlVersion;
    int m_SqlRoots;
    int m_SqlEntities;

    void OZS_Session(string id, string cls, vector spot)
    {
        m_Id = id;
        m_Class = cls;
        m_Spot = spot;
        m_Watchers = new array<ref OZS_Watcher>();
        m_Version = 0;
        m_Empty = 0;
        m_Ended = false;
    }

    // A WATCHER IS KEYED BY ITS UID, NOT BY ITS PlayerIdentity. The engine
    // hands OnRPC an identity that is not the same object as the player's own
    // GetIdentity(): the first version of this compared the two and never
    // found the player, so every message was sent to a null target -- which
    // silently becomes a global RPC the client ignores. Measured 2026-09-24.
    OZS_Watcher WatcherOf(PlayerIdentity who)
    {
        return WatcherOfUid(OZS_Controller.UidOfIdentity(who));
    }

    OZS_Watcher WatcherOfUid(string uid)
    {
        if (uid == "")
            return null;
        for (int i = 0; i < m_Watchers.Count(); i++)
        {
            if (m_Watchers.Get(i).m_Uid == uid)
                return m_Watchers.Get(i);
        }
        return null;
    }

    // ---- joining and leaving ---------------------------------------------

    bool Join(PlayerIdentity who, out string why)
    {
        if (WatcherOf(who))
        {
            // Asking twice is not an error: a reopened screen wants the box
            // again. The old proxy is replaced by a fresh stream.
            OZS_Watcher again = WatcherOf(who);
            again.Restart();
            return true;
        }
        if (!m_Auth)
        {
            m_Auth = OZS_Authority.Create(m_Id, m_Class, AuthoritySpot());
            if (!m_Auth)
            {
                why = "the authority could not be created";
                return false;
            }
        }
        OZS_Watcher w = new OZS_Watcher(this, who);
        m_Watchers.Insert(w);
        m_Empty = 0;
        // The first watcher pays for the fill; the rest arrive to a box that
        // is already there.
        if (m_Auth.OZS_GetState() == OZS_Const.STATE_CLOSED)
        {
            string openWhy;
            if (!OZS_Controller.Get().RequestOpenAs(m_Auth, "proxy", OZS_Controller.UidOfIdentity(who), openWhy))
            {
                m_Watchers.RemoveOrdered(m_Watchers.Count() - 1);
                why = "the box cannot be filled: " + openWhy;
                return false;
            }
        }
        OZ_Log.Info("storage: proxy: " + OZS_Controller.UidOfIdentity(who) + " is looking into " + m_Id + " (" + m_Watchers.Count().ToString() + " watcher(s))");
        return true;
    }

    void Leave(PlayerIdentity who, string cause)
    {
        string uid = OZS_Controller.UidOfIdentity(who);
        for (int i = m_Watchers.Count() - 1; i >= 0; i--)
        {
            if (m_Watchers.Get(i).m_Uid != uid)
                continue;
            m_Watchers.RemoveOrdered(i);
            OZ_Log.Info("storage: proxy: " + OZS_Controller.UidOfIdentity(who) + " left " + m_Id + " (" + cause + "), " + m_Watchers.Count().ToString() + " left");
        }
    }

    // A place for the authority nobody will ever stand in. It only has to be
    // a valid position: the anchor's own spot keeps a ground-built container
    // of the restore on real ground.
    vector AuthoritySpot()
    {
        return m_Spot;
    }

    // ---- the frame -------------------------------------------------------

    void OnFrame(float timeslice)
    {
        if (m_Ended)
            return;
        for (int i = m_Watchers.Count() - 1; i >= 0; i--)
        {
            OZS_Watcher w = m_Watchers.Get(i);
            if (!w.Alive())
            {
                OZ_Log.Info("storage: proxy: " + w.m_Uid + " is no longer on the server; " + m_Id + " lets them go");
                m_Watchers.RemoveOrdered(i);
                continue;
            }
            w.OnFrame(timeslice);
        }
        if (m_Watchers.Count() > 0)
        {
            m_Empty = 0;
            return;
        }
        m_Empty = m_Empty + timeslice;
    }

    bool IsDone()
    {
        if (m_Ended)
            return true;
        if (m_Watchers.Count() > 0)
            return false;
        if (m_Flying > 0)
            return false;
        return m_Empty >= OZS_Settings.Get().ProxyIdleSeconds;
    }

    // Nobody is looking any more. The authority is discarded -- not closed:
    // SQL was written operation by operation and is already current (§7).
    void End()
    {
        if (m_Ended)
            return;
        m_Ended = true;
        m_Watchers.Clear();
        if (m_Auth)
        {
            int gone = OZS_Authority.Discard(m_Id);
            OZ_Log.Info("storage: proxy: session " + m_Id + " ended, " + gone.ToString() + " entity(ies) released");
            m_Auth = null;
        }
    }

    // ---- what the watchers are told --------------------------------------

    // The whole box as rows, parents before children. The handles come from
    // the authority and are the only name an operation may use.
    int Snapshot(array<ref OZS_Row> into)
    {
        into.Clear();
        if (!m_Auth)
            return 0;
        OZS_Authority.Index(m_Auth);
        array<EntityAI> nodes = new array<EntityAI>();
        array<int> parents = new array<int>();
        OZS_Records.Flatten(m_Auth, -1, nodes, parents);
        for (int i = 1; i < nodes.Count(); i++)
        {
            EntityAI e = nodes.Get(i);
            if (!e)
                continue;
            int parentHandle = 0;
            if (parents.Get(i) > 0)
                parentHandle = OZS_Authority.Handle(m_Auth, nodes.Get(parents.Get(i)));
            OZS_Row r = new OZS_Row();
            Describe(e, OZS_Authority.Handle(m_Auth, e), parentHandle, r);
            into.Insert(r);
        }
        return into.Count();
    }

    // One entity as a row. Everything here is asked of the entity itself, so
    // a modded item answers for its own quantity and damage.
    static void Describe(EntityAI e, int handle, int parentHandle, OZS_Row r)
    {
        InventoryLocation il = new InventoryLocation();
        e.GetInventory().GetCurrentInventoryLocation(il);
        int flip = 0;
        if (il.GetFlip())
            flip = 1;
        r.Set(handle, parentHandle, il.GetType(), il.GetSlot(), il.GetRow(), il.GetCol(), flip, e.GetType());
        r.health = Math.Round(e.GetHealth01("", "") * 100);
        ItemBase item = ItemBase.Cast(e);
        if (item && item.HasQuantity())
        {
            r.qty = Math.Round(item.GetQuantity());
            r.qtyMax = Math.Round(item.GetQuantityMax());
        }
    }

    // Every proxy of this box hears every change (§8.4), including the one
    // whose player caused it: the proxy shows its own guess at once and this
    // is the authority's word, which always wins.
    void Tell(int change, OZS_Row r)
    {
        m_Version++;
        for (int i = 0; i < m_Watchers.Count(); i++)
            m_Watchers.Get(i).Change(change, r, m_Version);
    }

    void TellGone(int handle)
    {
        OZS_Row r = new OZS_Row();
        r.Set(handle, 0, 0, 0, -1, -1, 0, "");
        Tell(OZS_Const.CH_GONE, r);
    }

    void TellMoved(EntityAI e)
    {
        if (!e || !m_Auth)
            return;
        OZS_Row r = new OZS_Row();
        Describe(e, OZS_Authority.Handle(m_Auth, e), ParentHandle(e), r);
        Tell(OZS_Const.CH_MOVED, r);
    }

    void TellQuantity(EntityAI e)
    {
        if (!e || !m_Auth)
            return;
        OZS_Row r = new OZS_Row();
        Describe(e, OZS_Authority.Handle(m_Auth, e), ParentHandle(e), r);
        Tell(OZS_Const.CH_QTY, r);
    }

    void TellAdded(EntityAI e)
    {
        if (!e || !m_Auth)
            return;
        OZS_Row r = new OZS_Row();
        Describe(e, OZS_Authority.Handle(m_Auth, e), ParentHandle(e), r);
        Tell(OZS_Const.CH_ADDED, r);
    }

    int ParentHandle(EntityAI e)
    {
        EntityAI parent = e.GetHierarchyParent();
        if (!parent || parent == m_Auth)
            return 0;
        return OZS_Authority.Handle(m_Auth, parent);
    }

    // ---- the turn's bookkeeping ------------------------------------------

    // Somebody did something. The idle clock of the authority restarts, and so
    // does the box's own, so an old auto-close cannot fire under a session.
    void Touch()
    {
        m_Empty = 0;
        if (m_Auth)
            m_Auth.OZS_SetTouchedAt(GetGame().GetTickTime());
    }

    void OnCommitSent()
    {
        m_Flying++;
    }

    void OnCommitted(int version, int roots, int entities)
    {
        m_Flying--;
        if (m_Flying < 0)
            m_Flying = 0;
        m_SqlVersion = version;
        m_SqlRoots = roots;
        m_SqlEntities = entities;
    }

    // The session cannot go on: the bridge is gone, or a turn could not be
    // written. Everything the players did that reached SQL stands; the rest is
    // the one turn §9 is willing to lose. The screens are told and the
    // authority is let go.
    void Fail(string why)
    {
        if (m_Ended)
            return;
        OZ_Log.Error("storage: proxy: session " + m_Id + " ends: " + why);
        for (int i = 0; i < m_Watchers.Count(); i++)
            m_Watchers.Get(i).No(0, "#STR_OZ_ERR_NO_BRIDGE", m_Version);
        End();
    }

    // ---- operations ------------------------------------------------------

    // Stage C and later fill this in; the wire and the refusal path exist now
    // so a client can be told "no" from the first day.
    void Operate(PlayerIdentity who, int op, int handle, int other, int lt, int slot, int row, int col, int flip, int version)
    {
        OZS_Watcher w = WatcherOf(who);
        if (!w)
            return;
        if (!m_Auth || m_Auth.OZS_GetState() != OZS_Const.STATE_OPEN)
        {
            w.No(handle, "the box is not ready", m_Version);
            return;
        }
        if (version != m_Version)
        {
            // The proxy acted on a box that has moved on. Nothing is applied;
            // the proxy is told to catch up (§8.2).
            w.No(handle, "stale", m_Version);
            w.Restart();
            return;
        }
        OZS_Ops.Run(this, w, op, handle, other, lt, slot, row, col, flip);
    }

    string Status()
    {
        string s = m_Id + " v" + m_Version.ToString() + " watchers " + m_Watchers.Count().ToString();
        s = s + " sql v" + m_SqlVersion.ToString() + "/" + m_SqlRoots.ToString() + "r/" + m_SqlEntities.ToString() + "e";
        if (m_Flying > 0)
            s = s + " flying " + m_Flying.ToString();
        if (!m_Auth)
            return s + " NO AUTHORITY";
        s = s + " " + OZS_Const.StateName(m_Auth.OZS_GetState());
        s = s + " tree " + (OZS_Records.CountTree(m_Auth) - 1).ToString();
        for (int i = 0; i < m_Watchers.Count(); i++)
            s = s + " | " + m_Watchers.Get(i).Status();
        return s;
    }
}

// ---------------------------------------------------------------------------

class OZS_Watcher
{
    OZS_Session m_Session;
    string m_Uid;
    // The stream: a snapshot taken when the box became ready, sent in chunks.
    ref array<ref OZS_Row> m_Rows;
    int m_Sent;
    bool m_Begun;
    bool m_Whole;
    float m_Started;
    float m_Spent;
    // Changes that happened while the stream was still running. The snapshot
    // predates them, so they go after the end and never twice.
    ref array<ref OZS_Change> m_Queued;

    void OZS_Watcher(OZS_Session session, PlayerIdentity who)
    {
        m_Session = session;
        m_Uid = OZS_Controller.UidOfIdentity(who);
        m_Rows = new array<ref OZS_Row>();
        m_Queued = new array<ref OZS_Change>();
        Restart();
    }

    void Restart()
    {
        m_Rows.Clear();
        m_Queued.Clear();
        m_Sent = 0;
        m_Begun = false;
        m_Whole = false;
        m_Started = 0;
        m_Spent = 0;
    }

    // The player, found by uid rather than kept as a reference: a player who
    // dies and respawns is a new entity, and a PlayerIdentity kept across a
    // disconnect is a dangling pointer.
    Man Player()
    {
        array<Man> men = new array<Man>();
        GetGame().GetPlayers(men);
        for (int i = 0; i < men.Count(); i++)
        {
            Man man = men.Get(i);
            if (man && man.GetIdentity() && man.GetIdentity().GetPlainId() == m_Uid)
                return man;
        }
        return null;
    }

    // The identity to address a message to: THE PLAYER'S OWN, not the one the
    // RPC came in with. They are different objects for the same person.
    PlayerIdentity Who()
    {
        Man man = Player();
        if (!man)
            return null;
        return man.GetIdentity();
    }

    string Name()
    {
        PlayerIdentity id = Who();
        if (!id)
            return "";
        return id.GetName();
    }

    bool Alive()
    {
        return Player() != null;
    }

    void OnFrame(float timeslice)
    {
        if (m_Whole)
            return;
        if (!m_Session.m_Auth)
            return;
        if (m_Session.m_Auth.OZS_GetState() != OZS_Const.STATE_OPEN)
            return;
        float t0 = GetGame().GetTickTime();
        if (!m_Begun)
        {
            m_Started = t0;
            m_Session.Snapshot(m_Rows);
            Man target = Player();
            PlayerIdentity to = Who();
            string aim = "target=none";
            if (target)
                aim = "target=" + target.GetType() + "/" + target.GetNetworkIDString();
            if (to)
                aim = aim + " recipient=" + to.GetPlainId();
            else
                aim = aim + " recipient=none";
            OZ_Log.Dbg("storage: proxy: BEGIN of " + m_Session.m_Id + " to " + m_Uid + " " + aim);
            ScriptRPC begin = new ScriptRPC();
            begin.Write(m_Session.m_Id);
            begin.Write(m_Session.m_Class);
            begin.Write(m_Rows.Count());
            begin.Write(m_Session.m_Version);
            begin.Send(Player(), OZS_Const.RPC_PX_BEGIN, true, Who());
            m_Begun = true;
            m_Sent = 0;
        }
        int chunk = OZS_Settings.Get().ProxyRowsPerMessage;
        int perFrame = OZS_Settings.Get().ProxyMessagesPerFrame;
        for (int m = 0; m < perFrame && m_Sent < m_Rows.Count(); m++)
        {
            int n = m_Rows.Count() - m_Sent;
            if (n > chunk)
                n = chunk;
            ScriptRPC rows = new ScriptRPC();
            rows.Write(m_Session.m_Id);
            rows.Write(n);
            for (int i = 0; i < n; i++)
                OZS_Wire.WriteRow(rows, m_Rows.Get(m_Sent + i));
            rows.Send(Player(), OZS_Const.RPC_PX_ROWS, true, Who());
            m_Sent = m_Sent + n;
        }
        if (m_Sent < m_Rows.Count())
        {
            m_Spent = m_Spent + (GetGame().GetTickTime() - t0);
            return;
        }
        ScriptRPC end = new ScriptRPC();
        end.Write(m_Session.m_Id);
        end.Write(m_Rows.Count());
        end.Write(m_Session.m_Version);
        end.Send(Player(), OZS_Const.RPC_PX_END, true, Who());
        m_Whole = true;
        m_Spent = m_Spent + (GetGame().GetTickTime() - t0);
        OZ_Log.Info("storage: proxy: " + m_Uid + " has " + m_Rows.Count().ToString() + " row(s) of " + m_Session.m_Id + " in " + (m_Spent * 1000).ToString() + " ms of server time over " + ((GetGame().GetTickTime() - m_Started) * 1000).ToString() + " ms");
        Flush();
    }

    void Change(int change, OZS_Row r, int version)
    {
        if (!m_Whole)
        {
            m_Queued.Insert(new OZS_Change(change, r, version));
            return;
        }
        Post(change, r, version);
    }

    protected void Flush()
    {
        for (int i = 0; i < m_Queued.Count(); i++)
            Post(m_Queued.Get(i).m_What, m_Queued.Get(i).m_Row, m_Queued.Get(i).m_Version);
        m_Queued.Clear();
    }

    protected void Post(int change, OZS_Row r, int version)
    {
        ScriptRPC c = new ScriptRPC();
        c.Write(m_Session.m_Id);
        c.Write(change);
        c.Write(version);
        OZS_Wire.WriteRow(c, r);
        c.Send(Player(), OZS_Const.RPC_PX_CHANGE, true, Who());
    }

    // A refusal always carries the version the server is at, so the proxy can
    // decide whether a full resend is coming.
    void No(int handle, string why, int version)
    {
        ScriptRPC no = new ScriptRPC();
        no.Write(m_Session.m_Id);
        no.Write(handle);
        no.Write(version);
        no.Write(why);
        no.Send(Player(), OZS_Const.RPC_PX_NO, true, Who());
    }

    string Status()
    {
        string s = m_Uid + " " + m_Sent.ToString() + "/" + m_Rows.Count().ToString();
        if (m_Whole)
            return s + " whole";
        return s + " streaming";
    }
}

class OZS_Change
{
    int m_What;
    ref OZS_Row m_Row;
    int m_Version;

    void OZS_Change(int what, OZS_Row r, int version)
    {
        m_What = what;
        m_Row = r;
        m_Version = version;
    }
}
