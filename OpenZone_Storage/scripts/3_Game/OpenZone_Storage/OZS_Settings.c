// Admin-tunable numbers, $profile:OpenZone/OZ_Storage.json, loaded through the
// core's config service (defaults written on first boot, quarantine on a broken
// file, migration by Version). Every field has a default and a sane range;
// Validate clamps and warns rather than refusing the file.
class OZS_Settings : OZ_ConfigBase
{
    // OPENING: how much of one server frame the materialisation may take, and
    // how many entities per second may appear -- the second limit protects
    // the clients next to the box (measured 2026-09-16: 250/s never stalled
    // a client, one 5000-burst did).
    int OpenFrameBudgetMs;
    int OpenItemsPerSecond;
    // CLOSING: how much of one frame the capture may take (0.17 ms per
    // entity measured), and entities deleted per frame after the files are
    // written.
    int CloseFrameBudgetMs;
    int CloseDeletesPerFrame;
    // Auto-close: no viewers and no player within the radius for this long.
    float AutoCloseRadius;
    int AutoCloseQuietSeconds;
    // Viewers: the client heartbeat while its inventory screen shows the box,
    // the silence after which a viewer is dropped, and the distance beyond
    // which a viewer is dropped regardless (twice the engine's 2.5 m reach).
    int ViewerHeartbeatSeconds;
    int ViewerTimeoutSeconds;
    float ViewerMaxDistance;
    bool DebugLog;

    private static ref OZS_Settings s_Inst;

    override int LatestVersion()
    {
        return 1;
    }

    override void LoadDefaults()
    {
        Version = LatestVersion();
        OpenFrameBudgetMs = 20;
        OpenItemsPerSecond = 250;
        CloseFrameBudgetMs = 20;
        CloseDeletesPerFrame = 50;
        AutoCloseRadius = 15;
        AutoCloseQuietSeconds = 120;
        ViewerHeartbeatSeconds = 5;
        ViewerTimeoutSeconds = 15;
        ViewerMaxDistance = 5;
        DebugLog = false;
    }

    override bool Migrate(int from)
    {
        Version = LatestVersion();
        return true;
    }

    override void Validate(out int warnings)
    {
        warnings = 0;
        if (OpenFrameBudgetMs < 1 || OpenFrameBudgetMs > 100)
        {
            OZ_Log.Warn("storage settings: OpenFrameBudgetMs " + OpenFrameBudgetMs + " is outside 1..100, using 20");
            OpenFrameBudgetMs = 20;
            warnings++;
        }
        if (OpenItemsPerSecond < 10 || OpenItemsPerSecond > 5000)
        {
            OZ_Log.Warn("storage settings: OpenItemsPerSecond " + OpenItemsPerSecond + " is outside 10..5000, using 250");
            OpenItemsPerSecond = 250;
            warnings++;
        }
        if (CloseFrameBudgetMs < 1 || CloseFrameBudgetMs > 100)
        {
            OZ_Log.Warn("storage settings: CloseFrameBudgetMs " + CloseFrameBudgetMs + " is outside 1..100, using 20");
            CloseFrameBudgetMs = 20;
            warnings++;
        }
        if (CloseDeletesPerFrame < 1 || CloseDeletesPerFrame > 1000)
        {
            OZ_Log.Warn("storage settings: CloseDeletesPerFrame " + CloseDeletesPerFrame + " is outside 1..1000, using 50");
            CloseDeletesPerFrame = 50;
            warnings++;
        }
        if (AutoCloseRadius < 3 || AutoCloseRadius > 200)
        {
            OZ_Log.Warn("storage settings: AutoCloseRadius " + AutoCloseRadius + " is outside 3..200, using 15");
            AutoCloseRadius = 15;
            warnings++;
        }
        if (AutoCloseQuietSeconds < 10 || AutoCloseQuietSeconds > 86400)
        {
            OZ_Log.Warn("storage settings: AutoCloseQuietSeconds " + AutoCloseQuietSeconds + " is outside 10..86400, using 120");
            AutoCloseQuietSeconds = 120;
            warnings++;
        }
        if (ViewerHeartbeatSeconds < 1 || ViewerHeartbeatSeconds > 60)
        {
            OZ_Log.Warn("storage settings: ViewerHeartbeatSeconds " + ViewerHeartbeatSeconds + " is outside 1..60, using 5");
            ViewerHeartbeatSeconds = 5;
            warnings++;
        }
        if (ViewerTimeoutSeconds < ViewerHeartbeatSeconds * 2 || ViewerTimeoutSeconds > 300)
        {
            OZ_Log.Warn("storage settings: ViewerTimeoutSeconds " + ViewerTimeoutSeconds + " must be at least twice the heartbeat and at most 300, using " + (ViewerHeartbeatSeconds * 3));
            ViewerTimeoutSeconds = ViewerHeartbeatSeconds * 3;
            warnings++;
        }
        if (ViewerMaxDistance < 3 || ViewerMaxDistance > 50)
        {
            OZ_Log.Warn("storage settings: ViewerMaxDistance " + ViewerMaxDistance + " is outside 3..50, using 5");
            ViewerMaxDistance = 5;
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
        s = s + " close=" + s_Inst.CloseFrameBudgetMs + "ms/" + s_Inst.CloseDeletesPerFrame + " autoclose=" + s_Inst.AutoCloseRadius + "m/" + s_Inst.AutoCloseQuietSeconds + "s";
        s = s + " viewers=" + s_Inst.ViewerHeartbeatSeconds + "/" + s_Inst.ViewerTimeoutSeconds + "s/" + s_Inst.ViewerMaxDistance + "m";
        OZ_Log.Info(s);
    }
}
