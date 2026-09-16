// Client-side monitor: what arriving item entities cost the client.
//
// Every second it looks at how many ItemBase.EEInit happened since the last
// look. A second with new items opens a "burst"; three quiet seconds close it
// and write a summary. Each active second also writes one line, with the frame
// statistics of that second, the nearest probe crate's cargo count as the
// client sees it, and the distance to it. Written to
// $profile:OpenZone_StorageProbe/client.log of the CLIENT profile directory.
class OZ_ProbeClientMonitor
{
    static const string DIR = "$profile:OpenZone_StorageProbe";
    static const string LOG = "$profile:OpenZone_StorageProbe/client.log";

    protected float m_Acc;
    protected int   m_LastInits;
    protected int   m_LastDeletes;
    protected ref OZ_ProbeFrameStats m_Second;
    protected ref OZ_ProbeFrameStats m_Burst;
    protected bool  m_InBurst;
    protected int   m_Quiet;
    protected int   m_BurstInits;
    protected float m_BurstStart;
    protected int   m_BurstCargoAtEnd;
    protected bool  m_Started;
    protected float m_LastFrameAt;

    void OZ_ProbeClientMonitor()
    {
        m_Second = new OZ_ProbeFrameStats();
        m_Burst = new OZ_ProbeFrameStats();
        m_Acc = 0;
        m_InBurst = false;
        m_Started = false;
    }

    // Frame length from GetTickTime() between calls; `timeslice` is clamped at
    // 0.3 s by the engine (measured on the server 2026-09-16).
    void OnFrame(float timeslice)
    {
        float now = GetGame().GetTickTime();
        if (m_LastFrameAt == 0)
        {
            m_LastFrameAt = now;
            return;
        }
        float dtMs = (now - m_LastFrameAt) * 1000;
        m_LastFrameAt = now;
        m_Second.Add(dtMs);
        if (m_InBurst)
            m_Burst.Add(dtMs);

        m_Acc += timeslice;
        if (m_Acc < 1.0)
            return;
        m_Acc = 0;

        if (!m_Started)
        {
            MakeDirectory(DIR);
            m_Started = true;
            m_LastInits = OZ_ProbeCounters.s_ItemInits;
            m_LastDeletes = OZ_ProbeCounters.s_ItemDeletes;
            Append("client monitor started; inits so far " + m_LastInits);
            m_Second.Reset();
            return;
        }

        int inits = OZ_ProbeCounters.s_ItemInits;
        int deletes = OZ_ProbeCounters.s_ItemDeletes;
        int dInits = inits - m_LastInits;
        int dDeletes = deletes - m_LastDeletes;
        m_LastInits = inits;
        m_LastDeletes = deletes;

        int cargo = -1;
        float dist = -1;
        NearestCrateInfo(cargo, dist);

        if (dInits > 0 || dDeletes > 0)
        {
            if (!m_InBurst)
            {
                m_InBurst = true;
                m_Burst.Reset();
                m_BurstInits = 0;
                m_BurstStart = GetGame().GetTickTime();
                // The frames of this first second belong to the burst too.
                m_Burst.frames = m_Second.frames;
                m_Burst.sumMs = m_Second.sumMs;
                m_Burst.maxMs = m_Second.maxMs;
                m_Burst.over100 = m_Second.over100;
                m_Burst.over150 = m_Second.over150;
                m_Burst.over500 = m_Second.over500;
            }
            m_BurstInits += dInits;
            m_Quiet = 0;
            m_BurstCargoAtEnd = cargo;
            string line = "t=" + OZ_ProbeFrameStats.R1(GetGame().GetTickTime()) + " inits=" + inits;
            line = line + " d_inits=" + dInits + " d_deletes=" + dDeletes + " crate_cargo=" + cargo;
            line = line + " dist=" + OZ_ProbeFrameStats.R1(dist) + " " + m_Second.Text();
            Append(line);
        }
        else if (m_InBurst)
        {
            m_Quiet++;
            if (m_Quiet >= 3)
            {
                float secs = GetGame().GetTickTime() - m_BurstStart - 3;
                string summary = "burst items=" + m_BurstInits + " secs=" + OZ_ProbeFrameStats.R1(secs);
                summary = summary + " crate_cargo=" + m_BurstCargoAtEnd + " " + m_Burst.Text();
                Append(summary);
                string js = "burst_json {\"items\":" + m_BurstInits + ",\"secs\":" + OZ_ProbeFrameStats.R1(secs);
                js = js + ",\"crate_cargo\":" + m_BurstCargoAtEnd + "," + m_Burst.Json("frame_") + "}";
                Append(js);
                m_InBurst = false;
            }
        }

        m_Second.Reset();
    }

    void Flush()
    {
        if (m_InBurst)
            Append("burst interrupted items=" + m_BurstInits + " " + m_Burst.Text());
    }

    protected void NearestCrateInfo(out int cargoCount, out float dist)
    {
        Man player = GetGame().GetPlayer();
        if (!player)
            return;
        vector pos = player.GetPosition();
        array<Object> objects = new array<Object>();
        array<CargoBase> proxies = new array<CargoBase>();
        GetGame().GetObjectsAtPosition3D(pos, 150, objects, proxies);
        float best = 1000;
        EntityAI crate;
        for (int i = 0; i < objects.Count(); i++)
        {
            Object o = objects.Get(i);
            if (!o.IsKindOf("OZ_ProbeCrate"))
                continue;
            float d = vector.Distance(o.GetPosition(), pos);
            if (d < best)
            {
                best = d;
                crate = EntityAI.Cast(o);
            }
        }
        if (!crate)
            return;
        dist = best;
        CargoBase cargo = crate.GetInventory().GetCargo();
        if (cargo)
            cargoCount = cargo.GetItemCount();
    }

    protected void Append(string line)
    {
        FileHandle fh = OpenFile(LOG, FileMode.APPEND);
        if (!fh)
            return;
        FPrintln(fh, line);
        CloseFile(fh);
    }
}
