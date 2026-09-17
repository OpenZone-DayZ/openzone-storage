// The record format of a box's store: how one entity and its children are
// written to and read back from items.bin (full fidelity through OnStoreSave)
// and items.list (the readable fallback without blobs).
//
// items.bin record of one entity, in this order:
//   type, location (type, slot, row, col, flip), attachment count, cargo
//   count, then every child as a full record (attachments first, then cargo),
//   then the body: weapon chambers and internal magazines, the OnStoreSave
//   blob, magazine cartridges, health (global and per zone), lifetime.
// Children go before the parent's body so that on restore they exist before
// the parent's OnStoreLoad runs -- the order another mod chose on purpose, and
// the one measured to round-trip a loaded rifle, a radio's frequency and a
// CF_ModStorage carrier exactly (docs/measurements/2026-09-16, run 4).
//
// items.list line of one entity: fields separated by '|':
//   depth|type|loctype|slot|row|col|flip|health|quantity|liquid|ammo|chambers|zone=hp;zone=hp
class OZS_Records
{
    // Statistics of one read pass (the open job reports them).
    static int s_Created;
    static int s_Missed;
    static int s_LoadFails;

    // ---- counting ----------------------------------------------------------

    // The entity and everything under it.
    static int CountTree(EntityAI e)
    {
        if (!e)
            return 0;
        int n = 1;
        GameInventory inv = e.GetInventory();
        if (!inv)
            return n;
        int ac = inv.AttachmentCount();
        for (int a = 0; a < ac; a++)
            n = n + CountTree(inv.GetAttachmentFromIndex(a));
        CargoBase cargo = inv.GetCargo();
        if (cargo)
        {
            int cc = cargo.GetItemCount();
            for (int c = 0; c < cc; c++)
                n = n + CountTree(cargo.GetItem(c));
        }
        return n;
    }

    // ---- items.bin: write --------------------------------------------------

    // Writes the entity's record; returns the number of entities written.
    // `newRow`/`newCol` >= 0 replace the cell of a cargo root (the sorted
    // layout); children are always written where they are.
    static int WriteEntity(FileSerializer f, EntityAI e, int newRow = -1, int newCol = -1)
    {
        int count = 1;
        f.Write(e.GetType());
        InventoryLocation loc = new InventoryLocation();
        int lt = -1;
        int slot = -1;
        int row = 0;
        int col = 0;
        bool flip = false;
        GameInventory inv = e.GetInventory();
        if (inv && inv.GetCurrentInventoryLocation(loc))
        {
            lt = loc.GetType();
            slot = loc.GetSlot();
            row = loc.GetRow();
            col = loc.GetCol();
            flip = loc.GetFlip();
        }
        if (newRow >= 0 && newCol >= 0 && lt == InventoryLocationType.CARGO)
        {
            row = newRow;
            col = newCol;
            flip = false;
        }
        f.Write(lt);
        f.Write(slot);
        f.Write(row);
        f.Write(col);
        f.Write(flip);

        int ac = 0;
        int cc = 0;
        CargoBase cargo;
        if (inv)
        {
            ac = inv.AttachmentCount();
            cargo = inv.GetCargo();
            if (cargo)
                cc = cargo.GetItemCount();
        }
        f.Write(ac);
        f.Write(cc);
        for (int a = 0; a < ac; a++)
            count += WriteEntity(f, inv.GetAttachmentFromIndex(a));
        for (int c = 0; c < cc; c++)
            count += WriteEntity(f, cargo.GetItem(c));

        WriteBody(f, e);
        return count;
    }

    static void WriteBody(FileSerializer f, EntityAI e)
    {
        // 1) weapon chambers and internal magazines, before the script blob
        Weapon_Base w = Weapon_Base.Cast(e);
        int muzzles = 0;
        if (w)
            muzzles = w.GetMuzzleCount();
        f.Write(muzzles);
        for (int m = 0; m < muzzles; m++)
        {
            bool empty = w.IsChamberEmpty(m);
            f.Write(empty);
            if (!empty)
            {
                float cd;
                string ct;
                w.GetCartridgeInfo(m, cd, ct);
                f.Write(cd);
                f.Write(ct);
            }
            int ic = w.GetInternalMagazineCartridgeCount(m);
            f.Write(ic);
            for (int k = 0; k < ic; k++)
            {
                float id;
                string it;
                w.GetInternalMagazineCartridgeInfo(m, k, id, it);
                f.Write(id);
                f.Write(it);
            }
        }

        // 2) the script state: vanilla variables, energy, agents, CF_ModStorage, mods
        e.OnStoreSave(f);

        // 3) magazine cartridges
        Magazine mag = Magazine.Cast(e);
        bool isMag = false;
        if (mag)
            isMag = true;
        f.Write(isMag);
        if (mag)
        {
            int ammo = mag.GetAmmoCount();
            bool pile = mag.IsAmmoPile();
            f.Write(ammo);
            f.Write(pile);
            if (!pile)
            {
                for (int q = 0; q < ammo; q++)
                {
                    float qd;
                    string qt;
                    mag.GetCartridgeAtIndex(q, qd, qt);
                    f.Write(qd);
                    f.Write(qt);
                }
            }
        }

        // 4) health, global and per zone
        f.Write(e.GetHealth("", "Health"));
        TStringArray zones = new TStringArray();
        e.GetDamageZones(zones);
        f.Write(zones.Count());
        for (int z = 0; z < zones.Count(); z++)
        {
            f.Write(zones.Get(z));
            f.Write(e.GetHealth(zones.Get(z), "Health"));
        }

        // 5) lifetime
        f.Write(e.GetLifetime());
    }

    // ---- items.bin: read ---------------------------------------------------

    // Reads the next record and creates the entity under `parent`; `made` is
    // that entity (null for a stand-in), so the caller can remove a half-built
    // tree. False when the stream can no longer be followed: a class that does
    // not exist and could not even be stood in for, or an OnStoreLoad that
    // refused (the blob's length is unknown, so nothing behind it can be read
    // either).
    static bool ReadEntity(FileSerializer f, EntityAI parent, int saveVer, out EntityAI made)
    {
        made = null;
        string type;
        int lt;
        int slot;
        int row;
        int col;
        bool flip;
        int ac;
        int cc;
        if (!f.Read(type))
            return false;
        f.Read(lt);
        f.Read(slot);
        f.Read(row);
        f.Read(col);
        f.Read(flip);
        f.Read(ac);
        f.Read(cc);

        EntityAI e;
        bool viaGround = false;
        bool standIn = false;
        if (cc > 0)
        {
            // A container with cargo children: the engine refuses children
            // while it sits in cargo, so build it on the ground and move it
            // afterwards (measured 2026-09-16, run 4).
            vector pos = parent.GetPosition();
            pos[0] = pos[0] + 2;
            e = EntityAI.Cast(GetGame().CreateObjectEx(type, pos, ECE_PLACE_ON_SURFACE | ECE_NOLIFETIME));
            viaGround = true;
        }
        else if (lt == InventoryLocationType.ATTACHMENT)
        {
            InventoryLocation il = new InventoryLocation();
            il.SetAttachment(parent, null, slot);
            e = GameInventory.LocationCreateEntity(il, type, ECE_IN_INVENTORY, RF_DEFAULT);
        }
        else
        {
            e = parent.GetInventory().CreateEntityInCargoEx(type, 0, row, col, flip);
            // The cell may be taken or wrong (a sorted layout that did not
            // fit, an item whose size a mod update changed): any free cell
            // beats a lost item.
            if (!e)
                e = parent.GetInventory().CreateEntityInCargo(type);
        }

        if (!e)
        {
            // The record behind this header cannot be consumed without an
            // entity of this class. A LOCAL stand-in reads it and is thrown away.
            s_Missed++;
            OZ_Log.Warn("storage: cannot create " + type + " in " + parent.GetType() + " at " + row + "," + col + " (slot " + slot + ")");
            vector spare = parent.GetPosition();
            spare[1] = spare[1] + 50;
            e = EntityAI.Cast(GetGame().CreateObjectEx(type, spare, ECE_LOCAL));
            if (!e)
                return false;
            standIn = true;
        }
        if (!standIn)
            made = e;

        // Children first: attachments, then cargo, each a full record.
        EntityAI child;
        for (int a = 0; a < ac; a++)
        {
            if (!ReadEntity(f, e, saveVer, child))
            {
                if (standIn)
                    GetGame().ObjectDelete(e);
                return false;
            }
        }
        for (int c = 0; c < cc; c++)
        {
            if (!ReadEntity(f, e, saveVer, child))
            {
                if (standIn)
                    GetGame().ObjectDelete(e);
                return false;
            }
        }

        bool bodyOk = ReadBody(f, e, saveVer);
        if (standIn)
        {
            GetGame().ObjectDelete(e);
            return bodyOk;
        }
        if (!bodyOk)
            return false;

        if (viaGround)
        {
            InventoryLocation src = new InventoryLocation();
            e.GetInventory().GetCurrentInventoryLocation(src);
            InventoryLocation dst = new InventoryLocation();
            dst.SetCargo(parent, e, 0, row, col, flip);
            // TakeToDst in SERVER mode, never the bare LocationSyncMoveEntity:
            // both move the item on the server, but only the SERVER mode sends
            // the SYNC_MOVE command to the clients (inventory.c:1056-1073).
            // Without it the item sits in the box for the server and stays
            // drawn on the ground for every client -- a ghost that vanishes
            // when the box closes (seen by the owner 2026-09-17).
            bool placed = parent.GetInventory().TakeToDst(InventoryMode.SERVER, src, dst);
            if (!placed)
                placed = parent.GetInventory().TakeEntityToCargoEx(InventoryMode.SERVER, e, 0, row, col);
            if (!placed)
            {
                s_Missed++;
                OZ_Log.Warn("storage: " + type + " with " + cc + " items could not be moved into " + parent.GetType() + " at " + row + "," + col + "; it stays on the ground");
            }
        }
        s_Created++;
        return true;
    }

    // The body in the order WriteBody wrote it. Chambers go in before
    // OnStoreLoad, cartridges and health after it, as another mod does.
    static bool ReadBody(FileSerializer f, EntityAI e, int saveVer)
    {
        int muzzles;
        if (!f.Read(muzzles))
            return false;
        Weapon_Base w = Weapon_Base.Cast(e);
        for (int m = 0; m < muzzles; m++)
        {
            bool empty;
            f.Read(empty);
            if (!empty)
            {
                float cd;
                string ct;
                f.Read(cd);
                f.Read(ct);
                if (w)
                    w.PushCartridgeToChamber(m, cd, ct);
            }
            int ic;
            f.Read(ic);
            for (int k = 0; k < ic; k++)
            {
                float id;
                string it;
                f.Read(id);
                f.Read(it);
                if (w)
                    w.PushCartridgeToInternalMagazine(m, id, it);
            }
        }

        if (!e.OnStoreLoad(f, saveVer))
        {
            s_LoadFails++;
            OZ_Log.Warn("storage: " + e.GetType() + " refused its stored state (OnStoreLoad false, game save version " + saveVer + ")");
            return false;
        }

        bool isMag;
        f.Read(isMag);
        if (isMag)
        {
            int ammo;
            bool pile;
            f.Read(ammo);
            f.Read(pile);
            Magazine mag = Magazine.Cast(e);
            if (pile)
            {
                if (mag)
                    mag.ServerSetAmmoCount(ammo);
            }
            else
            {
                if (mag)
                    mag.ServerSetAmmoCount(0);
                for (int q = 0; q < ammo; q++)
                {
                    float qd;
                    string qt;
                    f.Read(qd);
                    f.Read(qt);
                    if (mag)
                        mag.ServerStoreCartridge(qd, qt);
                }
            }
        }

        float hp;
        f.Read(hp);
        e.SetHealth("", "Health", hp);
        int zc;
        f.Read(zc);
        for (int z = 0; z < zc; z++)
        {
            string zn;
            float zh;
            f.Read(zn);
            f.Read(zh);
            e.SetHealth(zn, "Health", zh);
        }

        float life;
        f.Read(life);
        e.SetLifetime(life);

        e.AfterStoreLoad();
        e.SetSynchDirty();
        if (w)
            w.Synchronize();
        GetGame().GetCallQueue(CALL_CATEGORY_SYSTEM).Call(e.EEOnAfterLoad);
        return true;
    }

    // ---- items.list: write -------------------------------------------------

    // One line per entity, children indented by depth; returns the number
    // of lines written. `newRow`/`newCol` as in WriteEntity.
    static int WriteListEntity(FileHandle fh, EntityAI e, int depth, int newRow = -1, int newCol = -1)
    {
        int count = 1;
        FPrintln(fh, ListLine(e, depth, newRow, newCol));
        GameInventory inv = e.GetInventory();
        if (!inv)
            return count;
        int ac = inv.AttachmentCount();
        for (int a = 0; a < ac; a++)
            count += WriteListEntity(fh, inv.GetAttachmentFromIndex(a), depth + 1);
        CargoBase cargo = inv.GetCargo();
        if (cargo)
        {
            int cc = cargo.GetItemCount();
            for (int c = 0; c < cc; c++)
                count += WriteListEntity(fh, cargo.GetItem(c), depth + 1);
        }
        return count;
    }

    static string ListLine(EntityAI e, int depth, int newRow = -1, int newCol = -1)
    {
        InventoryLocation loc = new InventoryLocation();
        int lt = -1;
        int slot = -1;
        int row = 0;
        int col = 0;
        int flip = 0;
        GameInventory inv = e.GetInventory();
        if (inv && inv.GetCurrentInventoryLocation(loc))
        {
            lt = loc.GetType();
            slot = loc.GetSlot();
            row = loc.GetRow();
            col = loc.GetCol();
            if (loc.GetFlip())
                flip = 1;
        }
        if (newRow >= 0 && newCol >= 0 && lt == InventoryLocationType.CARGO)
        {
            row = newRow;
            col = newCol;
            flip = 0;
        }
        float quantity = 0;
        int liquid = 0;
        ItemBase item = ItemBase.Cast(e);
        if (item)
        {
            if (item.HasQuantity())
                quantity = item.GetQuantity();
            liquid = item.GetLiquidType();
        }
        int ammo = 0;
        Magazine mag = Magazine.Cast(e);
        if (mag)
            ammo = mag.GetAmmoCount();
        int chambers = 0;
        Weapon_Base w = Weapon_Base.Cast(e);
        if (w)
        {
            for (int m = 0; m < w.GetMuzzleCount(); m++)
            {
                if (!w.IsChamberEmpty(m))
                    chambers++;
            }
        }
        string zones = "";
        TStringArray names = new TStringArray();
        e.GetDamageZones(names);
        for (int z = 0; z < names.Count(); z++)
        {
            if (z > 0)
                zones = zones + ";";
            zones = zones + names.Get(z) + "=" + e.GetHealth(names.Get(z), "Health");
        }
        string line = depth.ToString() + "|" + e.GetType() + "|" + lt + "|" + slot + "|" + row;
        line = line + "|" + col + "|" + flip + "|" + e.GetHealth("", "Health") + "|" + quantity;
        line = line + "|" + liquid + "|" + ammo + "|" + chambers + "|" + zones;
        return line;
    }
}
