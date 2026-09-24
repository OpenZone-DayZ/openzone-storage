// The AUTHORITATIVE container: a real box that nobody is ever told about.
// Design: docs/specs/2026-09-24-storage-proxy-inventory-design.md, stage A.
//
// WHY A REAL CONTAINER AND NOT A TABLE OF RECORDS. Everything the engine and
// every other mod know about what may go where -- CanReceiveItemIntoCargo,
// CanBeCombined, slot rules, sizes, every script override a mod ever wrote --
// can only be asked of a live entity. A table of records would make us the
// author of those rules; a real container keeps the engine as their author.
//
// WHY NOBODY SEES IT. Two flags, both measured on 2026-09-24:
//
//   ECE_LOCAL                 the object is not registered on the network at
//                             all -- netid stays "00", so no client can be
//                             told about it even by accident
//   ECE_NOPERSISTENCY_WORLD   the engine never writes it, or anything inside
//                             it, into the world save. After a crash there is
//                             nothing left at all: no second truth to
//                             reconcile, no sweep, no ghosts
//
// (results-nopersistency.md; the control box created without the second flag
// did come back, which is what makes the measurement mean anything.)
//
// The class of an authority is THE SAME CLASS as the box it stands for, so its
// grid, its slots and its rules match by construction rather than by a table
// somebody has to keep in step.
class OZS_Authority
{
    protected static ref array<ref OZS_AuthRec> s_Live;
    // Up for exactly one CreateObjectEx call. EEInit runs inside that call, so
    // there is no other moment at which the box can be told what it is before
    // it registers itself as an ordinary box in the world.
    protected static bool s_Making;

    static bool IsMaking()
    {
        return s_Making;
    }

    protected static array<ref OZS_AuthRec> Live()
    {
        if (!s_Live)
            s_Live = new array<ref OZS_AuthRec>();
        return s_Live;
    }

    // ---- making one ------------------------------------------------------

    // `forId` is the id whose contents this will hold -- a box's persistent id
    // or a stash's pair. `cls` must be the class of that box, or its grid will
    // not match what is stored and the restore will park roots it cannot
    // place. `at` only has to be a valid position; nobody will ever look at it.
    static OZ_StorageBox Create(string forId, string cls, vector at)
    {
        if (!GetGame() || !GetGame().IsServer())
            return null;
        if (forId == "" || cls == "")
        {
            OZ_Log.Error("storage: an authority needs both an id and a class");
            return null;
        }
        OZ_StorageBox already = Find(forId);
        if (already)
        {
            OZ_Log.Warn("storage: an authority for " + forId + " exists already; reusing it");
            return already;
        }
        s_Making = true;
        Object made = GetGame().CreateObjectEx(cls, at, ECE_LOCAL | ECE_NOPERSISTENCY_WORLD | ECE_NOLIFETIME);
        s_Making = false;
        OZ_StorageBox box = OZ_StorageBox.Cast(made);
        if (!box)
        {
            OZ_Log.Error("storage: the authority for " + forId + " could not be created as " + cls);
            if (made)
                GetGame().ObjectDelete(made);
            return null;
        }
        // AFTER creation: EEInit has left the id empty on purpose, and an
        // authority answers with the id of the box whose contents it holds.
        box.OZS_StandForId(forId);
        OZS_AuthRec rec = new OZS_AuthRec(box, forId);
        Live().Insert(rec);
        OZ_Log.Info("storage: authority for " + forId + " created as " + cls + ", netid " + box.GetNetworkIDString());
        return box;
    }

    // ---- finding one -----------------------------------------------------

    static OZ_StorageBox Find(string forId)
    {
        array<ref OZS_AuthRec> live = Live();
        for (int i = 0; i < live.Count(); i++)
        {
            OZS_AuthRec rec = live.Get(i);
            if (rec.m_Box && rec.m_For == forId)
                return rec.m_Box;
        }
        return null;
    }

    static bool Is(OZ_StorageBox box)
    {
        return RecOf(box) != null;
    }

    protected static OZS_AuthRec RecOf(OZ_StorageBox box)
    {
        if (!box)
            return null;
        array<ref OZS_AuthRec> live = Live();
        for (int i = 0; i < live.Count(); i++)
        {
            if (live.Get(i).m_Box == box)
                return live.Get(i);
        }
        return null;
    }

    // ---- handles ---------------------------------------------------------

    // EVERY OPERATION NAMES AN ITEM BY A HANDLE, NEVER BY ITS CELL. Cells move
    // the moment anybody rearranges anything, and two players in one box is
    // the point (design §8); a network id is no use either, because an item in
    // an unannounced container has none. So the authority hands out its own.
    static int Handle(OZ_StorageBox box, EntityAI item)
    {
        OZS_AuthRec rec = RecOf(box);
        if (!rec || !item)
            return 0;
        for (int i = 0; i < rec.m_Items.Count(); i++)
        {
            if (rec.m_Items.Get(i) == item)
                return rec.m_Handles.Get(i);
        }
        rec.m_Next++;
        rec.m_Items.Insert(item);
        rec.m_Handles.Insert(rec.m_Next);
        return rec.m_Next;
    }

    // A handle for everything in the box, in the order the descriptor will
    // name them. Returns how many entities are there. Handles already given
    // out keep their numbers -- that is the whole point of a handle.
    static int Index(OZ_StorageBox box)
    {
        OZS_AuthRec rec = RecOf(box);
        if (!rec)
            return 0;
        Forget(box);
        array<EntityAI> nodes = new array<EntityAI>();
        array<int> parents = new array<int>();
        OZS_Records.Flatten(box, -1, nodes, parents);
        // Node 0 is the box itself; it is not an item and gets no handle.
        for (int i = 1; i < nodes.Count(); i++)
            Handle(box, nodes.Get(i));
        return nodes.Count() - 1;
    }

    static EntityAI ByHandle(OZ_StorageBox box, int handle)
    {
        OZS_AuthRec rec = RecOf(box);
        if (!rec)
            return null;
        for (int i = 0; i < rec.m_Handles.Count(); i++)
        {
            if (rec.m_Handles.Get(i) == handle)
                return rec.m_Items.Get(i);
        }
        return null;
    }

    // Entries whose item is gone -- taken out, merged away, destroyed. Called
    // when the authority is asked to describe itself, so a long session does
    // not grow a table of the dead.
    static int Forget(OZ_StorageBox box)
    {
        OZS_AuthRec rec = RecOf(box);
        if (!rec)
            return 0;
        int dropped = 0;
        for (int i = rec.m_Items.Count() - 1; i >= 0; i--)
        {
            if (!rec.m_Items.Get(i))
            {
                rec.m_Items.RemoveOrdered(i);
                rec.m_Handles.RemoveOrdered(i);
                dropped++;
            }
        }
        return dropped;
    }

    // ---- ending one ------------------------------------------------------

    // DISCARD, and nothing else. An authority holds no truth of its own: the
    // truth is in SQL, written operation by operation (design §7), so letting
    // one go costs at most the turn that did not reach the base -- which is
    // exactly what a crash costs (§9). Nothing is written here, and nothing is
    // asked of the bridge.
    //
    // Contents are deleted deepest-first rather than left to the container's
    // own deletion. It probably takes them with it; "probably" is how this
    // project got its ghosts on 2026-09-18.
    static int Discard(string forId)
    {
        int gone = 0;
        array<ref OZS_AuthRec> live = Live();
        for (int i = live.Count() - 1; i >= 0; i--)
        {
            OZS_AuthRec rec = live.Get(i);
            if (rec.m_For != forId)
                continue;
            if (rec.m_Box)
            {
                gone = gone + DeleteTree(rec.m_Box);
                GetGame().ObjectDelete(rec.m_Box);
                OZ_Log.Info("storage: authority for " + forId + " discarded with " + gone.ToString() + " entity(ies)");
            }
            live.RemoveOrdered(i);
        }
        return gone;
    }

    // Everything under `root`, deepest first, root itself left alone.
    protected static int DeleteTree(EntityAI root)
    {
        array<EntityAI> nodes = new array<EntityAI>();
        array<int> parents = new array<int>();
        OZS_Records.Flatten(root, -1, nodes, parents);
        int gone = 0;
        // Flatten lists a parent before its children, so backwards is
        // deepest-first.
        for (int i = nodes.Count() - 1; i >= 1; i--)
        {
            if (nodes.Get(i))
            {
                GetGame().ObjectDelete(nodes.Get(i));
                gone++;
            }
        }
        return gone;
    }

    static string Status()
    {
        array<ref OZS_AuthRec> live = Live();
        string s = "authorities=" + live.Count();
        for (int i = 0; i < live.Count(); i++)
        {
            OZS_AuthRec rec = live.Get(i);
            s = s + " | " + rec.m_For;
            if (!rec.m_Box)
            {
                s = s + " GONE";
                continue;
            }
            // Handles of items that have left are dropped here, so the count
            // below is of live items and not of history.
            Forget(rec.m_Box);
            s = s + " " + rec.m_Box.GetType();
            s = s + " " + OZS_Const.StateName(rec.m_Box.OZS_GetState());
            s = s + " netid " + rec.m_Box.GetNetworkIDString();
            s = s + " roots " + rec.m_Box.OZS_CountEntities();
            s = s + " tree " + (OZS_Records.CountTree(rec.m_Box) - 1).ToString();
            s = s + " handles " + rec.m_Handles.Count();
        }
        return s;
    }
}

// One authority: the container, the id it stands for, and its handle table.
// Two parallel arrays rather than a map because Enforce's map wants a key it
// can hash, and an EntityAI is not one.
class OZS_AuthRec
{
    OZ_StorageBox m_Box;
    string m_For;
    ref array<EntityAI> m_Items;
    ref array<int> m_Handles;
    int m_Next;

    void OZS_AuthRec(OZ_StorageBox box, string forId)
    {
        m_Box = box;
        m_For = forId;
        m_Items = new array<EntityAI>();
        m_Handles = new array<int>();
        m_Next = 0;
    }
}
