// Writing one turn to SQL, as it happens.
// Design: docs/specs/2026-09-24-storage-proxy-inventory-design.md §7.
//
// WHY PER OPERATION AND NOT AT CLOSE. In this scheme the authoritative
// container is not saved by the engine, so nothing but SQL remembers the box.
// A commit at close would make a crash cost the whole session; a commit per
// operation costs the one turn that did not reach the base.
//
// THE UNIT IS A ROOT, NOT THE BOX. A record is a list of roots, each a blob of
// its whole subtree. Rewriting the box after every drag would be 0.17 ms per
// entity -- a fifth of a second on a full box. Rewriting the one root that
// changed is a few dozen bytes. A root is named by its POSITION in the record;
// the box keeps that order from the open (OZS_RootOrder) and the bridge
// numbers the rows of a version the same way, so neither side has to trust an
// index the other invented. A dropped root leaves the list on both sides and a
// new one joins the end: that is the whole agreement.
//
// THE VERSION IS NOT CUT PER TURN. The bridge forks the current version once,
// on the first change of a session, and mutates it after that. History stays
// readable -- one version per session rather than one per drag (§7).
class OZS_Commit
{
    protected static int s_Serial;

    static int NextSerial()
    {
        s_Serial++;
        if (s_Serial > 999999)
            s_Serial = 1;
        return s_Serial;
    }

    // The top-level item `e` hangs under, or `e` itself when it is one.
    static EntityAI TopOf(OZS_Session s, EntityAI e)
    {
        if (!s || !s.m_Auth || !e)
            return null;
        EntityAI up = e;
        while (up && up.GetHierarchyParent() && up.GetHierarchyParent() != s.m_Auth)
            up = up.GetHierarchyParent();
        return up;
    }

    // Which root of the record `e` belongs to. MUST BE TAKEN BEFORE A MOVE
    // when the move can change it: afterwards the entity belongs somewhere
    // else and the old root is no longer reachable from it.
    static int RootOf(OZS_Session s, EntityAI e)
    {
        EntityAI top = TopOf(s, e);
        if (!top)
            return -1;
        return s.m_Auth.OZS_RootPosition(top);
    }

    // ---- what the operations call ----------------------------------------

    // An item moved inside the box. `wasRoot` is the position it belonged to
    // before the move; pass -2 when the move cannot have changed it.
    static void Moved(OZS_Session s, EntityAI e, int wasRoot = -2)
    {
        if (!Ready(s))
            return;
        EntityAI top = TopOf(s, e);
        int now = s.m_Auth.OZS_RootPosition(top);
        OZS_Letter letter = new OZS_Letter(s);
        if (wasRoot >= 0 && wasRoot != now)
            letter.Rewrite(wasRoot);
        if (now >= 0)
            letter.Rewrite(now);
        else
            letter.Add(top);
        letter.Post();
    }

    static void Quantity(OZS_Session s, EntityAI e)
    {
        Moved(s, e, -2);
    }

    // A whole item arrived from outside the box.
    static void Added(OZS_Session s, EntityAI e)
    {
        if (!Ready(s))
            return;
        OZS_Letter letter = new OZS_Letter(s);
        letter.Add(TopOf(s, e));
        letter.Post();
    }

    // An item is no longer in the box: emptied into another stack, taken out,
    // destroyed. Called AFTER the entity is gone, so both numbers must have
    // been taken before it went: `wasRoot` is the root it belonged to, and
    // `wasItself` says whether it WAS that root rather than something nested
    // inside it.
    static void Left(OZS_Session s, int wasRoot, bool wasItself)
    {
        if (!Ready(s))
            return;
        OZS_Letter letter = new OZS_Letter(s);
        if (wasItself)
            letter.Drop(wasRoot);
        else
            letter.Rewrite(wasRoot);
        letter.Post();
    }

    static bool Ready(OZS_Session s)
    {
        if (!s || !s.m_Auth)
            return false;
        if (!OZS_Bridge.Up())
        {
            // §9: buffering in memory is exactly the window a per-operation
            // commit exists to close. The session ends instead.
            OZ_Log.Error("storage: proxy: the bridge is down; box " + s.m_Id + " cannot take another turn");
            s.Fail("the bridge is down");
            return false;
        }
        return true;
    }
}

// One letter to the bridge: the roots that changed, the roots that are gone,
// the roots that are new. An operation builds one and posts it once, so a turn
// that touches two roots is one round trip and cannot half-happen.
class OZS_Letter
{
    protected OZS_Session m_S;
    protected ref array<int> m_Rewrite;
    protected ref array<int> m_Drop;
    protected ref array<EntityAI> m_Add;

    void OZS_Letter(OZS_Session s)
    {
        m_S = s;
        m_Rewrite = new array<int>();
        m_Drop = new array<int>();
        m_Add = new array<EntityAI>();
    }

    void Rewrite(int pos)
    {
        if (pos >= 0 && m_Rewrite.Find(pos) < 0)
            m_Rewrite.Insert(pos);
    }

    void Drop(int pos)
    {
        if (pos >= 0 && m_Drop.Find(pos) < 0)
            m_Drop.Insert(pos);
    }

    void Add(EntityAI top)
    {
        if (top && m_Add.Find(top) < 0)
            m_Add.Insert(top);
    }

    void Post()
    {
        if (m_Rewrite.Count() == 0 && m_Drop.Count() == 0 && m_Add.Count() == 0)
            return;
        array<EntityAI> order = m_S.m_Auth.OZS_RootOrder();
        // The blobs, in the order the bridge reads them: first the rewrites,
        // then the additions. The letter says how many of each.
        array<EntityAI> blobs = new array<EntityAI>();
        array<int> at = new array<int>();
        for (int i = 0; i < m_Rewrite.Count(); i++)
        {
            int pos = m_Rewrite.Get(i);
            if (pos < 0 || pos >= order.Count() || !order.Get(pos))
                continue;
            at.Insert(pos);
            blobs.Insert(order.Get(pos));
        }
        int rewrites = blobs.Count();
        for (int a = 0; a < m_Add.Count(); a++)
            blobs.Insert(m_Add.Get(a));
        int entities = 0;
        for (int c = 0; c < blobs.Count(); c++)
            entities = entities + OZS_Records.CountTree(blobs.Get(c));

        string name = "";
        if (blobs.Count() > 0)
        {
            OZS_StoreWriter writer = new OZS_StoreWriter();
            string why;
            if (!writer.OpenOp(m_S.m_Auth, blobs.Count(), entities, OZS_Commit.NextSerial(), why))
            {
                OZ_Log.Error("storage: proxy: a turn of box " + m_S.m_Id + " cannot be written: " + why);
                m_S.Fail("the turn could not be written");
                return;
            }
            for (int b = 0; b < blobs.Count(); b++)
                writer.WriteRoot(blobs.Get(b));
            if (!writer.Finish(why))
            {
                OZ_Log.Error("storage: proxy: a turn of box " + m_S.m_Id + " was written short: " + why);
                m_S.Fail("the turn could not be written");
                return;
            }
            name = writer.Name();
        }

        OZS_OpLetter letter = new OZS_OpLetter();
        letter.id = m_S.m_Id;
        letter.file = name;
        letter.rewrite = new array<int>();
        letter.drop = new array<int>();
        for (int r = 0; r < at.Count(); r++)
            letter.rewrite.Insert(at.Get(r));
        for (int d = 0; d < m_Drop.Count(); d++)
            letter.drop.Insert(m_Drop.Get(d));
        letter.adds = m_Add.Count();
        letter.entities = entities;

        // The record's order follows the letter on this side too, and the
        // bridge renumbers its rows the same way. Drops are applied from the
        // back so the earlier positions keep their meaning while we go.
        for (int dd = m_Drop.Count() - 1; dd >= 0; dd--)
        {
            int gone = m_Drop.Get(dd);
            if (gone >= 0 && gone < order.Count())
                order.RemoveOrdered(gone);
        }
        for (int aa = 0; aa < m_Add.Count(); aa++)
            order.Insert(m_Add.Get(aa));

        string json;
        string err;
        if (!JsonFileLoader<OZS_OpLetter>.MakeData(letter, json, err, false))
        {
            OZ_Log.Error("storage: proxy: the turn letter cannot be written: " + err);
            m_S.Fail("the turn could not be written");
            return;
        }
        m_S.OnCommitSent();
        OZS_Bridge.Post(OZS_Const.ROUTE_OP, json, new OZS_OpReply(m_S));
    }
}

class OZS_OpLetter
{
    string id;
    string file;
    ref array<int> rewrite;
    ref array<int> drop;
    int adds;
    int entities;
}

class OZS_OpAnswer
{
    bool ok;
    string why;
    int version;
    int roots;
    int entities;
}

class OZS_OpReply : OZ_BridgeReply
{
    protected OZS_Session m_S;

    void OZS_OpReply(OZS_Session s)
    {
        m_S = s;
    }

    override void OnBody(string json)
    {
        if (!m_S)
            return;
        OZS_OpAnswer a = new OZS_OpAnswer();
        string err;
        if (!JsonFileLoader<OZS_OpAnswer>.LoadData(json, a, err) || !a)
        {
            OZ_Log.Error("storage: proxy: the bridge's answer to a turn cannot be read: " + err);
            m_S.Fail("the bridge's answer to a turn could not be read");
            return;
        }
        if (!a.ok)
        {
            OZ_Log.Error("storage: proxy: the bridge refused a turn of box " + m_S.m_Id + ": " + a.why);
            m_S.Fail("the bridge refused a turn: " + a.why);
            return;
        }
        m_S.OnCommitted(a.version, a.roots, a.entities);
    }

    override void OnFail(int code)
    {
        if (!m_S)
            return;
        OZ_Log.Error("storage: proxy: a turn of box " + m_S.m_Id + " never reached the bridge (code " + code.ToString() + ")");
        m_S.Fail("a turn never reached the bridge");
    }
}
