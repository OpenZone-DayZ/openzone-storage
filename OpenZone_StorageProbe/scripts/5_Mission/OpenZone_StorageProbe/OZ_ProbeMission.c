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
#ifndef NO_GUI
    protected ref OZ_ProbeClientControl m_OZ_ProbeControl;
#endif

    override void OnInit()
    {
        super.OnInit();
        if (!GetGame() || GetGame().IsDedicatedServer())
            return;
        m_OZ_ProbeMonitor = new OZ_ProbeClientMonitor();
#ifndef NO_GUI
        m_OZ_ProbeControl = new OZ_ProbeClientControl();
#endif
    }

    override void OnUpdate(float timeslice)
    {
        super.OnUpdate(timeslice);
        if (m_OZ_ProbeMonitor)
            m_OZ_ProbeMonitor.OnFrame(timeslice);
#ifndef NO_GUI
        if (m_OZ_ProbeControl)
            m_OZ_ProbeControl.OnFrame(timeslice);
#endif
    }

    override void OnMissionFinish()
    {
        if (m_OZ_ProbeMonitor)
            m_OZ_ProbeMonitor.Flush();
        m_OZ_ProbeMonitor = null;
        super.OnMissionFinish();
    }
}
