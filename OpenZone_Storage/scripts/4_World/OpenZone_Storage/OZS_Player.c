// A player leaving the world: their viewer entries go; the box itself is
// left to the auto-close timer (owner 2026-09-16). OnDisconnect runs after
// the logout timer and before the character is saved (CF's measured
// ordering); EEKilled runs before the corpse exists.
modded class PlayerBase
{
    // The identity is gone by the time OnDisconnect runs; the name is kept
    // from the connect for the log line.
    protected string m_OZS_Name;

    override void OnConnect()
    {
        super.OnConnect();
        if (GetIdentity())
            m_OZS_Name = GetIdentity().GetName();
    }

    string OZS_GetName()
    {
        if (GetIdentity())
            return GetIdentity().GetName();
        return m_OZS_Name;
    }

    override void OnDisconnect()
    {
        if (GetGame() && GetGame().IsServer())
        {
            OZS_Controller.Get().OnPlayerLeft(this);
            OZS_Proxies.Get().DropPlayer(GetIdentity(), "disconnected");
        }
        super.OnDisconnect();
    }

    // THE ASKING HALF OF THE PROXY WIRE. A client's message rides on its own
    // player entity -- not on the box, because an RPC addressed to an Object
    // only reaches a side that has that object, and a box four kilometres away
    // is not streamed in (measured 2026-09-24, the first try sent a whole
    // session on the box and the client never heard a word of it).
    //
    // The ANSWERING half cannot ride on anything: a server -> client message
    // arrives with its target null whatever the server addressed it to, so the
    // client listens at DayZGame.Event_OnRPC instead. See OZS_Mirrors.Listen.
    override void OnRPC(PlayerIdentity sender, int rpc_type, ParamsReadContext ctx)
    {
        if (OZS_Proxies.OnWire(sender, rpc_type, ctx))
            return;
        super.OnRPC(sender, rpc_type, ctx);
    }

    override void EEKilled(Object killer)
    {
        if (GetGame() && GetGame().IsServer())
        {
            OZS_Controller.Get().OnPlayerLeft(this);
            OZS_Proxies.Get().DropPlayer(GetIdentity(), "died");
        }
        super.EEKilled(killer);
    }
}
