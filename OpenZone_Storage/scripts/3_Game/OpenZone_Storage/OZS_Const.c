// Constants of the storage boxes: the states, the wire format the bridge
// parses, the routes of the bridge's storage side and the pacing of the
// exchange (design 2026-09-19, "SQL as the truth, a file as the wire").
class OZS_Const
{
    static const string DIR          = "$profile:OpenZone\\Storage";
    // The exchange directory: the game writes a close file here and reads
    // the cache the bridge names. Nothing else of a box lives on this disk.
    static const string DIR_XCHG     = "$profile:OpenZone\\Storage\\xchg";
    static const string SETTINGS     = "$profile:OpenZone\\OZ_Storage.json";
    static const string SETTINGS_TAG = "Storage";

    // A placed box is always CLOSED under the proxy; OPENING and OPEN are the
    // states of an AUTHORITY being filled and standing filled. CLOSING was
    // the close job's and is never entered since 2026-09-26; it keeps its
    // number so StateName reads an old save right.
    static const int STATE_CLOSED  = 0;
    static const int STATE_OPENING = 1;
    static const int STATE_OPEN    = 2;
    static const int STATE_CLOSING = 3;

    // The box's own OnStoreSave block.
    static const int SAVE_VERSION = 1;

    // The wire (design section 2): version 3, a random marker of four ints
    // after the header and after every root, BIN_END as the trailer. The
    // bridge parses the header and every root's descriptor and carves the
    // bodies by the marker.
    static const int BIN_VERSION = 3;
    static const int BIN_END     = 20260916;

    // Routes of the bridge, without the leading slash OZ_BridgeClient adds.
    static const string ROUTE_BOOT    = "v1/storage/boot";
    static const string ROUTE_CLASSES = "v1/storage/classes";
    // v1/storage/close -- a placed box's cargo captured whole -- went with
    // the old scheme on 2026-09-26; the bridge still serves it, this mod
    // never calls it. A session writes per turn (ROUTE_OP) and closes with
    // its last, absolute letter.
    static const string ROUTE_CLOSED  = "v1/storage/closed";
    static const string ROUTE_OPEN    = "v1/storage/open";
    static const string ROUTE_OPENED  = "v1/storage/opened";
    static const string ROUTE_PARK    = "v1/storage/park";
    static const string ROUTE_EVENTS  = "v1/storage/events";
    // One turn of a proxy session: the roots that changed, went or arrived
    // (design 2026-09-24 §7).
    static const string ROUTE_OP      = "v1/storage/op";
    // The kind of the poll items the bridge may send this mod (live
    // commands, a later sub-project) and the name of its sink.
    static const string SINK_KIND     = "storage";

    // Seconds a job waits for the bridge's reply before it gives up; the
    // request itself dies at ten, this only guards a lost callback.
    static const float REPLY_TIMEOUT = 12.0;
    // Seconds between attempts of the boot exchange while the bridge is down.
    static const float BOOT_RETRY    = 5.0;
    // How many times an open asks again after a parked root.
    static const int   OPEN_RETRIES  = 3;
    // Events: one batch per second at most, this many per batch, this many
    // waiting before the oldest is dropped.
    static const float EVENT_FLUSH   = 1.0;
    static const int   EVENT_BATCH   = 200;
    static const int   EVENT_QUEUE   = 500;

    // RPC_VIEW_ID and RPC_SORT_ID were here until 2026-09-26: a client saying
    // "I am looking at this box" and the old Sort button. Both belonged to the
    // scheme where the PLACED box held the items; the proxy has its own wire
    // (RPC_PX_*) and a sort is an ordinary turn on it.

    // The proxy wire (design 2026-09-24). Client -> server messages ride on
    // the player's own entity (OZS_Player.OnRPC); server -> client ones
    // arrive with no target at all and are heard at DayZGame.Event_OnRPC
    // (OZS_Mirrors.Listen). Every message names its box by id in its body,
    // because the authoritative container has no network id to address.
    //
    // THE IDS ARE SMALL ON PURPOSE. A date-shaped id like 20260924 travels
    // client -> server perfectly well, but the same
    // id sent server -> ONE CLIENT never arrives: measured 2026-09-24, the
    // server logged the send with the right target and the right recipient and
    // the client's DayZGame.OnRPC never saw it. Vanilla's own ERPCs end in the
    // low hundreds and CF's whole framework rides on 10042.
    //
    // Server -> ONE client:
    static const int RPC_PX_BEGIN  = 20501;  // a stream starts: id, class, count
    static const int RPC_PX_ROWS   = 20502;  // a chunk of item descriptors
    static const int RPC_PX_END    = 20503;  // the stream is whole
    static const int RPC_PX_CHANGE = 20504;  // one change, to every proxy of this box
    static const int RPC_PX_NO     = 20505;  // an operation was refused
    static const int RPC_PX_GONE   = 20509;  // this session is over: drop the proxy
    // Client -> server:
    static const int RPC_PX_OPEN   = 20506;  // show me this box
    static const int RPC_PX_OP     = 20507;  // do this to it
    static const int RPC_PX_SHUT   = 20508;  // I have closed the screen

    // What an operation is. MOVE stays inside the box; OUT and IN cross the
    // boundary; COMBINE and SWAP are the engine's own, asked of the authority.
    // How far beside the player a proxy stands. Close enough for the vanilla
    // inventory's vicinity list to find it, far enough not to sit inside the
    // player's own collision.
    static const float PROXY_ASIDE = 0.7;

    static const int OP_MOVE    = 1;
    static const int OP_OUT     = 2;
    static const int OP_IN      = 3;
    static const int OP_COMBINE = 4;
    static const int OP_SWAP    = 5;
    // One end in the box and one outside, trading places. Its own operation
    // rather than a flavour of OP_SWAP: the two items are named differently
    // -- the one inside by handle, the one outside by network id -- and the
    // steps it takes are the boundary's, not the box's.
    static const int OP_XSWAP   = 6;
    // One stack becomes two, both inside the box. `netLow` carries WHICH split
    // the screen asked for, because vanilla has two and they are not the same
    // call: SPLIT_HALF is `SplitItemToInventoryLocation`, which Magazine
    // overrides to move cartridges one at a time with their own damage, and
    // SPLIT_MAX is `SplitIntoStackMaxToInventoryLocationEx`, which works on
    // quantity and therefore does nothing at all to an ammo pile -- in vanilla
    // too, because `ShouldSplitQuantity` wants `GetQuantity() > 1` and a pile's
    // quantity is 1 (measured 2026-09-25).
    static const int OP_SPLIT   = 7;
    // A SORT IS A TURN LIKE ANY OTHER (owner, 2026-09-26). It used to be the
    // old scheme's own gesture -- close the placed box with new cells, open it
    // again into the world -- which cannot work on a proxy: the placed box is
    // never open, so the sort's first test refused every press.
    static const int OP_SORT    = 8;
    static const int SPLIT_HALF = 0;
    static const int SPLIT_MAX  = 1;

    // How many operations may wait for the wire before a client is told to
    // start again (§8.3). A drag is one operation and a player cannot make
    // them much faster than a bridge round trip; a client further ahead than
    // this is drawing a box it cannot justify.
    static const int MAX_WAITING = 16;

    // How many times one session may rewrite its record to end a
    // disagreement before giving up on rewriting. See OZS_Session.m_Repairs.
    static const int MAX_REPAIRS = 3;

    // HOW FAR FROM THE ANCHOR A SESSION MAY BE USED, in metres. The server
    // asks it at the open and once a frame after; the client's own reach
    // test is not consulted, because a crafted message never runs it
    // (review 2026-09-26, A1). Two metres of reach (UAMaxDistances.DEFAULT),
    // 0.7 m from the player to the proxy, and the radius the vicinity panel
    // keeps the proxy in: six metres is past all of them together, and the
    // stash has used the same figure since 2026-09-23 (StashMaxDistance).
    static const float SESSION_LEASH = 6.0;

    // The least time, in seconds, between two restreams of one watcher. A
    // restream is a fresh snapshot and every row of the box again, and a
    // client that can trigger one per message can keep the server doing
    // nothing else (review 2026-09-26, A3). A restart asked for sooner is
    // kept and done when the gap has passed, never dropped.
    static const float RESTART_GAP = 1.0;

    // What a change is, as the server tells it to every proxy.
    static const int CH_MOVED   = 1;   // handle -> a new place
    static const int CH_GONE    = 2;   // handle is no longer in the box
    static const int CH_ADDED   = 3;   // a whole item arrived (one descriptor)
    static const int CH_QTY     = 4;   // a stack changed size

    static const float SORT_COOLDOWN = 10.0;
    // 45 days: our classes are not in types.xml, so the central economy
    // would otherwise remove a placed box by its config lifetime.
    static const float BOX_LIFETIME = 3888000.0;
    static const float DEPLOY_SECONDS = 10.0;

    // Seconds after mission start before the boot exchange and the summary:
    // the world's persistent entities load a few frames after OnMissionStart.
    static const float SUMMARY_DELAY = 15.0;

    static const int SLOT_COUNT = 6;

    // The search bar's height and one line of the count list, in layout pixels.
    static const int UI_BAR_PX = 34;
    static const int UI_LINE_PX = 17;
    // How many class lines the count list prints before it switches to
    // "N more classes" instead of growing without limit.
    static const int UI_COUNT_LINES = 24;

    // ---- the personal stash's key ----------------------------------------
    //
    // A box is keyed by the engine's persistent id. A STASH is keyed by a
    // PAIR -- which anchor, and whose -- because the same player at two
    // lockers has two stashes and one locker holds one per player. The pair
    // is spelled as a single string, `s_<anchor>_<uid>`, so the bridge needs
    // no schema for it: box_id is opaque text there.
    //
    // Separators: `_` between the three parts and `x` inside the anchor key,
    // because a position key and a persistent id both already use `-` for
    // their own fields and for minus signs. Nothing here can be a path.
    static const string STASH_PREFIX = "s_";

    static string StashId(string anchor, string uid)
    {
        return STASH_PREFIX + anchor + "_" + uid;
    }

    static bool IsStashId(string id)
    {
        return id.Length() > 2 && id.Substring(0, 2) == STASH_PREFIX;
    }

    // The anchor half, or "" if this is not a stash id. Splitting is by the
    // LAST underscore: the anchor key may hold one of its own some day, the
    // uid never will.
    static string StashAnchorOf(string id)
    {
        if (!IsStashId(id))
            return "";
        int cut = LastUnderscore(id);
        if (cut <= 2)
            return "";
        return id.Substring(2, cut - 2);
    }

    static string StashOwnerOf(string id)
    {
        if (!IsStashId(id))
            return "";
        int cut = LastUnderscore(id);
        if (cut < 0)
            return "";
        return id.Substring(cut + 1, id.Length() - cut - 1);
    }

    protected static int LastUnderscore(string s)
    {
        int at = -1;
        for (int i = 0; i < s.Length(); i++)
        {
            if (s.Substring(i, 1) == "_")
                at = i;
        }
        return at;
    }

    // An anchor's half of the key, from where it stands. Whole metres: the
    // locker does not move, and two lockers a metre apart would be one anchor
    // to a player anyway. Negative coordinates do not occur on a DayZ map, so
    // the key is digits and one `x`.
    static string AnchorKeyAt(vector pos)
    {
        int x = Math.Round(pos[0]);
        int z = Math.Round(pos[2]);
        if (x < 0)
            x = 0;
        if (z < 0)
            z = 0;
        return x.ToString() + "x" + z.ToString();
    }

    static string StateName(int state)
    {
        if (state == STATE_OPENING)
            return "OPENING";
        if (state == STATE_OPEN)
            return "OPEN";
        if (state == STATE_CLOSING)
            return "CLOSING";
        return "CLOSED";
    }
}
