// The server's one storage controller: the registry of placed boxes, the
// fill of an authority from SQL (the open job), the boot exchange with the
// bridge, the gate that refuses every fill while the bridge is down, and the
// admin's live commands (design 2026-09-19, sections 3.3 and 6; proxy design
// 2026-09-24). A static singleton, reset at mission finish (statics survive
// a mission restart inside one process).
//
// WHAT LEFT ON 2026-09-26, with the old scheme: the close job that captured
// a placed box's cargo into SQL, the auto-close that ran it on idle boxes,
// the boot close of a box saved open with cargo, and the viewer list. Under
// the proxy a placed box never holds anything -- the contents live in an
// authority the session writes per turn and whole at its end -- so there is
// nothing left for a close to capture.

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
    protected ref array<ref OZS_OpenJob> m_OpenJobs;
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
    protected int m_BootNew;

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
        m_OpenJobs = new array<ref OZS_OpenJob>();
        m_SummaryDue = -1;
        m_BootDone = false;
        m_BootInFlight = false;
        m_BootRetryAt = -1;
        m_BootWaitSaid = false;
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

    // ---- the fill --------------------------------------------------------

    // The open job on an authority: SQL's record read root by root into the
    // container a session stands on. `box` is that authority; a placed box
    // is never filled any more (OZS_Proxies.Open refuses one that is not
    // CLOSED, and nothing opens one).
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

    void OnOpenFailed(OZ_StorageBox box, string uid, string why)
    {
        NotifyUid(uid, "#STR_OZS_OPEN_FAILED");
        // A FILL THAT FAILED IS A SESSION THAT CANNOT GO ON: its watchers
        // would wait for a stream that never begins, and the box would be
        // dead for them until they relogged (review 2026-09-26, C2).
        if (box && box.OZS_IsAuthority())
        {
            OZS_Session s = OZS_Proxies.Get().Find(box.OZS_GetId());
            if (s)
                s.OnFillFailed(why);
        }
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

    // A SORT IS A TURN OF THE SESSION (owner, 2026-09-26): OZS_Ops.Sort plans
    // the layout, sends it as one absolute letter, and the authority is
    // refilled from the record by the same open job that filled it in the
    // first place -- item by item, on the frame budget. The old scheme's sort
    // closed the placed box with new cells and opened it again into the
    // world; it went with the close job.

    // ---- who ---------------------------------------------------------------

    // The plain SteamID64 behind an identity, or "" -- the same spelling Uid()
    // gets off a PlayerBase.
    static string UidOfIdentity(PlayerIdentity who)
    {
        if (!who)
            return "";
        return who.GetPlainId();
    }

    // THE VIEWER LIST IS GONE, and with it OnView, FindViewer, HasViewers and
    // ViewerCount (owner, 2026-09-26).
    //
    // It answered one question the old scheme could not answer otherwise: is
    // anybody browsing this box right now, so that a close waits. The client
    // had to tell us, because the server cannot see an inventory screen. Under
    // the proxy the question is answered exactly by a session's watchers, and
    // the placed box is never open, so there was nothing left to wait for.


    // WHO MOVED IT: THE SESSION'S WATCHERS, NOT A VIEWER LIST.
    //
    // The audit needs a name for every put and take, and until now it asked a
    // list the client filled by scanning its vicinity. That list was keyed on
    // the PLACED box, while the items move in the AUTHORITY -- so it never
    // matched, and every put and take in the box's history was written with no
    // uid and no name at all (visible in any history page: `put | | Ammo_22`).
    //
    // The session already knows exactly who is looking, and one of them did it.
    // Several watchers are named in the note, as before, because with two
    // people in one box the mod cannot say which of them it was.
    void ViewerWho(OZ_StorageBox box, out string uid, out string name, out string note)
    {
        uid = "";
        name = "";
        note = "";
        if (!box)
            return;
        OZS_Session s = OZS_Proxies.Get().Find(box.OZS_GetId());
        if (!s)
            return;
        int n = 0;
        string names = "";
        for (int i = 0; i < s.m_Watchers.Count(); i++)
        {
            OZS_Watcher w = s.m_Watchers.Get(i);
            if (!w)
                continue;
            n++;
            if (n == 1)
            {
                uid = w.m_Uid;
                name = w.Name();
            }
            if (names != "")
                names = names + ", ";
            names = names + w.Name();
        }
        if (n > 1)
        {
            uid = "";
            name = "";
            note = "viewers: " + names;
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

    // ---- live commands of the admin side (design section 3.5) ----

    // report: where and how the box is. close: now, viewers or not. remove:
    // a closed box leaves the world (its versions stay in SQL). Every
    // command answers with an admin_result event carrying its ref.
    void AdminCommand(OZS_CommandLetter c)
    {
        OZ_StorageBox box = FindById(c.id);
        // THE SESSION, WHEN THERE IS ONE. Under the proxy the placed box is
        // never open -- the contents live in an authority nobody is told
        // about -- so "is anybody in it" is a question for OZS_Proxies, not
        // for the box's state. Asked of the box, a close answered "not open"
        // for every box that was in use, and a remove deleted boxes that
        // were (review 2026-09-26, E1, E2). A stash has no placed box at
        // all, which is why the session is looked up first.
        OZS_Session live = OZS_Proxies.Get().Find(c.id);
        bool ok = false;
        string note = "";
        if (c.cmd == "close" && live)
        {
            int inIt = live.m_Watchers.Count();
            live.Close("admin " + c.by);
            ok = true;
            note = "the session is ending; " + inIt.ToString() + " watcher(s) were sent away";
        }
        else if (c.cmd == "report" && !box && live)
        {
            ok = true;
            note = "no placed box | session: " + live.Status();
        }
        else if (!box)
        {
            note = "no such box";
        }
        else if (c.cmd == "report")
        {
            ok = true;
            note = OZS_Const.StateName(box.OZS_GetState()) + " entities=" + box.OZS_CountEntities() + " stored=" + box.OZS_GetStoredCount();
            note = note + " at " + box.GetPosition().ToString(false);
            if (live)
                note = note + " | session: " + live.Status();
        }
        else if (c.cmd == "close")
        {
            note = "nobody is in the box; there is nothing to close";
        }
        else if (c.cmd == "remove")
        {
            if (live)
            {
                note = "the box is in use by " + live.m_Watchers.Count().ToString() + " player(s); close it first";
            }
            else if (box.OZS_GetState() != OZS_Const.STATE_CLOSED)
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

    // The Steam id: what the events and the bridge's history are keyed by.
    static string Uid(PlayerBase player)
    {
        if (!player || !player.GetIdentity())
            return "";
        return player.GetIdentity().GetPlainId();
    }

    // A player disconnecting or dying reaches the proxy directly
    // (OZS_Player.c -> OZS_Proxies.DropPlayer); there is nothing left for the
    // controller to do about it. The auto-close tick stood here until
    // 2026-09-26, closing placed boxes nobody had touched; under the proxy a
    // session ends when its last watcher leaves (OZS_Session.IsDone).

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
        OZS_Audit.Flush(now);
        if (m_OpenJobs.Count() == 0)
            return;
        OZS_Settings st = OZS_Settings.Get();
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
        // The bridge answered: from here the gate is open.
        m_BootDone = true;
        m_BootWaitSaid = false;
        m_BootSqlWon = 0;
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
        s = s + ", new to the bridge " + m_BootNew.ToString();
        OZ_Log.Info(s);
        int classes = 0;
        if (a.classes)
            classes = a.classes.Count();
        ClassesCheck(a.classes);
        OZ_Log.Info("storage: world loaded: boxes=" + BoxCount() + " open=" + OpenCount() + " classes to check=" + classes.ToString());
    }

    // One box, one rule (design section 3.3, as it stands under the proxy):
    // a placed box holds nothing, so whatever the engine's save put in it is
    // the leftover of a scheme this build no longer runs; SQL's record is
    // the truth, and a box the bridge does not know starts empty.
    protected void ApplyBootRule(OZ_StorageBox box, OZS_BootAnswerBox ab)
    {
        int state = box.OZS_GetState();
        int entities = box.OZS_CountEntities();
        string id = box.OZS_GetId();
        string s = "storage: boot: box " + id + " " + OZS_Const.StateName(state) + " with " + entities.ToString() + " entities, bridge says " + ab.status;
        if (entities > 0)
        {
            // NOTHING IS WRITTEN AND NOTHING IS DELETED. The close job that
            // used to capture such cargo into a new version went with the
            // old scheme (2026-09-26), and deleting is not this mod's to
            // decide. The items stay in the placed box for an admin to look
            // at; the box itself is closed, because that is what it is.
            OZ_Log.Error(s + " -> a placed box holds cargo, which no build since 2026-09-24 puts there; it is left as it is for an admin");
        }
        if (ab.status == "closed")
        {
            box.OZS_SetState(OZS_Const.STATE_CLOSED);
            box.OZS_SetStoredCount(ab.roots);
            if (ab.held_by != "")
                OZ_Log.Info(s + " -> closed here, " + ab.roots.ToString() + " stored; server " + ab.held_by + " holds it open and every open from here is refused until it lets go");
            else if (entities == 0)
                OZ_Log.Dbg(s + " -> closed, " + ab.roots.ToString() + " stored");
            return;
        }
        if (ab.status == "open")
        {
            // SQL believes the box open: a session the last run never ended
            // -- a crash, a kill -- and whose `closed` therefore never went.
            // The record is at the last confirmed turn and is the truth;
            // until the bridge hears `closed`, every admin change would be
            // refused with "the box is open".
            m_BootSqlWon++;
            box.OZS_SetState(OZS_Const.STATE_CLOSED);
            box.OZS_SetStoredCount(ab.roots);
            PostClosedAtBoot(id, ab.version);
            OZ_Log.Info(s + " -> a session the last run never ended; SQL wins, " + ab.roots.ToString() + " stored");
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

    // A fill under way is undone. The sessions have already ended
    // (OZS_Proxies.EndAll runs first), so the only open jobs left are those
    // of authorities that never finished filling, and nothing half-restored
    // survives.
    void CloseAll()
    {
        Prune();
        for (int i = 0; i < m_Boxes.Count(); i++)
        {
            OZ_StorageBox b = m_Boxes.Get(i);
            if (b.OZS_GetState() == OZS_Const.STATE_OPENING)
            {
                OZS_OpenJob opening = FindOpenJob(b);
                if (opening)
                    opening.Cancel("mission finish");
            }
        }
        m_OpenJobs.Clear();
    }

    string Status()
    {
        Prune();
        string s = "boxes=" + m_Boxes.Count() + " opening=" + m_OpenJobs.Count();
        s = s + " bridge=" + OZS_Bridge.Up() + " boot_done=" + m_BootDone;
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
