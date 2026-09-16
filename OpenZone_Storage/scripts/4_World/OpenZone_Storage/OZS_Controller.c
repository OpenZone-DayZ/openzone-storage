// The server's one storage controller: the registry of boxes, the Open and
// Close requests, the per-frame stepping of jobs, the viewers, the auto-close
// and the boot reconciliation. A static singleton, reset at mission finish
// (statics survive a mission restart inside one process).
//
// Implemented so far: registry, Close with the store and the paced deletion.
// Open still flips to OPEN at once; the open job, the viewers and the boot
// reconciliation arrive with the later tasks.
class OZS_Controller
{
    protected static ref OZS_Controller s_Inst;
    protected static int s_IdSerial = 0;

    // Weak references on purpose: a deleted box reads null, and Unregister
    // runs from EEDelete anyway.
    protected ref array<OZ_StorageBox> m_Boxes;
    protected ref array<ref OZS_CloseJob> m_CloseJobs;

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

    bool RequestOpen(OZ_StorageBox box, PlayerBase player, out string why)
    {
        int state = box.OZS_GetState();
        if (state != OZS_Const.STATE_CLOSED)
        {
            why = "#STR_OZS_OPENING";
            return false;
        }
        box.OZS_SetState(OZS_Const.STATE_OPEN);
        OZ_Log.Info("storage: box " + box.OZS_GetId() + " opened by " + Who(player) + " (" + box.OZS_GetStoredCount() + " stored)");
        return true;
    }

    // Close = lock, capture, then delete over frames. The lock (state
    // CLOSING) comes first so that nothing enters or leaves the box after the
    // capture; a failed capture unlocks and refuses, and nothing is removed.
    bool RequestClose(OZ_StorageBox box, PlayerBase player, out string why)
    {
        int state = box.OZS_GetState();
        if (state != OZS_Const.STATE_OPEN)
        {
            why = "#STR_OZS_OPENING";
            return false;
        }
        if (HasViewers(box))
        {
            why = "#STR_OZS_BUSY";
            return false;
        }
        OZS_CloseJob job = BeginClose(box, Who(player), why);
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

    bool HasViewers(OZ_StorageBox box)
    {
        return false;
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
        if (m_CloseJobs.Count() == 0)
            return;
        OZS_Settings st = OZS_Settings.Get();
        float budgetSec = st.CloseFrameBudgetMs * 0.001;
        for (int i = m_CloseJobs.Count() - 1; i >= 0; i--)
        {
            if (m_CloseJobs.Get(i).Tick(budgetSec, st.CloseDeletesPerFrame))
                m_CloseJobs.RemoveOrdered(i);
        }
    }

    void OnBoxSaved(OZ_StorageBox box)
    {
    }

    void Reconcile(OZ_StorageBox box)
    {
        string s = "storage: box " + box.OZS_GetId() + " loaded as " + OZS_Const.StateName(box.OZS_GetState());
        s = s + " with " + box.OZS_CountEntities() + " entities, " + box.OZS_GetStoredCount() + " stored";
        OZ_Log.Dbg(s);
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
        }
        m_CloseJobs.Clear();
    }

    // ---- reporting -------------------------------------------------------

    string Status()
    {
        Prune();
        string s = "boxes=" + m_Boxes.Count() + " closing=" + m_CloseJobs.Count();
        for (int i = 0; i < m_Boxes.Count(); i++)
        {
            OZ_StorageBox b = m_Boxes.Get(i);
            s = s + " | " + b.GetType() + " " + b.OZS_GetId() + " " + OZS_Const.StateName(b.OZS_GetState());
            s = s + " entities=" + b.OZS_CountEntities() + " stored=" + b.OZS_GetStoredCount();
        }
        return s;
    }

    static string Who(PlayerBase player)
    {
        if (!player || !player.GetIdentity())
            return "server";
        return player.GetIdentity().GetName();
    }

    static void Notify(PlayerBase player, string text)
    {
        if (!player || !player.GetIdentity())
            return;
        NotificationSystem.SendNotificationToPlayerIdentityExtended(player.GetIdentity(), 4, "#STR_OZS_TITLE", text, "set:dayz_inventory image:cat_common_cargo");
    }
}
