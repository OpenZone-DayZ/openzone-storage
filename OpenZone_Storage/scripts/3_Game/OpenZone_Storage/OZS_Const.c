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
    // Version of the items.bin record format.
    static const int BIN_VERSION = 1;

    // CF RPC keys: the pair (mod, function) is the whole namespace.
    static const string RPC_MOD  = "OpenZone_Storage";
    static const string RPC_VIEW = "OZS_View";

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
