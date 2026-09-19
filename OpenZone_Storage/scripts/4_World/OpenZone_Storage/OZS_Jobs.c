// The paced jobs of a box. A job owns nothing in the world: it holds the box
// and the entities by plain (weak) reference, so a box or an item deleted by
// someone else reads null and is skipped.

// CLOSING, in two phases. CAPTURE writes the root entities into the store a
// frame's time budget at a time (0.17 ms per entity measured 2026-09-16),
// then commits the files. DELETE removes the roots a budget of entities per
// frame (5000 deletes in one frame = 259 ms measured, batches of 50 =
// 4--8 ms) and flips the box to CLOSED after the last one. The box is locked
// (state CLOSING, gates shut) from Begin on, so the capture sees a still
// picture.
class OZS_CloseJob
{
    static const int PHASE_CAPTURE = 0;
    static const int PHASE_DELETE  = 1;

    protected OZ_StorageBox m_Box;
    protected ref array<EntityAI> m_Roots;
    protected ref OZS_StoreWriter m_Writer;
    protected string m_Who;
    protected int m_Phase;
    protected int m_Next;
    protected int m_Entities;
    protected int m_Deleted;
    protected int m_CaptureFrames;
    protected int m_DeleteFrames;
    protected float m_CaptureMs;
    protected float m_CommitMs;
    protected float m_DeleteMs;
    protected float m_MaxStepMs;
    // A sort: the cargo roots get new cells from OZS_Sorter (aligned with
    // m_Roots, -1 = keep), and the box reopens when the close is done.
    protected bool m_Sorted;
    protected bool m_Reopen;
    protected ref array<int> m_NewRows;
    protected ref array<int> m_NewCols;

    void OZS_CloseJob(OZ_StorageBox box, string who, bool sorted = false)
    {
        m_Box = box;
        m_Who = who;
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

    // Locks the box, lists its roots and opens the store. False when the
    // store cannot be opened: the box is OPEN again and nothing was touched.
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
            OZ_Log.Dbg("storage: box " + m_Box.OZS_GetId() + " sort plan: " + placed + " of " + m_Roots.Count() + " roots placed");
        }
        m_Writer = new OZS_StoreWriter();
        if (!m_Writer.Open(m_Box, m_Roots.Count(), m_Entities, why))
        {
            m_Box.OZS_SetState(OZS_Const.STATE_OPEN);
            return false;
        }
        m_Box.OZS_SetStoredCount(m_Roots.Count());
        m_Next = 0;
        return true;
    }

    // One frame of work. True when the job is over (done or abandoned).
    bool Tick(float budgetSec, int deleteBudget)
    {
        if (!m_Box)
        {
            Abandon("the box is gone");
            return true;
        }
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

            string why;
            float c0 = GetGame().GetTickTime();
            bool committed = m_Writer.Commit(why);
            float c1 = GetGame().GetTickTime();
            Account(c0, c1, m_CommitMs);
            if (!committed)
            {
                m_Box.OZS_SetState(OZS_Const.STATE_OPEN);
                OZ_Log.Error("storage: box " + m_Box.OZS_GetId() + " could not be closed by " + m_Who + ": " + why + "; nothing was removed");
                return true;
            }
            string s = "storage: box " + m_Box.OZS_GetId() + " closing by " + m_Who + ": " + m_Roots.Count() + " items";
            s = s + " (" + m_Entities + " entities) written in " + m_CaptureFrames + " frame(s), " + R1(m_CaptureMs) + " ms";
            s = s + " + commit " + R1(m_CommitMs) + " ms";
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

    // Everything that is left, in this frame (mission finish, boot rules).
    void Flush()
    {
        for (int guard = 0; guard < 4; guard++)
        {
            bool over = Tick(1000000, 1000000);
            if (over)
                return;
        }
    }

    protected void Account(float from, float to, inout float total)
    {
        float ms = (to - from) * 1000;
        total = total + ms;
        if (ms > m_MaxStepMs)
            m_MaxStepMs = ms;
    }

    protected void Abandon(string why)
    {
        if (m_Writer)
            m_Writer.Abort();
        OZ_Log.Warn("storage: close job abandoned: " + why);
    }

    protected void Finish()
    {
        m_Box.OZS_SetState(OZS_Const.STATE_CLOSED);
        string s = "storage: box " + m_Box.OZS_GetId() + " closed by " + m_Who + ": deleted " + m_Deleted + " entities in ";
        s = s + m_DeleteFrames + " frame(s), " + R1(m_DeleteMs) + " ms; longest step " + R1(m_MaxStepMs) + " ms";
        OZ_Log.Info(s);
        OZS_Controller.Get().OnClosed(m_Box, m_Reopen, m_Who);
    }

    static string R1(float v)
    {
        float r = Math.Round(v * 10) / 10;
        return r.ToString();
    }
}

// OPENING: reads items.bin a root at a time within a frame budget and a
// rate of entities per second (250/s never stalled a client next to the box,
// one 5000-burst did -- measured 2026-09-16), falls back to items.list when
// the blob cannot be followed, and flips the box to OPEN after the last
// root. The files stay until the engine has saved the open box once
// (OZS_Controller.OnBoxSaved), so a crash before that loses nothing.
class OZS_OpenJob
{
    static const int MODE_BIN  = 0;
    static const int MODE_LIST = 1;
    static const int MODE_NONE = 2;
    static const int LIST_UNREAD  = 0;
    static const int LIST_USABLE  = 1;
    static const int LIST_REFUSED = 2;

    protected OZ_StorageBox m_Box;
    protected string m_Id;
    protected string m_Who;
    protected int m_Mode;
    protected ref FileSerializer m_Bin;
    protected int m_SaveVer;
    // The stamp of the items.bin header, kept so the list fallback can prove
    // the two files are the same commit before it splices one onto the other.
    protected string m_BinStamp;
    protected int m_Roots;
    protected int m_Entities;
    protected int m_Next;
    protected ref array<ref OZS_ListRec> m_List;
    protected int m_ListState;
    protected int m_ListAt;
    protected int m_FallbackFrom;
    // Roots that came from items.list because their own file could not be
    // read (the index itself fine).
    protected int m_FromList;
    protected float m_Tokens;
    protected float m_Started;
    protected int m_Frames;
    protected float m_WorkMs;
    protected float m_MaxStepMs;
    protected int m_Created;
    protected int m_Missed;
    protected int m_Fails;
    // Containers created this frame, moved into their parents next frame.
    protected ref array<ref OZS_Move> m_Moves;

    void OZS_OpenJob(OZ_StorageBox box, string who)
    {
        m_Moves = new array<ref OZS_Move>();
        m_Box = box;
        m_Who = who;
        m_Mode = MODE_NONE;
        m_FallbackFrom = -1;
        m_ListState = LIST_UNREAD;
        m_FromList = 0;
        m_BinStamp = "";
        m_Created = 0;
        m_Missed = 0;
        m_Fails = 0;
    }

    bool IsFor(OZ_StorageBox box)
    {
        return m_Box == box;
    }

    // Locks the box as OPENING and opens the store. False when nothing is
    // readable: the box is CLOSED again and the files are untouched.
    bool Begin(out string why)
    {
        m_Id = m_Box.OZS_GetId();
        m_Started = GetGame().GetTickTime();
        m_Box.OZS_SetState(OZS_Const.STATE_OPENING);
        m_Box.OZS_SetRestoring(true);
        m_Tokens = 0;
        m_Next = 0;
        string binWhy;
        if (!OpenBin(binWhy))
        {
            OZ_Log.Warn("storage: box " + m_Id + ": " + binWhy + "; trying items.list");
            if (!OpenList(0))
            {
                m_Box.OZS_SetRestoring(false);
                m_Box.OZS_SetState(OZS_Const.STATE_CLOSED);
                why = binWhy + ", and items.list cannot be used either";
                return false;
            }
        }
        return true;
    }

    protected bool OpenBin(out string why)
    {
        string path = OZS_Store.BinPath(m_Id);
        if (!FileExist(path))
        {
            why = "no items.bin";
            return false;
        }
        FileSerializer index = new FileSerializer();
        if (!index.Open(path, FileMode.READ))
        {
            why = "items.bin cannot be opened";
            return false;
        }
        int ver;
        if (!index.Read(ver) || ver != OZS_Const.BIN_VERSION)
        {
            index.Close();
            why = "items.bin format version " + ver + " is not " + OZS_Const.BIN_VERSION;
            return false;
        }
        string type;
        string id;
        index.Read(m_SaveVer);
        index.Read(m_BinStamp);
        index.Read(type);
        index.Read(id);
        index.Read(m_Roots);
        index.Read(m_Entities);
        index.Close();
        if (id != m_Id)
            OZ_Log.Warn("storage: box " + m_Id + " items.bin was written for box " + id);
        if (m_SaveVer != GetGame().SaveVersion())
            OZ_Log.Info("storage: box " + m_Id + " was stored under game save version " + m_SaveVer + ", the game runs " + GetGame().SaveVersion() + "; items load their older state");
        m_Mode = MODE_BIN;
        return true;
    }

    // Reads items.list once and checks that it belongs to the index's
    // commit. The two are written in one pass with one stamp, so a list
    // stamped otherwise is a survivor of an older commit whose delete
    // failed, and splicing it onto this store would restore the wrong items:
    // refused, kept aside, both stamps said. True when the list can be used.
    protected bool LoadList()
    {
        if (m_ListState != LIST_UNREAD)
            return m_ListState == LIST_USABLE;
        m_ListState = LIST_REFUSED;
        m_List = new array<ref OZS_ListRec>();
        if (OZS_ListFallback.Read(m_Id, m_List) < 0)
        {
            m_List = null;
            OZ_Log.Error("storage: box " + m_Id + " has no readable items.list");
            return false;
        }
        if (m_BinStamp != "")
        {
            string listStamp = OZS_Store.ListStamp(m_Id);
            if (listStamp != m_BinStamp)
            {
                // Copied aside, or the box's next close overwrites the only
                // copy of the state this list describes.
                string keep = OZS_Store.ListPath(m_Id) + ".failed-" + OZS_Store.FileStamp();
                CopyFile(OZS_Store.ListPath(m_Id), keep);
                string e = "storage: box " + m_Id + " items.list is stamped " + listStamp;
                e = e + " but items.bin is stamped " + m_BinStamp;
                e = e + "; the pair does not match, the fallback is refused and the list is kept as " + keep;
                OZ_Log.Error(e);
                m_List = null;
                return false;
            }
        }
        m_ListState = LIST_USABLE;
        return true;
    }

    // Every root from `fromRoot` on comes from items.list: the index cannot
    // be used at all (absent, unreadable, or of an older format).
    protected bool OpenList(int fromRoot)
    {
        if (!LoadList())
        {
            m_Mode = MODE_NONE;
            return false;
        }
        int listRoots = OZS_ListFallback.RootCount(m_List);
        if (m_Roots == 0)
            m_Roots = listRoots;
        m_ListAt = OZS_ListFallback.RootIndex(m_List, fromRoot);
        m_FallbackFrom = fromRoot;
        if (m_ListAt < 0)
        {
            OZ_Log.Warn("storage: box " + m_Id + " items.list holds " + listRoots + " roots, nothing left from root " + fromRoot);
            m_Mode = MODE_NONE;
            return true;
        }
        OZ_Log.Warn("storage: box " + m_Id + ": roots " + fromRoot + ".." + (listRoots - 1) + " come from items.list without script state");
        m_Mode = MODE_LIST;
        return true;
    }

    // One frame of work. True when the job is over.
    bool Tick(float budgetSec, float rate, float timeslice)
    {
        if (!m_Box)
        {
            Abort();
            int dropped = OZS_Records.DropMoves(m_Moves);
            OZ_Log.Warn("storage: open job abandoned: the box is gone; " + dropped + " container(s) waiting on the ground removed");
            return true;
        }
        m_Tokens = m_Tokens + rate * timeslice;
        if (m_Tokens > rate)
            m_Tokens = rate;
        float frameStart = GetGame().GetTickTime();
        float now = frameStart;
        // The moves queued by the previous frame's records go first: every
        // container they target is at least one frame old by now.
        if (m_Moves.Count() > 0)
        {
            int missedByMoves = OZS_Records.s_Missed;
            OZS_Records.ApplyMoves(m_Moves);
            m_Missed = m_Missed + OZS_Records.s_Missed - missedByMoves;
        }
        while (m_Mode != MODE_NONE && m_Tokens >= 1)
        {
            // The record counters are static and shared by every job in
            // flight, so each step's delta is credited to this job at once.
            int created0 = OZS_Records.s_Created;
            int missed0 = OZS_Records.s_Missed;
            int fails0 = OZS_Records.s_LoadFails;
            if (m_Mode == MODE_BIN)
                StepBin();
            else
                StepList();
            int made = OZS_Records.s_Created + OZS_Records.s_Missed - created0 - missed0;
            m_Created = m_Created + OZS_Records.s_Created - created0;
            m_Missed = m_Missed + OZS_Records.s_Missed - missed0;
            m_Fails = m_Fails + OZS_Records.s_LoadFails - fails0;
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
        // Moves queued this frame run next frame, so the job lasts one more.
        if (m_Mode != MODE_NONE || m_Moves.Count() > 0)
            return false;
        Finish();
        return true;
    }

    // One root from its own file. A file that is missing, of another
    // commit or broken inside costs that root alone: it comes from
    // items.list instead, and the next root is read from its own file.
    protected void StepBin()
    {
        if (m_Next >= m_Roots)
        {
            m_Mode = MODE_NONE;
            return;
        }
        int n = m_Next;
        m_Next++;
        string path = OZS_Store.RootPath(m_Id, n);
        EntityAI made;
        string why;
        int missedBefore = OZS_Records.s_Missed;
        int queued = m_Moves.Count();
        if (ReadRootFile(path, n, made, why))
            return;
        // Drop what the broken file left: its ground containers are the
        // moves queued from `queued` on, the rest sits in the box under
        // `made`, and what ReadEntity deleted itself is no longer `made`.
        OZS_Records.s_Missed = missedBefore;
        OZS_Records.DropMovesFrom(m_Moves, queued);
        if (made && !made.IsSetForDeletion())
            GetGame().ObjectDelete(made);
        string kept = "";
        if (FileExist(path))
        {
            kept = path + ".failed-" + OZS_Store.FileStamp();
            CopyFile(path, kept);
            kept = "; kept as " + kept;
        }
        OZ_Log.Error("storage: box " + m_Id + " root " + n + " of " + m_Roots + " cannot be read (" + why + ")" + kept + "; it comes from items.list");
        RootFromList(n);
    }

    // False with `why` when the file is absent, of another commit, or
    // breaks inside its record.
    protected bool ReadRootFile(string path, int n, out EntityAI made, out string why)
    {
        made = null;
        why = "";
        if (!FileExist(path))
        {
            why = "no file " + path;
            return false;
        }
        m_Bin = new FileSerializer();
        if (!m_Bin.Open(path, FileMode.READ))
        {
            m_Bin = null;
            why = path + " cannot be opened";
            return false;
        }
        int ver;
        string stamp;
        int index;
        if (!m_Bin.Read(ver) || ver != OZS_Const.BIN_VERSION)
        {
            CloseBin();
            why = "format version " + ver;
            return false;
        }
        m_Bin.Read(stamp);
        m_Bin.Read(index);
        if (stamp != m_BinStamp || index != n)
        {
            CloseBin();
            why = "stamped " + stamp + " as root " + index + " where the index says " + m_BinStamp + " root " + n;
            return false;
        }
        if (!OZS_Records.ReadEntity(m_Bin, m_Box, m_SaveVer, made, m_Moves))
        {
            CloseBin();
            why = "the record cannot be followed";
            return false;
        }
        int end;
        if (!m_Bin.Read(end) || end != OZS_Const.BIN_END)
            OZ_Log.Warn("storage: box " + m_Id + " root " + n + " has no trailer; its file may be truncated");
        CloseBin();
        return true;
    }

    // Root n from items.list, when the list can be trusted.
    protected void RootFromList(int n)
    {
        if (!LoadList())
        {
            OZS_Records.s_Missed++;
            OZ_Log.Error("storage: box " + m_Id + " root " + n + " is lost: neither its file nor items.list can be used");
            return;
        }
        int at = OZS_ListFallback.RootIndex(m_List, n);
        if (at < 0)
        {
            OZS_Records.s_Missed++;
            OZ_Log.Error("storage: box " + m_Id + " root " + n + " is lost: items.list holds " + OZS_ListFallback.RootCount(m_List) + " roots");
            return;
        }
        OZS_ListFallback.Make(m_List, at, m_Box, m_Moves);
        m_FromList++;
    }

    protected void StepList()
    {
        if (m_ListAt < 0 || m_ListAt >= m_List.Count())
        {
            m_Mode = MODE_NONE;
            return;
        }
        OZS_ListFallback.Make(m_List, m_ListAt, m_Box, m_Moves);
        m_ListAt = OZS_ListFallback.SubtreeEnd(m_List, m_ListAt);
        m_Next++;
    }

    protected void CloseBin()
    {
        if (m_Bin)
        {
            m_Bin.Close();
            m_Bin = null;
        }
    }

    // Nothing more is read or created.
    void Abort()
    {
        CloseBin();
        m_Mode = MODE_NONE;
    }

    // Mission finish while opening: the files stay (they win at boot), the
    // half-restored cargo goes, the box is CLOSED.
    void Cancel(string why)
    {
        Abort();
        int waiting = OZS_Records.DropMoves(m_Moves);
        if (!m_Box)
            return;
        array<EntityAI> roots = new array<EntityAI>();
        m_Box.OZS_GetRoots(roots);
        for (int i = 0; i < roots.Count(); i++)
        {
            if (roots.Get(i))
                GetGame().ObjectDelete(roots.Get(i));
        }
        m_Box.OZS_SetRestoring(false);
        m_Box.OZS_SetState(OZS_Const.STATE_CLOSED);
        OZ_Log.Warn("storage: box " + m_Id + " opening cancelled (" + why + "): " + roots.Count() + " half-restored items and " + waiting + " container(s) waiting on the ground removed, the files stay");
    }

    protected void Finish()
    {
        m_Box.OZS_SetRestoring(false);
        m_Box.OZS_SetState(OZS_Const.STATE_OPEN);
        OZS_Controller.Get().OnOpened(m_Box);
        float wall = GetGame().GetTickTime() - m_Started;
        string s = "storage: box " + m_Id + " opened by " + m_Who + ": " + m_Next + " items (" + m_Created + " entities) in " + m_Frames + " frame(s),";
        s = s + " work " + OZS_CloseJob.R1(m_WorkMs) + " ms, longest step " + OZS_CloseJob.R1(m_MaxStepMs) + " ms, wall " + OZS_CloseJob.R1(wall) + " s";
        s = s + ", missed " + m_Missed + ", refusals " + m_Fails;
        if (m_FallbackFrom >= 0)
            s = s + ", items.list from root " + m_FallbackFrom;
        if (m_FromList > 0)
            s = s + ", " + m_FromList + " root(s) from items.list";
        OZ_Log.Info(s);
    }
}
