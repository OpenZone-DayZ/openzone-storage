// Ghost hunting on the stand, 2026-09-18.
//
// A plain ObjectDelete of three nested trees (pouch > protector case > first
// aid kit > two bandages) that the owner's client had just laid out of a
// storage box left the top two levels of each behind: network id 0,
// inventory location UNKNOWN, still found by GetObjectsAtPosition, and a
// second ObjectDelete did nothing to them. The server refuses a juncture on
// anything set for deletion (junctures.c), which is how a client that still
// draws such an entity fails to pick it up; and a persistence save may keep
// it. This watcher logs every change of the entities around one spot with
// what the engine says about their deletion, so a live repro shows the
// moment and the state instead of a verdict read minutes later.
//
//   world_exec verb=oz_ghost args={"op":"watch","pos":"x y z","radius":"6","types":"PlateCarrierPouches,SmallProtectorCase,FirstAidKit,BandageDressing"}
//   world_exec verb=oz_ghost args={"op":"scan","pos":"x y z","radius":"6"}
//   world_exec verb=oz_ghost args={"op":"redelete","pos":"x y z","radius":"6","mode":"bottomup"}
//   world_exec verb=oz_ghost args={"op":"stop"}
class OZ_GhostWatch
{
    static const string FILE = "$profile:OpenZone_StorageProbe/ghost.txt";
    static const float PERIOD = 0.5;

    protected static ref OZ_GhostWatch s_Instance;

    protected bool m_Running;
    protected vector m_Pos;
    protected float m_Radius;
    protected string m_Types;
    protected float m_Until;
    protected float m_Timer;
    protected int m_Ticks;
    protected int m_Changes;
    protected ref map<string, int> m_Last;

    static OZ_GhostWatch Get()
    {
        if (!s_Instance)
            s_Instance = new OZ_GhostWatch();
        return s_Instance;
    }

    static void Reset()
    {
        s_Instance = null;
    }

    // ---- the verb ---------------------------------------------------------

    string Start(vector pos, float radius, string typesCsv, float seconds)
    {
        if (m_Running)
            Stop();
        m_Pos = pos;
        m_Radius = radius;
        m_Types = TypesFilter(typesCsv);
        m_Until = 0;
        if (seconds > 0)
            m_Until = GetGame().GetTickTime() + seconds;
        m_Timer = 0;
        m_Ticks = 0;
        m_Changes = 0;
        m_Last = new map<string, int>();
        m_Running = true;
        Append("=== watch at " + pos.ToString(false) + " r=" + radius + " types=" + m_Types + " " + Clock());
        int zombies;
        int lines = Diff(zombies);
        return "watching " + pos.ToString(false) + " r=" + radius + ": baseline " + lines + " entity(ies), " + zombies + " zombie(s) -> " + FILE;
    }

    string Stop()
    {
        if (!m_Running)
            return "not watching";
        m_Running = false;
        Append("=== watch stopped after " + m_Ticks + " tick(s), " + m_Changes + " change(s) " + Clock());
        return "stopped after " + m_Ticks + " tick(s), " + m_Changes + " change(s)";
    }

    string Scan(vector pos, float radius, string typesCsv)
    {
        map<string, int> now = Snapshot(pos, radius, TypesFilter(typesCsv));
        Append("=== scan at " + pos.ToString(false) + " r=" + radius + " " + Clock());
        int zombies = 0;
        int total = 0;
        for (int i = 0; i < now.Count(); i++)
        {
            string key = now.GetKey(i);
            int n = now.GetElement(i);
            total = total + n;
            if (IsZombieLine(key))
                zombies = zombies + n;
            Append(Times(n) + Mark(key) + key);
        }
        Append("total " + total + ", zombies " + zombies);
        return "scan: " + total + " entity(ies), " + zombies + " zombie(s) -> " + FILE;
    }

    // Tries one way of removing what ObjectDelete left behind, on every
    // zombie root within the radius; the next scan or watch tick tells
    // whether it worked.
    //   bottomup -- ObjectDelete each entity of the tree, deepest first
    //   delete   -- EntityAI.Delete(), the deferred vanilla path
    //   remote   -- RemoteObjectTreeDelete, then ObjectDelete
    string Redelete(vector pos, float radius, string mode)
    {
        array<Object> objs = new array<Object>();
        GetGame().GetObjectsAtPosition(pos, radius, objs, null);
        int roots = 0;
        int calls = 0;
        for (int i = 0; i < objs.Count(); i++)
        {
            EntityAI e = EntityAI.Cast(objs.Get(i));
            if (!e || e.GetHierarchyParent() || !IsZombie(e))
                continue;
            roots++;
            if (mode == "bottomup")
            {
                array<EntityAI> order = new array<EntityAI>();
                PostOrder(e, order);
                for (int k = 0; k < order.Count(); k++)
                {
                    GetGame().ObjectDelete(order.Get(k));
                    calls++;
                }
            }
            else if (mode == "delete")
            {
                e.Delete();
                calls++;
            }
            else if (mode == "remote")
            {
                GetGame().RemoteObjectTreeDelete(e);
                GetGame().ObjectDelete(e);
                calls = calls + 2;
            }
        }
        string s = "redelete " + mode + ": " + roots + " zombie root(s), " + calls + " call(s)";
        Append("=== " + s + " " + Clock());
        return s;
    }

    // ---- the frame --------------------------------------------------------

    void OnFrame(float timeslice)
    {
        if (!m_Running)
            return;
        m_Timer = m_Timer + timeslice;
        if (m_Timer < PERIOD)
            return;
        m_Timer = 0;
        if (m_Until > 0 && GetGame().GetTickTime() > m_Until)
        {
            Stop();
            return;
        }
        m_Ticks++;
        int zombies;
        Diff(zombies);
    }

    // Writes what appeared and what went since the last tick; returns how
    // many entities the new picture holds.
    protected int Diff(out int zombies)
    {
        map<string, int> now = Snapshot(m_Pos, m_Radius, m_Types);
        array<string> gone = new array<string>();
        array<string> came = new array<string>();
        int total = 0;
        zombies = 0;
        for (int i = 0; i < now.Count(); i++)
        {
            string key = now.GetKey(i);
            int n = now.GetElement(i);
            total = total + n;
            if (IsZombieLine(key))
                zombies = zombies + n;
            int was = 0;
            m_Last.Find(key, was);
            if (n > was)
                came.Insert(Times(n - was) + Mark(key) + key);
        }
        for (int j = 0; j < m_Last.Count(); j++)
        {
            string oldKey = m_Last.GetKey(j);
            int before = m_Last.GetElement(j);
            int left = 0;
            now.Find(oldKey, left);
            if (before > left)
                gone.Insert(Times(before - left) + Mark(oldKey) + oldKey);
        }
        m_Last = now;
        if (gone.Count() + came.Count() == 0)
            return total;
        m_Changes++;
        Append("--- t=" + GetGame().GetTickTime() + " " + Clock() + " entities=" + total + " zombies=" + zombies);
        for (int g = 0; g < gone.Count(); g++)
            Append("  - " + gone.Get(g));
        for (int c = 0; c < came.Count(); c++)
            Append("  + " + came.Get(c));
        return total;
    }

    // ---- the picture ------------------------------------------------------

    // Every entity around `pos`, as "line -> how many share it": roots from
    // the world's index and their subtrees, plus whatever the players there
    // carry. No positions in the line, so an item that only settles on the
    // ground is not a change.
    static map<string, int> Snapshot(vector pos, float radius, string types)
    {
        map<string, int> lines = new map<string, int>();
        array<Object> objs = new array<Object>();
        GetGame().GetObjectsAtPosition(pos, radius, objs, null);
        for (int i = 0; i < objs.Count(); i++)
        {
            Object o = objs.Get(i);
            PlayerBase player = PlayerBase.Cast(o);
            if (player)
            {
                array<EntityAI> carried = new array<EntityAI>();
                player.GetInventory().EnumerateInventory(InventoryTraversalType.PREORDER, carried);
                for (int k = 0; k < carried.Count(); k++)
                {
                    EntityAI c = carried.Get(k);
                    if (c && c != player && Wanted(c, types))
                        Add(lines, Line(c, objs, "player"));
                }
                continue;
            }
            EntityAI e = EntityAI.Cast(o);
            if (!e)
                continue;
            if (!ItemBase.Cast(e) && !OZ_StorageBox.Cast(e))
                continue;
            if (e.GetHierarchyParent())
            {
                // Indexed with a parent: the split state, its own line.
                Add(lines, Line(e, objs, "SPLIT"));
                continue;
            }
            Walk(e, objs, types, lines);
        }
        return lines;
    }

    static void Walk(EntityAI e, array<Object> indexed, string types, map<string, int> lines)
    {
        if (OZ_StorageBox.Cast(e) || Wanted(e, types))
            Add(lines, Line(e, indexed, ""));
        GameInventory inv = e.GetInventory();
        if (!inv)
            return;
        for (int a = 0; a < inv.AttachmentCount(); a++)
        {
            EntityAI att = inv.GetAttachmentFromIndex(a);
            if (att)
                Walk(att, indexed, types, lines);
        }
        CargoBase cargo = inv.GetCargo();
        if (!cargo)
            return;
        for (int c = 0; c < cargo.GetItemCount(); c++)
        {
            EntityAI item = cargo.GetItem(c);
            if (item)
                Walk(item, indexed, types, lines);
        }
    }

    // type #netid loc=N in=Parent#id, then only the flags that are set.
    static string Line(EntityAI e, array<Object> indexed, string where)
    {
        InventoryLocation il = new InventoryLocation();
        e.GetInventory().GetCurrentInventoryLocation(il);
        string s = e.GetType() + " #" + e.GetNetworkIDString() + " loc=" + il.GetType();
        EntityAI parent = e.GetHierarchyParent();
        if (parent)
            s = s + " in=" + parent.GetType() + "#" + parent.GetNetworkIDString();
        if (where != "")
            s = s + " " + where;
        if (indexed.Find(e) >= 0)
            s = s + " W";
        if (e.ToDelete())
            s = s + " TODELETE";
        if (e.IsPendingDeletion())
            s = s + " PENDING";
        if (e.IsPreparedToDelete())
            s = s + " PREPARED";
        if (e.IsSetForDeletion())
            s = s + " SETDEL";
        if (GetGame().HasInventoryJunctureItem(e))
            s = s + " JUNCTURE";
        if (e.GetInventory().IsInventoryLocked())
            s = s + " LOCKED";
        ItemBase ib = ItemBase.Cast(e);
        if (ib && ib.CanBeMovedOverride())
            s = s + " MOVABLE-OVERRIDE";
        OZ_StorageBox box = OZ_StorageBox.Cast(e);
        if (box)
            s = s + " box=" + box.OZS_GetId() + " " + OZS_Const.StateName(box.OZS_GetState());
        return s;
    }

    static bool IsZombie(EntityAI e)
    {
        if (e.GetNetworkIDString() == "00")
            return true;
        InventoryLocation il = new InventoryLocation();
        e.GetInventory().GetCurrentInventoryLocation(il);
        return il.GetType() == InventoryLocationType.UNKNOWN;
    }

    static bool IsZombieLine(string key)
    {
        return key.IndexOf(" #00 ") >= 0 || key.IndexOf(" loc=0") >= 0;
    }

    static void PostOrder(EntityAI e, array<EntityAI> order)
    {
        GameInventory inv = e.GetInventory();
        if (inv)
        {
            for (int a = 0; a < inv.AttachmentCount(); a++)
            {
                EntityAI att = inv.GetAttachmentFromIndex(a);
                if (att)
                    PostOrder(att, order);
            }
            CargoBase cargo = inv.GetCargo();
            if (cargo)
            {
                for (int c = 0; c < cargo.GetItemCount(); c++)
                {
                    EntityAI item = cargo.GetItem(c);
                    if (item)
                        PostOrder(item, order);
                }
            }
        }
        order.Insert(e);
    }

    // ---- small helpers ----------------------------------------------------

    static string TypesFilter(string csv)
    {
        if (csv == "")
            return "";
        array<string> parts = new array<string>();
        csv.Split(",", parts);
        string f = "|";
        for (int i = 0; i < parts.Count(); i++)
            f = f + parts.Get(i).Trim() + "|";
        return f;
    }

    static bool Wanted(EntityAI e, string types)
    {
        if (types == "")
            return ItemBase.Cast(e) != null;
        return types.IndexOf("|" + e.GetType() + "|") >= 0;
    }

    static void Add(map<string, int> lines, string key)
    {
        int n = 0;
        lines.Find(key, n);
        lines.Set(key, n + 1);
    }

    static string Times(int n)
    {
        if (n == 1)
            return "";
        return n.ToString() + "x ";
    }

    static string Mark(string key)
    {
        if (IsZombieLine(key))
            return "ZOMBIE ";
        return "";
    }

    static string Clock()
    {
        int h;
        int m;
        int s;
        GetHourMinuteSecond(h, m, s);
        return h.ToStringLen(2) + ":" + m.ToStringLen(2) + ":" + s.ToStringLen(2);
    }

    static void Append(string text)
    {
        FileHandle fh = OpenFile(FILE, FileMode.APPEND);
        if (fh == 0)
            return;
        FPrintln(fh, text);
        CloseFile(fh);
    }
}

// The watcher's frames and its reset. No `extends` on a modded class.
modded class MissionServer
{
    override void OnUpdate(float timeslice)
    {
        super.OnUpdate(timeslice);
        OZ_GhostWatch.Get().OnFrame(timeslice);
    }

    override void OnMissionFinish()
    {
        OZ_GhostWatch.Get().Stop();
        OZ_GhostWatch.Reset();
        super.OnMissionFinish();
    }
}
