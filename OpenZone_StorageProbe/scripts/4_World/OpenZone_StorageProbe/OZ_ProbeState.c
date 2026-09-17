// Research helpers for items 2-4 of the brief: exact recreation, full item
// state, nested containers and persistence. All server side, all immediate
// (one frame), all reporting through the results log.
//
//   stock      put a state-rich set of items into the crate (rifle with a loaded
//              magazine and a chambered round, damaged zones, a wet warm rag
//              with quantity, a canteen of water, a battery with charge, a radio
//              tuned to index 3, an OpenZone data carrier with two notes)
//   inspect    one JSON line per cargo item with everything the probe can read
//   blob_save  serialise the crate's cargo tree into $profile:.../blob.bin the
//              record way: header, children, weapon chambers, OnStoreSave,
//              magazine cartridges, health per zone, lifetime
//   blob_load  recreate the tree from that file into the (empty) crate
//   nest       create bags ON THE GROUND, fill them, then move them into the
//              crate's cargo (the only order the engine allows)
//   give       move the first cargo item into the connected player's inventory
//   player_count  how many items of a class the player carries
class OZ_ProbeState
{
    static const string BLOB = "$profile:OpenZone_StorageProbe/blob.bin";
    static const string BLOBTIME = "$profile:OpenZone_StorageProbe/blobtime.bin";

    // ------------------------------------------------- OnStoreSave timing
    //
    // What does the pair OnStoreSave / OnStoreLoad cost by itself, apart from
    // the file? ScriptReadWriteContext is a ParamsWriteContext over memory
    // (gameplay.c:134), and OnStoreSave takes exactly that, so the same calls
    // can be timed with and without the disk:
    //   A  save into memory   -- the script work and the native writes, no I/O
    //   B  load from memory   -- the other half of the pair
    //   C  save into a file   -- the same work plus FileSerializer
    // C minus A is what the file costs.
    static string BlobTime(EntityAI crate)
    {
        array<EntityAI> items = new array<EntityAI>();
        CargoBase cargo = crate.GetInventory().GetCargo();
        int n = 0;
        if (cargo)
            n = cargo.GetItemCount();
        for (int i = 0; i < n; i++)
        {
            EntityAI e = cargo.GetItem(i);
            if (e)
                items.Insert(e);
        }
        GameInventory inv = crate.GetInventory();
        for (int a = 0; a < inv.AttachmentCount(); a++)
        {
            EntityAI att = inv.GetAttachmentFromIndex(a);
            if (att)
                items.Insert(att);
        }
        int count = items.Count();
        if (count == 0)
            return "the crate is empty, nothing to time";

        ScriptReadWriteContext mem = new ScriptReadWriteContext();
        ParamsWriteContext w = mem.GetWriteContext();
        float t0 = GetGame().GetTickTime();
        for (int s = 0; s < count; s++)
            items.Get(s).OnStoreSave(w);
        float t1 = GetGame().GetTickTime();

        ParamsReadContext r = mem.GetReadContext();
        int ver = GetGame().SaveVersion();
        int fails = 0;
        float t2 = GetGame().GetTickTime();
        for (int l = 0; l < count; l++)
        {
            if (!items.Get(l).OnStoreLoad(r, ver))
                fails++;
        }
        float t3 = GetGame().GetTickTime();

        float t4 = 0;
        float t5 = 0;
        FileSerializer f = new FileSerializer();
        if (f.Open(BLOBTIME, FileMode.WRITE))
        {
            t4 = GetGame().GetTickTime();
            for (int c = 0; c < count; c++)
                items.Get(c).OnStoreSave(f);
            t5 = GetGame().GetTickTime();
            f.Close();
        }

        float saveMs = (t1 - t0) * 1000;
        float loadMs = (t3 - t2) * 1000;
        float fileMs = (t5 - t4) * 1000;
        string d = "n=" + count;
        d = d + " save_mem=" + R1(saveMs) + "ms";
        d = d + " (" + R2(saveMs / count) + " per item)";
        d = d + " load_mem=" + R1(loadMs) + "ms";
        d = d + " (" + R2(loadMs / count) + " per item)";
        d = d + " save_file=" + R1(fileMs) + "ms";
        d = d + " (" + R2(fileMs / count) + " per item)";
        d = d + " load_refusals=" + fails;
        return d;
    }
    static const int BLOB_VERSION = 1;

    // ---------------------------------------------------------------- stock

    static string Stock(EntityAI crate)
    {
        GameInventory inv = crate.GetInventory();
        string r = "";

        EntityAI akm = inv.CreateEntityInCargo("AKM");
        if (akm)
        {
            Weapon_Base w = Weapon_Base.Cast(akm);
            EntityAI magE = akm.GetInventory().CreateAttachment("Mag_AKM_30Rnd");
            Magazine mag = Magazine.Cast(magE);
            if (mag)
                mag.ServerSetAmmoCount(17);
            if (w)
            {
                w.PushCartridgeToChamber(0, 0.0, "Bullet_762x39");
                w.Synchronize();
            }
            akm.SetHealth("", "Health", 40);
            TStringArray zones = new TStringArray();
            akm.GetDamageZones(zones);
            if (zones.Count() > 0)
                akm.SetHealth(zones.Get(0), "Health", 20);
            r = r + "AKM hp40 zone0=20 mag17 chamber;";
        }

        ItemBase rag = ItemBase.Cast(inv.CreateEntityInCargo("Rag"));
        if (rag)
        {
            rag.SetQuantity(3);
            rag.SetWet(0.5);
            rag.SetTemperature(30);
            r = r + "Rag q3 wet0.5 t30;";
        }

        ItemBase canteen = ItemBase.Cast(inv.CreateEntityInCargo("Canteen"));
        if (canteen)
        {
            canteen.SetLiquidType(LIQUID_WATER);
            canteen.SetQuantity(500);
            r = r + "Canteen water q500;";
        }

        ItemBase batt = ItemBase.Cast(inv.CreateEntityInCargo("Battery9V"));
        if (batt && batt.HasEnergyManager())
        {
            batt.GetCompEM().SetEnergy(5);
            r = r + "Battery9V e5;";
        }

        string radioType = "OZ_Radio_100m";
        if (!GetGame().ConfigIsExisting("CfgVehicles " + radioType))
            radioType = "PersonalRadio";
        ItemTransmitter tr = ItemTransmitter.Cast(inv.CreateEntityInCargo(radioType));
        if (tr)
        {
            tr.SetFrequencyByIndex(3);
            r = r + radioType + " freq3;";
        }

        if (GetGame().ConfigIsExisting("CfgVehicles OZ_DataCarrier_Chip"))
        {
            EntityAI chip = inv.CreateEntityInCargo("OZ_DataCarrier_Chip");
            if (chip)
            {
                // No compile-time dependency on the PDA: call its public writer by name.
                bool ok = false;
                Param2<string, int> p = new Param2<string, int>("[{\"probe\":1},{\"probe\":2}]", 2);
                g_Game.GameScript.CallFunctionParams(chip, "OZ_WriteNotes", ok, p);
                r = r + "OZ_DataCarrier_Chip notes2 ok=" + ok + ";";
            }
        }
        return r;
    }

    // -------------------------------------------------------------- inspect

    static string Describe(EntityAI e, int depth)
    {
        string s = "{\"type\":\"" + e.GetType() + "\"";
        InventoryLocation loc = new InventoryLocation();
        GameInventory inv = e.GetInventory();
        if (inv && inv.GetCurrentInventoryLocation(loc))
        {
            int lt = loc.GetType();
            if (lt == InventoryLocationType.CARGO || lt == InventoryLocationType.PROXYCARGO)
                s = s + ",\"cell\":\"" + loc.GetRow() + "," + loc.GetCol() + "\"";
            else if (lt == InventoryLocationType.ATTACHMENT)
                s = s + ",\"slot\":\"" + InventorySlots.GetSlotName(loc.GetSlot()) + "\"";
            else
                s = s + ",\"loctype\":" + lt;
        }
        s = s + ",\"hp\":" + R1(e.GetHealth("", "Health"));

        TStringArray zones = new TStringArray();
        e.GetDamageZones(zones);
        string z = "";
        for (int i = 0; i < zones.Count(); i++)
        {
            if (z != "")
                z = z + ";";
            z = z + zones.Get(i) + "=" + R1(e.GetHealth(zones.Get(i), "Health"));
        }
        s = s + ",\"zones\":\"" + z + "\"";

        ItemBase item = ItemBase.Cast(e);
        if (item)
        {
            s = s + ",\"qty\":" + R1(item.GetQuantity()) + ",\"wet\":" + R2(item.GetWet());
            s = s + ",\"temp\":" + R1(item.GetTemperature()) + ",\"liquid\":" + item.GetLiquidType();
            s = s + ",\"agents\":" + item.GetAgents() + ",\"life\":" + R1(e.GetLifetime());
            if (item.HasEnergyManager())
                s = s + ",\"energy\":" + R2(item.GetCompEM().GetEnergy());
        }

        Magazine mag = Magazine.Cast(e);
        if (mag)
        {
            s = s + ",\"ammo\":" + mag.GetAmmoCount();
            float md;
            string mt;
            if (mag.GetAmmoCount() > 0 && mag.GetCartridgeAtIndex(0, md, mt))
                s = s + ",\"cart0\":\"" + mt + "\"";
        }

        Weapon_Base w = Weapon_Base.Cast(e);
        if (w)
        {
            bool loaded = !w.IsChamberEmpty(0);
            s = s + ",\"chamber\":" + BoolText(loaded);
            float cd;
            string ct;
            if (loaded && w.GetCartridgeInfo(0, cd, ct))
                s = s + ",\"chamber_type\":\"" + ct + "\"";
            s = s + ",\"internal\":" + w.GetInternalMagazineCartridgeCount(0);
        }

        ItemTransmitter tr = ItemTransmitter.Cast(e);
        if (tr)
            s = s + ",\"freq\":" + tr.GetTunedFrequencyIndex();

        if (e.GetType().IndexOf("OZ_DataCarrier") == 0)
        {
            int notes = -1;
            g_Game.GameScript.CallFunction(e, "OZ_NoteCount", notes, 0);
            s = s + ",\"notes\":" + notes;
        }

        if (inv && depth < 3)
        {
            string kids = "";
            int ac = inv.AttachmentCount();
            for (int a = 0; a < ac; a++)
            {
                if (kids != "")
                    kids = kids + ",";
                kids = kids + Describe(inv.GetAttachmentFromIndex(a), depth + 1);
            }
            CargoBase cargo = inv.GetCargo();
            if (cargo)
            {
                for (int c = 0; c < cargo.GetItemCount(); c++)
                {
                    if (kids != "")
                        kids = kids + ",";
                    kids = kids + Describe(cargo.GetItem(c), depth + 1);
                }
            }
            if (kids != "")
                s = s + ",\"children\":[" + kids + "]";
        }
        return s + "}";
    }

    // Every cargo item of the crate, one JSON per line, plus a hash of the whole text.
    static string Inspect(EntityAI crate, out int hash)
    {
        string all = "";
        CargoBase cargo = crate.GetInventory().GetCargo();
        int n = 0;
        if (cargo)
            n = cargo.GetItemCount();
        for (int i = 0; i < n; i++)
        {
            string line = Describe(cargo.GetItem(i), 0);
            all = all + line + "\n";
        }
        hash = all.Hash();
        return all;
    }

    // ------------------------------------------------------------ blob save

    static bool SaveBlob(EntityAI crate, out string detail)
    {
        FileSerializer f = new FileSerializer();
        if (!f.Open(BLOB, FileMode.WRITE))
        {
            detail = "cannot open " + BLOB + " for writing";
            return false;
        }
        CargoBase cargo = crate.GetInventory().GetCargo();
        int n = 0;
        if (cargo)
            n = cargo.GetItemCount();
        int saveVer = GetGame().SaveVersion();
        f.Write(BLOB_VERSION);
        f.Write(saveVer);
        f.Write(n);
        int written = 0;
        for (int i = 0; i < n; i++)
            written += WriteEntity(f, cargo.GetItem(i));
        f.Close();
        detail = "blob: " + n + " roots, " + written + " entities, game save version " + saveVer;
        return true;
    }

    // Header, children (attachments then cargo), then the body -- so that on
    // restore the children exist before the parent's OnStoreLoad runs, the order
    // chosen on purpose.
    static int WriteEntity(FileSerializer f, EntityAI e)
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

    // ------------------------------------------------------------ blob load

    static bool LoadBlob(EntityAI crate, out string detail, out int created, out int failed, out int loadFails)
    {
        created = 0;
        failed = 0;
        loadFails = 0;
        FileSerializer f = new FileSerializer();
        if (!f.Open(BLOB, FileMode.READ))
        {
            detail = "cannot open " + BLOB + " for reading";
            return false;
        }
        int ver;
        int saveVer;
        int n;
        if (!f.Read(ver) || !f.Read(saveVer) || !f.Read(n))
        {
            f.Close();
            detail = "blob header unreadable";
            return false;
        }
        for (int i = 0; i < n; i++)
        {
            if (!ReadEntity(f, crate, saveVer, created, failed, loadFails))
            {
                f.Close();
                detail = "stream broke at root " + i + " of " + n + "; created " + created;
                detail = detail + " missed " + failed + " loadfails " + loadFails;
                return false;
            }
        }
        f.Close();
        detail = "restored " + n + " roots, " + created + " entities, missed " + failed;
        detail = detail + ", OnStoreLoad refusals " + loadFails + ", save version " + saveVer;
        return true;
    }

    // Reads the next entity and creates it under `parent`. False when the
    // stream can no longer be followed (a class that does not exist, or an
    // OnStoreLoad that refused -- the blob's length is unknown, so nothing
    // behind it can be read either).
    static bool ReadEntity(FileSerializer f, EntityAI parent, int saveVer, inout int created, inout int failed, inout int loadFails)
    {
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
            // A container with cargo children: the engine refuses children while
            // it sits in cargo, so build it on the ground and move it afterwards.
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
        }

        if (!e)
        {
            // The blob behind this header cannot be consumed without an entity of
            // this class. A LOCAL stand-in reads it and is thrown away.
            failed++;
            vector spare = parent.GetPosition();
            spare[1] = spare[1] + 50;
            e = EntityAI.Cast(GetGame().CreateObjectEx(type, spare, ECE_LOCAL));
            if (!e)
                return false;
            standIn = true;
        }

        // Children first: attachments, then cargo, each a full entity record.
        for (int a = 0; a < ac; a++)
        {
            if (!ReadEntity(f, e, saveVer, created, failed, loadFails))
                return false;
        }
        for (int c = 0; c < cc; c++)
        {
            if (!ReadEntity(f, e, saveVer, created, failed, loadFails))
                return false;
        }

        bool bodyOk = ReadBody(f, e, saveVer, loadFails);
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
            bool placed = GameInventory.LocationSyncMoveEntity(src, dst);
            if (!placed)
                placed = parent.GetInventory().TakeEntityToCargoEx(InventoryMode.SERVER, e, 0, row, col);
            if (!placed)
                failed++;
        }
        created++;
        return true;
    }

    // The body in the order WriteBody wrote it. Chambers go in before
    // OnStoreLoad, cartridges and health after it.
    static bool ReadBody(FileSerializer f, EntityAI e, int saveVer, inout int loadFails)
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
            loadFails++;
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

    // ------------------------------------------------------------- nest

    static string Nest(EntityAI crate, string bagType, string childType, int childCount, int bags)
    {
        int moved = 0;
        int kept = 0;
        int viaSync = 0;
        int viaTake = 0;
        int noCell = 0;
        for (int b = 0; b < bags; b++)
        {
            vector pos = crate.GetPosition();
            pos[0] = pos[0] + 2;
            EntityAI bag = EntityAI.Cast(GetGame().CreateObjectEx(bagType, pos, ECE_PLACE_ON_SURFACE | ECE_NOLIFETIME));
            if (!bag)
                continue;
            int filled = 0;
            for (int c = 0; c < childCount; c++)
            {
                if (bag.GetInventory().CreateEntityInCargo(childType))
                    filled++;
            }
            InventoryLocation dst = new InventoryLocation();
            bool placed = false;
            if (crate.GetInventory().FindFreeLocationFor(bag, FindInventoryLocationType.CARGO, dst))
            {
                InventoryLocation src = new InventoryLocation();
                bag.GetInventory().GetCurrentInventoryLocation(src);
                if (GameInventory.LocationSyncMoveEntity(src, dst))
                {
                    placed = true;
                    viaSync++;
                }
                else if (crate.GetInventory().TakeEntityToCargoEx(InventoryMode.SERVER, bag, dst.GetIdx(), dst.GetRow(), dst.GetCol()))
                {
                    placed = true;
                    viaTake++;
                }
            }
            else
            {
                noCell++;
            }
            if (placed && bag.GetHierarchyParent() == crate)
            {
                moved++;
                CargoBase bc = bag.GetInventory().GetCargo();
                if (bc && bc.GetItemCount() == filled)
                    kept++;
            }
            else
            {
                GetGame().ObjectDelete(bag);
            }
        }
        string r = "bags=" + bags + " moved=" + moved + " children_kept=" + kept;
        r = r + " via_sync=" + viaSync + " via_take=" + viaTake + " no_cell=" + noCell;
        return r;
    }

    // ------------------------------------------------------------- player

    static string Give(EntityAI crate, Man player)
    {
        CargoBase cargo = crate.GetInventory().GetCargo();
        if (!cargo || cargo.GetItemCount() == 0)
            return "the crate is empty";
        EntityAI item = cargo.GetItem(0);
        string type = item.GetType();
        bool ok = player.ServerTakeEntityToInventory(FindInventoryLocationType.ANY, item);
        return "gave " + type + " to the player: " + ok;
    }

    // Drop the first item of `type` the player carries onto the ground beside them.
    static string Drop(Man player, string type)
    {
        array<EntityAI> items = new array<EntityAI>();
        player.GetInventory().EnumerateInventory(InventoryTraversalType.PREORDER, items);
        for (int i = 0; i < items.Count(); i++)
        {
            EntityAI e = items.Get(i);
            if (e.GetType() != type)
                continue;
            bool ok = player.ServerDropEntity(e);
            return "dropped " + type + ": " + ok;
        }
        return "the player carries no " + type;
    }

    static int PlayerCount(Man player, string type)
    {
        array<EntityAI> items = new array<EntityAI>();
        player.GetInventory().EnumerateInventory(InventoryTraversalType.PREORDER, items);
        int n = 0;
        for (int i = 0; i < items.Count(); i++)
        {
            if (items.Get(i).GetType() == type)
                n++;
        }
        return n;
    }

    // ------------------------------------------------------------- helpers

    static string R1(float v)
    {
        float r = Math.Round(v * 10) / 10;
        return r.ToString();
    }

    static string R2(float v)
    {
        float r = Math.Round(v * 100) / 100;
        return r.ToString();
    }

    static string BoolText(bool b)
    {
        if (b)
            return "true";
        return "false";
    }
}
