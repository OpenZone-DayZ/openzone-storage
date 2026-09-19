// The `oz_storage` verb, following the bridge's contract for project verbs.
//
//   world_exec verb=oz_storage args={"op":"spawn","size":"large","pos":"x y z"}
//   world_exec verb=oz_storage args={"op":"list"}
//   world_exec verb=oz_storage args={"op":"open","id":"<box id>"}      (or pos, or nearest to the player)
//   world_exec verb=oz_storage args={"op":"close","id":"<box id>"}
//   world_exec verb=oz_storage args={"op":"status","id":"<box id>"}
//   world_exec verb=oz_storage args={"op":"files","id":"<box id>"}
//   world_exec verb=oz_storage args={"op":"slot","id":"<box id>","item":"AKM","slot":"OZ_Weapon_1","mag":"Mag_AKM_30Rnd","ammo":"17","chamber":"Bullet_762x39"}
modded class DZMCP_BridgeCore
{
    override protected string KnownVerbs()
    {
        return super.KnownVerbs() + ", oz_storage";
    }

    override protected bool IsKnownVerb(string verb)
    {
        if (verb == "oz_storage")
            return true;
        return super.IsKnownVerb(verb);
    }

    override protected void Dispatch(string verb, string raw)
    {
        if (verb != "oz_storage")
        {
            super.Dispatch(verb, raw);
            return;
        }

        DZMCP_CommandFull full = new DZMCP_CommandFull();
        string parseError;
        if (!m_Json.ReadFromString(full, raw, parseError))
        {
            FinishCommand(DZMCP_STATUS_FAILED, "oz_storage: the args block could not be parsed -- every value must be a string: " + Excerpt(parseError));
            return;
        }

        map<string, string> args = full.args;
        string op = OZS_Arg(args, "op", "list");
        string detail;
        bool ok = OZS_Run(op, args, detail);
        if (ok)
            FinishCommand(DZMCP_STATUS_DONE, detail);
        else
            FinishCommand(DZMCP_STATUS_FAILED, detail);
    }

    protected string OZS_Arg(map<string, string> args, string key, string fallback)
    {
        if (!args)
            return fallback;
        string v;
        if (args.Find(key, v))
            return v;
        return fallback;
    }

    protected bool OZS_Run(string op, map<string, string> args, out string detail)
    {
        OZS_Controller c = OZS_Controller.Get();

        if (op == "list")
        {
            detail = c.Status();
            return true;
        }

        if (op == "tune")
        {
            // Runtime overrides of the settings, for the stand only.
            OZS_Settings st = OZS_Settings.Get();
            string v = OZS_Arg(args, "autoclose", "");
            if (v != "")
                st.AutoCloseSeconds = v.ToInt();
            v = OZS_Arg(args, "timeout", "");
            if (v != "")
                st.ViewerTimeoutSeconds = v.ToInt();
            v = OZS_Arg(args, "distance", "");
            if (v != "")
                st.ViewerMaxDistance = v.ToFloat();
            v = OZS_Arg(args, "rate", "");
            if (v != "")
                st.OpenItemsPerSecond = v.ToInt();
            v = OZS_Arg(args, "deletes", "");
            if (v != "")
                st.CloseDeletesPerFrame = v.ToInt();
            v = OZS_Arg(args, "close_budget", "");
            if (v != "")
                st.CloseFrameBudgetMs = v.ToInt();
            detail = "autoclose=" + st.AutoCloseSeconds + "s viewers=" + st.ViewerTimeoutSeconds + "s/" + st.ViewerMaxDistance + "m rate=" + st.OpenItemsPerSecond;
            detail = detail + " close=" + st.CloseFrameBudgetMs + "ms/" + st.CloseDeletesPerFrame;
            return true;
        }

        if (op == "spawn")
        {
            string size = OZS_Arg(args, "size", "large");
            string type = "OZ_StorageBox_Large";
            if (size == "small")
                type = "OZ_StorageBox_Small";
            else if (size == "medium")
                type = "OZ_StorageBox_Medium";
            vector pos;
            string posText = OZS_Arg(args, "pos", "");
            if (posText == "")
            {
                if (!OZS_PlayerPos(pos))
                {
                    detail = "spawn needs pos=\"x y z\" when nobody is connected";
                    return false;
                }
            }
            else
            {
                pos = posText.ToVector();
            }
            Object o = GetGame().CreateObjectEx(type, pos, ECE_PLACE_ON_SURFACE | ECE_NOLIFETIME);
            OZ_StorageBox box = OZ_StorageBox.Cast(o);
            if (!box)
            {
                detail = "the engine returned no " + type + " at " + pos.ToString();
                return false;
            }
            detail = "spawned " + type + " id=" + box.OZS_GetId() + " at " + box.GetPosition().ToString();
            return true;
        }

        // Every box at once, in one frame: the load test of ten boxes opening
        // or closing together needs the requests to land in the same frame,
        // which ten separate bridge commands never do.
        if (op == "open_all" || op == "close_all")
        {
            array<OZ_StorageBox> all = c.Boxes();
            int took = 0;
            int refused = 0;
            for (int bi = 0; bi < all.Count(); bi++)
            {
                OZ_StorageBox b = all.Get(bi);
                if (!b)
                    continue;
                string whyAll;
                bool ok;
                if (op == "open_all")
                    ok = c.RequestOpen(b, null, whyAll);
                else
                    ok = c.RequestClose(b, null, whyAll);
                if (ok)
                    took++;
                else
                    refused++;
            }
            detail = op + ": " + took + " accepted, " + refused + " refused, of " + all.Count() + " boxes";
            return true;
        }

        OZ_StorageBox target = OZS_Pick(args, detail);
        if (!target)
            return false;

        if (op == "status")
        {
            detail = target.GetType() + " id=" + target.OZS_GetId() + " state=" + OZS_Const.StateName(target.OZS_GetState());
            detail = detail + " entities=" + target.OZS_CountEntities() + " stored=" + target.OZS_GetStoredCount();
            detail = detail + " slots=[" + OZS_Slots(target) + "] viewers=" + c.ViewerCount(target);
            return true;
        }
        if (op == "open")
        {
            string whyOpen;
            if (!c.RequestOpen(target, null, whyOpen))
            {
                detail = "open refused: " + whyOpen;
                return false;
            }
            detail = "open accepted for " + target.OZS_GetId() + ", state now " + OZS_Const.StateName(target.OZS_GetState());
            return true;
        }
        if (op == "close")
        {
            string whyClose;
            if (!c.RequestClose(target, null, whyClose))
            {
                detail = "close refused: " + whyClose;
                return false;
            }
            detail = "close accepted for " + target.OZS_GetId() + ", state now " + OZS_Const.StateName(target.OZS_GetState());
            return true;
        }

        if (op == "sort")
        {
            string whySort;
            if (!c.RequestSortAs(target, "server", "", "", whySort))
            {
                detail = "sort refused: " + whySort;
                return false;
            }
            detail = "sort accepted for " + target.OZS_GetId() + ", state now " + OZS_Const.StateName(target.OZS_GetState());
            return true;
        }
        if (op == "lower")
        {
            // What the engine's ToLower does to non-ASCII text (the search).
            string t = OZS_Arg(args, "text", "");
            t.ToLower();
            detail = "[" + t + "] find=" + t.IndexOf(OZS_Arg(args, "find", "x"));
            return true;
        }
        if (op == "files")
        {
            // The box as the engine sees it, the bridge's reachability, and
            // what sits in the exchange directory: the cache of this box and
            // any close file of it still waiting for the bridge.
            string bid = target.OZS_GetId();
            detail = "box " + bid + " " + OZS_Const.StateName(target.OZS_GetState()) + " stored=" + target.OZS_GetStoredCount() + " entities=" + target.OZS_CountEntities();
            detail = detail + " bridge=" + OZS_Bridge.Up() + " boot_done=" + c.BootDone();
            detail = detail + " cache=" + FileExist(OZS_Store.XchgPath(bid + ".bin"));
            string name;
            FileAttr attr;
            int waiting = 0;
            int others = 0;
            FindFileHandle h = FindFile(OZS_Const.DIR_XCHG + "\\*", name, attr, FindFileFlags.ALL);
            if (h)
            {
                bool more = true;
                while (more)
                {
                    if (name != "" && name != "." && name != ".." && name != bid + ".bin")
                    {
                        if (name.IndexOf(bid + "-") == 0)
                            waiting++;
                        else
                            others++;
                    }
                    more = FindNextFile(h, name, attr);
                }
                CloseFindFile(h);
            }
            detail = detail + " close_files_waiting=" + waiting + " other_files=" + others;
            return true;
        }
        if (op == "slot")
        {
            // A weapon into a weapon slot of the box, with an optional loaded
            // magazine and a chambered round -- the composite case of the
            // store. Refused by the box's own gates unless it is open.
            string item = OZS_Arg(args, "item", "AKM");
            string slotName = OZS_Arg(args, "slot", "OZ_Weapon_1");
            string magType = OZS_Arg(args, "mag", "");
            int ammo = OZS_Arg(args, "ammo", "0").ToInt();
            string chamber = OZS_Arg(args, "chamber", "");
            int slotId = InventorySlots.GetSlotIdFromString(slotName);
            if (slotId == InventorySlots.INVALID)
            {
                detail = "unknown slot " + slotName;
                return false;
            }
            EntityAI weapon = target.GetInventory().CreateAttachmentEx(item, slotId);
            if (!weapon)
            {
                detail = "the box refused " + item + " in " + slotName + " (state " + OZS_Const.StateName(target.OZS_GetState()) + ")";
                return false;
            }
            detail = "attached " + weapon.GetType() + " in " + slotName;
            if (magType != "")
            {
                EntityAI magE = weapon.GetInventory().CreateAttachment(magType);
                Magazine mag = Magazine.Cast(magE);
                if (mag)
                {
                    if (ammo > 0)
                        mag.ServerSetAmmoCount(ammo);
                    detail = detail + ", " + magType + " with " + mag.GetAmmoCount();
                }
                else
                {
                    detail = detail + ", no " + magType;
                }
            }
            Weapon_Base w = Weapon_Base.Cast(weapon);
            if (w && chamber != "")
            {
                w.PushCartridgeToChamber(0, 0.0, chamber);
                w.Synchronize();
                detail = detail + ", chambered " + chamber;
            }
            return true;
        }

        detail = "unknown op '" + op + "'; known: list, spawn, status, open, close, open_all, close_all, sort, files, slot, tune, lower";
        return false;
    }

    // The box the caller means: by id, else the nearest to pos, else the
    // nearest to the connected player.
    protected OZ_StorageBox OZS_Pick(map<string, string> args, out string detail)
    {
        OZS_Controller c = OZS_Controller.Get();
        string id = OZS_Arg(args, "id", "");
        if (id != "")
        {
            OZ_StorageBox byId = c.FindById(id);
            if (!byId)
                detail = "no box with id " + id;
            return byId;
        }
        vector pos;
        string posText = OZS_Arg(args, "pos", "");
        if (posText != "")
        {
            pos = posText.ToVector();
        }
        else if (!OZS_PlayerPos(pos))
        {
            detail = "name the box with id=, or pos=, or connect a player";
            return null;
        }
        OZ_StorageBox near = c.Nearest(pos, 100);
        if (!near)
            detail = "no box within 100 m of " + pos.ToString();
        return near;
    }

    // What hangs in the weapon slots: "AKM[Mag_AKM_30Rnd:17,+1]" per slot.
    protected string OZS_Slots(OZ_StorageBox box)
    {
        string s = "";
        GameInventory inv = box.GetInventory();
        if (!inv)
            return s;
        for (int a = 0; a < inv.AttachmentCount(); a++)
        {
            EntityAI att = inv.GetAttachmentFromIndex(a);
            if (!att)
                continue;
            if (s != "")
                s = s + " ";
            s = s + att.GetType();
            Weapon_Base w = Weapon_Base.Cast(att);
            if (!w)
                continue;
            Magazine mag = w.GetMagazine(0);
            string inside = "";
            if (mag)
                inside = mag.GetType() + ":" + mag.GetAmmoCount();
            if (!w.IsChamberEmpty(0))
                inside = inside + ",+1";
            s = s + "[" + inside + "]";
        }
        return s;
    }

    protected bool OZS_PlayerPos(out vector pos)
    {
        array<Man> players = new array<Man>();
        GetGame().GetPlayers(players);
        if (players.Count() == 0)
            return false;
        pos = players.Get(0).GetPosition();
        return true;
    }
}
