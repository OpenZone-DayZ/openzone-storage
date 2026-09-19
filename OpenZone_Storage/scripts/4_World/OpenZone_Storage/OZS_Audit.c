// The events of the boxes (design 2026-09-19, section 3.4): who opened,
// closed, put and took what, and what the exchange with the bridge did. A
// queue in memory, flushed to the bridge in batches of at most EVENT_BATCH
// once a second; while the bridge is down the boxes do not work either, so
// the queue stays small, and past EVENT_QUEUE the oldest record is dropped
// with a count that rides on the next batch. No file journal, by the
// owner's decision.
class OZS_Audit
{
    static ref array<ref OZS_EventRec> s_Queue;
    static float s_LastFlush;
    static int s_Dropped;

    static void Log(string kind, string box, string uid, string name, string type, float qty, int row, int col, string slot, string note)
    {
        if (!s_Queue)
            s_Queue = new array<ref OZS_EventRec>();
        if (s_Queue.Count() >= OZS_Const.EVENT_QUEUE)
        {
            s_Queue.RemoveOrdered(0);
            s_Dropped++;
        }
        OZS_EventRec e = new OZS_EventRec();
        e.at = OZS_Store.Stamp();
        e.kind = kind;
        e.box = box;
        e.uid = uid;
        e.name = name;
        e.type = type;
        e.qty = qty;
        e.row = row;
        e.col = col;
        e.slot = slot;
        e.note = note;
        s_Queue.Insert(e);
    }

    // An item entering or leaving a box: the actor is the box's single
    // viewer, or the viewers are named in the note.
    static void Item(string kind, OZ_StorageBox box, EntityAI item, string slot)
    {
        if (!box || !item)
            return;
        string uid;
        string name;
        string note;
        OZS_Controller.Get().ViewerWho(box, uid, name, note);
        float qty = 0;
        ItemBase ib = ItemBase.Cast(item);
        if (ib && ib.HasQuantity())
            qty = ib.GetQuantity();
        int row = -1;
        int col = -1;
        InventoryLocation loc = new InventoryLocation();
        GameInventory inv = item.GetInventory();
        if (inv && inv.GetCurrentInventoryLocation(loc) && loc.GetType() == InventoryLocationType.CARGO && loc.GetParent() == box)
        {
            row = loc.GetRow();
            col = loc.GetCol();
        }
        Log(kind, box.OZS_GetId(), uid, name, item.GetType(), qty, row, col, slot, note);
    }

    static void Flush(float now)
    {
        if (!s_Queue || s_Queue.Count() == 0)
            return;
        if (now - s_LastFlush < OZS_Const.EVENT_FLUSH)
            return;
        if (!OZS_Bridge.Up())
            return;
        s_LastFlush = now;
        OZS_EventsLetter letter = new OZS_EventsLetter();
        int n = 0;
        while (s_Queue.Count() > 0 && n < OZS_Const.EVENT_BATCH)
        {
            letter.events.Insert(s_Queue.Get(0));
            s_Queue.RemoveOrdered(0);
            n++;
        }
        if (s_Dropped > 0)
        {
            OZS_EventRec first = letter.events.Get(0);
            first.note = first.note + " (" + s_Dropped.ToString() + " earlier event(s) dropped)";
            s_Dropped = 0;
        }
        string json;
        string err;
        if (!JsonFileLoader<OZS_EventsLetter>.MakeData(letter, json, err, false))
        {
            OZ_Log.Warn("storage: " + n.ToString() + " event(s) cannot be serialised: " + err);
            return;
        }
        OZS_Bridge.Post(OZS_Const.ROUTE_EVENTS, json, new OZS_AckReply("events"));
    }

    static void Reset()
    {
        s_Queue = null;
        s_Dropped = 0;
        s_LastFlush = 0;
    }
}
