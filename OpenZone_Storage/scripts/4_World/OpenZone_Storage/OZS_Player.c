// A player leaving the world: their viewer entries go, and an open box next
// to them with nobody else around closes now rather than after the quiet
// period. OnDisconnect runs after the logout timer and before the character
// is saved (CF's measured ordering); EEKilled runs before the corpse exists.
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
            OZS_Controller.Get().OnPlayerGone(this, "disconnect");
        super.OnDisconnect();
    }

    override void EEKilled(Object killer)
    {
        if (GetGame() && GetGame().IsServer())
            OZS_Controller.Get().OnPlayerGone(this, "death");
        super.EEKilled(killer);
    }
}
