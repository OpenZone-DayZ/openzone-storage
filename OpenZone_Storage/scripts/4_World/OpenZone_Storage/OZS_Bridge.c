// The storage side of the bridge (design 2026-09-19, section 3): the
// letters the game posts, the answers it reads, the sink that keeps these
// routes out of the PDA's cache, and the replies that hand a bridge answer
// back to the job or the controller that asked.
//
// Every letter and answer is a class of public fields, serialised whole by
// JsonFileLoader: the field names are the JSON names the bridge reads. A
// field cannot be called `class` in Enforce, so the box class travels as
// `cls`; the bridge reads either spelling.

// ---- letters ----

class OZS_BootBox
{
    string id;
    string cls;
    string state;
    int entities;
    string pos;
}

class OZS_BootLetter
{
    ref array<ref OZS_BootBox> boxes;

    void OZS_BootLetter()
    {
        boxes = new array<ref OZS_BootBox>();
    }
}

class OZS_ClassesLetter
{
    ref array<string> missing;
    ref array<string> present;

    void OZS_ClassesLetter()
    {
        missing = new array<string>();
        present = new array<string>();
    }
}

class OZS_CloseLetter
{
    string id;
    string stamp;
    string file;
    int roots;
    int entities;
    string by;
    string why;
}

// open {id, by}, opened {id, stamp}, closed {id, version}: the bridge reads
// what each route needs and ignores the rest.
class OZS_IdLetter
{
    string id;
    string by;
    string stamp;
    int version;
}

class OZS_ParkLetter
{
    string id;
    string stamp;
    int root;
    string type;
    string why;
}

class OZS_EventRec
{
    string at;
    string kind;
    string box;
    string uid;
    string name;
    string type;
    float qty;
    int row;
    int col;
    string slot;
    string note;
}

class OZS_EventsLetter
{
    ref array<ref OZS_EventRec> events;

    void OZS_EventsLetter()
    {
        events = new array<ref OZS_EventRec>();
    }
}

// ---- answers ----

class OZS_Answer
{
    bool ok;
    string why;
}

class OZS_BootAnswerBox
{
    string id;
    string status;
    int version;
    int roots;
}

class OZS_BootAnswer
{
    bool ok;
    string why;
    ref array<ref OZS_BootAnswerBox> boxes;
    ref array<string> classes;
}

class OZS_ClassesAnswer
{
    bool ok;
    string why;
    int parked;
    int unparked;
}

class OZS_CloseAnswer
{
    bool ok;
    string why;
    int version;
    int roots;
    int entities;
}

class OZS_OpenAnswer
{
    bool ok;
    string why;
    bool empty;
    string file;
    string stamp;
    int roots;
    int entities;
}

class OZS_ParkAnswer
{
    bool ok;
    string why;
    int version;
    int parked;
    string type;
}

// ---- the sink ----

// Declares every storage route neutral: an unknown route counts as a write
// and clears the whole read cache of the bridge client, which would empty
// the PDA's pages on every batch of events.
class OZS_BridgeSink : OZ_BridgeSink
{
    override void Neutral(array<string> routes)
    {
        routes.Insert(OZS_Const.ROUTE_BOOT);
        routes.Insert(OZS_Const.ROUTE_CLASSES);
        routes.Insert(OZS_Const.ROUTE_CLOSE);
        routes.Insert(OZS_Const.ROUTE_CLOSED);
        routes.Insert(OZS_Const.ROUTE_OPEN);
        routes.Insert(OZS_Const.ROUTE_OPENED);
        routes.Insert(OZS_Const.ROUTE_PARK);
        routes.Insert(OZS_Const.ROUTE_EVENTS);
    }

    // A live command of the admin side (design section 3.5): close, remove,
    // report. The controller answers with an admin_result event.
    override void Deliver(string json)
    {
        OZS_CommandLetter c = new OZS_CommandLetter();
        string err;
        if (!JsonFileLoader<OZS_CommandLetter>.LoadData(json, c, err) || !c || c.cmd == "")
        {
            OZ_Log.Warn("storage: the bridge delivered a storage item that is not a command: " + err);
            return;
        }
        OZS_Controller.Get().AdminCommand(c);
    }
}

// What the bridge pushes into the poll for this mod.
class OZS_CommandLetter
{
    string cmd;
    string id;
    string by;
    string token;
}

class OZS_Bridge
{
    static ref OZS_BridgeSink s_Sink;

    static void Subscribe()
    {
        if (s_Sink)
            return;
        s_Sink = new OZS_BridgeSink();
        OZ_BridgeClient.Subscribe(OZS_Const.SINK_KIND, s_Sink);
    }

    // The bridge answered within the last minute. Every transition of a box
    // is refused while this is false (design section 6).
    static bool Up()
    {
        return OZ_BridgeClient.Alive();
    }

    static bool Post(string route, string letter, OZ_BridgeReply reply)
    {
        if (!Up())
        {
            if (reply)
                reply.OnFail(0);
            return false;
        }
        OZ_BridgeClient.Call(route, letter, reply);
        return true;
    }
}

// ---- replies ----

// A fire-and-forget route (opened, closed, events): only a refusal is
// worth a line.
class OZS_AckReply : OZ_BridgeReply
{
    protected string m_What;

    void OZS_AckReply(string what)
    {
        m_What = what;
    }

    override void OnBody(string json)
    {
        OZS_Answer a = new OZS_Answer();
        string err;
        if (JsonFileLoader<OZS_Answer>.LoadData(json, a, err) && a && !a.ok)
            OZ_Log.Warn("storage: the bridge refused " + m_What + ": " + a.why);
    }

    override void OnFail(int code)
    {
        OZ_Log.Warn("storage: the bridge did not answer " + m_What + " (code " + code.ToString() + ")");
    }
}

// The job is held weakly: a job the controller dropped is not called back.
class OZS_CloseReply : OZ_BridgeReply
{
    protected OZS_CloseJob m_Job;

    void OZS_CloseReply(OZS_CloseJob job)
    {
        m_Job = job;
    }

    override void OnBody(string json)
    {
        if (!m_Job)
            return;
        OZS_CloseAnswer a = new OZS_CloseAnswer();
        string err;
        if (!JsonFileLoader<OZS_CloseAnswer>.LoadData(json, a, err) || !a)
        {
            m_Job.OnBridge(false, "the answer cannot be read: " + err, 0);
            return;
        }
        m_Job.OnBridge(a.ok, a.why, a.version);
    }

    override void OnFail(int code)
    {
        if (m_Job)
            m_Job.OnBridge(false, "no answer (code " + code.ToString() + ")", 0);
    }
}

class OZS_OpenReply : OZ_BridgeReply
{
    protected OZS_OpenJob m_Job;

    void OZS_OpenReply(OZS_OpenJob job)
    {
        m_Job = job;
    }

    override void OnBody(string json)
    {
        if (!m_Job)
            return;
        OZS_OpenAnswer a = new OZS_OpenAnswer();
        string err;
        if (!JsonFileLoader<OZS_OpenAnswer>.LoadData(json, a, err) || !a)
        {
            m_Job.OnOpenFailed("the answer cannot be read: " + err);
            return;
        }
        m_Job.OnOpenAnswer(a);
    }

    override void OnFail(int code)
    {
        if (m_Job)
            m_Job.OnOpenFailed("no answer (code " + code.ToString() + ")");
    }
}

class OZS_ParkReply : OZ_BridgeReply
{
    protected OZS_OpenJob m_Job;

    void OZS_ParkReply(OZS_OpenJob job)
    {
        m_Job = job;
    }

    override void OnBody(string json)
    {
        if (!m_Job)
            return;
        OZS_ParkAnswer a = new OZS_ParkAnswer();
        string err;
        if (!JsonFileLoader<OZS_ParkAnswer>.LoadData(json, a, err) || !a)
        {
            m_Job.OnParked(false, "the answer cannot be read: " + err);
            return;
        }
        m_Job.OnParked(a.ok, a.why);
    }

    override void OnFail(int code)
    {
        if (m_Job)
            m_Job.OnParked(false, "no answer (code " + code.ToString() + ")");
    }
}

class OZS_BootReply : OZ_BridgeReply
{
    override void OnBody(string json)
    {
        OZS_BootAnswer a = new OZS_BootAnswer();
        string err;
        if (!JsonFileLoader<OZS_BootAnswer>.LoadData(json, a, err) || !a)
        {
            OZS_Controller.Get().OnBootAnswer(null, "the answer cannot be read: " + err);
            return;
        }
        OZS_Controller.Get().OnBootAnswer(a, "");
    }

    override void OnFail(int code)
    {
        OZS_Controller.Get().OnBootAnswer(null, "no answer (code " + code.ToString() + ")");
    }
}

class OZS_ClassesReply : OZ_BridgeReply
{
    override void OnBody(string json)
    {
        OZS_ClassesAnswer a = new OZS_ClassesAnswer();
        string err;
        if (!JsonFileLoader<OZS_ClassesAnswer>.LoadData(json, a, err) || !a)
        {
            OZ_Log.Warn("storage: the classes answer cannot be read: " + err);
            return;
        }
        if (!a.ok)
        {
            OZ_Log.Warn("storage: the bridge refused the classes check: " + a.why);
            return;
        }
        if (a.parked > 0 || a.unparked > 0)
            OZ_Log.Info("storage: classes checked: the bridge parked " + a.parked.ToString() + " root(s) and returned " + a.unparked.ToString());
        else
            OZ_Log.Dbg("storage: classes checked: nothing parked, nothing returned");
    }

    override void OnFail(int code)
    {
        OZ_Log.Warn("storage: the bridge did not answer the classes check (code " + code.ToString() + ")");
    }
}
