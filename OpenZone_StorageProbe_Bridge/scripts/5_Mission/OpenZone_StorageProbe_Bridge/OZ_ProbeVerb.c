// The `oz_probe` verb, following the bridge's own contract for project verbs
// (DZMCP_BridgeCore.c, "HOW A PROJECT ADDS ITS OWN VERB"): the verb is added to
// KnownVerbs/IsKnownVerb, routed in Dispatch, and its handler checks its own
// arguments. Every job answers within its tick -- long work runs in
// MissionServer.OnUpdate and is read back with op=status.
//
//   world_exec verb=oz_probe args={"op":"crate","pos":"x y z","size":"big"}
//   world_exec verb=oz_probe args={"op":"fill","item":"Paper","n":"5000","batch":"0","mode":"loc"}
//   world_exec verb=oz_probe args={"op":"status"}
modded class DZMCP_BridgeCore
{
    override protected string KnownVerbs()
    {
        return super.KnownVerbs() + ", oz_probe, oz_ghost";
    }

    override protected bool IsKnownVerb(string verb)
    {
        if (verb == "oz_probe" || verb == "oz_ghost")
            return true;
        return super.IsKnownVerb(verb);
    }

    override protected void Dispatch(string verb, string raw)
    {
        if (verb != "oz_probe" && verb != "oz_ghost")
        {
            super.Dispatch(verb, raw);
            return;
        }

        DZMCP_CommandFull full = new DZMCP_CommandFull();
        string parseError;
        if (!m_Json.ReadFromString(full, raw, parseError))
        {
            FinishCommand(DZMCP_STATUS_FAILED, "oz_probe: the args block could not be parsed -- every value must be a string: " + Excerpt(parseError));
            return;
        }

        map<string, string> args = full.args;
        string op = "status";
        if (args)
        {
            string given;
            if (args.Find("op", given))
                op = given;
        }

        string detail;
        bool ok;
        if (verb == "oz_ghost")
            ok = GhostCommand(op, args, detail);
        else
            ok = OZ_Probe.Get().Command(op, args, detail);
        if (ok)
            FinishCommand(DZMCP_STATUS_DONE, detail);
        else
            FinishCommand(DZMCP_STATUS_FAILED, detail);
    }

    // oz_ghost: the zombie watcher (OZ_GhostWatch.c).
    protected bool GhostCommand(string op, map<string, string> args, out string detail)
    {
        vector pos = GhostArg(args, "pos", "0 0 0").ToVector();
        float radius = GhostArg(args, "radius", "6").ToFloat();
        string types = GhostArg(args, "types", "");
        OZ_GhostWatch w = OZ_GhostWatch.Get();
        if (op == "watch")
        {
            detail = w.Start(pos, radius, types, GhostArg(args, "seconds", "0").ToFloat());
            return true;
        }
        if (op == "stop")
        {
            detail = w.Stop();
            return true;
        }
        if (op == "scan")
        {
            detail = w.Scan(pos, radius, types);
            return true;
        }
        if (op == "redelete")
        {
            detail = w.Redelete(pos, radius, GhostArg(args, "mode", "bottomup"));
            return true;
        }
        if (op == "moves")
        {
            // The SERVER half of the divergence measurement: the same battery
            // OZ_ProbeClientControl runs on a client mirror, run here on a box
            // the server built. A diff of the two reports is the answer to
            // "can the mirror and the authority disagree about a move".
            string mCls = GhostArg(args, "class", "SeaChest");
            string mItem = GhostArg(args, "item", "BandageDressing");
            int mCount = GhostArg(args, "count", "3").ToInt();
            array<Man> men = new array<Man>();
            GetGame().GetPlayers(men);
            if (men.Count() == 0)
            {
                detail = "nobody is connected";
                return false;
            }
            vector mAt = men.Get(0).GetPosition();
            mAt[0] = mAt[0] - 3;
            // The same flags the design gives the authoritative box, so this
            // measures the real thing and not a lookalike.
            Object mMade = GetGame().CreateObjectEx(mCls, mAt, ECE_PLACE_ON_SURFACE | ECE_LOCAL | ECE_NOPERSISTENCY_WORLD);
            EntityAI mBox = EntityAI.Cast(mMade);
            if (!mBox)
            {
                detail = "the server refused to create " + mCls;
                return false;
            }
            for (int mi = 0; mi < mCount; mi++)
                mBox.GetInventory().CreateEntityInCargo(mItem);
            array<string> mReport = new array<string>();
            OZ_ProbeBattery.Run(mBox, mReport);
            detail = "";
            for (int mr = 0; mr < mReport.Count(); mr++)
                detail = detail + mReport.Get(mr) + " | ";
            GetGame().ObjectDelete(mBox);
            return true;
        }
        if (op == "unpublish")
        {
            // THE CALL THE DESIGN'S "put an item INTO the box" PATH STANDS ON.
            // Vanilla's own header warns "do not use if not sure what you do",
            // and this project has already been burnt once by a networking
            // shortcut (the undeletable ghosts of 2026-09-18), so the question
            // is asked rather than assumed:
            //
            //   after RemoteObjectTreeDelete, is the entity still ALIVE and
            //   WORKING on the server?
            //
            // A crate is made ANNOUNCED and filled, then unannounced, and then
            // every property the design depends on is checked: is it still
            // there, does its inventory answer, can it still take an item, can
            // its items still be moved, and can it be deleted cleanly
            // afterwards.
            string uCls = GhostArg(args, "class", "SeaChest");
            string uItem = GhostArg(args, "item", "BandageDressing");
            array<Man> uMen = new array<Man>();
            GetGame().GetPlayers(uMen);
            if (uMen.Count() == 0)
            {
                detail = "nobody is connected";
                return false;
            }
            vector uAt = uMen.Get(0).GetPosition();
            uAt[0] = uAt[0] + 4;
            // ANNOUNCED on purpose: this is an item that was in the world and
            // is being taken into the box, which is the real direction.
            Object uMade = GetGame().CreateObjectEx(uCls, uAt, ECE_PLACE_ON_SURFACE | ECE_NOPERSISTENCY_WORLD);
            EntityAI uBox = EntityAI.Cast(uMade);
            if (!uBox)
            {
                detail = "could not create " + uCls;
                return false;
            }
            for (int ui = 0; ui < 2; ui++)
                uBox.GetInventory().CreateEntityInCargo(uItem);
            string before = "before: netid " + uBox.GetNetworkIDString() + ", cargo " + uBox.GetInventory().GetCargo().GetItemCount();

            GetGame().RemoteObjectTreeDelete(uBox);

            string after = "after: ";
            if (!uBox)
            {
                detail = before + " | " + after + "THE ENTITY IS GONE -- unpublishing destroyed it";
                return true;
            }
            after = after + "alive, netid " + uBox.GetNetworkIDString();
            GameInventory uInv = uBox.GetInventory();
            if (!uInv || !uInv.GetCargo())
            {
                detail = before + " | " + after + ", BUT ITS INVENTORY IS GONE";
                return true;
            }
            after = after + ", cargo " + uInv.GetCargo().GetItemCount();
            // Can it still take something? That is the whole point of the box.
            EntityAI uMore = EntityAI.Cast(uInv.CreateEntityInCargo(uItem));
            after = after + ", accepts a new item: " + (uMore != null).ToString();
            after = after + ", cargo now " + uInv.GetCargo().GetItemCount();
            // Can what is inside still be moved? The screen depends on it.
            EntityAI uFirst = EntityAI.Cast(uInv.GetCargo().GetItem(0));
            InventoryLocation uSrc = new InventoryLocation();
            uFirst.GetInventory().GetCurrentInventoryLocation(uSrc);
            InventoryLocation uDst = new InventoryLocation();
            uDst.SetCargo(uBox, uFirst, 0, 5, 5, false);
            bool uMoved = uInv.TakeToDst(InventoryMode.LOCAL, uSrc, uDst);
            InventoryLocation uNow = new InventoryLocation();
            uFirst.GetInventory().GetCurrentInventoryLocation(uNow);
            after = after + ", a move inside: " + uMoved.ToString() + " -> " + uNow.GetRow() + "," + uNow.GetCol();
            // And does it delete cleanly, or does it become one of the zombies
            // of 2026-09-18?
            GetGame().ObjectDelete(uBox);
            after = after + ", after ObjectDelete: pending=" + uBox.IsPendingDeletion().ToString();
            detail = before + " | " + after;
            return true;
        }
        detail = "unknown op '" + op + "'; known: watch, stop, scan, redelete, moves, unpublish";
        return false;
    }

    protected string GhostArg(map<string, string> args, string key, string fallback)
    {
        if (!args)
            return fallback;
        string v;
        if (args.Find(key, v))
            return v;
        return fallback;
    }
}
