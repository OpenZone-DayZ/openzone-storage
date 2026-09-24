// STAND ONLY: every RPC the ENGINE hands this process, before any mod's
// Object.OnRPC can consume it.
//
// THE INSTRUMENT THAT SETTLED THE PROXY WIRE (2026-09-24). A handler on
// PlayerBase.OnRPC saw nothing while the server logged a send with the right
// target and the right recipient. This one, above it, showed two things at
// once: an id of 20260924 never arrived at all, and with a small id the same
// message arrived with its TARGET NULL. Both halves of the answer, neither of
// them visible from inside Object.OnRPC.
//
// Off by default -- the whole proxy wire would otherwise write a line per
// chunk. Turn it on with `rpc on` in the probe's control file.
modded class DayZGame
{
    static bool s_OZ_Trace;

    override void OnRPC(PlayerIdentity sender, Object target, int rpc_type, ParamsReadContext ctx)
    {
        if (s_OZ_Trace)
        {
            string what = "[OpenZone] probe: game OnRPC " + rpc_type.ToString();
            if (target)
                what = what + " target=" + target.GetType();
            else
                what = what + " target=none";
            what = what + " client=" + IsClient().ToString() + " server=" + IsServer().ToString();
            ErrorEx(what, ErrorExSeverity.WARNING);
        }
        super.OnRPC(sender, target, rpc_type, ctx);
    }
}
