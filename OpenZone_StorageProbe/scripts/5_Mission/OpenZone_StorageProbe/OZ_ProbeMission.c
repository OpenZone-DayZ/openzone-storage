// Mission hooks: the server job engine and the client monitor get their frames
// here. No `extends` on a modded class -- it would silently leave the chain.
modded class MissionServer
{
    override void OnInit()
    {
        super.OnInit();
        OZ_Probe.Get().OnMissionInit();
    }

    override void OnUpdate(float timeslice)
    {
        super.OnUpdate(timeslice);
        OZ_Probe.Get().OnFrame(timeslice);
    }

    override void OnMissionFinish()
    {
        OZ_Probe.Get().OnMissionFinish();
        super.OnMissionFinish();
    }
}

modded class MissionGameplay
{
    protected ref OZ_ProbeClientMonitor m_OZ_ProbeMonitor;

    override void OnInit()
    {
        super.OnInit();
        if (!GetGame() || GetGame().IsDedicatedServer())
            return;
        m_OZ_ProbeMonitor = new OZ_ProbeClientMonitor();
    }

    override void OnUpdate(float timeslice)
    {
        super.OnUpdate(timeslice);
        if (m_OZ_ProbeMonitor)
            m_OZ_ProbeMonitor.OnFrame(timeslice);
    }

    override void OnMissionFinish()
    {
        if (m_OZ_ProbeMonitor)
            m_OZ_ProbeMonitor.Flush();
        m_OZ_ProbeMonitor = null;
        super.OnMissionFinish();
    }
}
