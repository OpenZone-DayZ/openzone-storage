// Frame-time accumulator shared by the server and the client monitors.
//
// Fed with MissionServer.OnUpdate / MissionGameplay.OnUpdate `timeslice`, i.e.
// the length of the PREVIOUS frame. A batch of work done inside frame N is
// therefore visible in the sample delivered at frame N+1, which is why every
// job keeps monitoring for a cooldown after its last batch.
class OZ_ProbeFrameStats
{
    int   frames;
    float sumMs;
    float maxMs;
    int   over100;
    int   over150;
    int   over500;

    void OZ_ProbeFrameStats()
    {
        Reset();
    }

    void Reset()
    {
        frames  = 0;
        sumMs   = 0;
        maxMs   = 0;
        over100 = 0;
        over150 = 0;
        over500 = 0;
    }

    void Add(float dtMs)
    {
        frames++;
        sumMs += dtMs;
        if (dtMs > maxMs)
            maxMs = dtMs;
        if (dtMs > 100)
            over100++;
        if (dtMs > 150)
            over150++;
        if (dtMs > 500)
            over500++;
    }

    float AvgMs()
    {
        if (frames == 0)
            return 0;
        return sumMs / frames;
    }

    // JSON fragment without braces, so callers can splice it into their own object.
    string Json(string prefix)
    {
        string s = "\"" + prefix + "frames\":" + frames;
        s = s + ",\"" + prefix + "avg_ms\":" + OZ_ProbeFrameStats.R1(AvgMs());
        s = s + ",\"" + prefix + "max_ms\":" + OZ_ProbeFrameStats.R1(maxMs);
        s = s + ",\"" + prefix + "over100\":" + over100;
        s = s + ",\"" + prefix + "over150\":" + over150;
        s = s + ",\"" + prefix + "over500\":" + over500;
        return s;
    }

    string Text()
    {
        string s = "frames=" + frames + " avg=" + OZ_ProbeFrameStats.R1(AvgMs()) + "ms";
        s = s + " max=" + OZ_ProbeFrameStats.R1(maxMs) + "ms >100ms=" + over100;
        s = s + " >150ms=" + over150 + " >500ms=" + over500;
        return s;
    }

    // One decimal, as text. Enforce prints floats with many digits otherwise.
    static string R1(float v)
    {
        float r = Math.Round(v * 10) / 10;
        return r.ToString();
    }
}
