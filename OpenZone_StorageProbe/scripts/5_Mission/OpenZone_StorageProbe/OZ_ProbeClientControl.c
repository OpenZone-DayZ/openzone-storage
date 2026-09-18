// Stand only, client side: a file the stand writes and the client polls once
// a second, because the bridge cannot reach the client and a headless stand
// has no mouse for the inventory screen's widgets.
//
//   $profile:OpenZone_StorageProbe/control.txt, first line:
//     search <text>   -- as if typed into the storage search bar
//     clear           -- empty the search
//     sort [n]        -- press the Sort button (change n to press again)
//     inventory [n]   -- open the inventory screen
//     take <cls> [n]  -- the nearest loose <cls> into the hands (predictive,
//                        the vanilla Take action's path)
//     into <cls> [n]  -- the nearest loose <cls> into the cargo of what the
//                        player holds (a drag onto the held item)
//     onto <cls> [n]  -- what the player holds into the cargo of the nearest
//                        loose <cls> (a drag onto an item on the ground)
//     grab <cls> [n]  -- the nearest <cls> that sits in some container's cargo
//                        into the hands (predictive)
//     stashto <cls> [n] -- what the player holds into the nearest <cls>'s cargo
//     drop [n]        -- what the player holds onto the ground (predictive)
//     stash [n]       -- move what the player holds into the nearest storage
//                        box, the predictive way the inventory screen does it
//     tree <r> [n]    -- the same tree the server's `oz_probe tree` prints,
//                        from this client's point of view, into scan.txt
//     scan <r> [n]    -- list every item THIS CLIENT has lying loose within r
//                        metres of the player: the server's own list of the
//                        same spot tells which of them are ghosts
#ifndef NO_GUI
class OZ_ProbeClientControl
{
    static const string FILE = "$profile:OpenZone_StorageProbe/control.txt";
    static const string SCAN_FILE = "$profile:OpenZone_StorageProbe/scan.txt";

    protected float  m_Timer;
    protected string m_Last;

    void OnFrame(float timeslice)
    {
        m_Timer = m_Timer + timeslice;
        if (m_Timer < 1.0)
            return;
        m_Timer = 0;
        if (!FileExist(FILE))
            return;
        FileHandle fh = OpenFile(FILE, FileMode.READ);
        if (fh == 0)
            return;
        string line;
        FGets(fh, line);
        CloseFile(fh);
        line = line.Trim();
        if (line == m_Last)
            return;
        m_Last = line;
        Apply(line);
    }

    protected void Apply(string line)
    {
        // WARNING, because INFO never reaches the retail client's .RPT.
        if (line.IndexOf("search ") == 0)
        {
            OZS_Search.Set(line.Substring(7, line.Length() - 7));
            ErrorEx("[OpenZone] probe control: search [" + OZS_Search.s_Text + "] version " + OZS_Search.s_Version, ErrorExSeverity.WARNING);
        }
        else if (line == "clear")
        {
            OZS_Search.Set("");
            ErrorEx("[OpenZone] probe control: search cleared", ErrorExSeverity.WARNING);
        }
        else if (line.IndexOf("sort") == 0)
        {
            OZS_ClientViewer.Get().RequestSort();
            ErrorEx("[OpenZone] probe control: sort requested", ErrorExSeverity.WARNING);
        }
        else if (line.IndexOf("inventory") == 0)
        {
            GetGame().GetMission().ShowInventory();
            ErrorEx("[OpenZone] probe control: inventory shown", ErrorExSeverity.WARNING);
        }
        else if (line.IndexOf("scan") == 0)
        {
            Scan(line);
        }
        else if (line.IndexOf("tree") == 0)
        {
            ClientTree(line);
        }
        else if (line.IndexOf("stash") == 0)
        {
            Stash(line);
        }
        else if (line.IndexOf("take ") == 0 || line.IndexOf("into ") == 0 || line.IndexOf("onto ") == 0)
        {
            Move(line);
        }
        else if (line.IndexOf("grab ") == 0 || line.IndexOf("stashto ") == 0 || line.IndexOf("drop") == 0)
        {
            RoundTrip(line);
        }
    }

    // Every loose item the client knows about near the player, one line each,
    // then a count. "Loose" = no hierarchy parent: in the world, not in any
    // inventory. The radius comes after the word ("scan 15"); anything after
    // the radius only makes the line differ so the same scan can run twice.
    // The three moves a player makes with the mouse to build a nested stack,
    // each through the same predictive call the inventory screen uses.
    protected void Move(string line)
    {
        array<string> parts = new array<string>();
        line.Split(" ", parts);
        string verb = parts.Get(0);
        string cls = "";
        if (parts.Count() > 1)
            cls = parts.Get(1);
        PlayerBase me = PlayerBase.Cast(GetGame().GetPlayer());
        string what = "=== " + verb + " [" + line + "] t=" + GetGame().GetTickTime();
        if (!me)
        {
            Note(what + ": no player");
            return;
        }
        EntityAI held = me.GetHumanInventory().GetEntityInHands();
        EntityAI loose = NearestLoose(me.GetPosition(), cls);
        if (!loose)
        {
            Note(what + ": no loose " + cls + " within 4 m");
            return;
        }
        bool ok = false;
        if (verb == "take")
        {
            me.PredictiveTakeEntityToHands(loose);
            Note(what + ": " + loose.GetType() + " -> hands (requested)");
            return;
        }
        if (!held)
        {
            Note(what + ": nothing in hands");
            return;
        }
        if (verb == "into")
        {
            ok = me.PredictiveTakeEntityToTargetCargo(held, loose);
            Note(what + ": " + loose.GetType() + " -> " + held.GetType() + " (held) predictive=" + ok);
            return;
        }
        ok = me.PredictiveTakeEntityToTargetCargo(loose, held);
        Note(what + ": " + held.GetType() + " (held) -> " + loose.GetType() + " predictive=" + ok);
    }

    protected EntityAI NearestLoose(vector at, string cls)
    {
        array<Object> objs = new array<Object>();
        GetGame().GetObjectsAtPosition(at, 4, objs, null);
        EntityAI best = null;
        float bestDist = 1000;
        for (int i = 0; i < objs.Count(); i++)
        {
            EntityAI e = EntityAI.Cast(objs.Get(i));
            if (!e || !e.IsKindOf(cls) || e.GetHierarchyParent())
                continue;
            float d = vector.Distance(e.GetPosition(), at);
            if (d < bestDist)
            {
                bestDist = d;
                best = e;
            }
        }
        return best;
    }

    // The owner's round trip, step by step: out of a container into the
    // hands, back into a container, onto the ground.
    protected void RoundTrip(string line)
    {
        array<string> parts = new array<string>();
        line.Split(" ", parts);
        string verb = parts.Get(0);
        string cls = "";
        if (parts.Count() > 1)
            cls = parts.Get(1);
        PlayerBase me = PlayerBase.Cast(GetGame().GetPlayer());
        string what = "=== " + verb + " [" + line + "] t=" + GetGame().GetTickTime();
        if (!me)
        {
            Note(what + ": no player");
            return;
        }
        EntityAI held = me.GetHumanInventory().GetEntityInHands();
        if (verb == "drop")
        {
            if (!held)
            {
                Note(what + ": nothing in hands");
                return;
            }
            me.PredictiveDropEntity(held);
            Note(what + ": " + held.GetType() + " -> ground (requested)");
            return;
        }
        if (verb == "grab")
        {
            EntityAI inside = NearestInCargo(me, cls);
            if (!inside)
            {
                Note(what + ": no " + cls + " inside a container within 4 m");
                return;
            }
            me.PredictiveTakeEntityToHands(inside);
            Note(what + ": " + inside.GetType() + " (in " + inside.GetHierarchyParent().GetType() + ") -> hands (requested)");
            return;
        }
        if (!held)
        {
            Note(what + ": nothing in hands");
            return;
        }
        EntityAI target = NearestLoose(me.GetPosition(), cls);
        if (!target)
        {
            Note(what + ": no loose " + cls + " within 4 m");
            return;
        }
        bool ok = me.PredictiveTakeEntityToTargetCargo(target, held);
        Note(what + ": " + held.GetType() + " -> " + target.GetType() + " predictive=" + ok);
    }

    // The nearest <cls> that is in the cargo of a container lying in the
    // world (not the player's own inventory).
    protected EntityAI NearestInCargo(PlayerBase me, string cls)
    {
        array<Object> objs = new array<Object>();
        GetGame().GetObjectsAtPosition(me.GetPosition(), 4, objs, null);
        for (int i = 0; i < objs.Count(); i++)
        {
            EntityAI box = EntityAI.Cast(objs.Get(i));
            if (!box || box.GetHierarchyParent() || box == me)
                continue;
            CargoBase cargo = box.GetInventory().GetCargo();
            if (!cargo)
                continue;
            for (int k = 0; k < cargo.GetItemCount(); k++)
            {
                EntityAI item = cargo.GetItem(k);
                if (item && item.IsKindOf(cls))
                    return item;
            }
        }
        return null;
    }

    protected void ClientTree(string line)
    {
        float radius = 8;
        array<string> parts = new array<string>();
        line.Split(" ", parts);
        if (parts.Count() > 1)
            radius = parts.Get(1).ToFloat();
        PlayerBase me = PlayerBase.Cast(GetGame().GetPlayer());
        if (!me)
            return;
        // The server's code is shared: OZ_ProbeState lives in 4_World, which
        // the client compiles too, and it only reads.
        string done = OZ_ProbeState.Tree(me.GetPosition(), radius, SCAN_FILE);
        Note("=== " + line + " -> " + done);
    }

    protected void Note(string text)
    {
        FileHandle nf = OpenFile(SCAN_FILE, FileMode.APPEND);
        if (nf == 0)
            return;
        FPrintln(nf, text);
        CloseFile(nf);
    }

    // What the player holds, into the nearest storage box's cargo, through
    // PredictiveTakeEntityToTargetCargo: the same call the inventory screen
    // makes when a player drags an item from the hands onto a container.
    protected void Stash(string line)
    {
        FileHandle stashFile = OpenFile(SCAN_FILE, FileMode.APPEND);
        PlayerBase me = PlayerBase.Cast(GetGame().GetPlayer());
        EntityAI held = null;
        if (me)
            held = me.GetHumanInventory().GetEntityInHands();
        OZ_StorageBox box = null;
        if (me)
        {
            array<Object> objs = new array<Object>();
            GetGame().GetObjectsAtPosition(me.GetPosition(), 6, objs, null);
            for (int i = 0; i < objs.Count(); i++)
            {
                OZ_StorageBox b = OZ_StorageBox.Cast(objs.Get(i));
                if (b)
                {
                    box = b;
                    break;
                }
            }
        }
        string what = "=== stash [" + line + "] t=" + GetGame().GetTickTime();
        if (!held)
            what = what + ": nothing in hands";
        else if (!box)
            what = what + ": no storage box within 6 m";
        else
            what = what + ": " + held.GetType() + " -> " + box.GetType() + " predictive=" + me.PredictiveTakeEntityToTargetCargo(box, held);
        if (stashFile != 0)
        {
            FPrintln(stashFile, what);
            CloseFile(stashFile);
        }
    }

    protected void Scan(string line)
    {
        float radius = 15;
        array<string> parts = new array<string>();
        line.Split(" ", parts);
        if (parts.Count() > 1)
            radius = parts.Get(1).ToFloat();
        PlayerBase me = PlayerBase.Cast(GetGame().GetPlayer());
        if (!me)
        {
            ErrorEx("[OpenZone] probe scan: no player", ErrorExSeverity.WARNING);
            return;
        }
        array<Object> objs = new array<Object>();
        GetGame().GetObjectsAtPosition(me.GetPosition(), radius, objs, null);
        // The .RPT of the retail client is flushed minutes late, so the scan
        // also goes to its own file, appended, one block per scan, the way the
        // client monitor writes client.log.
        FileHandle scanFile = OpenFile(SCAN_FILE, FileMode.APPEND);
        if (scanFile != 0)
            FPrintln(scanFile, "=== scan [" + line + "] t=" + GetGame().GetTickTime() + " at " + me.GetPosition().ToString(false));
        int loose = 0;
        for (int i = 0; i < objs.Count(); i++)
        {
            ItemBase it = ItemBase.Cast(objs.Get(i));
            if (!it)
                continue;
            if (it.GetHierarchyParent())
                continue;
            loose++;
            string row = "[OpenZone] probe scan: loose " + it.GetType() + " at " + it.GetPosition().ToString(false);
            row = row + " netid " + it.GetNetworkIDString();
            ErrorEx(row, ErrorExSeverity.WARNING);
            if (scanFile != 0)
                FPrintln(scanFile, "loose " + it.GetType() + " at " + it.GetPosition().ToString(false) + " netid " + it.GetNetworkIDString());
        }
        ErrorEx("[OpenZone] probe scan: " + loose + " loose item(s) within " + radius + " m of " + me.GetPosition().ToString(false), ErrorExSeverity.WARNING);
        if (scanFile != 0)
        {
            FPrintln(scanFile, "total " + loose + " loose within " + radius + " m");
            CloseFile(scanFile);
        }
    }
}
#endif
