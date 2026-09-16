// The server's one storage controller: the registry of boxes, the Open and
// Close requests, the per-frame stepping of jobs, the viewers, the auto-close
// and the boot reconciliation. A static singleton, reset at mission finish
// (statics survive a mission restart inside one process).
//
// Implemented so far: registry, Close (store + paced deletion), Open (paced
// materialisation with the list fallback), the deferred release of the files,
// the viewers, the auto-close, players leaving. The boot reconciliation
// arrives with the next task.

// One client's inventory screen showing one box.
class OZS_Viewer
{
    string m_PlayerId;
    string m_Name;
    OZ_StorageBox m_Box;
    float m_Seen;
}

class OZS_Controller
{
    protected static ref OZS_Controller s_Inst;
    protected static int s_IdSerial = 0;

    // Weak references on purpose: a deleted box reads null, and Unregister
    // runs from EEDelete anyway.
    protected ref array<OZ_StorageBox> m_Boxes;
    protected ref array<ref OZS_CloseJob> m_CloseJobs;
    protected ref array<ref OZS_OpenJob> m_OpenJobs;
    // Boxes opened since the engine last saved them: their files are still
    // the truth and go only after that save (spec section 7).
    protected ref array<string> m_FilesPending;
    // Boxes the engine has saved open: their files go at the tick time held.
    protected ref map<string, float> m_FilesRelease;
    // Who is looking at which box, as reported by the clients' inventory
    // screens (OZS_ClientViewer); pruned by time and distance.
    protected ref array<ref OZS_Viewer> m_Viewers;
    protected float m_AutoTimer;
    // The world's persistent entities load a few frames after
    // OnMissionStart (measured 2026-09-16), so the boot summary waits.
    protected float m_SummaryDue;
    protected int m_BootFilesWon;
    protected int m_BootClosedFromCargo;
    protected int m_BootEmptied;

    static OZS_Controller Get()
    {
        if (!s_Inst)
            s_Inst = new OZS_Controller();
        return s_Inst;
    }

    static void Reset()
    {
        s_Inst = null;
    }

    void OZS_Controller()
    {
        m_Boxes = new array<OZ_StorageBox>();
        m_CloseJobs = new array<ref OZS_CloseJob>();
        m_OpenJobs = new array<ref OZS_OpenJob>();
        m_FilesPending = new array<string>();
        m_FilesRelease = new map<string, float>();
        m_Viewers = new array<ref OZS_Viewer>();
        m_AutoTimer = 0;
        m_SummaryDue = -1;
    }

    // Mission start: the summary line goes out once the world has loaded.
    void OnMissionStarted()
    {
        m_SummaryDue = GetGame().GetTickTime() + OZS_Const.SUMMARY_DELAY;
    }

    protected void BootSummary()
    {
        Prune();
        string s = "storage: world loaded: boxes=" + m_Boxes.Count() + " open=" + OpenCount();
        s = s + " (boot rules: files won " + m_BootFilesWon + ", closed from cargo " + m_BootClosedFromCargo + ", emptied " + m_BootEmptied + ")";
        OZ_Log.Info(s);
    }

    // Box ids are the store key and must not repeat across restarts: UTC
    // date and time, a per-process serial and a random tail.
    static string NewId()
    {
        s_IdSerial++;
        int y;
        int mo;
        int d;
        int h;
        int mi;
        int s;
        GetYearMonthDayUTC(y, mo, d);
        GetHourMinuteSecondUTC(h, mi, s);
        string id = y.ToString() + Pad2(mo) + Pad2(d) + "-" + Pad2(h) + Pad2(mi) + Pad2(s);
        id = id + "-" + s_IdSerial + "-" + Math.RandomInt(1000, 9999);
        return id;
    }

    protected static string Pad2(int v)
    {
        if (v < 10)
            return "0" + v;
        return "" + v;
    }

    // ---- registry --------------------------------------------------------

    void Register(OZ_StorageBox box)
    {
        if (m_Boxes.Find(box) < 0)
            m_Boxes.Insert(box);
    }

    void Unregister(OZ_StorageBox box)
    {
        int i = m_Boxes.Find(box);
        if (i >= 0)
            m_Boxes.Remove(i);
    }

    int BoxCount()
    {
        Prune();
        return m_Boxes.Count();
    }

    int OpenCount()
    {
        Prune();
        int n = 0;
        for (int i = 0; i < m_Boxes.Count(); i++)
        {
            if (m_Boxes.Get(i).OZS_GetState() != OZS_Const.STATE_CLOSED)
                n++;
        }
        return n;
    }

    OZ_StorageBox FindById(string id)
    {
        Prune();
        for (int i = 0; i < m_Boxes.Count(); i++)
        {
            if (m_Boxes.Get(i).OZS_GetId() == id)
                return m_Boxes.Get(i);
        }
        return null;
    }

    OZ_StorageBox Nearest(vector pos, float radius)
    {
        Prune();
        OZ_StorageBox best;
        float bestDist = radius;
        for (int i = 0; i < m_Boxes.Count(); i++)
        {
            float d = vector.Distance(m_Boxes.Get(i).GetPosition(), pos);
            if (d <= bestDist)
            {
                bestDist = d;
                best = m_Boxes.Get(i);
            }
        }
        return best;
    }

    array<OZ_StorageBox> Boxes()
    {
        Prune();
        return m_Boxes;
    }

    // Drop the null slots a deleted box leaves behind.
    protected void Prune()
    {
        for (int i = m_Boxes.Count() - 1; i >= 0; i--)
        {
            if (!m_Boxes.Get(i))
                m_Boxes.RemoveOrdered(i);
        }
    }

    // ---- requests --------------------------------------------------------

    // Open = OPENING and a paced job that reads the store; a box without a
    // store opens at once.
    bool RequestOpen(OZ_StorageBox box, PlayerBase player, out string why)
    {
        int state = box.OZS_GetState();
        if (state != OZS_Const.STATE_CLOSED)
        {
            why = "#STR_OZS_OPENING";
            return false;
        }
        string who = Who(player);
        if (!OZS_Store.HasFiles(box.OZS_GetId()))
        {
            box.OZS_SetState(OZS_Const.STATE_OPEN);
            box.OZS_SetStoredCount(0);
            OZ_Log.Info("storage: box " + box.OZS_GetId() + " opened by " + who + " (no store, empty)");
            return true;
        }
        OZS_OpenJob job = new OZS_OpenJob(box, who);
        string err;
        if (!job.Begin(err))
        {
            OZ_Log.Error("storage: box " + box.OZS_GetId() + " cannot be opened by " + who + ": " + err);
            why = "#STR_OZS_OPEN_FAILED";
            return false;
        }
        m_OpenJobs.Insert(job);
        OZ_Log.Info("storage: box " + box.OZS_GetId() + " opening by " + who + " (" + box.OZS_GetStoredCount() + " stored)");
        return true;
    }

    // The open job finished: the files stay until the engine saves the box.
    void OnOpened(OZ_StorageBox box)
    {
        string id = box.OZS_GetId();
        if (m_FilesPending.Find(id) < 0)
            m_FilesPending.Insert(id);
    }

    protected OZS_OpenJob FindOpenJob(OZ_StorageBox box)
    {
        for (int i = 0; i < m_OpenJobs.Count(); i++)
        {
            if (m_OpenJobs.Get(i).IsFor(box))
                return m_OpenJobs.Get(i);
        }
        return null;
    }

    // Close = lock, capture, then delete over frames. The lock (state
    // CLOSING) comes first so that nothing enters or leaves the box after the
    // capture; a failed capture unlocks and refuses, and nothing is removed.
    bool RequestClose(OZ_StorageBox box, PlayerBase player, out string why)
    {
        // The player at the box has their inventory screen closed (actions
        // run from the world view), so their own stale entry does not count.
        return RequestCloseAs(box, Who(player), PlayerId(player), why);
    }

    bool RequestCloseAs(OZ_StorageBox box, string who, string exceptPlayerId, out string why)
    {
        int state = box.OZS_GetState();
        if (state != OZS_Const.STATE_OPEN)
        {
            why = "#STR_OZS_OPENING";
            return false;
        }
        if (HasViewers(box, exceptPlayerId))
        {
            why = "#STR_OZS_BUSY";
            return false;
        }
        OZS_CloseJob job = BeginClose(box, who, why);
        if (!job)
            return false;
        m_CloseJobs.Insert(job);
        return true;
    }

    // Synchronous close: files, then every entity in this frame. Mission
    // finish and the boot rules use it; players never do.
    bool CloseNow(OZ_StorageBox box, string who, out string why)
    {
        OZS_CloseJob job = BeginClose(box, who, why);
        if (!job)
            return false;
        job.Flush();
        return true;
    }

    protected OZS_CloseJob BeginClose(OZ_StorageBox box, string who, out string why)
    {
        OZS_CloseJob job = new OZS_CloseJob(box, who);
        string err;
        if (!job.Begin(err))
        {
            OZ_Log.Error("storage: box " + box.OZS_GetId() + " could not be closed by " + who + ": " + err);
            why = "#STR_OZS_STORE_FAILED";
            return null;
        }
        return job;
    }

    // ---- viewers ---------------------------------------------------------

    // A client's inventory screen started or stopped showing the box.
    void OnView(OZ_StorageBox box, PlayerIdentity sender, bool viewing)
    {
        if (!sender || !box)
            return;
        string pid = sender.GetId();
        OZS_Viewer v = FindViewer(box, pid);
        if (viewing)
        {
            if (v)
            {
                v.m_Seen = GetGame().GetTickTime();
                return;
            }
            v = new OZS_Viewer();
            v.m_PlayerId = pid;
            v.m_Name = sender.GetName();
            v.m_Box = box;
            v.m_Seen = GetGame().GetTickTime();
            m_Viewers.Insert(v);
            OZ_Log.Dbg("storage: " + v.m_Name + " looks at box " + box.OZS_GetId());
            return;
        }
        if (v)
        {
            m_Viewers.RemoveItem(v);
            OZ_Log.Dbg("storage: " + v.m_Name + " stops looking at box " + box.OZS_GetId());
        }
    }

    bool HasViewers(OZ_StorageBox box, string exceptPlayerId = "")
    {
        return ViewerCount(box, exceptPlayerId) > 0;
    }

    int ViewerCount(OZ_StorageBox box, string exceptPlayerId = "")
    {
        PruneViewers();
        int n = 0;
        for (int i = 0; i < m_Viewers.Count(); i++)
        {
            OZS_Viewer v = m_Viewers.Get(i);
            if (v.m_Box == box && v.m_PlayerId != exceptPlayerId)
                n++;
        }
        return n;
    }

    protected OZS_Viewer FindViewer(OZ_StorageBox box, string pid)
    {
        for (int i = 0; i < m_Viewers.Count(); i++)
        {
            OZS_Viewer v = m_Viewers.Get(i);
            if (v.m_Box == box && v.m_PlayerId == pid)
                return v;
        }
        return null;
    }

    // Drops entries whose box is gone, whose heartbeat stopped, or whose
    // player is no longer there or no longer within reach of the box.
    protected void PruneViewers()
    {
        if (m_Viewers.Count() == 0)
            return;
        OZS_Settings st = OZS_Settings.Get();
        float now = GetGame().GetTickTime();
        for (int i = m_Viewers.Count() - 1; i >= 0; i--)
        {
            OZS_Viewer v = m_Viewers.Get(i);
            string gone = "";
            if (!v.m_Box)
            {
                gone = "the box is gone";
            }
            else if (now - v.m_Seen > st.ViewerTimeoutSeconds)
            {
                gone = "no heartbeat for " + st.ViewerTimeoutSeconds + " s";
            }
            else
            {
                PlayerBase p = FindPlayer(v.m_PlayerId);
                if (!p)
                    gone = "the player is gone";
                else if (vector.Distance(p.GetPosition(), v.m_Box.GetPosition()) > st.ViewerMaxDistance)
                    gone = "the player is farther than " + st.ViewerMaxDistance + " m";
            }
            if (gone != "")
            {
                OZ_Log.Dbg("storage: viewer " + v.m_Name + " dropped: " + gone);
                m_Viewers.RemoveOrdered(i);
            }
        }
    }

    protected void DropViewersOf(string pid)
    {
        for (int i = m_Viewers.Count() - 1; i >= 0; i--)
        {
            if (m_Viewers.Get(i).m_PlayerId == pid)
                m_Viewers.RemoveOrdered(i);
        }
    }

    static PlayerBase FindPlayer(string pid)
    {
        array<Man> players = new array<Man>();
        GetGame().GetPlayers(players);
        for (int i = 0; i < players.Count(); i++)
        {
            Man m = players.Get(i);
            if (m && m.GetIdentity() && m.GetIdentity().GetId() == pid)
                return PlayerBase.Cast(m);
        }
        return null;
    }

    static string PlayerId(PlayerBase player)
    {
        if (!player || !player.GetIdentity())
            return "";
        return player.GetIdentity().GetId();
    }

    // Any connected player (other than `except`) within `radius` of the box.
    static bool AnyPlayerNear(OZ_StorageBox box, float radius, PlayerBase except)
    {
        array<Man> players = new array<Man>();
        GetGame().GetPlayers(players);
        for (int i = 0; i < players.Count(); i++)
        {
            Man m = players.Get(i);
            if (!m || m == except)
                continue;
            if (vector.Distance(m.GetPosition(), box.GetPosition()) <= radius)
                return true;
        }
        return false;
    }

    // ---- auto-close and players leaving --------------------------------

    // Every AUTO_TICK seconds: an OPEN box with nobody looking and nobody
    // within AutoCloseRadius for AutoCloseQuietSeconds closes itself.
    protected void AutoCloseTick()
    {
        Prune();
        OZS_Settings st = OZS_Settings.Get();
        float now = GetGame().GetTickTime();
        for (int i = 0; i < m_Boxes.Count(); i++)
        {
            OZ_StorageBox b = m_Boxes.Get(i);
            if (b.OZS_GetState() != OZS_Const.STATE_OPEN)
            {
                b.OZS_SetQuietSince(0);
                continue;
            }
            if (AnyPlayerNear(b, st.AutoCloseRadius, null) || HasViewers(b))
            {
                b.OZS_SetQuietSince(0);
                continue;
            }
            if (b.OZS_GetQuietSince() <= 0)
            {
                b.OZS_SetQuietSince(now);
                continue;
            }
            if (now - b.OZS_GetQuietSince() < st.AutoCloseQuietSeconds)
                continue;
            string why;
            if (!RequestCloseAs(b, "auto-close", "", why))
                OZ_Log.Warn("storage: box " + b.OZS_GetId() + " auto-close refused: " + why);
            b.OZS_SetQuietSince(0);
        }
    }

    // A player disconnects or dies: their viewer entries go, and an open box
    // within AutoCloseRadius of them with nobody else near or looking closes
    // now instead of after the quiet period.
    void OnPlayerGone(PlayerBase player, string how)
    {
        if (!player)
            return;
        string pid = PlayerId(player);
        if (pid != "")
            DropViewersOf(pid);
        Prune();
        OZS_Settings st = OZS_Settings.Get();
        for (int i = 0; i < m_Boxes.Count(); i++)
        {
            OZ_StorageBox b = m_Boxes.Get(i);
            if (b.OZS_GetState() != OZS_Const.STATE_OPEN)
                continue;
            if (vector.Distance(b.GetPosition(), player.GetPosition()) > st.AutoCloseRadius)
                continue;
            if (AnyPlayerNear(b, st.AutoCloseRadius, player) || HasViewers(b, pid))
                continue;
            string why;
            if (!RequestCloseAs(b, Who(player) + " (" + how + ")", pid, why))
                OZ_Log.Warn("storage: box " + b.OZS_GetId() + " could not close on " + how + " of " + Who(player) + ": " + why);
        }
    }

    protected OZS_CloseJob FindCloseJob(OZ_StorageBox box)
    {
        for (int i = 0; i < m_CloseJobs.Count(); i++)
        {
            if (m_CloseJobs.Get(i).IsFor(box))
                return m_CloseJobs.Get(i);
        }
        return null;
    }

    // ---- lifecycle hooks -------------------------------------------------

    // Every job gets the whole budget: two boxes closing in the same frame
    // is rare, and the budget is far under the frame criterion anyway.
    void OnFrame(float timeslice)
    {
        if (m_SummaryDue > 0 && GetGame().GetTickTime() >= m_SummaryDue)
        {
            m_SummaryDue = -1;
            BootSummary();
        }
        m_AutoTimer = m_AutoTimer + timeslice;
        if (m_AutoTimer >= OZS_Const.AUTO_TICK)
        {
            m_AutoTimer = 0;
            AutoCloseTick();
            ReleaseFiles();
        }
        if (m_CloseJobs.Count() == 0 && m_OpenJobs.Count() == 0)
            return;
        // The budgets are per frame and per server, not per box: several
        // boxes in flight share them, so four boxes opening at once cost the
        // frame the same as one.
        OZS_Settings st = OZS_Settings.Get();
        int closing = m_CloseJobs.Count();
        if (closing > 0)
        {
            float closeSec = st.CloseFrameBudgetMs * 0.001 / closing;
            int deletes = st.CloseDeletesPerFrame / closing;
            if (deletes < 1)
                deletes = 1;
            for (int i = closing - 1; i >= 0; i--)
            {
                if (m_CloseJobs.Get(i).Tick(closeSec, deletes))
                    m_CloseJobs.RemoveOrdered(i);
            }
        }
        int opening = m_OpenJobs.Count();
        if (opening > 0)
        {
            float openSec = st.OpenFrameBudgetMs * 0.001 / opening;
            float rate = st.OpenItemsPerSecond / opening;
            for (int j = opening - 1; j >= 0; j--)
            {
                if (m_OpenJobs.Get(j).Tick(openSec, rate, timeslice))
                    m_OpenJobs.RemoveOrdered(j);
            }
        }
    }

    // The engine is saving the box. Once it has saved an OPEN box, its cargo
    // is in the engine's own store and the files may go -- a little later,
    // because OnStoreSave runs while that save is still being written.
    void OnBoxSaved(OZ_StorageBox box)
    {
        if (m_FilesPending.Count() == 0)
            return;
        if (box.OZS_GetState() != OZS_Const.STATE_OPEN)
            return;
        string id = box.OZS_GetId();
        int i = m_FilesPending.Find(id);
        if (i < 0)
            return;
        m_FilesPending.Remove(i);
        m_FilesRelease.Set(id, GetGame().GetTickTime() + OZS_Const.RELEASE_DELAY);
    }

    protected void ReleaseFiles()
    {
        if (m_FilesRelease.Count() == 0)
            return;
        float now = GetGame().GetTickTime();
        array<string> due = new array<string>();
        for (int i = 0; i < m_FilesRelease.Count(); i++)
        {
            if (m_FilesRelease.GetElement(i) <= now)
                due.Insert(m_FilesRelease.GetKey(i));
        }
        for (int d = 0; d < due.Count(); d++)
        {
            string id = due.Get(d);
            m_FilesRelease.Remove(id);
            OZ_StorageBox box = FindById(id);
            // Closed again in the meantime: the new files are the truth, keep them.
            if (box && box.OZS_GetState() != OZS_Const.STATE_OPEN)
                continue;
            OZS_Store.Delete(id);
            OZ_Log.Dbg("storage: box " + id + " saved open by the engine; its files are released");
        }
    }

    // Boot, per loaded box (spec section 7): one truth. Files present: they
    // win, and the cargo of a half-done transition goes. No files: the
    // engine's own cargo is the truth and is closed into files now.
    void Reconcile(OZ_StorageBox box)
    {
        string id = box.OZS_GetId();
        int state = box.OZS_GetState();
        int entities = box.OZS_CountEntities();
        bool files = OZS_Store.HasFiles(id);
        string s = "storage: boot: box " + id + " " + OZS_Const.StateName(state) + " with " + entities + " entities, files=" + files;
        box.OZS_SetRestoring(false);
        box.OZS_SetQuietSince(0);

        if (files)
        {
            if (entities > 0)
            {
                array<EntityAI> stale = new array<EntityAI>();
                box.OZS_GetRoots(stale);
                for (int i = 0; i < stale.Count(); i++)
                {
                    if (stale.Get(i))
                        GetGame().ObjectDelete(stale.Get(i));
                }
                s = s + " -> files win, " + stale.Count() + " stale item(s) removed";
            }
            else
            {
                s = s + " -> files win";
            }
            int roots = OZS_Store.HeaderRoots(id);
            if (roots >= 0)
                box.OZS_SetStoredCount(roots);
            box.OZS_SetState(OZS_Const.STATE_CLOSED);
            if (entities > 0 || state != OZS_Const.STATE_CLOSED)
            {
                m_BootFilesWon++;
                OZ_Log.Info(s);
            }
            else
            {
                OZ_Log.Dbg(s);
            }
            return;
        }

        if (entities > 0)
        {
            string why;
            if (CloseNow(box, "boot", why))
            {
                m_BootClosedFromCargo++;
                OZ_Log.Info(s + " -> no files, closed from the engine's cargo");
            }
            else
            {
                box.OZS_SetState(OZS_Const.STATE_OPEN);
                OZ_Log.Error(s + " -> no files and the store cannot be written (" + why + "); the box stays open with its cargo");
            }
            return;
        }

        if (state != OZS_Const.STATE_CLOSED || box.OZS_GetStoredCount() > 0)
        {
            m_BootEmptied++;
            if (box.OZS_GetStoredCount() > 0)
                OZ_Log.Warn(s + " -> no files and nothing inside, but " + box.OZS_GetStoredCount() + " items were recorded: the store is missing; the box is empty now");
            else
                OZ_Log.Info(s + " -> no files and nothing inside; the box is empty now");
            box.OZS_SetState(OZS_Const.STATE_CLOSED);
            box.OZS_SetStoredCount(0);
            return;
        }
        OZ_Log.Dbg(s + " -> nothing to do");
    }

    // Mission finish: every open box is closed in this frame; a box already
    // closing finishes now.
    void CloseAll()
    {
        Prune();
        for (int i = 0; i < m_Boxes.Count(); i++)
        {
            OZ_StorageBox b = m_Boxes.Get(i);
            int state = b.OZS_GetState();
            if (state == OZS_Const.STATE_OPEN)
            {
                string why;
                if (!CloseNow(b, "mission finish", why))
                    OZ_Log.Error("storage: box " + b.OZS_GetId() + " stays open at mission finish: " + why);
            }
            else if (state == OZS_Const.STATE_CLOSING)
            {
                OZS_CloseJob job = FindCloseJob(b);
                if (job)
                    job.Flush();
            }
            else if (state == OZS_Const.STATE_OPENING)
            {
                OZS_OpenJob opening = FindOpenJob(b);
                if (opening)
                    opening.Cancel("mission finish");
            }
        }
        m_CloseJobs.Clear();
        m_OpenJobs.Clear();
    }

    // ---- reporting -------------------------------------------------------

    string Status()
    {
        Prune();
        PruneViewers();
        string s = "boxes=" + m_Boxes.Count() + " closing=" + m_CloseJobs.Count() + " opening=" + m_OpenJobs.Count() + " files_pending=" + m_FilesPending.Count();
        s = s + " viewers=" + m_Viewers.Count();
        for (int i = 0; i < m_Boxes.Count(); i++)
        {
            OZ_StorageBox b = m_Boxes.Get(i);
            s = s + " | " + b.GetType() + " " + b.OZS_GetId() + " " + OZS_Const.StateName(b.OZS_GetState());
            s = s + " entities=" + b.OZS_CountEntities() + " stored=" + b.OZS_GetStoredCount() + " viewers=" + ViewerCount(b);
        }
        return s;
    }

    static string Who(PlayerBase player)
    {
        if (!player)
            return "server";
        string name = player.OZS_GetName();
        if (name == "")
            return "a player";
        return name;
    }

    static void Notify(PlayerBase player, string text)
    {
        if (!player || !player.GetIdentity())
            return;
        NotificationSystem.SendNotificationToPlayerIdentityExtended(player.GetIdentity(), 4, "#STR_OZS_TITLE", text, "set:dayz_inventory image:cat_common_cargo");
    }
}
