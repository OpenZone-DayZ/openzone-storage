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
    static const string ROUTE_CLOSE   = "v1/storage/close";
    static const string ROUTE_CLOSED  = "v1/storage/closed";
    static const string ROUTE_OPEN    = "v1/storage/open";
    static const string ROUTE_OPENED  = "v1/storage/opened";
    static const string ROUTE_PARK    = "v1/storage/park";
    static const string ROUTE_EVENTS  = "v1/storage/events";
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

    static const int RPC_VIEW_ID = 20260916;
    static const int RPC_SORT_ID = 20260917;

    static const float SORT_COOLDOWN = 10.0;
    // 45 days: our classes are not in types.xml, so the central economy
    // would otherwise remove a placed box by its config lifetime.
    static const float BOX_LIFETIME = 3888000.0;
    static const float DEPLOY_SECONDS = 10.0;

    static const float VIEW_HEARTBEAT = 4.0;
    static const float VIEW_SCAN      = 0.5;
    static const float AUTO_TICK     = 5.0;
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
