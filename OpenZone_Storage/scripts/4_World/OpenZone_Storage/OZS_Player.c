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
            OZS_Controller.Get().OnPlayerLeft(this);
        super.OnDisconnect();
    }

    override void EEKilled(Object killer)
    {
        if (GetGame() && GetGame().IsServer())
            OZS_Controller.Get().OnPlayerLeft(this);
        super.EEKilled(killer);
    }
}
