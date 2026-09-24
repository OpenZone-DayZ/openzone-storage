// The server's one storage controller: the registry of boxes, the Open,
// Close and Sort requests, the per-frame stepping of jobs, the viewers, the
// auto-close, the boot exchange with the bridge and the gate that refuses
// every transition while the bridge is down (design 2026-09-19, sections 3.3
// and 6). A static singleton, reset at mission finish (statics survive a
// mission restart inside one process).

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
    // True from OnMissionFinish on: EEDelete cannot tell the world's teardown
    // from a box somebody blew up. Statics survive a mission restart, so it
    // is cleared on the way in as well as set on the way out.
    protected static bool s_Shutdown = false;

    // Weak references on purpose: a deleted box reads null, and Unregister
    // runs from EEDelete anyway.
    protected ref array<OZ_StorageBox> m_Boxes;
    protected ref array<ref OZS_CloseJob> m_CloseJobs;
    protected ref array<ref OZS_OpenJob> m_OpenJobs;
    // Who is looking at which box, as reported by the clients' inventory
    // screens (OZS_ClientViewer); pruned by time and distance.
    protected ref array<ref OZS_Viewer> m_Viewers;
    protected float m_AutoTimer;
    // The world's persistent entities load a few frames after
    // OnMissionStart (measured 2026-09-16), so the boot exchange waits.
    protected float m_SummaryDue;
    // The boot exchange: not done until the bridge answered; retried every
    // BOOT_RETRY seconds while it is down; every box is unavailable before.
    protected bool m_BootDone;
    protected bool m_BootInFlight;
    protected float m_BootRetryAt;
    protected bool m_BootWaitSaid;
    protected int m_BootSqlWon;
    protected int m_BootClosed;
    protected int m_BootNew;
    // The classes check waits for the boot closes: the bridge returns a
    // parked root only into a closed box, and a box the engine closes at
    // boot is closed in SQL only once its close job has finished.
    protected int m_BootClosesPending;
    protected ref array<string> m_PendingClasses;

    static OZS_Controller Get()
    {
        if (!s_Inst)
            s_Inst = new OZS_Controller();
        return s_Inst;
    }

    static void Reset()
    {
        s_Inst = null;
        OZS_Audit.Reset();
    }

    static bool IsShuttingDown()
    {
        return s_Shutdown;
    }

    static void SetShuttingDown(bool on)
    {
        s_Shutdown = on;
    }

    void OZS_Controller()
    {
        m_Boxes = new array<OZ_StorageBox>();
        m_CloseJobs = new array<ref OZS_CloseJob>();
        m_OpenJobs = new array<ref OZS_OpenJob>();
        m_Viewers = new array<ref OZS_Viewer>();
        m_AutoTimer = 0;
        m_SummaryDue = -1;
        m_BootDone = false;
        m_BootInFlight = false;
        m_BootRetryAt = -1;
        m_BootWaitSaid = false;
        m_BootClosesPending = 0;
    }

    // Mission start: the boot exchange goes out once the world has loaded.
    void OnMissionStarted()
    {
        s_Shutdown = false;
        OZS_Store.EnsureDirs();
        m_SummaryDue = GetGame().GetTickTime() + OZS_Const.SUMMARY_DELAY;
    }

    // ---- ids -------------------------------------------------------------

    // The fallback when the engine has not given a persistent id yet: UTC
    // date and time, a per-process serial and a random tail. Every box on
    // the stand and the live server gets the persistent id (OZ_StorageBox).
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

    // ---- the gate ----------------------------------------------------------

    // Transitions need the bridge: answered the boot exchange, and alive now.
    bool Ready()
    {
        if (!m_BootDone)
            return false;
        return OZS_Bridge.Up();
    }

    bool BootDone()
    {
        return m_BootDone;
    }

    // ---- requests --------------------------------------------------------

    bool RequestOpen(OZ_StorageBox box, PlayerBase player, out string why)
    {
        return RequestOpenAs(box, Who(player), Uid(player), why);
    }

    bool RequestOpenAs(OZ_StorageBox box, string who, string uid, out string why)
    {
        int state = box.OZS_GetState();
        if (state != OZS_Const.STATE_CLOSED)
        {
            why = "#STR_OZS_OPENING";
            return false;
        }
        // The other half of the rule in OZS_Proxies.Open: a box somebody is
        // looking at through a proxy must not also be materialised here, or
        // the same items would be in the world twice. An authority is exempt
        // -- it IS the proxy's box, and this is how it gets filled.
        if (!box.OZS_IsAuthority() && OZS_Proxies.Get().Find(box.OZS_GetId()))
        {
            why = "#STR_OZS_BUSY";
            return false;
        }
        if (!Ready())
        {
            why = "#STR_OZ_ERR_NO_BRIDGE";
            OZS_Audit.Log("unavailable", box.OZS_GetId(), uid, who, "", 0, -1, -1, "", "open refused: the bridge is down");
            return false;
        }
        OZS_OpenJob job = new OZS_OpenJob(box, who, uid);
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

    void OnOpened(OZ_StorageBox box)
    {
        box.OZS_SetTouchedAt(GetGame().GetTickTime());
    }

    void OnOpenFailed(OZ_StorageBox box, string uid, string why)
    {
        NotifyUid(uid, "#STR_OZS_OPEN_FAILED");
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

    bool RequestClose(OZ_StorageBox box, PlayerBase player, out string why)
    {
        return RequestCloseAs(box, Who(player), Uid(player), "player", PlayerId(player), why);
    }

    // `cause` is the bridge's word: player, idle, boot.
    bool RequestCloseAs(OZ_StorageBox box, string who, string uid, string cause, string exceptPlayerId, out string why)
    {
        int state = box.OZS_GetState();
        if (state != OZS_Const.STATE_OPEN)
        {
            why = "#STR_OZS_OPENING";
            return false;
        }
        if (!Ready())
        {
            why = "#STR_OZ_ERR_NO_BRIDGE";
            return false;
        }
        if (HasViewers(box, exceptPlayerId))
        {
            why = "#STR_OZS_BUSY";
            return false;
        }
        OZS_CloseJob job = BeginClose(box, who, uid, cause, why);
        if (!job)
            return false;
        m_CloseJobs.Insert(job);
        return true;
    }

    void RequestSort(OZ_StorageBox box, PlayerIdentity sender)
    {
        if (!sender || !box)
            return;
        PlayerBase player = FindPlayer(sender.GetId());
        string why;
        if (!RequestSortAs(box, sender.GetName(), sender.GetPlainId(), sender.GetId(), why))
        {
            Notify(player, why);
            return;
        }
        Notify(player, "#STR_OZS_SORTING");
    }

    bool RequestSortAs(OZ_StorageBox box, string who, string uid, string exceptPlayerId, out string why)
    {
        if (box.OZS_GetState() != OZS_Const.STATE_OPEN)
        {
            why = "#STR_OZS_OPENING";
            return false;
        }
        if (!Ready())
        {
            why = "#STR_OZ_ERR_NO_BRIDGE";
            return false;
        }
        float now = GetGame().GetTickTime();
        if (now - box.OZS_GetLastSort() < OZS_Const.SORT_COOLDOWN)
        {
            why = "#STR_OZS_SORT_WAIT";
            return false;
        }
        if (HasViewers(box, exceptPlayerId))
        {
            why = "#STR_OZS_BUSY";
            return false;
        }
        OZS_CloseJob job = new OZS_CloseJob(box, who + " (sort)", uid, "sort", true);
        string err;
        if (!job.Begin(err))
        {
            OZ_Log.Error("storage: box " + box.OZS_GetId() + " could not be sorted by " + who + ": " + err);
            why = "#STR_OZS_STORE_FAILED";
            return false;
        }
        box.OZS_SetLastSort(now);
        m_CloseJobs.Insert(job);
        OZS_Audit.Log("sort", box.OZS_GetId(), uid, who, "", 0, -1, -1, "", "");
        return true;
    }

    void OnClosed(OZ_StorageBox box, bool reopen, string who, string uid)
    {
        if (who == "boot")
            BootCloseDone();
        // A stash that is closed is a stash that is over: its contents are in
        // SQL now, so the empty invisible crate goes. A box stays where it was
        // placed and is never removed by a close.
        OZ_PersonalStash stash = OZ_PersonalStash.Cast(box);
        if (stash && !reopen)
        {
            OZS_Stashes.Drop(stash, who);
            return;
        }
        if (!reopen || !box)
            return;
        string why;
        if (!RequestOpenAs(box, who, uid, why))
            OZ_Log.Warn("storage: box " + box.OZS_GetId() + " could not reopen after the sort: " + why);
    }

    void OnCloseFailed(OZ_StorageBox box, string who, string uid, string why)
    {
        if (who == "boot")
            BootCloseDone();
        NotifyUid(uid, "#STR_OZS_STORE_FAILED");
    }

    protected OZS_CloseJob BeginClose(OZ_StorageBox box, string who, string uid, string cause, out string why)
    {
        OZS_CloseJob job = new OZS_CloseJob(box, who, uid, cause);
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
        // Exit one of the four: the window is shut. For a box that only means
        // the idle clock may now run out; for a stash it means the session is
        // over, and waiting sixty seconds to store a kit the player has
        // finished with would only widen the window a crash could take it in.
        OZ_PersonalStash stash = OZ_PersonalStash.Cast(box);
        if (stash && stash.OZS_OwnerUid() == UidOfIdentity(sender))
        {
            string why;
            if (!OZS_Stashes.Close(stash, "window", why))
                OZ_Log.Warn("storage: stash " + stash.OZS_GetId() + " (window) will not close: " + why);
        }
    }

    // The plain SteamID64 behind an identity, or "" -- the same spelling Uid()
    // gets off a PlayerBase.
    static string UidOfIdentity(PlayerIdentity who)
    {
        if (!who)
            return "";
        return who.GetPlainId();
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

    // The actor of an item moving in or out of a box: its single viewer,
    // with the Steam id of the player behind the screen; several viewers
    // are named in the note instead (design section 3.4).
    void ViewerWho(OZ_StorageBox box, out string uid, out string name, out string note)
    {
        uid = "";
        name = "";
        note = "";
        PruneViewers();
        int n = 0;
        string names = "";
        for (int i = 0; i < m_Viewers.Count(); i++)
        {
            OZS_Viewer v = m_Viewers.Get(i);
            if (v.m_Box != box)
                continue;
            n++;
            if (n == 1)
            {
                name = v.m_Name;
                PlayerBase p = FindPlayer(v.m_PlayerId);
                uid = Uid(p);
            }
            if (names != "")
                names = names + ", ";
            names = names + v.m_Name;
        }
        if (n > 1)
        {
            uid = "";
            name = "";
            note = "viewers: " + names;
        }
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

    // A refusal the player sees as a localised string, in words for the
    // admin's console.
    protected static string Words(string why)
    {
        if (why == "#STR_OZS_OPENING")
            return "the box is in a transition";
        if (why == "#STR_OZS_BUSY")
            return "someone is looking at the box";
        if (why == "#STR_OZ_ERR_NO_BRIDGE")
            return "the bridge is down";
        if (why == "#STR_OZS_STORE_FAILED")
            return "the close could not start";
        if (why == "#STR_OZS_OPEN_FAILED")
            return "the open could not start";
        return why;
    }

    // An admin closing a box now: whoever looks at it stops counting.
    protected void DropViewersOfBox(OZ_StorageBox box)
    {
        for (int i = m_Viewers.Count() - 1; i >= 0; i--)
        {
            if (m_Viewers.Get(i).m_Box == box)
                m_Viewers.RemoveOrdered(i);
        }
    }

    // ---- live commands of the admin side (design section 3.5) ----

    // report: where and how the box is. close: now, viewers or not. remove:
    // a closed box leaves the world (its versions stay in SQL). Every
    // command answers with an admin_result event carrying its ref.
    void AdminCommand(OZS_CommandLetter c)
    {
        OZ_StorageBox box = FindById(c.id);
        bool ok = false;
        string note = "";
        if (!box)
        {
            note = "no such box";
        }
        else if (c.cmd == "report")
        {
            ok = true;
            note = OZS_Const.StateName(box.OZS_GetState()) + " entities=" + box.OZS_CountEntities() + " stored=" + box.OZS_GetStoredCount();
            note = note + " viewers=" + ViewerCount(box) + " at " + box.GetPosition().ToString(false);
        }
        else if (c.cmd == "close")
        {
            if (box.OZS_GetState() != OZS_Const.STATE_OPEN)
            {
                note = "the box is " + OZS_Const.StateName(box.OZS_GetState()) + ", not open";
            }
            else
            {
                // Whoever looks at it stops counting: the admin's close goes
                // through even with a screen open on the box.
                DropViewersOfBox(box);
                string why;
                ok = RequestCloseAs(box, "admin " + c.by, "", "admin", "", why);
                if (ok)
                    note = "closing";
                else
                    note = Words(why);
            }
        }
        else if (c.cmd == "remove")
        {
            if (box.OZS_GetState() != OZS_Const.STATE_CLOSED)
            {
                note = "the box is " + OZS_Const.StateName(box.OZS_GetState()) + "; close it first";
            }
            else
            {
                ok = true;
                note = "removed from the world at " + box.GetPosition().ToString(false);
                GetGame().ObjectDelete(box);
            }
        }
        else
        {
            note = "unknown command " + c.cmd;
        }
        string verdict = "refused";
        if (ok)
            verdict = "ok";
        OZ_Log.Info("storage: admin " + c.by + " asked " + c.cmd + " of box " + c.id + ": " + verdict + " " + note);
        OZS_Audit.Log("admin_result", c.id, "", c.by, "", 0, -1, -1, "", c.token + ": " + verdict + " " + note);
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

    static PlayerBase FindPlayerByUid(string uid)
    {
        if (uid == "")
            return null;
        array<Man> players = new array<Man>();
        GetGame().GetPlayers(players);
        for (int i = 0; i < players.Count(); i++)
        {
            Man m = players.Get(i);
            if (m && m.GetIdentity() && m.GetIdentity().GetPlainId() == uid)
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

    // The Steam id: what the events and the bridge's history are keyed by.
    static string Uid(PlayerBase player)
    {
        if (!player || !player.GetIdentity())
            return "";
        return player.GetIdentity().GetPlainId();
    }

    // ---- auto-close and players leaving --------------------------------

    // Every AUTO_TICK seconds: an OPEN box closes AutoCloseSeconds after it
    // was last touched. While someone is still looking at the box, or the
    // bridge is down, the close waits; the tick tries again.
    protected void AutoCloseTick()
    {
        Prune();
        if (!Ready())
            return;
        OZS_Settings st = OZS_Settings.Get();
        float now = GetGame().GetTickTime();
        for (int i = 0; i < m_Boxes.Count(); i++)
        {
            OZ_StorageBox b = m_Boxes.Get(i);
            if (b.OZS_GetState() != OZS_Const.STATE_OPEN)
            {
                b.OZS_SetTouchedAt(0);
                continue;
            }
            // Exit two of the four (design section 4): away or idle. A stash
            // watches the distance as well as the clock, and it does not wait
            // for the viewer list -- its only viewer is its owner, and the
            // reason to close is that the owner has gone.
            OZ_PersonalStash st2 = OZ_PersonalStash.Cast(b);
            if (st2)
            {
                string cause;
                if (OZS_Stashes.ShouldClose(st2, now, cause))
                {
                    string stashWhy;
                    if (!OZS_Stashes.Close(st2, cause, stashWhy))
                        OZ_Log.Warn("storage: stash " + st2.OZS_GetId() + " (" + cause + ") will not close: " + stashWhy);
                }
                continue;
            }
            if (b.OZS_GetTouchedAt() <= 0)
            {
                b.OZS_SetTouchedAt(now);
                continue;
            }
            if (now - b.OZS_GetTouchedAt() < st.AutoCloseSeconds)
                continue;
            if (HasViewers(b))
                continue;
            string why;
            if (!RequestCloseAs(b, "auto-close", "", "idle", "", why))
                OZ_Log.Warn("storage: box " + b.OZS_GetId() + " auto-close refused: " + why);
        }
    }

    // Exits three and four of the four: the player disconnected or died.
    // Both arrive here through PlayerBase (OZS_Player.c), and OnDisconnect
    // runs BEFORE the character is saved, which is the window the design
    // asks for.
    void OnPlayerLeft(PlayerBase player)
    {
        if (!player)
            return;
        string pid = PlayerId(player);
        if (pid != "")
            DropViewersOf(pid);
        string uid = Uid(player);
        if (uid != "")
            OZS_Stashes.CloseAllOf(uid, "left");
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

    // ---- per frame -------------------------------------------------------

    void OnFrame(float timeslice)
    {
        float now = GetGame().GetTickTime();
        if (m_SummaryDue > 0 && now >= m_SummaryDue)
        {
            m_SummaryDue = -1;
            BootExchange();
        }
        if (!m_BootDone && !m_BootInFlight && m_BootRetryAt > 0 && now >= m_BootRetryAt)
        {
            m_BootRetryAt = -1;
            BootExchange();
        }
        m_AutoTimer = m_AutoTimer + timeslice;
        if (m_AutoTimer >= OZS_Const.AUTO_TICK)
        {
            m_AutoTimer = 0;
            AutoCloseTick();
        }
        OZS_Audit.Flush(now);
        if (m_CloseJobs.Count() == 0 && m_OpenJobs.Count() == 0)
            return;
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

    // ---- boot ------------------------------------------------------------

    // A box loaded from the world's save: nothing is decided here. The boot
    // exchange asks the bridge what SQL knows and applies the rules once.
    void Reconcile(OZ_StorageBox box)
    {
        box.OZS_SetRestoring(false);
        box.OZS_SetTouchedAt(0);
        OZ_Log.Dbg("storage: boot: box " + box.OZS_GetId() + " " + OZS_Const.StateName(box.OZS_GetState()) + " with " + box.OZS_CountEntities() + " entities, waiting for the bridge");
    }

    // Every box the engine has, with the state it remembers, to the bridge
    // (design section 3.3). Retried while the bridge is down.
    protected void BootExchange()
    {
        if (m_BootDone || m_BootInFlight)
            return;
        if (!OZS_Bridge.Up())
        {
            if (!m_BootWaitSaid)
            {
                OZ_Log.Warn("storage: boot: the bridge is down; every box is unavailable until it answers (asking every " + OZS_Const.BOOT_RETRY.ToString() + " s)");
                m_BootWaitSaid = true;
            }
            m_BootRetryAt = GetGame().GetTickTime() + OZS_Const.BOOT_RETRY;
            return;
        }
        Prune();
        OZS_BootLetter letter = new OZS_BootLetter();
        for (int i = 0; i < m_Boxes.Count(); i++)
        {
            OZ_StorageBox b = m_Boxes.Get(i);
            OZS_BootBox bb = new OZS_BootBox();
            bb.id = b.OZS_GetId();
            bb.cls = b.GetType();
            bb.state = OZS_Const.StateName(b.OZS_GetState());
            bb.entities = b.OZS_CountEntities();
            bb.pos = b.GetPosition().ToString(false);
            letter.boxes.Insert(bb);
        }
        string json;
        string err;
        if (!JsonFileLoader<OZS_BootLetter>.MakeData(letter, json, err, false))
        {
            OZ_Log.Error("storage: boot: the letter cannot be written: " + err);
            m_BootRetryAt = GetGame().GetTickTime() + OZS_Const.BOOT_RETRY;
            return;
        }
        m_BootInFlight = true;
        OZS_Bridge.Post(OZS_Const.ROUTE_BOOT, json, new OZS_BootReply());
        OZ_Log.Info("storage: boot: asking the bridge about " + m_Boxes.Count().ToString() + " box(es)");
    }

    // The bridge's answer, or the reason there is none.
    void OnBootAnswer(OZS_BootAnswer a, string failure)
    {
        m_BootInFlight = false;
        if (!a || !a.ok)
        {
            string why = failure;
            if (a)
                why = a.why;
            OZ_Log.Warn("storage: boot: the bridge did not answer the boot exchange (" + why + "); asking again in " + OZS_Const.BOOT_RETRY.ToString() + " s");
            m_BootRetryAt = GetGame().GetTickTime() + OZS_Const.BOOT_RETRY;
            return;
        }
        // The bridge answered: from here the gate is open, and the boot
        // closes below go through it like any other close.
        m_BootDone = true;
        m_BootWaitSaid = false;
        m_BootSqlWon = 0;
        m_BootClosed = 0;
        m_BootNew = 0;
        int answered = 0;
        if (a.boxes)
            answered = a.boxes.Count();
        for (int i = 0; i < answered; i++)
        {
            OZS_BootAnswerBox ab = a.boxes.Get(i);
            OZ_StorageBox box = FindById(ab.id);
            if (!box)
                continue;
            ApplyBootRule(box, ab);
        }
        string s = "storage: boot: " + answered.ToString() + " box(es) answered by the bridge: SQL won " + m_BootSqlWon.ToString();
        s = s + ", closed from the engine's cargo " + m_BootClosed.ToString() + ", new to the bridge " + m_BootNew.ToString();
        OZ_Log.Info(s);
        int classes = 0;
        if (a.classes)
            classes = a.classes.Count();
        if (m_BootClosesPending > 0)
        {
            m_PendingClasses = new array<string>();
            for (int c = 0; c < classes; c++)
                m_PendingClasses.Insert(a.classes.Get(c));
            OZ_Log.Info("storage: boot: the classes check waits for " + m_BootClosesPending.ToString() + " boot close(s)");
        }
        else
        {
            ClassesCheck(a.classes);
        }
        OZ_Log.Info("storage: world loaded: boxes=" + BoxCount() + " open=" + OpenCount() + " classes to check=" + classes.ToString());
        // AFTER the boot rule, never before: a stash that survived a crash
        // with cargo has just been queued for a boot close by ApplyBootRule,
        // and the sweep only has to deal with the ones that came back empty
        // or closed. Contents to SQL first, entity second -- the sweep never
        // deletes a stash itself, it asks for a close and OnClosed drops it.
        OZS_Stashes.Sweep();
    }

    // A boot close finished (or failed): once the last one has, the classes
    // check goes out and the bridge can return parked roots into the boxes
    // that are closed now.
    protected void BootCloseDone()
    {
        if (m_BootClosesPending > 0)
            m_BootClosesPending--;
        if (m_BootClosesPending > 0 || !m_PendingClasses)
            return;
        array<string> classes = m_PendingClasses;
        m_PendingClasses = null;
        ClassesCheck(classes);
    }

    // One box, one rule (design section 3.3): SQL wins over a half-done
    // transition; an OPEN box with cargo is the engine's truth and closes
    // into a new version; a box the bridge does not know starts from what
    // it holds.
    protected void ApplyBootRule(OZ_StorageBox box, OZS_BootAnswerBox ab)
    {
        int state = box.OZS_GetState();
        int entities = box.OZS_CountEntities();
        string id = box.OZS_GetId();
        string s = "storage: boot: box " + id + " " + OZS_Const.StateName(state) + " with " + entities.ToString() + " entities, bridge says " + ab.status;
        bool engineWins = false;
        if (entities > 0 && state == OZS_Const.STATE_OPEN)
            engineWins = true;
        if (entities > 0 && ab.status != "closed")
            engineWins = true;
        if (engineWins)
        {
            box.OZS_SetState(OZS_Const.STATE_OPEN);
            string why;
            if (RequestCloseAs(box, "boot", "", "boot", "", why))
            {
                m_BootClosed++;
                m_BootClosesPending++;
                OZ_Log.Info(s + " -> the engine's cargo is the truth, closing it into a new version");
            }
            else
            {
                OZ_Log.Error(s + " -> the engine's cargo is the truth but it cannot be closed (" + why + "); the box stays open");
            }
            return;
        }
        if (entities > 0)
        {
            array<EntityAI> stale = new array<EntityAI>();
            box.OZS_GetRoots(stale);
            for (int i = 0; i < stale.Count(); i++)
            {
                if (stale.Get(i))
                    GetGame().ObjectDelete(stale.Get(i));
            }
            m_BootSqlWon++;
            OZ_Log.Info(s + " -> SQL wins, " + stale.Count().ToString() + " stale item(s) of a half-done transition removed");
        }
        if (ab.status == "closed")
        {
            box.OZS_SetState(OZS_Const.STATE_CLOSED);
            box.OZS_SetStoredCount(ab.roots);
            if (entities == 0)
                OZ_Log.Dbg(s + " -> closed, " + ab.roots.ToString() + " stored");
            return;
        }
        if (ab.status == "open")
        {
            // The engine has nothing and SQL believes the box open. Two
            // different pasts look like this: the engine SAVED the box open
            // and empty (the players emptied it: a version with no roots
            // tells the bridge so), or the engine's save is older than the
            // open (a crash before an autosave: the engine never really had
            // the items, SQL's version is still the truth and wins).
            if (state == OZS_Const.STATE_OPEN)
            {
                string emptyWhy;
                if (RequestCloseAs(box, "boot", "", "boot", "", emptyWhy))
                {
                    m_BootClosesPending++;
                    OZ_Log.Info(s + " -> saved open and empty; a version with no roots closes it");
                }
                else
                    OZ_Log.Warn(s + " -> saved open and empty, and the empty close was refused (" + emptyWhy + ")");
                return;
            }
            m_BootSqlWon++;
            box.OZS_SetState(OZS_Const.STATE_CLOSED);
            box.OZS_SetStoredCount(ab.roots);
            // SQL still believes the box open: the open the crash cut short
            // never posted its `closed`, and until the next real close every
            // admin change would be refused with "the box is open".
            PostClosedAtBoot(id, ab.version);
            OZ_Log.Info(s + " -> the engine's save predates the open; SQL wins, " + ab.roots.ToString() + " stored");
            return;
        }
        m_BootNew++;
        box.OZS_SetState(OZS_Const.STATE_CLOSED);
        box.OZS_SetStoredCount(0);
        OZ_Log.Info(s + " -> new to the bridge, empty");
    }

    // The `closed` of a close the engine never made (measured 2026-09-19: a
    // kill right after an open left SQL saying open through the next boot).
    protected void PostClosedAtBoot(string id, int version)
    {
        OZS_IdLetter ack = new OZS_IdLetter();
        ack.id = id;
        ack.by = "boot";
        ack.version = version;
        string json;
        string err;
        if (JsonFileLoader<OZS_IdLetter>.MakeData(ack, json, err, false))
            OZS_Bridge.Post(OZS_Const.ROUTE_CLOSED, json, new OZS_AckReply("closed at boot"));
    }

    // Which of the classes SQL holds exist on this server: the bridge parks
    // the roots of the missing ones and returns parked roots whose classes
    // are back (design section 3.3, step 4).
    protected void ClassesCheck(array<string> classes)
    {
        OZS_ClassesLetter letter = new OZS_ClassesLetter();
        int n = 0;
        if (classes)
            n = classes.Count();
        for (int i = 0; i < n; i++)
        {
            string t = classes.Get(i);
            if (t == "")
                continue;
            // Items live in CfgVehicles, weapons in CfgWeapons, magazines and
            // ammunition piles in CfgMagazines.
            bool exists = GetGame().ConfigIsExisting("CfgVehicles " + t);
            if (!exists)
                exists = GetGame().ConfigIsExisting("CfgWeapons " + t);
            if (!exists)
                exists = GetGame().ConfigIsExisting("CfgMagazines " + t);
            if (exists)
                letter.present.Insert(t);
            else
                letter.missing.Insert(t);
        }
        if (letter.missing.Count() > 0)
        {
            string names = "";
            for (int m = 0; m < letter.missing.Count(); m++)
            {
                if (m > 0)
                    names = names + ", ";
                names = names + letter.missing.Get(m);
            }
            OZ_Log.Warn("storage: boot: " + letter.missing.Count().ToString() + " stored class(es) do not exist on this server: " + names + "; their roots are parked by the bridge");
        }
        string json;
        string err;
        if (!JsonFileLoader<OZS_ClassesLetter>.MakeData(letter, json, err, false))
        {
            OZ_Log.Error("storage: boot: the classes letter cannot be written: " + err);
            return;
        }
        OZS_Bridge.Post(OZS_Const.ROUTE_CLASSES, json, new OZS_ClassesReply());
    }

    // ---- mission finish ------------------------------------------------------

    // No close can complete here (a close waits for the bridge and there is
    // no frame loop to wait in), so an open box stays open in the engine's
    // own save and the boot rules close it next time. A deletion already
    // under way completes, an open under way is undone.
    void CloseAll()
    {
        Prune();
        for (int i = 0; i < m_Boxes.Count(); i++)
        {
            OZ_StorageBox b = m_Boxes.Get(i);
            int state = b.OZS_GetState();
            if (state == OZS_Const.STATE_CLOSING)
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
            else if (state == OZS_Const.STATE_OPEN)
            {
                OZ_Log.Info("storage: box " + b.OZS_GetId() + " stays open at mission finish with " + b.OZS_CountEntities() + " entities; the boot rules close it");
            }
        }
        m_CloseJobs.Clear();
        m_OpenJobs.Clear();
    }

    string Status()
    {
        Prune();
        PruneViewers();
        string s = "boxes=" + m_Boxes.Count() + " closing=" + m_CloseJobs.Count() + " opening=" + m_OpenJobs.Count();
        s = s + " viewers=" + m_Viewers.Count() + " bridge=" + OZS_Bridge.Up() + " boot_done=" + m_BootDone;
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

    static void NotifyUid(string uid, string text)
    {
        PlayerBase p = FindPlayerByUid(uid);
        if (p)
            Notify(p, text);
    }
}
