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

    void OZS_CloseJob(OZ_StorageBox box, string who)
    {
        m_Box = box;
        m_Who = who;
        m_Roots = new array<EntityAI>();
        m_Phase = PHASE_CAPTURE;
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
    }

    static string R1(float v)
    {
        float r = Math.Round(v * 10) / 10;
        return r.ToString();
    }
}
