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

        OZ_StorageBox target = OZS_Pick(args, detail);
        if (!target)
            return false;

        if (op == "status")
        {
            detail = target.GetType() + " id=" + target.OZS_GetId() + " state=" + OZS_Const.StateName(target.OZS_GetState());
            detail = detail + " entities=" + target.OZS_CountEntities() + " stored=" + target.OZS_GetStoredCount();
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

        if (op == "files")
        {
            string bid = target.OZS_GetId();
            detail = "box " + bid + " files=" + OZS_Store.HasFiles(bid);
            detail = detail + " bin=" + FileExist(OZS_Store.BinPath(bid)) + " list=" + FileExist(OZS_Store.ListPath(bid));
            detail = detail + " lines=" + OZS_Store.ListLines(bid) + " head=[" + OZS_Store.ListHeader(bid) + "]";
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

        detail = "unknown op '" + op + "'; known: list, spawn, status, open, close, files, slot";
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
