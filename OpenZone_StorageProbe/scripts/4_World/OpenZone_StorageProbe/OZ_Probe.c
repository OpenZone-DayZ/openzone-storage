// The server-side job engine of the storage probe.
//
// One job at a time. A job is started by a bridge verb and then advanced from
// MissionServer.OnUpdate, one batch per frame (batch = 0 means everything in a
// single frame -- the naive "open and it all appears" model). While a job runs
// and for a cooldown after its last batch, every frame's timeslice is
// accumulated, so the cost that the engine pays in the frames AFTER the script
// call (deferred deletes, replication) is part of the answer.
//
// Jobs:
//   crate    create OZ_ProbeCrate (or OZ_ProbeCrateSmall) at pos; reports the
//            grid the engine actually gave it.
//   fill     create `n` items of `item` in the crate. mode=loc uses
//            GameInventory.LocationCreateEntity with an explicit row/col (the
//            path a restore would take), mode=find lets the engine search a
//            free cell (CreateEntityInCargo), mode=ex uses CreateEntityInCargoEx.
//   clear    ObjectDelete every item in the crate's cargo.
//   capture  read type, cell, health and quantity of every cargo item into a
//            snapshot; on completion serialise it to JSON and save it, timing
//            the serialisation and the file write separately.
//   load     JsonFileLoader.LoadFile of that snapshot in ONE frame -- the very
//            call that froze the live server, measured for a 5000-item file.
//   restore  recreate the loaded snapshot cell by cell (LocationCreateEntity +
//            SetHealth01 + SetQuantity).
//   status   progress of the running job or the last result.
//   baseline reset and read the idle frame statistics.
//
// Results: one JSON line per finished job appended to
// $profile:OpenZone_StorageProbe/results.log, plus the script log.

class OZ_ProbeItemRecord
{
    string type;
    int    row;
    int    col;
    bool   flip;
    float  health;
    float  quantity;
}

class OZ_ProbeSnapshot
{
    string crate;
    int    width;
    int    height;
    ref array<ref OZ_ProbeItemRecord> items;

    void OZ_ProbeSnapshot()
    {
        items = new array<ref OZ_ProbeItemRecord>();
    }
}

class OZ_Probe
{
    static const string DIR      = "$profile:OpenZone_StorageProbe";
    static const string RESULTS  = "$profile:OpenZone_StorageProbe/results.log";
    static const string SNAPSHOT = "$profile:OpenZone_StorageProbe/snapshot.json";
    // Seconds of frame monitoring after the last batch. Time, not frames: an
    // idle dedicated server runs its main loop at thousands of frames a second
    // (measured 2026-09-16: 254 025 frames in 96 s, avg 0.2 ms), so a frame
    // count would end before the engine's deferred work even starts.
    static const float  COOLDOWN_SECONDS = 3.0;

    protected static ref OZ_Probe s_Instance;

    static OZ_Probe Get()
    {
        if (!s_Instance)
            s_Instance = new OZ_Probe();
        return s_Instance;
    }

    // ---- job state --------------------------------------------------------
    protected string   m_Op;
    protected int      m_JobId;
    protected EntityAI m_Crate;        // weak on purpose: a deleted crate reads null
    protected string   m_ItemType;
    protected int      m_ItemW;
    protected int      m_ItemH;
    protected int      m_GridW;
    protected int      m_GridH;
    protected int      m_Target;
    protected int      m_Done;
    protected int      m_Failed;
    protected int      m_FirstFailAt;
    protected int      m_Batch;
    protected string   m_Mode;
    protected int      m_Cursor;
    protected float    m_StartedAt;
    protected int      m_Frames;
    protected float    m_WorkMsSum;
    protected float    m_WorkMsMax;
    protected int      m_WorkTicksSum;
    protected int      m_Batches;
    protected float    m_CooldownUntil;
    protected int      m_GapMs;
    protected int      m_NextGapMs;
    protected float    m_LastBatchAt;
    protected int      m_InitsAtStart;
    protected int      m_DeletesAtStart;
    protected string   m_Extra;
    protected ref array<string>        m_Attach;
    protected ref array<string>        m_Cargo;
    protected string   m_AttachText;
    protected string   m_CargoText;
    protected string   m_CargoMode;
    protected string   m_Persist;
    protected int      m_Children;
    protected int      m_ChildMiss;
    protected ref OZ_ProbeFrameStats   m_Frame;
    protected ref OZ_ProbeFrameStats   m_Idle;
    protected ref array<EntityAI>      m_Items;
    protected ref OZ_ProbeSnapshot     m_Snapshot;
    protected string   m_LastResult;
    protected int      m_JobsDone;
    protected float    m_LastFrameAt;

    void OZ_Probe()
    {
        m_Op = "";
        m_JobId = 0;
        m_Frame = new OZ_ProbeFrameStats();
        m_Idle = new OZ_ProbeFrameStats();
        m_Items = new array<EntityAI>();
        m_Attach = new array<string>();
        m_Cargo = new array<string>();
        m_LastResult = "";
        m_Extra = "";
        m_AttachText = "";
        m_CargoText = "";
    }

    void OnMissionInit()
    {
        MakeDirectory(DIR);
        Print("[OpenZone] storage probe loaded; results in " + RESULTS);
    }

    void OnMissionFinish()
    {
        if (m_Op != "")
        {
            string s = "[OpenZone] storage probe: job " + m_JobId + " " + m_Op;
            s = s + " interrupted by mission finish at " + m_Done + "/" + m_Target;
            Print(s);
        }
    }

    // Called every server frame from MissionServer.OnUpdate.
    //
    // The frame length is taken from GetTickTime() between two calls, NOT from
    // `timeslice`: the engine clamps timeslice at 0.3 s (measured 2026-09-16 --
    // a frame with 724 ms of script work arrived as timeslice = 300 ms).
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
        m_Idle.Add(dtMs);

        if (m_Op == "")
            return;

        m_Frame.Add(dtMs);
        m_Frames++;

        if (m_Done + m_Failed < m_Target)
        {
            // gap_ms > 0 rate-limits the job: the next batch waits until this
            // many milliseconds have passed since the previous one. That is how
            // "N items per second towards a nearby client" is measured.
            if (m_GapMs > 0 && m_Batches > 0 && (now - m_LastBatchAt) * 1000 < m_GapMs)
                return;
            m_LastBatchAt = now;
            DoBatch();
            return;
        }

        if (m_CooldownUntil == 0)
        {
            m_CooldownUntil = now + COOLDOWN_SECONDS;
            return;
        }
        if (now >= m_CooldownUntil)
            Finish();
    }

    // ---- verbs ------------------------------------------------------------

    // Entry point of the bridge verb. Returns immediately; long jobs run in
    // OnFrame and are read back with op=status.
    bool Command(string op, map<string, string> args, out string detail)
    {
        if (op == "status")
        {
            detail = Status();
            return true;
        }
        if (op == "baseline")
        {
                detail = "idle frames since last baseline: " + m_Idle.Text();
            detail = detail + "; inits=" + OZ_ProbeCounters.s_ItemInits + " deletes=" + OZ_ProbeCounters.s_ItemDeletes;
            m_Idle.Reset();
            return true;
        }
        if (op == "crate")
            return CmdCrate(args, detail);
        if (op == "find")
            return CmdFind(args, detail);

        // Immediate research ops (items 2-4 of the brief); all need a crate.
        if (op == "stock" || op == "inspect" || op == "blob_save" || op == "blob_load" || op == "nest" || op == "give")
        {
            if (!RequireCrate(detail))
                return false;
            return CmdImmediate(op, args, detail);
        }
        if (op == "player_count")
        {
            array<Man> players = new array<Man>();
            GetGame().GetPlayers(players);
            if (players.Count() == 0)
            {
                detail = "nobody is connected";
                return false;
            }
            string what = Arg(args, "item", "Paper");
            detail = "player carries " + OZ_ProbeState.PlayerCount(players.Get(0), what) + " x " + what;
            return true;
        }

        if (m_Op != "")
        {
            detail = "job " + m_JobId + " (" + m_Op + ") is still running: " + m_Done + "/" + m_Target + "; wait for it";
            return false;
        }

        // Optional for every job: gap_ms between batches (0 = every frame).
        m_NextGapMs = Arg(args, "gap_ms", "0").ToInt();

        if (op == "fill")
            return CmdFill(args, detail);
        if (op == "clear")
            return CmdClear(args, detail);
        if (op == "capture")
            return CmdCapture(args, detail);
        if (op == "load")
            return CmdLoad(args, detail);
        if (op == "restore")
            return CmdRestore(args, detail);
        if (op == "delete_crate")
            return CmdDeleteCrate(args, detail);

        detail = "unknown op '" + op + "'; known: status, baseline, crate, find, fill, clear, capture, load, restore, delete_crate";
        return false;
    }

    protected bool CmdImmediate(string op, map<string, string> args, out string detail)
    {
        float t0 = GetGame().GetTickTime();
        bool ok = true;
        string extra = "";
        if (op == "stock")
        {
            detail = OZ_ProbeState.Stock(m_Crate);
        }
        else if (op == "inspect")
        {
            int hash;
            string text = OZ_ProbeState.Inspect(m_Crate, hash);
            array<string> lines = new array<string>();
            text.Split("\n", lines);
            for (int i = 0; i < lines.Count(); i++)
            {
                if (lines.Get(i) != "")
                    AppendLine(RESULTS, "inspect " + lines.Get(i));
            }
            detail = "inspected " + lines.Count() + " item(s), hash=" + hash + ", cargo=" + CargoCount(m_Crate);
            extra = ",\"hash\":" + hash;
        }
        else if (op == "blob_save")
        {
            ok = OZ_ProbeState.SaveBlob(m_Crate, detail);
        }
        else if (op == "blob_load")
        {
            int created;
            int failed;
            int loadFails;
            ok = OZ_ProbeState.LoadBlob(m_Crate, detail, created, failed, loadFails);
            extra = ",\"created\":" + created + ",\"missed\":" + failed + ",\"load_refusals\":" + loadFails;
        }
        else if (op == "nest")
        {
            string bagType = Arg(args, "item", "TaloonBag_Blue");
            string childType = Arg(args, "child", "Paper");
            int children = Arg(args, "children", "10").ToInt();
            int bags = Arg(args, "n", "5").ToInt();
            detail = OZ_ProbeState.Nest(m_Crate, bagType, childType, children, bags);
        }
        else if (op == "give")
        {
            array<Man> players = new array<Man>();
            GetGame().GetPlayers(players);
            if (players.Count() == 0)
            {
                detail = "nobody is connected";
                ok = false;
            }
            else
            {
                detail = OZ_ProbeState.Give(m_Crate, players.Get(0));
            }
        }
        float ms = (GetGame().GetTickTime() - t0) * 1000;
        detail = detail + " [" + OZ_ProbeFrameStats.R1(ms) + " ms]";
        string line = "{\"op\":\"" + op + "\",\"ok\":" + BoolText(ok) + ",\"ms\":" + OZ_ProbeFrameStats.R1(ms);
        line = line + extra + ",\"detail\":\"" + Escape(detail) + "\"}";
        AppendLine(RESULTS, line);
        Print("[OpenZone] storage probe " + op + ": " + detail);
        return ok;
    }

    protected bool CmdCrate(map<string, string> args, out string detail)
    {
        string posText = Arg(args, "pos", "");
        vector pos;
        if (posText == "")
        {
            if (!PlayerPosition(pos))
            {
                detail = "crate needs pos=\"x y z\" when nobody is connected";
                return false;
            }
        }
        else
        {
            pos = posText.ToVector();
        }

        string type = "OZ_ProbeCrate";
        string size = Arg(args, "size", "big");
        if (size == "small")
            type = "OZ_ProbeCrateSmall";
        else if (size == "wide")
            type = "OZ_ProbeCrateWide";
        else if (size == "huge")
            type = "OZ_ProbeCrateHuge";
        else if (size == "square")
            type = "OZ_ProbeCrateSquare";

        float t0 = GetGame().GetTickTime();
        Object o = GetGame().CreateObjectEx(type, pos, ECE_PLACE_ON_SURFACE | ECE_NOLIFETIME);
        float ms = (GetGame().GetTickTime() - t0) * 1000;
        EntityAI crate = EntityAI.Cast(o);
        if (!crate)
        {
            detail = "the engine returned nothing for " + type + " at " + pos.ToString();
            return false;
        }
        m_Crate = crate;

        string grid = GridText(crate);
        detail = "created " + type + " at " + crate.GetPosition().ToString();
        detail = detail + " in " + OZ_ProbeFrameStats.R1(ms) + " ms; cargo grid " + grid;
        Print("[OpenZone] storage probe: " + detail);
        return true;
    }

    protected bool CmdFind(map<string, string> args, out string detail)
    {
        vector pos;
        string posText = Arg(args, "pos", "");
        if (posText != "")
            pos = posText.ToVector();
        else if (!PlayerPosition(pos))
        {
            detail = "find needs pos=\"x y z\" when nobody is connected";
            return false;
        }

        EntityAI crate = NearestCrate(pos, 100);
        if (!crate)
        {
            detail = "no OZ_ProbeCrate within 100 m of " + pos.ToString();
            return false;
        }
        m_Crate = crate;
        detail = "using " + crate.GetType() + " at " + crate.GetPosition().ToString();
        detail = detail + "; cargo grid " + GridText(crate) + "; items " + CargoCount(crate);
        return true;
    }

    protected bool CmdDeleteCrate(map<string, string> args, out string detail)
    {
        if (!m_Crate)
        {
            detail = "no crate selected; run op=crate or op=find first";
            return false;
        }
        int items = CargoCount(m_Crate);
        GetGame().ObjectDelete(m_Crate);
        m_Crate = null;
        detail = "crate deleted with " + items + " items still in cargo (the engine deletes them with it)";
        return true;
    }

    protected bool CmdFill(map<string, string> args, out string detail)
    {
        if (!RequireCrate(detail))
            return false;

        m_ItemType = Arg(args, "item", "Paper");
        // Weapons live in CfgWeapons and magazines in CfgMagazines, not CfgVehicles.
        string cfgPath = "";
        if (GetGame().ConfigIsExisting(CFG_VEHICLESPATH + " " + m_ItemType))
            cfgPath = CFG_VEHICLESPATH;
        else if (GetGame().ConfigIsExisting(CFG_WEAPONSPATH + " " + m_ItemType))
            cfgPath = CFG_WEAPONSPATH;
        else if (GetGame().ConfigIsExisting(CFG_MAGAZINESPATH + " " + m_ItemType))
            cfgPath = CFG_MAGAZINESPATH;
        if (cfgPath == "")
        {
            detail = "no config class '" + m_ItemType + "' in CfgVehicles, CfgWeapons or CfgMagazines";
            return false;
        }
        TIntArray size = new TIntArray();
        GetGame().ConfigGetIntArray(cfgPath + " " + m_ItemType + " itemSize", size);
        m_ItemW = 1;
        m_ItemH = 1;
        if (size.Count() >= 2)
        {
            m_ItemW = size.Get(0);
            m_ItemH = size.Get(1);
        }
        if (m_ItemW < 1)
            m_ItemW = 1;
        if (m_ItemH < 1)
            m_ItemH = 1;

        m_Mode = Arg(args, "mode", "loc");
        if (m_Mode != "loc" && m_Mode != "find" && m_Mode != "ex")
        {
            detail = "mode must be loc, find or ex";
            return false;
        }

        // Composite items: attachments created on every item (attach="Mag_AKM_30Rnd,AK_WoodBttstck")
        // and, for containers, items created in every item's own cargo (cargo="Paper,Paper").
        m_Attach.Clear();
        m_Cargo.Clear();
        string attachText = Arg(args, "attach", "");
        if (attachText != "")
            attachText.Split(",", m_Attach);
        string cargoText = Arg(args, "cargo", "");
        if (cargoText != "")
            cargoText.Split(",", m_Cargo);
        m_AttachText = attachText;
        m_CargoText = cargoText;
        m_CargoMode = Arg(args, "cargo_mode", "find");
        // persist=dyn creates with ECE_DYNAMIC_PERSISTENCY (mode=loc only): the
        // engine's own "not saved until a player takes it" flag, see objectspawner.c.
        m_Persist = Arg(args, "persist", "normal");

        int n = Arg(args, "n", "100").ToInt();
        int batch = Arg(args, "batch", "0").ToInt();
        m_Cursor = Arg(args, "start", "-1").ToInt();
        if (m_Cursor < 0)
            m_Cursor = CargoCount(m_Crate);   // append after what is already there (1x1 assumption)

        Begin("fill", n, batch);
        // Enforce refuses long expressions ("Formula too complex"), so strings are built in steps.
        detail = "job " + m_JobId + " fill started: " + n + " x " + m_ItemType;
        detail = detail + " (" + m_ItemW + "x" + m_ItemH + ") mode=" + m_Mode;
        detail = detail + " batch=" + batch + " attach=" + m_Attach.Count() + " cargo=" + m_Cargo.Count();
        detail = detail + " into " + GridText(m_Crate) + " from cell " + m_Cursor;
        return true;
    }

    protected bool CmdClear(map<string, string> args, out string detail)
    {
        if (!RequireCrate(detail))
            return false;

        m_Items.Clear();
        CargoBase cargo = m_Crate.GetInventory().GetCargo();
        if (cargo)
        {
            int count = cargo.GetItemCount();
            for (int i = 0; i < count; i++)
                m_Items.Insert(cargo.GetItem(i));
        }
        int batch = Arg(args, "batch", "0").ToInt();
        Begin("clear", m_Items.Count(), batch);
        detail = "job " + m_JobId + " clear started: " + m_Items.Count() + " items, batch=" + batch;
        return true;
    }

    protected bool CmdCapture(map<string, string> args, out string detail)
    {
        if (!RequireCrate(detail))
            return false;

        m_Items.Clear();
        CargoBase cargo = m_Crate.GetInventory().GetCargo();
        if (cargo)
        {
            int count = cargo.GetItemCount();
            for (int i = 0; i < count; i++)
                m_Items.Insert(cargo.GetItem(i));
        }
        m_Snapshot = new OZ_ProbeSnapshot();
        m_Snapshot.crate = m_Crate.GetType();
        if (cargo)
        {
            m_Snapshot.width = cargo.GetWidth();
            m_Snapshot.height = cargo.GetHeight();
        }
        int batch = Arg(args, "batch", "0").ToInt();
        Begin("capture", m_Items.Count(), batch);
        detail = "job " + m_JobId + " capture started: " + m_Items.Count() + " items, batch=" + batch;
        return true;
    }

    // Single-frame by design: the point is to measure the blocking load.
    protected bool CmdLoad(map<string, string> args, out string detail)
    {
        if (!FileExist(SNAPSHOT))
        {
            detail = "no snapshot at " + SNAPSHOT + "; run op=capture first";
            return false;
        }
        Begin("load", 0, 0);
        OZ_ProbeSnapshot snap = new OZ_ProbeSnapshot();
        string err;
        float t0 = GetGame().GetTickTime();
        int c0 = TickCount(0);
        bool ok = JsonFileLoader<OZ_ProbeSnapshot>.LoadFile(SNAPSHOT, snap, err);
        int ticks = TickCount(c0);
        float ms = (GetGame().GetTickTime() - t0) * 1000;
        m_WorkMsSum = ms;
        m_WorkMsMax = ms;
        m_WorkTicksSum = ticks;
        m_Batches = 1;
        int items = 0;
        if (ok)
        {
            m_Snapshot = snap;
            items = snap.items.Count();
        }
        m_Extra = "\"load_ok\":" + BoolText(ok) + ",\"items\":" + items;
        if (!ok)
            m_Extra = m_Extra + ",\"load_error\":\"" + Escape(err) + "\"";
        detail = "job " + m_JobId + " load: ok=" + ok + " items=" + items;
        detail = detail + " in " + OZ_ProbeFrameStats.R1(ms) + " ms (" + ticks + " ticks)";
        detail = detail + "; watching " + COOLDOWN_SECONDS + " s";
        return true;
    }

    protected bool CmdRestore(map<string, string> args, out string detail)
    {
        if (!RequireCrate(detail))
            return false;
        if (!m_Snapshot || m_Snapshot.items.Count() == 0)
        {
            detail = "no snapshot in memory; run op=capture or op=load first";
            return false;
        }
        int batch = Arg(args, "batch", "0").ToInt();
        Begin("restore", m_Snapshot.items.Count(), batch);
        detail = "job " + m_JobId + " restore started: " + m_Snapshot.items.Count() + " items, batch=" + batch + " into " + GridText(m_Crate);
        return true;
    }

    // ---- job mechanics ----------------------------------------------------

    protected void Begin(string op, int target, int batch)
    {
        m_JobId++;
        m_Op = op;
        m_Target = target;
        m_Done = 0;
        m_Failed = 0;
        m_FirstFailAt = -1;
        m_Batch = batch;
        m_GapMs = m_NextGapMs;
        m_NextGapMs = 0;
        m_LastBatchAt = 0;
        m_StartedAt = GetGame().GetTickTime();
        m_Frames = 0;
        m_WorkMsSum = 0;
        m_WorkMsMax = 0;
        m_WorkTicksSum = 0;
        m_Batches = 0;
        m_CooldownUntil = 0;
        m_InitsAtStart = OZ_ProbeCounters.s_ItemInits;
        m_DeletesAtStart = OZ_ProbeCounters.s_ItemDeletes;
        m_Extra = "";
        m_Children = 0;
        m_ChildMiss = 0;
        m_Frame.Reset();
        if (m_Crate)
        {
            CargoBase cargo = m_Crate.GetInventory().GetCargo();
            if (cargo)
            {
                m_GridW = cargo.GetWidth();
                m_GridH = cargo.GetHeight();
            }
        }
        Print("[OpenZone] storage probe: job " + m_JobId + " " + op + " begins, target " + target + ", batch " + batch);
    }

    protected void DoBatch()
    {
        int left = m_Target - m_Done - m_Failed;
        int n = left;
        if (m_Batch > 0 && n > m_Batch)
            n = m_Batch;

        float t0 = GetGame().GetTickTime();
        int c0 = TickCount(0);
        for (int i = 0; i < n; i++)
        {
            if (m_Op == "fill")
                FillOne();
            else if (m_Op == "clear")
                ClearOne();
            else if (m_Op == "capture")
                CaptureOne();
            else if (m_Op == "restore")
                RestoreOne();
            else
                m_Failed++;
        }
        int ticks = TickCount(c0);
        float ms = (GetGame().GetTickTime() - t0) * 1000;
        m_WorkMsSum += ms;
        m_WorkTicksSum += ticks;
        if (ms > m_WorkMsMax)
            m_WorkMsMax = ms;
        m_Batches++;

        if (m_Done + m_Failed >= m_Target)
            AfterLastBatch();
    }

    protected void AfterLastBatch()
    {
        if (m_Op == "capture")
        {
            string json;
            float t0 = GetGame().GetTickTime();
            int c0 = TickCount(0);
            JsonSerializer js = new JsonSerializer();
            bool ok = js.WriteToString(m_Snapshot, false, json);
            int ticksSer = TickCount(c0);
            float msSer = (GetGame().GetTickTime() - t0) * 1000;

            string err;
            t0 = GetGame().GetTickTime();
            c0 = TickCount(0);
            bool saved = JsonFileLoader<OZ_ProbeSnapshot>.SaveFile(SNAPSHOT, m_Snapshot, err);
            int ticksSave = TickCount(c0);
            float msSave = (GetGame().GetTickTime() - t0) * 1000;

            // The compact "own window" payload: what a list UI would send instead of entities.
            int compactBytes = 0;
            t0 = GetGame().GetTickTime();
            for (int i = 0; i < m_Snapshot.items.Count(); i++)
            {
                OZ_ProbeItemRecord r = m_Snapshot.items.Get(i);
                int hp = Math.Round(r.health * 100);
                int qty = Math.Round(r.quantity);
                string row = r.type + "|" + r.row + "|" + r.col + "|" + hp;
                row = row + "|" + qty + ";";
                compactBytes += row.Length();
            }
            float msCompact = (GetGame().GetTickTime() - t0) * 1000;

            m_Extra = "\"json_ok\":" + BoolText(ok) + ",\"json_bytes\":" + json.Length();
            m_Extra = m_Extra + ",\"json_ms\":" + OZ_ProbeFrameStats.R1(msSer) + ",\"json_ticks\":" + ticksSer;
            m_Extra = m_Extra + ",\"save_ok\":" + BoolText(saved) + ",\"save_ms\":" + OZ_ProbeFrameStats.R1(msSave);
            m_Extra = m_Extra + ",\"save_ticks\":" + ticksSave + ",\"compact_bytes\":" + compactBytes;
            m_Extra = m_Extra + ",\"compact_ms\":" + OZ_ProbeFrameStats.R1(msCompact);
            if (!saved)
                m_Extra = m_Extra + ",\"save_error\":\"" + Escape(err) + "\"";
        }
    }

    protected void FillOne()
    {
        int perRow = m_GridW / m_ItemW;
        if (perRow < 1)
            perRow = 1;
        int idx = m_Cursor;
        int row = (idx / perRow) * m_ItemH;
        int col = (idx % perRow) * m_ItemW;
        m_Cursor++;

        EntityAI created;
        if (m_Mode == "find")
        {
            created = m_Crate.GetInventory().CreateEntityInCargo(m_ItemType);
        }
        else if (m_Mode == "ex")
        {
            created = m_Crate.GetInventory().CreateEntityInCargoEx(m_ItemType, 0, row, col, false);
        }
        else
        {
            InventoryLocation loc = new InventoryLocation();
            loc.SetCargo(m_Crate, null, 0, row, col, false);
            int flags = ECE_IN_INVENTORY;
            if (m_Persist == "dyn")
                flags = flags | ECE_DYNAMIC_PERSISTENCY;
            created = GameInventory.LocationCreateEntity(loc, m_ItemType, flags, RF_DEFAULT);
        }

        if (!created)
        {
            m_Failed++;
            if (m_FirstFailAt < 0)
                m_FirstFailAt = idx;
            return;
        }
        m_Done++;

        int a;
        for (a = 0; a < m_Attach.Count(); a++)
        {
            EntityAI att = created.GetInventory().CreateAttachment(m_Attach.Get(a));
            if (att)
                m_Children++;
            else
                m_ChildMiss++;
        }
        // Children in the new item's own cargo. Three paths, because a
        // container that sits in cargo refuses the normal path
        // (Container_Base.CanReceiveItemIntoCargo -> !AreChildrenAccessible):
        //   find  CreateEntityInCargo (engine finds a cell; goes through CanReceive)
        //   ex    CreateEntityInCargoEx at an explicit cell (one-slot children assumed)
        //   loc   LocationCreateEntity with SetCargo on the child location
        int innerW = 1;
        CargoBase innerCargo = created.GetInventory().GetCargo();
        if (innerCargo)
            innerW = innerCargo.GetWidth();
        if (innerW < 1)
            innerW = 1;
        for (a = 0; a < m_Cargo.Count(); a++)
        {
            int innerRow = a / innerW;
            int innerCol = a % innerW;
            EntityAI inner;
            if (m_CargoMode == "ex")
            {
                inner = created.GetInventory().CreateEntityInCargoEx(m_Cargo.Get(a), 0, innerRow, innerCol, false);
            }
            else if (m_CargoMode == "loc")
            {
                InventoryLocation innerLoc = new InventoryLocation();
                innerLoc.SetCargo(created, null, 0, innerRow, innerCol, false);
                inner = GameInventory.LocationCreateEntity(innerLoc, m_Cargo.Get(a), ECE_IN_INVENTORY, RF_DEFAULT);
            }
            else
            {
                inner = created.GetInventory().CreateEntityInCargo(m_Cargo.Get(a));
            }
            if (inner)
                m_Children++;
            else
                m_ChildMiss++;
        }
    }

    protected void ClearOne()
    {
        int i = m_Done + m_Failed;
        EntityAI e = m_Items.Get(i);
        if (e)
        {
            GetGame().ObjectDelete(e);
            m_Done++;
        }
        else
        {
            m_Failed++;
        }
    }

    protected void CaptureOne()
    {
        int i = m_Done + m_Failed;
        EntityAI e = m_Items.Get(i);
        if (!e)
        {
            m_Failed++;
            return;
        }
        OZ_ProbeItemRecord r = new OZ_ProbeItemRecord();
        r.type = e.GetType();
        InventoryLocation loc = new InventoryLocation();
        if (e.GetInventory().GetCurrentInventoryLocation(loc))
        {
            r.row = loc.GetRow();
            r.col = loc.GetCol();
            r.flip = loc.GetFlip();
        }
        r.health = e.GetHealth01("", "");
        ItemBase item = ItemBase.Cast(e);
        if (item)
            r.quantity = item.GetQuantity();
        m_Snapshot.items.Insert(r);
        m_Done++;
    }

    protected void RestoreOne()
    {
        int i = m_Done + m_Failed;
        OZ_ProbeItemRecord r = m_Snapshot.items.Get(i);
        InventoryLocation loc = new InventoryLocation();
        loc.SetCargo(m_Crate, null, 0, r.row, r.col, r.flip);
        EntityAI created = GameInventory.LocationCreateEntity(loc, r.type, ECE_IN_INVENTORY, RF_DEFAULT);
        if (!created)
        {
            m_Failed++;
            if (m_FirstFailAt < 0)
                m_FirstFailAt = i;
            return;
        }
        created.SetHealth01("", "", r.health);
        ItemBase item = ItemBase.Cast(created);
        if (item && r.quantity > 0)
            item.SetQuantity(r.quantity);
        m_Done++;
    }

    protected void Finish()
    {
        float wallMs = (GetGame().GetTickTime() - m_StartedAt) * 1000;
        int cargoCount = -1;
        if (m_Crate)
            cargoCount = CargoCount(m_Crate);

        int initsDelta = OZ_ProbeCounters.s_ItemInits - m_InitsAtStart;
        int deletesDelta = OZ_ProbeCounters.s_ItemDeletes - m_DeletesAtStart;

        string line = "{\"job\":" + m_JobId + ",\"op\":\"" + m_Op + "\"";
        if (m_Op == "fill")
        {
            line = line + ",\"item\":\"" + m_ItemType + "\",\"item_size\":\"" + m_ItemW + "x" + m_ItemH + "\"";
            line = line + ",\"mode\":\"" + m_Mode + "\",\"attach\":\"" + m_AttachText + "\"";
            line = line + ",\"cargo\":\"" + m_CargoText + "\",\"cargo_mode\":\"" + m_CargoMode + "\",\"children\":" + m_Children;
            line = line + ",\"children_missed\":" + m_ChildMiss + ",\"persist\":\"" + m_Persist + "\"";
        }
        line = line + ",\"target\":" + m_Target + ",\"done\":" + m_Done + ",\"missed\":" + m_Failed;
        line = line + ",\"first_miss_at\":" + m_FirstFailAt + ",\"batch\":" + m_Batch + ",\"batches\":" + m_Batches;
        line = line + ",\"gap_ms\":" + m_GapMs;
        line = line + ",\"grid\":\"" + m_GridW + "x" + m_GridH + "\"";
        line = line + ",\"wall_ms\":" + OZ_ProbeFrameStats.R1(wallMs) + ",\"work_ms_sum\":" + OZ_ProbeFrameStats.R1(m_WorkMsSum);
        line = line + ",\"work_ms_max\":" + OZ_ProbeFrameStats.R1(m_WorkMsMax) + ",\"work_ticks_sum\":" + m_WorkTicksSum;
        line = line + ",\"inits_delta\":" + initsDelta + ",\"deletes_delta\":" + deletesDelta;
        line = line + ",\"cargo_count\":" + cargoCount;
        line = line + "," + m_Frame.Json("frame_");
        if (m_Extra != "")
            line = line + "," + m_Extra;
        line = line + ",\"tick_time\":" + OZ_ProbeFrameStats.R1(GetGame().GetTickTime()) + "}";

        AppendLine(RESULTS, line);
        Print("[OpenZone] storage probe result " + line);

        m_LastResult = "job " + m_JobId + " " + m_Op + " done=" + m_Done + " missed=" + m_Failed;
        m_LastResult = m_LastResult + " wall=" + OZ_ProbeFrameStats.R1(wallMs) + "ms work=" + OZ_ProbeFrameStats.R1(m_WorkMsSum);
        m_LastResult = m_LastResult + "ms(max " + OZ_ProbeFrameStats.R1(m_WorkMsMax) + ") " + m_Frame.Text();
        m_JobsDone++;
        m_Op = "";
        m_Items.Clear();
    }

    protected string Status()
    {
        if (m_Op == "")
        {
            if (m_LastResult == "")
                return "idle, no job yet; idle " + m_Idle.Text();
            return "idle; last: " + m_LastResult;
        }
        int processed = m_Done + m_Failed;
        string s = "running job " + m_JobId + " " + m_Op + ": " + processed + "/" + m_Target;
        float left = 0;
        if (m_CooldownUntil > 0)
            left = m_CooldownUntil - GetGame().GetTickTime();
        s = s + " (missed " + m_Failed + "), frames " + m_Frames + ", cooldown " + OZ_ProbeFrameStats.R1(left) + "s";
        s = s + ", " + m_Frame.Text();
        return s;
    }

    // ---- helpers ----------------------------------------------------------

    protected bool RequireCrate(out string detail)
    {
        if (!m_Crate)
        {
            detail = "no crate selected; run op=crate or op=find first";
            return false;
        }
        return true;
    }

    protected string Arg(map<string, string> args, string key, string fallback)
    {
        if (!args)
            return fallback;
        string v;
        if (args.Find(key, v))
            return v;
        return fallback;
    }

    protected bool PlayerPosition(out vector pos)
    {
        array<Man> players = new array<Man>();
        GetGame().GetPlayers(players);
        if (players.Count() == 0)
            return false;
        pos = players.Get(0).GetPosition();
        return true;
    }

    protected EntityAI NearestCrate(vector pos, float radius)
    {
        array<Object> objects = new array<Object>();
        array<CargoBase> proxies = new array<CargoBase>();
        GetGame().GetObjectsAtPosition3D(pos, radius, objects, proxies);
        EntityAI best;
        float bestDist = radius + 1;
        for (int i = 0; i < objects.Count(); i++)
        {
            Object o = objects.Get(i);
            if (!o.IsKindOf("OZ_ProbeCrate"))
                continue;
            float d = vector.Distance(o.GetPosition(), pos);
            if (d < bestDist)
            {
                bestDist = d;
                best = EntityAI.Cast(o);
            }
        }
        return best;
    }

    protected string GridText(EntityAI crate)
    {
        CargoBase cargo = crate.GetInventory().GetCargo();
        if (!cargo)
            return "none";
        return cargo.GetWidth().ToString() + "x" + cargo.GetHeight().ToString();
    }

    protected int CargoCount(EntityAI crate)
    {
        CargoBase cargo = crate.GetInventory().GetCargo();
        if (!cargo)
            return -1;
        return cargo.GetItemCount();
    }

    protected string BoolText(bool b)
    {
        if (b)
            return "true";
        return "false";
    }

    protected string Escape(string s)
    {
        string r = s;
        r.Replace("\"", "'");
        r.Replace("\n", " ");
        return r;
    }

    protected void AppendLine(string path, string line)
    {
        FileHandle fh = OpenFile(path, FileMode.APPEND);
        if (!fh)
        {
            Print("[OpenZone] storage probe: could not open " + path + " for append");
            return;
        }
        FPrintln(fh, line);
        CloseFile(fh);
    }
}
