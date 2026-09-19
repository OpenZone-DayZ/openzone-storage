// The two transitions of a box as jobs that run a slice per frame and wait
// for one bridge reply each (design 2026-09-19, sections 3.1 and 3.2).
//
// Close: write the wire file within the frame budget, tell the bridge,
// wait for its answer, and only then delete the entities -- the engine's
// cargo is the truth until SQL has the box. Refused or unanswered: the file
// goes and the box stays open with everything in it.
//
// Open: ask the bridge, read the cache it names root by root at the item
// rate, publish each root once. A root the engine cannot read is deleted
// with everything this open created so far, parked with the bridge, and
// the open is asked again; a half-restored box is never left standing.

class OZS_CloseJob
{
    static const int PHASE_CAPTURE = 0;
    static const int PHASE_WAIT    = 1;
    static const int PHASE_DELETE  = 2;
    static const int PHASE_DONE    = 3;

    protected OZ_StorageBox m_Box;
    protected ref array<EntityAI> m_Roots;
    protected ref OZS_StoreWriter m_Writer;
    protected string m_Who;
    protected string m_Uid;
    protected string m_Why;
    protected int m_Phase;
    protected int m_Next;
    protected int m_Entities;
    protected int m_Deleted;
    protected int m_CaptureFrames;
    protected int m_DeleteFrames;
    protected float m_CaptureMs;
    protected float m_DeleteMs;
    protected float m_MaxStepMs;
    protected float m_Posted;
    protected float m_WaitMs;
    protected int m_Version;
    protected bool m_Sorted;
    protected bool m_Reopen;
    protected ref array<int> m_NewRows;
    protected ref array<int> m_NewCols;
    protected bool m_Answered;
    protected bool m_Accepted;
    protected string m_Refusal;

    // `why` is the bridge's word for the close: player, idle, sort, boot.
    void OZS_CloseJob(OZ_StorageBox box, string who, string uid, string why, bool sorted = false)
    {
        m_Box = box;
        m_Who = who;
        m_Uid = uid;
        m_Why = why;
        m_Roots = new array<EntityAI>();
        m_Phase = PHASE_CAPTURE;
        m_Sorted = sorted;
        m_Reopen = sorted;
        m_NewRows = new array<int>();
        m_NewCols = new array<int>();
    }

    OZ_StorageBox Box()
    {
        return m_Box;
    }

    bool IsFor(OZ_StorageBox box)
    {
        return m_Box == box;
    }

    bool Begin(out string why)
    {
        m_Box.OZS_SetState(OZS_Const.STATE_CLOSING);
        m_Box.OZS_GetRoots(m_Roots);
        m_Entities = 0;
        for (int i = 0; i < m_Roots.Count(); i++)
            m_Entities = m_Entities + OZS_Records.CountTree(m_Roots.Get(i));
        if (m_Sorted)
        {
            int placed = OZS_Sorter.Plan(m_Box, m_Roots, m_NewRows, m_NewCols);
            OZ_Log.Dbg("storage: box " + m_Box.OZS_GetId() + " sort plan: " + placed.ToString() + " of " + m_Roots.Count().ToString() + " roots placed");
        }
        m_Next = 0;
        if (m_Roots.Count() == 0)
            return true;
        m_Writer = new OZS_StoreWriter();
        if (!m_Writer.Open(m_Box, m_Roots.Count(), m_Entities, why))
        {
            m_Writer = null;
            m_Box.OZS_SetState(OZS_Const.STATE_OPEN);
            return false;
        }
        return true;
    }

    bool Tick(float budgetSec, int deleteBudget)
    {
        if (!m_Box)
        {
            Abandon("the box is gone");
            return true;
        }
        if (m_Phase == PHASE_DONE)
            return true;
        float frameStart = GetGame().GetTickTime();
        float now = frameStart;

        if (m_Phase == PHASE_CAPTURE)
        {
            while (m_Next < m_Roots.Count())
            {
                if (m_Sorted)
                    m_Writer.WriteRoot(m_Roots.Get(m_Next), m_NewRows.Get(m_Next), m_NewCols.Get(m_Next));
                else
                    m_Writer.WriteRoot(m_Roots.Get(m_Next));
                m_Next++;
                now = GetGame().GetTickTime();
                if (now - frameStart >= budgetSec)
                    break;
            }
            m_CaptureFrames++;
            Account(frameStart, now, m_CaptureMs);
            if (m_Next < m_Roots.Count())
                return false;
            PostClose();
            return m_Phase == PHASE_DONE;
        }

        if (m_Phase == PHASE_WAIT)
        {
            if (!m_Answered)
            {
                if (now - m_Posted <= OZS_Const.REPLY_TIMEOUT)
                    return false;
                OnBridge(false, "no answer within " + OZS_Const.REPLY_TIMEOUT.ToString() + " s", 0);
            }
            m_WaitMs = (now - m_Posted) * 1000;
            if (!m_Accepted)
            {
                Revert("the bridge did not take the close: " + m_Refusal);
                return true;
            }
            string s = "storage: box " + m_Box.OZS_GetId() + " closing by " + m_Who + ": " + m_Roots.Count().ToString() + " items";
            s = s + " (" + m_Entities.ToString() + " entities) written in " + m_CaptureFrames.ToString() + " frame(s), " + R1(m_CaptureMs) + " ms";
            s = s + "; the bridge holds version " + m_Version.ToString() + " after " + R1(m_WaitMs) + " ms";
            OZ_Log.Info(s);
            m_Phase = PHASE_DELETE;
            m_Next = 0;
            return false;
        }

        int done = 0;
        while (m_Next < m_Roots.Count() && done < deleteBudget)
        {
            EntityAI e = m_Roots.Get(m_Next);
            m_Next++;
            if (!e)
                continue;
            int n = OZS_Records.CountTree(e);
            GetGame().ObjectDelete(e);
            done = done + n;
            m_Deleted = m_Deleted + n;
        }
        m_DeleteFrames++;
        Account(frameStart, GetGame().GetTickTime(), m_DeleteMs);
        if (m_Next < m_Roots.Count())
            return false;
        Finish();
        return true;
    }

    // The file is complete: tell the bridge and wait.
    protected void PostClose()
    {
        string fileName = "";
        string stamp = OZS_Store.Stamp();
        if (m_Writer)
        {
            string err;
            if (!m_Writer.Finish(err))
            {
                Revert("the file could not be finished: " + err);
                return;
            }
            fileName = m_Writer.Name();
            stamp = m_Writer.Stamp();
        }
        OZS_CloseLetter letter = new OZS_CloseLetter();
        letter.id = m_Box.OZS_GetId();
        letter.stamp = stamp;
        letter.file = fileName;
        letter.roots = m_Roots.Count();
        letter.entities = m_Entities;
        letter.by = m_Uid;
        letter.why = m_Why;
        string json;
        string jerr;
        if (!JsonFileLoader<OZS_CloseLetter>.MakeData(letter, json, jerr, false))
        {
            Revert("the close letter cannot be written: " + jerr);
            return;
        }
        m_Answered = false;
        m_Accepted = false;
        m_Refusal = "";
        m_Posted = GetGame().GetTickTime();
        m_Phase = PHASE_WAIT;
        OZS_Bridge.Post(OZS_Const.ROUTE_CLOSE, json, new OZS_CloseReply(this));
    }

    // From the reply, or from the timeout above.
    void OnBridge(bool ok, string why, int version)
    {
        if (m_Phase != PHASE_WAIT)
            return;
        m_Answered = true;
        m_Accepted = ok;
        m_Refusal = why;
        m_Version = version;
    }

    // Mission finish: a deletion under way completes now (SQL already holds
    // the box); a close still writing or still waiting is undone, the box
    // stays open in the engine's own save and the boot rules close it.
    void Flush()
    {
        if (m_Phase == PHASE_DELETE)
        {
            for (int guard = 0; guard < 4; guard++)
            {
                if (Tick(1000000, 1000000))
                    return;
            }
            return;
        }
        if (m_Phase == PHASE_CAPTURE || m_Phase == PHASE_WAIT)
            Revert("mission finish");
    }

    protected void Account(float from, float to, inout float total)
    {
        float ms = (to - from) * 1000;
        total = total + ms;
        if (ms > m_MaxStepMs)
            m_MaxStepMs = ms;
    }

    // The close did not happen: the file goes (the bridge never read it
    // into SQL, or refused it; a file the bridge already promoted into the
    // cache no longer has this name and stays), the box keeps its cargo.
    protected void Revert(string why)
    {
        if (m_Writer)
        {
            why = why + " (file " + m_Writer.Name() + ")";
            m_Writer.Abort();
        }
        m_Phase = PHASE_DONE;
        string id = "";
        if (m_Box)
        {
            id = m_Box.OZS_GetId();
            m_Box.OZS_SetState(OZS_Const.STATE_OPEN);
            m_Box.OZS_SetTouchedAt(GetGame().GetTickTime());
        }
        OZ_Log.Error("storage: box " + id + " could not be closed by " + m_Who + ": " + why + "; nothing was removed");
        OZS_Controller.Get().OnCloseFailed(m_Box, m_Uid, why);
    }

    protected void Abandon(string why)
    {
        if (m_Writer)
            m_Writer.Abort();
        m_Phase = PHASE_DONE;
        OZ_Log.Warn("storage: close job abandoned: " + why);
    }

    protected void Finish()
    {
        m_Phase = PHASE_DONE;
        m_Box.OZS_SetState(OZS_Const.STATE_CLOSED);
        m_Box.OZS_SetStoredCount(m_Roots.Count());
        string id = m_Box.OZS_GetId();
        string s = "storage: box " + id + " closed by " + m_Who + ": deleted " + m_Deleted.ToString() + " entities in ";
        s = s + m_DeleteFrames.ToString() + " frame(s), " + R1(m_DeleteMs) + " ms; longest step " + R1(m_MaxStepMs) + " ms";
        OZ_Log.Info(s);
        OZS_IdLetter ack = new OZS_IdLetter();
        ack.id = id;
        ack.version = m_Version;
        string json;
        string err;
        if (JsonFileLoader<OZS_IdLetter>.MakeData(ack, json, err, false))
            OZS_Bridge.Post(OZS_Const.ROUTE_CLOSED, json, new OZS_AckReply("closed"));
        string note = m_Why + " version=" + m_Version.ToString() + " write_ms=" + R1(m_CaptureMs) + " wait_ms=" + R1(m_WaitMs) + " delete_ms=" + R1(m_DeleteMs);
        OZS_Audit.Log("close", id, m_Uid, m_Who, "", m_Roots.Count(), -1, -1, "", note);
        OZS_Controller.Get().OnClosed(m_Box, m_Reopen, m_Who, m_Uid);
    }

    static string R1(float v)
    {
        float r = Math.Round(v * 10) / 10;
        return r.ToString();
    }
}

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

    bool Begin(out string why)
    {
        m_Id = m_Box.OZS_GetId();
        m_Started = GetGame().GetTickTime();
        m_Box.OZS_SetState(OZS_Const.STATE_OPENING);
        m_Box.OZS_SetRestoring(true);
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
        OZS_Controller.Get().OnOpenFailed(m_Box, m_Uid, why);
    }

    protected void Finish()
    {
        m_Phase = PHASE_DONE;
        CloseFile();
        m_Box.OZS_SetRestoring(false);
        m_Box.OZS_SetState(OZS_Const.STATE_OPEN);
        m_Box.OZS_SetStoredCount(m_Roots);
        OZS_Controller.Get().OnOpened(m_Box);
        float wall = GetGame().GetTickTime() - m_Started;
        string s = "storage: box " + m_Id + " opened by " + m_Who + ": " + m_Roots.ToString() + " items (" + m_Created.ToString() + " entities) in " + m_Frames.ToString() + " frame(s),";
        s = s + " work " + OZS_CloseJob.R1(m_WorkMs) + " ms, longest step " + OZS_CloseJob.R1(m_MaxStepMs) + " ms, wall " + OZS_CloseJob.R1(wall) + " s";
        s = s + ", missed " + m_Missed.ToString() + ", parked " + m_Parked.ToString() + ", attempts " + m_Attempt.ToString();
        OZ_Log.Info(s);
        OZS_IdLetter ack = new OZS_IdLetter();
        ack.id = m_Id;
        ack.stamp = m_Stamp;
        string json;
        string err;
        if (JsonFileLoader<OZS_IdLetter>.MakeData(ack, json, err, false))
            OZS_Bridge.Post(OZS_Const.ROUTE_OPENED, json, new OZS_AckReply("opened"));
        string note = "roots=" + m_Roots.ToString() + " work_ms=" + OZS_CloseJob.R1(m_WorkMs) + " parked=" + m_Parked.ToString();
        OZS_Audit.Log("open", m_Id, m_Uid, m_Who, "", m_Roots, -1, -1, "", note);
    }
}
