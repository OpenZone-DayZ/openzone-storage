// The `oz_storage` verb, following the bridge's contract for project verbs.
//
//   world_exec verb=oz_storage args={"op":"spawn","size":"large","pos":"x y z"}
//   world_exec verb=oz_storage args={"op":"list"}
//   world_exec verb=oz_storage args={"op":"open","id":"<box id>"}      (or pos, or nearest to the player)
//   world_exec verb=oz_storage args={"op":"close","id":"<box id>"}
//   world_exec verb=oz_storage args={"op":"status","id":"<box id>"}
//   world_exec verb=oz_storage args={"op":"files","id":"<box id>"}
//   world_exec verb=oz_storage args={"op":"slot","id":"<box id>","item":"AKM","slot":"OZ_Weapon_1","mag":"Mag_AKM_30Rnd","ammo":"17","chamber":"Bullet_762x39"}
//   world_exec verb=oz_storage args={"op":"probe","class":"OZ_PersonalStash","slot":"Body"}          (stand measurement)
//   world_exec verb=oz_storage args={"op":"stash","uid":"76561198000000000","offset":"2"}          (stand: a stash with a chosen owner)
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

        if (op == "chain")
        {
            // STAND ONLY: hang a CHAIN of attachments of ANY LENGTH off a
            // container and then put something in the last one's cargo.
            //
            // `chain` is "Class@Slot,Class@Slot,..." -- as many links as you
            // like. Nothing here counts levels, because nothing in the mod
            // counts levels either: the restore walks a parent-indexed tree of
            // whatever depth the capture found. The only depth limit in play
            // is the ENGINE's, AreChildrenAccessible()'s budget of
            // INVENTORY_MAX_REACHABLE_DEPTH_ATT = 2 attachment steps, and this
            // op exists to find where that budget actually runs out rather
            // than to assert a number.
            string cHost = OZS_Arg(args, "host", "OZ_PersonalStash");
            string cChain = OZS_Arg(args, "chain", "");
            string cInner = OZS_Arg(args, "inner", "Nail");
            vector cAt;
            if (!OZS_PlayerPos(cAt))
            {
                detail = "nobody is connected";
                return false;
            }
            array<Object> cAround = new array<Object>();
            GetGame().GetObjectsAtPosition3D(cAt, 40.0, cAround, null);
            EntityAI cNode = null;
            for (int ci = 0; ci < cAround.Count(); ci++)
            {
                EntityAI cCand = EntityAI.Cast(cAround.Get(ci));
                if (cCand && cCand.GetType() == cHost)
                {
                    cNode = cCand;
                    break;
                }
            }
            if (!cNode)
            {
                detail = "no '" + cHost + "' within 40 m";
                return false;
            }
            array<string> cLinks = new array<string>();
            cChain.Split(",", cLinks);
            string cPath = cHost;
            for (int cl = 0; cl < cLinks.Count(); cl++)
            {
                array<string> cPair = new array<string>();
                cLinks.Get(cl).Split("@", cPair);
                if (cPair.Count() != 2)
                {
                    detail = "link " + (cl + 1).ToString() + " is not Class@Slot: '" + cLinks.Get(cl) + "'";
                    return false;
                }
                EntityAI cMade = EntityAI.Cast(cNode.GetInventory().CreateAttachmentEx(cPair.Get(0), InventorySlots.GetSlotIdFromString(cPair.Get(1))));
                if (!cMade)
                {
                    detail = "REFUSED at link " + (cl + 1).ToString() + ": '" + cPair.Get(0) + "' would not go into the '" + cPair.Get(1) + "' slot of " + cNode.GetType() + " (reachable=" + cNode.GetInventory().AreChildrenAccessible().ToString() + ")";
                    return false;
                }
                cNode = cMade;
                cPath = cPath + " <- " + cPair.Get(0) + "@" + cPair.Get(1);
            }
            Object cLoose = GetGame().CreateObjectEx(cInner, cAt, ECE_PLACE_ON_SURFACE);
            EntityAI cItem = EntityAI.Cast(cLoose);
            if (!cItem)
            {
                detail = "'" + cInner + "' could not be created at all -- is that a class?";
                return false;
            }
            bool cReach = cNode.GetInventory().AreChildrenAccessible();
            if (!cNode.GetInventory().TakeEntityToCargo(InventoryMode.SERVER, cItem))
            {
                detail = cPath + ": REFUSED '" + cInner + "' into the cargo of the last link (AreChildrenAccessible=" + cReach.ToString() + ")";
                return false;
            }
            detail = cPath + " <- '" + cInner + "' in its cargo: every link accepted";
            return true;
        }

        if (op == "nest")
        {
            // STAND ONLY: does a container of class `nestHost` take an item into
            // its cargo, and does THAT item then take one into its own? The
            // owner reported that items inside the personal stash refuse to
            // hold anything, so this asks the engine the same question twice
            // and reports which of the two refused.
            string hostClass = OZS_Arg(args, "host", "OZ_PersonalStash");
            string outerClass = OZS_Arg(args, "item", "TTSKOJacket");
            string innerClass = OZS_Arg(args, "inner", "BandageDressing");
            vector nestAt;
            if (!OZS_PlayerPos(nestAt))
            {
                detail = "nobody is connected";
                return false;
            }
            array<Object> nestAround = new array<Object>();
            GetGame().GetObjectsAtPosition3D(nestAt, 40.0, nestAround, null);
            EntityAI nestHost = null;
            for (int ni = 0; ni < nestAround.Count(); ni++)
            {
                EntityAI nestCand = EntityAI.Cast(nestAround.Get(ni));
                if (nestCand && nestCand.GetType() == hostClass)
                {
                    nestHost = nestCand;
                    break;
                }
            }
            if (!nestHost)
            {
                detail = "no '" + hostClass + "' within 40 m";
                return false;
            }

            // With `slot`, the outer item is ATTACHED instead of dropped into
            // cargo. That is the whole question: the engine lets an attachment
            // keep its own cargo reachable and a cargo item does not.
            string nestSlot = OZS_Arg(args, "slot", "");
            EntityAI nestOuter = null;
            if (nestSlot != "")
                nestOuter = EntityAI.Cast(nestHost.GetInventory().CreateAttachmentEx(outerClass, InventorySlots.GetSlotIdFromString(nestSlot)));
            else
                nestOuter = EntityAI.Cast(nestHost.GetInventory().CreateEntityInCargo(outerClass));
            if (!nestOuter)
            {
                detail = hostClass + " REFUSED '" + outerClass + "' into '" + nestSlot + "' (empty means cargo)";
                return false;
            }
            // The MOVE path, not the creation path: a player dragging an item
            // is a move, and the two have different validation. Creating into
            // a nested container fails even in a plain vanilla box, so the
            // creation path says nothing about what the owner saw.
            Object nestLoose = GetGame().CreateObjectEx(innerClass, nestAt, ECE_PLACE_ON_SURFACE);
            EntityAI nestInner = EntityAI.Cast(nestLoose);
            if (!nestInner)
            {
                detail = "'" + innerClass + "' could not be created at all -- is that a class?";
                return false;
            }
            bool nestMoved = nestOuter.GetInventory().TakeEntityToCargo(InventoryMode.SERVER, nestInner);
            if (!nestMoved)
            {
                detail = hostClass + " took '" + outerClass + "', but the MOVE of '" + innerClass + "' into ITS cargo was refused";
                return false;
            }
            detail = hostClass + " took '" + outerClass + "', which took a MOVED '" + innerClass + "': both levels accepted";
            return true;
        }

        if (op == "stash")
        {
            // STAND ONLY, measurement M2 (spec 2026-09-23 §7.2): put a stash
            // in the world with a chosen owner, so two of them with different
            // owners can be looked at from two clients. The real opening path
            // (task C) will create these itself; this is the instrument that
            // lets the filter be judged before that path exists.
            string stashUid = OZS_Arg(args, "uid", "");
            float stashStep = OZS_Arg(args, "offset", "0").ToFloat();
            vector stashAt;
            if (!OZS_PlayerPos(stashAt))
            {
                detail = "nobody is connected";
                return false;
            }
            stashAt[0] = stashAt[0] + stashStep;
            // One scope per method in Enforce: `made` is taken by the probe op below.
            Object stashMade = GetGame().CreateObjectEx("OZ_PersonalStash", stashAt, ECE_PLACE_ON_SURFACE);
            OZ_PersonalStash stash = OZ_PersonalStash.Cast(stashMade);
            if (!stash)
            {
                detail = "OZ_PersonalStash could not be created";
                return false;
            }
            if (stashUid != "")
                stash.OZS_SetOwner(stashUid);
            // `anchor` and `open` make this verb able to produce the state a
            // player would: a stash with a real key, OPEN. Without them the
            // stash is CLOSED, and a closed box refuses every attachment --
            // which looks like a slot defect and is not one.
            string stashAnchor = OZS_Arg(args, "anchor", "");
            if (stashAnchor == "")
                stashAnchor = OZS_Const.AnchorKeyAt(stashAt);
            stash.OZS_SetAnchor(stashAnchor);
            detail = "stash " + stash.OZS_GetId() + " at " + stashAt.ToString(false);
            if (OZS_Arg(args, "open", "") != "")
            {
                string stashWhy;
                PlayerBase stashWho = OZS_Controller.FindPlayerByUid(stash.OZS_OwnerUid());
                bool stashOpened = false;
                if (stashWho)
                    stashOpened = OZS_Controller.Get().RequestOpen(stash, stashWho, stashWhy);
                else
                    stashOpened = OZS_Controller.Get().RequestOpenAs(stash, "probe", stash.OZS_OwnerUid(), stashWhy);
                if (!stashOpened)
                {
                    detail = detail + ", but it would not open: " + stashWhy;
                    return false;
                }
                detail = detail + ", opening";
            }
            return true;
        }

        if (op == "probe")
        {
            // STAND ONLY, measurement (personal stash spec 2026-09-23 M1):
            // does an ORDINARY container accept an item in a VANILLA character
            // slot? Reads the nearest entity of `class` -- any entity, not only
            // a storage box -- and reports what its inventory says and whether
            // the attachment was made. Nothing else in the mod depends on it.
            string probeClass = OZS_Arg(args, "class", "OZ_StashSlotProbe");
            string probeItem = OZS_Arg(args, "item", "");
            string probeSlot = OZS_Arg(args, "slot", "Body");
            vector probeAt;
            if (!OZS_PlayerPos(probeAt))
            {
                detail = "nobody is connected";
                return false;
            }
            array<Object> found = new array<Object>();
            GetGame().GetObjectsAtPosition3D(probeAt, 30.0, found, null);
            EntityAI host = null;
            for (int fi = 0; fi < found.Count(); fi++)
            {
                EntityAI cand = EntityAI.Cast(found.Get(fi));
                if (cand && cand.GetType() == probeClass)
                {
                    host = cand;
                    break;
                }
            }
            if (!host)
            {
                detail = "no " + probeClass + " within 30 m of the player";
                return false;
            }
            int probeId = InventorySlots.GetSlotIdFromString(probeSlot);
            if (probeId == InventorySlots.INVALID)
            {
                detail = "the engine does not know a slot called " + probeSlot;
                return false;
            }
            detail = probeClass + ": slot " + probeSlot + " id=" + probeId.ToString();
            if (host.GetInventory().HasInventorySlot(probeId))
                detail += ", declared on the container YES";
            else
                detail += ", declared on the container NO";
            if (probeItem == "")
                return true;
            EntityAI made = host.GetInventory().CreateAttachmentEx(probeItem, probeId);
            if (made)
                detail += "; " + made.GetType() + " ATTACHED";
            else
                detail += "; " + probeItem + " REFUSED";
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

        detail = "unknown op '" + op + "'; known: list, spawn, status, open, close, open_all, close_all, sort, files, slot, tune, lower, probe, stash, nest, chain";
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
