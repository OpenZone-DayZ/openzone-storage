// Admin-tunable numbers, $profile:OpenZone/OZ_Storage.json, loaded through the
// core's config service (defaults written on first boot, quarantine on a broken
// file, migration by Version). Every field has a default and a sane range;
// Validate clamps and warns rather than refusing the file.
class OZS_Settings : OZ_ConfigBase
{
    // FILLING: how much of one server frame the materialisation of a box's
    // contents into its authority may take, and how many entities per second
    // may appear -- the second limit protects the clients next to the box
    // (measured 2026-09-16: 250/s never stalled a client, one 5000-burst
    // did).
    int OpenFrameBudgetMs;
    int OpenItemsPerSecond;
    // FAKE PING, STAND ONLY. Milliseconds added to every operation on its
    // way in -- the whole round trip, on one side, because the player only
    // ever sees the moment the answer arrives. The stand and the client
    // share a machine, so without this the box is judged at a latency no
    // player will ever have. 0 is off, which is what a real server runs.
    //
    // It used to hold the OUTBOUND message back instead, and a built
    // ScriptRPC does not survive the frame it was made in: the box stopped
    // opening (measured 2026-09-25). An operation is a handful of numbers
    // and keeps as long as it likes (OZS_Session.OperateAs).
    int FakePingMs;
    // HOLD THE ITEM UNTIL THE RECORD HAS TAKEN THE TURN. Off by default, which
    // is the design's own choice (§7): the item is handed over in the same
    // frame and the letter is only on its way, so a session that fails
    // afterwards leaves the item with the player AND in the record. On, the
    // hand-out waits for the bridge's answer -- nothing can be duplicated, and
    // the price is one round trip per take-out (measured ~100 ms, and the wire
    // figure in a session's status line says what it is on this server).
    bool WaitForRecord;
    // The PROXY (design 2026-09-24). A box's contents go to one player as
    // chunked RPCs; these say how big a chunk is and how many chunks may
    // leave in one frame. ProxyIdleSeconds is how long an authoritative
    // container waits after the last watcher left -- this is about memory,
    // not about hiding loot, so it is short.
    int ProxyRowsPerMessage;
    int ProxyMessagesPerFrame;
    int ProxyIdleSeconds;
    bool DebugLog;

    // WHAT WENT ON 2026-09-26, with the old scheme: CloseFrameBudgetMs and
    // CloseDeletesPerFrame paced a close job that captured a placed box's
    // cargo into SQL; AutoCloseSeconds closed such a box when nobody touched
    // it; StashIdleSeconds and StashMaxDistance did the same for a physical
    // stash. Under the proxy the placed box never holds anything, a session
    // ends when its last watcher leaves (ProxyIdleSeconds), and the leash
    // that keeps a player near the box is OZS_Const.SESSION_LEASH. A file
    // that still carries those names is read without them.

    private static ref OZS_Settings s_Inst;

    override int LatestVersion()
    {
        return 6;
    }

    override void LoadDefaults()
    {
        // 5 ms: the sampling profiler (script-profile.ps1, 200 Hz) merges
        // back-to-back frames of 20 ms into one "freeze" of their sum, and
        // resolves frames of 5 ms (measured 2026-09-16); the jobs need far
        // less per frame anyway (an open at 250/s takes about 2 ms).
        Version = LatestVersion();
        OpenFrameBudgetMs = 5;
        // 500/s measured 2026-09-17 with a client standing at the box: a
        // 1443-item box in 2.9 s, longest server step 2 ms, client frames
        // under 15 ms. 250/s was twice as slow for nothing; 1000/s cut it to
        // 1.5 s but produced one 54 ms server step.
        OpenItemsPerSecond = 500;
        // 40 rows per message and 4 messages a frame: the size is measured
        // (see docs/measurements/2026-09-24), the pace is 160 items a frame,
        // which puts a thousand-item box on a client inside a second without
        // a burst either end would feel.
        ProxyRowsPerMessage = 40;
        ProxyMessagesPerFrame = 4;
        ProxyIdleSeconds = 20;
        DebugLog = false;
        FakePingMs = 0;
        WaitForRecord = false;
    }

    // A missing number reads as zero rather than as a default, so a field a
    // version did not know is filled here -- otherwise the first Validate
    // would clamp it and warn about a number the admin never wrote.
    override bool Migrate(int from)
    {
        if (from < 3)
        {
            ProxyRowsPerMessage = 40;
            ProxyMessagesPerFrame = 4;
            ProxyIdleSeconds = 20;
        }
        if (from < 4)
        {
            FakePingMs = 0;
            WaitForRecord = false;
        }
        // v4 -> v5 (2026-09-26): the three Viewer* knobs went with the
        // client's vicinity scan. v5 -> v6 (2026-09-26): the five knobs of
        // the old scheme went with it (see the note above). Nothing to fill
        // in either time; a file written earlier still carries the names,
        // and they sit there until the next save rewrites it without them.
        Version = LatestVersion();
        return true;
    }

    override void Validate(out int warnings)
    {
        warnings = 0;
        if (OpenFrameBudgetMs < 1 || OpenFrameBudgetMs > 100)
        {
            OZ_Log.Warn("storage settings: OpenFrameBudgetMs " + OpenFrameBudgetMs + " is outside 1..100, using 5");
            OpenFrameBudgetMs = 5;
            warnings++;
        }
        if (OpenItemsPerSecond < 10 || OpenItemsPerSecond > 5000)
        {
            OZ_Log.Warn("storage settings: OpenItemsPerSecond " + OpenItemsPerSecond + " is outside 10..5000, using 500");
            OpenItemsPerSecond = 500;
            warnings++;
        }
        if (FakePingMs < 0 || FakePingMs > 2000)
        {
            OZ_Log.Warn("storage settings: FakePingMs " + FakePingMs + " is outside 0..2000, using 0");
            FakePingMs = 0;
            warnings++;
        }
        if (FakePingMs > 0)
            OZ_Log.Warn("storage settings: FakePingMs is " + FakePingMs + " -- every operation is delayed on purpose. This is a stand setting; a live server leaves it at 0");
        if (ProxyRowsPerMessage < 1 || ProxyRowsPerMessage > 200)
        {
            OZ_Log.Warn("storage settings: ProxyRowsPerMessage " + ProxyRowsPerMessage + " is outside 1..200, using 40");
            ProxyRowsPerMessage = 40;
            warnings++;
        }
        if (ProxyMessagesPerFrame < 1 || ProxyMessagesPerFrame > 100)
        {
            OZ_Log.Warn("storage settings: ProxyMessagesPerFrame " + ProxyMessagesPerFrame + " is outside 1..100, using 4");
            ProxyMessagesPerFrame = 4;
            warnings++;
        }
        if (ProxyIdleSeconds < 1 || ProxyIdleSeconds > 3600)
        {
            OZ_Log.Warn("storage settings: ProxyIdleSeconds " + ProxyIdleSeconds + " is outside 1..3600, using 20");
            ProxyIdleSeconds = 20;
            warnings++;
        }
    }

    // Defaults until Load() ran (the client never loads the file and never
    // needs anything but the defaults).
    static OZS_Settings Get()
    {
        if (!s_Inst)
        {
            s_Inst = new OZS_Settings();
            s_Inst.LoadDefaults();
        }
        return s_Inst;
    }

    // Server, once, from MissionServer.OnInit.
    static void Load()
    {
        s_Inst = new OZS_Settings();
        s_Inst.LoadDefaults();
        OZ_ConfigLoader<OZS_Settings>.Load(OZS_Const.SETTINGS, OZS_Const.SETTINGS_TAG, s_Inst);
        OZ_Log.SetDebug(s_Inst.DebugLog || OZ_Log.IsDebug());
        string s = "storage settings: budget=" + s_Inst.OpenFrameBudgetMs + "ms rate=" + s_Inst.OpenItemsPerSecond + "/s";
        s = s + " proxy=" + s_Inst.ProxyRowsPerMessage + "x" + s_Inst.ProxyMessagesPerFrame + "/frame idle=" + s_Inst.ProxyIdleSeconds + "s";
        s = s + " wait_for_record=" + s_Inst.WaitForRecord + " fake_ping=" + s_Inst.FakePingMs + "ms";
        OZ_Log.Info(s);
    }
}
