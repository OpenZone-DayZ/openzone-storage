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
        return super.KnownVerbs() + ", oz_probe";
    }

    override protected bool IsKnownVerb(string verb)
    {
        if (verb == "oz_probe")
            return true;
        return super.IsKnownVerb(verb);
    }

    override protected void Dispatch(string verb, string raw)
    {
        if (verb != "oz_probe")
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
        bool ok = OZ_Probe.Get().Command(op, args, detail);
        if (ok)
            FinishCommand(DZMCP_STATUS_DONE, detail);
        else
            FinishCommand(DZMCP_STATUS_FAILED, detail);
    }
}
