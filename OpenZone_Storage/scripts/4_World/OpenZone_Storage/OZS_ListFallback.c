// The readable fallback of a store: items.list parsed into records and
// materialised on the native layer only (type, place, health, quantity,
// liquid, magazine count) when items.bin cannot be followed -- an unknown
// class, an OnStoreLoad that refused, a truncated file. Script state (wetness,
// temperature, CF_ModStorage, a radio's frequency) is lost for those items,
// and the log says so per box.
class OZS_ListRec
{
    int    depth;
    string type;
    int    lt;
    int    slot;
    int    row;
    int    col;
    bool   flip;
    float  health;
    float  quantity;
    int    liquid;
    int    ammo;
    int    chambers;
    string zones;
    bool   hasCargoChild;
}

class OZS_ListFallback
{
    // Parses items.list of the box into records. Returns the record count,
    // or -1 when the file is missing or the header is not ours.
    static int Read(string id, array<ref OZS_ListRec> into)
    {
        FileHandle fh = OpenFile(OZS_Store.ListPath(id), FileMode.READ);
        if (fh == 0)
            return -1;
        string line;
        if (FGets(fh, line) <= 0)
        {
            CloseFile(fh);
            return -1;
        }
        if (line.IndexOf(OZS_Const.LIST_HEAD) != 0)
        {
            CloseFile(fh);
            OZ_Log.Error("storage: box " + id + " items.list does not start with " + OZS_Const.LIST_HEAD);
            return -1;
        }
        int bad = 0;
        while (FGets(fh, line) > 0)
        {
            OZS_ListRec r = new OZS_ListRec();
            if (Parse(line, r))
            {
                into.Insert(r);
            }
            else
            {
                bad++;
                if (bad <= 5)
                    OZ_Log.Warn("storage: box " + id + " items.list: cannot parse [" + line + "]");
            }
        }
        CloseFile(fh);
        if (bad > 5)
            OZ_Log.Warn("storage: box " + id + " items.list: " + bad + " lines could not be parsed");
        MarkCargoChildren(into);
        return into.Count();
    }

    // depth|type|loctype|slot|row|col|flip|health|quantity|liquid|ammo|chambers|zones
    // (Split drops empty fields, and only the last one can be empty.)
    static bool Parse(string line, OZS_ListRec r)
    {
        array<string> p = new array<string>();
        line.Split("|", p);
        if (p.Count() < 12)
            return false;
        r.depth = p.Get(0).ToInt();
        r.type = p.Get(1);
        r.lt = p.Get(2).ToInt();
        r.slot = p.Get(3).ToInt();
        r.row = p.Get(4).ToInt();
        r.col = p.Get(5).ToInt();
        r.flip = p.Get(6).ToInt() == 1;
        r.health = p.Get(7).ToFloat();
        r.quantity = p.Get(8).ToFloat();
        r.liquid = p.Get(9).ToInt();
        r.ammo = p.Get(10).ToInt();
        r.chambers = p.Get(11).ToInt();
        r.zones = "";
        if (p.Count() >= 13)
            r.zones = p.Get(12).Trim();
        return true;
    }

    // A record needs the ground path when any child one level deeper sits
    // in its cargo.
    static void MarkCargoChildren(array<ref OZS_ListRec> recs)
    {
        for (int i = 0; i < recs.Count(); i++)
        {
            OZS_ListRec r = recs.Get(i);
            r.hasCargoChild = false;
            for (int j = i + 1; j < recs.Count(); j++)
            {
                OZS_ListRec c = recs.Get(j);
                if (c.depth <= r.depth)
                    break;
                if (c.depth == r.depth + 1 && c.lt == InventoryLocationType.CARGO)
                {
                    r.hasCargoChild = true;
                    break;
                }
            }
        }
    }

    // Index of the record after the subtree of record i.
    static int SubtreeEnd(array<ref OZS_ListRec> recs, int i)
    {
        int d = recs.Get(i).depth;
        int j = i + 1;
        while (j < recs.Count() && recs.Get(j).depth > d)
            j++;
        return j;
    }

    // Index of the n-th root record (depth 0), or -1.
    static int RootIndex(array<ref OZS_ListRec> recs, int n)
    {
        int seen = 0;
        for (int i = 0; i < recs.Count(); i++)
        {
            if (recs.Get(i).depth == 0)
            {
                if (seen == n)
                    return i;
                seen++;
            }
        }
        return -1;
    }

    static int RootCount(array<ref OZS_ListRec> recs)
    {
        int n = 0;
        for (int i = 0; i < recs.Count(); i++)
        {
            if (recs.Get(i).depth == 0)
                n++;
        }
        return n;
    }

    // Creates record i and its subtree under `parent`; returns the entity or
    // null. Counts into OZS_Records.s_Created / s_Missed like the blob path.
    static EntityAI Make(array<ref OZS_ListRec> recs, int i, EntityAI parent)
    {
        OZS_ListRec r = recs.Get(i);
        int end = SubtreeEnd(recs, i);
        EntityAI e;
        bool viaGround = r.hasCargoChild;
        if (viaGround)
        {
            vector pos = parent.GetPosition();
            pos[0] = pos[0] + 2;
            e = EntityAI.Cast(GetGame().CreateObjectEx(r.type, pos, ECE_PLACE_ON_SURFACE | ECE_NOLIFETIME));
        }
        else if (r.lt == InventoryLocationType.ATTACHMENT)
        {
            InventoryLocation il = new InventoryLocation();
            il.SetAttachment(parent, null, r.slot);
            e = GameInventory.LocationCreateEntity(il, r.type, ECE_IN_INVENTORY, RF_DEFAULT);
        }
        else
        {
            e = parent.GetInventory().CreateEntityInCargoEx(r.type, 0, r.row, r.col, r.flip);
            if (!e)
                e = parent.GetInventory().CreateEntityInCargo(r.type);
        }
        if (!e)
        {
            OZS_Records.s_Missed++;
            OZ_Log.Warn("storage: list fallback cannot create " + r.type + " in " + parent.GetType() + " at " + r.row + "," + r.col + " (slot " + r.slot + ")");
            return null;
        }

        int k = i + 1;
        while (k < end)
        {
            Make(recs, k, e);
            k = SubtreeEnd(recs, k);
        }

        Apply(e, r);

        if (viaGround)
        {
            InventoryLocation src = new InventoryLocation();
            e.GetInventory().GetCurrentInventoryLocation(src);
            InventoryLocation dst = new InventoryLocation();
            dst.SetCargo(parent, e, 0, r.row, r.col, r.flip);
            bool placed = GameInventory.LocationSyncMoveEntity(src, dst);
            if (!placed)
                placed = parent.GetInventory().TakeEntityToCargoEx(InventoryMode.SERVER, e, 0, r.row, r.col);
            if (!placed)
            {
                OZS_Records.s_Missed++;
                OZ_Log.Warn("storage: list fallback: " + r.type + " could not be moved into " + parent.GetType() + " at " + r.row + "," + r.col + "; it stays on the ground");
            }
        }
        OZS_Records.s_Created++;
        return e;
    }

    // The native layer: health, zones, quantity, liquid, magazine count.
    static void Apply(EntityAI e, OZS_ListRec r)
    {
        e.SetHealth("", "Health", r.health);
        if (r.zones != "")
        {
            array<string> pairs = new array<string>();
            r.zones.Split(";", pairs);
            for (int i = 0; i < pairs.Count(); i++)
            {
                array<string> kv = new array<string>();
                pairs.Get(i).Split("=", kv);
                if (kv.Count() == 2)
                    e.SetHealth(kv.Get(0), "Health", kv.Get(1).ToFloat());
            }
        }
        Magazine mag = Magazine.Cast(e);
        if (mag)
        {
            mag.ServerSetAmmoCount(r.ammo);
        }
        else
        {
            ItemBase item = ItemBase.Cast(e);
            if (item)
            {
                // A battery's quantity is its energy: it goes through the
                // energy manager, or the item reads full again.
                if (item.HasEnergyManager())
                    item.GetCompEM().SetEnergy(r.quantity);
                else if (item.HasQuantity() && r.quantity > 0)
                    item.SetQuantity(r.quantity);
                if (r.liquid > 0)
                    item.SetLiquidType(r.liquid);
            }
        }
        e.SetSynchDirty();
    }
}
