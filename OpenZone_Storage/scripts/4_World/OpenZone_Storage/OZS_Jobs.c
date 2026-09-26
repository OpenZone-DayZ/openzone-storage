// The fill of an authority as a job that runs a slice per frame and waits
// for one bridge reply (design 2026-09-19, section 3.2; proxy design
// 2026-09-24): ask the bridge, read the cache it names root by root at the
// item rate, build each root local into the unannounced container. A root
// the engine cannot read is deleted with everything this open created so
// far, parked with the bridge, and the open is asked again; a half-restored
// box is never left standing.
//
// The close job -- the other transition, a placed box's cargo captured into
// SQL -- stood here until 2026-09-26 and went with the old scheme.

class OZS_OpenJob
{
    static const int PHASE_REQUEST = 0;
    static const int PHASE_READ    = 1;
    static const int PHASE_PARK    = 2;
    static const int PHASE_DONE    = 3;

    protected OZ_StorageBox m_Box;
    protected string m_Id;
    protected string m_Who;
    protected string m_Uid;
    protected int m_Phase;
    protected ref FileSerializer m_File;
    protected string m_FileName;
    protected int m_SaveVer;
    protected string m_Stamp;
    protected int m_Roots;
    protected int m_Entities;
    protected int m_Next;
    protected int m_M0;
    protected int m_M1;
    protected int m_M2;
    protected int m_M3;
    protected float m_Tokens;
    protected float m_Started;
    protected float m_Posted;
    protected int m_Frames;
    protected float m_WorkMs;
    protected float m_MaxStepMs;
    protected int m_Created;
    protected int m_Missed;
    protected int m_Attempt;
    protected int m_Parked;
    protected ref array<ref OZS_Move> m_Moves;
    protected bool m_Answered;
    protected ref OZS_OpenAnswer m_Answer;
    protected string m_Failure;
    protected bool m_ParkAnswered;
    protected bool m_ParkOk;
    protected string m_ParkWhy;

    void OZS_OpenJob(OZ_StorageBox box, string who, string uid)
    {
        m_Moves = new array<ref OZS_Move>();
        m_Box = box;
        m_Who = who;
        m_Uid = uid;
        m_Phase = PHASE_REQUEST;
        m_Attempt = 0;
        m_Parked = 0;
        m_Created = 0;
        m_Missed = 0;
        m_Stamp = "";
    }

    bool IsFor(OZ_StorageBox box)
    {
        return m_Box == box;
    }

    static string R1(float v)
    {
        float r = Math.Round(v * 10) / 10;
        return r.ToString();
    }

    bool Begin(out string why)
    {
        m_Id = m_Box.OZS_GetId();
        m_Started = GetGame().GetTickTime();
        m_Box.OZS_SetState(OZS_Const.STATE_OPENING);
        m_Box.OZS_SetRestoring(true);
        // The order the record lists the roots in is the order they are read;
        // a per-operation commit names a root by that position (§7).
        m_Box.OZS_ForgetRoots();
        m_Tokens = 0;
        m_Next = 0;
        if (!OZS_Bridge.Up())
        {
            m_Box.OZS_SetRestoring(false);
            m_Box.OZS_SetState(OZS_Const.STATE_CLOSED);
            why = "the bridge is down";
            return false;
        }
        Request();
        return true;
    }

    protected void Request()
    {
        m_Attempt++;
        // A retry re-reads the whole record from the beginning, so whatever
        // root positions were noted belong to a file that is being replaced.
        if (m_Box)
            m_Box.OZS_ForgetRoots();
        OZS_IdLetter letter = new OZS_IdLetter();
        letter.id = m_Id;
        letter.by = m_Uid;
        string json;
        string err;
        m_Answered = false;
        m_Answer = null;
        m_Failure = "";
        m_Posted = GetGame().GetTickTime();
        m_Phase = PHASE_REQUEST;
        if (!JsonFileLoader<OZS_IdLetter>.MakeData(letter, json, err, false))
        {
            OnOpenFailed("the open letter cannot be written: " + err);
            return;
        }
        OZS_Bridge.Post(OZS_Const.ROUTE_OPEN, json, new OZS_OpenReply(this));
    }

    void OnOpenAnswer(OZS_OpenAnswer a)
    {
        if (m_Phase != PHASE_REQUEST)
            return;
        m_Answered = true;
        m_Answer = a;
    }

    void OnOpenFailed(string why)
    {
        if (m_Phase != PHASE_REQUEST)
            return;
        m_Answered = true;
        m_Answer = null;
        m_Failure = why;
    }

    void OnParked(bool ok, string why)
    {
        if (m_Phase != PHASE_PARK)
            return;
        m_ParkAnswered = true;
        m_ParkOk = ok;
        m_ParkWhy = why;
    }

    bool Tick(float budgetSec, float rate, float timeslice)
    {
        if (!m_Box)
        {
            Abort();
            int dropped = OZS_Records.DropMoves(m_Moves);
            OZ_Log.Warn("storage: open job abandoned: the box is gone; " + dropped.ToString() + " container(s) waiting on the ground removed");
            return true;
        }
        if (m_Phase == PHASE_DONE)
            return true;
        float now = GetGame().GetTickTime();

        if (m_Phase == PHASE_REQUEST)
        {
            if (!m_Answered)
            {
                if (now - m_Posted <= OZS_Const.REPLY_TIMEOUT)
                    return false;
                OnOpenFailed("no answer within " + OZS_Const.REPLY_TIMEOUT.ToString() + " s");
            }
            if (!m_Answer)
            {
                Fail(m_Failure);
                return true;
            }
            if (!m_Answer.ok)
            {
                Fail("the bridge refused: " + m_Answer.why);
                return true;
            }
            if (m_Answer.empty)
            {
                m_Roots = 0;
                m_Entities = 0;
                Finish();
                return true;
            }
            if (!OpenFile(m_Answer.file))
            {
                Fail(m_Failure);
                return true;
            }
            m_Phase = PHASE_READ;
            m_Next = 0;
        }

        if (m_Phase == PHASE_PARK)
        {
            if (!m_ParkAnswered)
            {
                if (now - m_Posted <= OZS_Const.REPLY_TIMEOUT)
                    return false;
                OnParked(false, "no answer within " + OZS_Const.REPLY_TIMEOUT.ToString() + " s");
            }
            if (!m_ParkOk)
            {
                Fail("a root could not be parked: " + m_ParkWhy);
                return true;
            }
            if (m_Attempt >= OZS_Const.OPEN_RETRIES)
            {
                Fail("too many unreadable roots in one open (" + m_Parked.ToString() + " parked)");
                return true;
            }
            Request();
            return false;
        }

        m_Tokens = m_Tokens + rate * timeslice;
        if (m_Tokens > rate)
            m_Tokens = rate;
        float frameStart = now;
        if (m_Moves.Count() > 0)
        {
            int missedByMoves = OZS_Records.s_Missed;
            OZS_Records.ApplyMoves(m_Moves);
            m_Missed = m_Missed + OZS_Records.s_Missed - missedByMoves;
        }
        while (m_Phase == PHASE_READ && m_Next < m_Roots && m_Tokens >= 1)
        {
            int made = StepRoot();
            if (made < 0)
                break;
            if (made < 1)
                made = 1;
            m_Tokens = m_Tokens - made;
            now = GetGame().GetTickTime();
            if (now - frameStart >= budgetSec)
                break;
        }
        m_Frames++;
        float ms = (now - frameStart) * 1000;
        m_WorkMs = m_WorkMs + ms;
        if (ms > m_MaxStepMs)
            m_MaxStepMs = ms;
        if (m_Phase != PHASE_READ)
            return false;
        if (m_Next < m_Roots || m_Moves.Count() > 0)
            return false;
        CloseFile();
        Finish();
        return true;
    }

    // One root; the entities it created, or -1 when it was parked.
    protected int StepRoot()
    {
        int n = m_Next;
        int queued = m_Moves.Count();
        int missedBefore = OZS_Records.s_Missed;
        int created;
        string why;
        string type;
        if (OZS_Records.ReadRoot(m_File, m_Box, m_SaveVer, m_M0, m_M1, m_M2, m_M3, m_Moves, created, why, type))
        {
            m_Box.OZS_NoteRoot(OZS_Records.s_LastRoot);
            m_Next++;
            m_Created = m_Created + created;
            m_Missed = m_Missed + OZS_Records.s_Missed - missedBefore;
            if (m_Next == m_Roots)
                CheckTrailer();
            return created;
        }
        OZS_Records.DropMovesFrom(m_Moves, queued);
        OZ_Log.Error("storage: box " + m_Id + " root " + n.ToString() + " of " + m_Roots.ToString() + " (" + type + ") cannot be read: " + why + "; the bridge is asked to park it");
        Park(n, type, why);
        return -1;
    }

    // Everything this open created goes, the bridge parks the root, and the
    // open is asked again from a box that is empty once more.
    protected void Park(int root, string type, string why)
    {
        CloseFile();
        OZS_Records.DropMoves(m_Moves);
        RemoveRestored();
        m_Created = 0;
        m_Missed = 0;
        m_Parked++;
        string reason = "refused";
        if (why.Contains("marker"))
            reason = "desync";
        else if (why.Contains("no room"))
            reason = "no_room";
        OZS_ParkLetter letter = new OZS_ParkLetter();
        letter.id = m_Id;
        letter.stamp = m_Stamp;
        letter.root = root;
        letter.type = type;
        letter.why = reason;
        string json;
        string err;
        m_ParkAnswered = false;
        m_ParkOk = false;
        m_ParkWhy = "";
        m_Posted = GetGame().GetTickTime();
        m_Phase = PHASE_PARK;
        OZS_Audit.Log(reason, m_Id, m_Uid, m_Who, type, 0, -1, -1, "", "root " + root.ToString() + ": " + why);
        if (!JsonFileLoader<OZS_ParkLetter>.MakeData(letter, json, err, false))
        {
            OnParked(false, "the park letter cannot be written: " + err);
            return;
        }
        OZS_Bridge.Post(OZS_Const.ROUTE_PARK, json, new OZS_ParkReply(this));
    }

    protected bool OpenFile(string name)
    {
        m_FileName = name;
        string path = OZS_Store.XchgPath(name);
        if (!FileExist(path))
        {
            m_Failure = "the bridge named " + name + ", which does not exist";
            return false;
        }
        m_File = new FileSerializer();
        if (!m_File.Open(path, FileMode.READ))
        {
            m_File = null;
            m_Failure = "cannot open " + name;
            return false;
        }
        string boxClass;
        string boxId;
        string why;
        if (OZS_Store.ReadHeader(m_File, m_SaveVer, m_Stamp, boxClass, boxId, m_Roots, m_Entities, why) == 0)
        {
            CloseFile();
            m_Failure = name + ": " + why;
            return false;
        }
        if (!m_File.Read(m_M0) || !m_File.Read(m_M1) || !m_File.Read(m_M2) || !m_File.Read(m_M3))
        {
            CloseFile();
            m_Failure = name + ": the marker cannot be read";
            return false;
        }
        if (boxId != m_Id)
            OZ_Log.Warn("storage: box " + m_Id + ": the file " + name + " was written for box " + boxId);
        if (m_SaveVer != GetGame().SaveVersion())
            OZ_Log.Info("storage: box " + m_Id + " was stored under game save version " + m_SaveVer.ToString() + ", the game runs " + GetGame().SaveVersion().ToString() + "; items load their older state");
        return true;
    }

    protected void CheckTrailer()
    {
        int end;
        if (!m_File.Read(end) || end != OZS_Const.BIN_END)
            OZ_Log.Warn("storage: box " + m_Id + ": " + m_FileName + " does not end with BIN_END after the last root");
    }

    protected void CloseFile()
    {
        if (m_File)
        {
            m_File.Close();
            m_File = null;
        }
    }

    // The roots this open has already put into the box.
    protected int RemoveRestored()
    {
        if (!m_Box)
            return 0;
        array<EntityAI> roots = new array<EntityAI>();
        m_Box.OZS_GetRoots(roots);
        for (int i = 0; i < roots.Count(); i++)
        {
            if (roots.Get(i))
                GetGame().ObjectDelete(roots.Get(i));
        }
        return roots.Count();
    }

    void Abort()
    {
        CloseFile();
        m_Phase = PHASE_DONE;
    }

    // Mission finish, or the box gone: nothing half-restored survives.
    void Cancel(string why)
    {
        Abort();
        int waiting = OZS_Records.DropMoves(m_Moves);
        if (!m_Box)
            return;
        int gone = RemoveRestored();
        m_Box.OZS_SetRestoring(false);
        m_Box.OZS_SetState(OZS_Const.STATE_CLOSED);
        OZ_Log.Warn("storage: box " + m_Id + " opening cancelled (" + why + "): " + gone.ToString() + " half-restored items and " + waiting.ToString() + " container(s) on the ground removed");
    }

    protected void Fail(string why)
    {
        Abort();
        OZS_Records.DropMoves(m_Moves);
        RemoveRestored();
        m_Box.OZS_SetRestoring(false);
        m_Box.OZS_SetState(OZS_Const.STATE_CLOSED);
        OZ_Log.Error("storage: box " + m_Id + " could not be opened by " + m_Who + ": " + why);
        // The bridge marked the box open when it handed out the contents;
        // it is closed again, and SQL must know at once.
        OZS_IdLetter back = new OZS_IdLetter();
        back.id = m_Id;
        back.version = 0;
        string json;
        string err;
        if (JsonFileLoader<OZS_IdLetter>.MakeData(back, json, err, false))
            OZS_Bridge.Post(OZS_Const.ROUTE_CLOSED, json, new OZS_AckReply("closed after a failed open"));
        OZS_Controller.Get().OnOpenFailed(m_Box, m_Uid, why);
    }

    protected void Finish()
    {
        m_Phase = PHASE_DONE;
        CloseFile();
        m_Box.OZS_SetRestoring(false);
        m_Box.OZS_SetState(OZS_Const.STATE_OPEN);
        m_Box.OZS_SetStoredCount(m_Roots);
        float wall = GetGame().GetTickTime() - m_Started;
        string s = "storage: box " + m_Id + " opened by " + m_Who + ": " + m_Roots.ToString() + " items (" + m_Created.ToString() + " entities) in " + m_Frames.ToString() + " frame(s),";
        s = s + " work " + R1(m_WorkMs) + " ms, longest step " + R1(m_MaxStepMs) + " ms, wall " + R1(wall) + " s";
        s = s + ", missed " + m_Missed.ToString() + ", parked " + m_Parked.ToString() + ", attempts " + m_Attempt.ToString();
        // The record's order is what every later commit names a root by, so a
        // box that ends an open knowing fewer roots than it read will add the
        // missing ones back as duplicates at the first move.
        int noted = m_Box.OZS_RootOrder().Count();
        int inBox = OZS_Records.CountTree(m_Box) - 1;
        s = s + ", order " + noted.ToString() + ", in the box " + inBox.ToString();
        OZ_Log.Info(s);
        // WHAT WAS READ AND WHAT IS THERE MUST BE THE SAME NUMBER. A restore
        // that reports "missed 0" and still ends with fewer entities than it
        // created has lost them somewhere between the two, and the box will be
        // written back short.
        if (inBox != m_Created)
        {
            OZ_Log.Error("storage: box " + m_Id + " created " + m_Created.ToString() + " entities and ended with " + inBox.ToString() + " in it; " + (m_Created - inBox).ToString() + " went missing during the restore");
            array<EntityAI> left = new array<EntityAI>();
            m_Box.OZS_GetRoots(left);
            string had = "";
            for (int q = 0; q < left.Count(); q++)
                had = had + " " + left.Get(q).GetType() + "(" + OZS_Records.CountTree(left.Get(q)).ToString() + ")";
            OZ_Log.Error("storage: box " + m_Id + " holds:" + had);
        }
        if (noted != m_Roots)
            OZ_Log.Error("storage: box " + m_Id + " read " + m_Roots.ToString() + " root(s) but noted " + noted.ToString() + " in its order; every commit after this one will name the wrong root");
        OZS_IdLetter ack = new OZS_IdLetter();
        ack.id = m_Id;
        ack.stamp = m_Stamp;
        string json;
        string err;
        if (JsonFileLoader<OZS_IdLetter>.MakeData(ack, json, err, false))
            OZS_Bridge.Post(OZS_Const.ROUTE_OPENED, json, new OZS_AckReply("opened"));
        string note = "roots=" + m_Roots.ToString() + " work_ms=" + R1(m_WorkMs) + " parked=" + m_Parked.ToString();
        OZS_Audit.Log("open", m_Id, m_Uid, m_Who, "", m_Roots, -1, -1, "", note);
    }
}
