// Constants of the storage mod. Nothing here is configurable by an admin; the
// numbers an admin may tune live in OZS_Settings.
class OZS_Const
{
    // Where a box keeps its files: <DIR>/<box id>/items.bin and items.list.
    static const string DIR          = "$profile:OpenZone\\Storage";
    static const string SETTINGS     = "$profile:OpenZone\\OZ_Storage.json";
    static const string SETTINGS_TAG = "Storage";

    // The box's state machine, netsynced as an int (0..3).
    static const int STATE_CLOSED  = 0;
    static const int STATE_OPENING = 1;
    static const int STATE_OPEN    = 2;
    static const int STATE_CLOSING = 3;

    // Version of the box's own OnStoreSave block (written first, read first).
    static const int SAVE_VERSION = 1;
    // Version of the items.bin record format, and the trailer that proves
    // the file was written to the end.
    static const int BIN_VERSION = 1;
    static const int BIN_END     = 20260916;
    // The files of one box under DIR/<box id>/; written as .new first and
    // copied over the live names, so a crash never leaves a half file live.
    static const string FILE_BIN  = "items.bin";
    static const string FILE_LIST = "items.list";
    static const string FILE_NEW  = ".new";
    static const string LIST_HEAD = "OZS-LIST";

    // The viewer RPC rides on the box entity itself (Object.RPCSingleParam ->
    // OZ_StorageBox.OnRPC), so the sender's identity and the box come for
    // free and no id crosses the wire. The number only has to differ from
    // the ids vanilla and other mods handle in ItemBase.OnRPC.
    static const int RPC_VIEW_ID = 20260916;
    // The sort request from the client's search bar, on the box entity too.
    static const int RPC_SORT_ID = 20260917;
    // Seconds a box refuses a second sort after one.
    static const float SORT_COOLDOWN = 10.0;
    // The lifetime a box gets on every server start, 45 days. Without it the
    // central economy removes a placed box: our classes are not in types.xml,
    // and a box created by a kit had only the config default (a box placed by
    // the owner vanished overnight, 2026-09-17). An admin can still override
    // it through types.xml; this is the floor that works without one.
    static const float BOX_LIFETIME = 3888000.0;
    // Seconds the deploy action of a kit is held (owner 2026-09-16).
    static const float DEPLOY_SECONDS = 10.0;
    // Client: seconds between "still looking" repeats while the inventory
    // screen shows a box, and the scan cadence of that screen. The server's
    // ViewerTimeoutSeconds must stay above the heartbeat (default 15 s).
    static const float VIEW_HEARTBEAT = 4.0;
    static const float VIEW_SCAN      = 0.5;
    // Server: seconds between auto-close checks, and how long after the
    // engine saved an open box its files are kept (the save is still being
    // written when OnStoreSave runs).
    static const float AUTO_TICK     = 5.0;
    static const float RELEASE_DELAY = 3.0;
    // Seconds after mission start before the boot summary is written.
    static const float SUMMARY_DELAY = 15.0;

    // How many weapon slots the largest box declares (CfgSlots Slot_OZ_Weapon_1..6).
    static const int SLOT_COUNT = 6;

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
