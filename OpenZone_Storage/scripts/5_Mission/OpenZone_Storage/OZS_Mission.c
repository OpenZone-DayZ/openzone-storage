// Mission hooks of the storage mod. No `extends` on a modded class.
modded class MissionServer
{
    override void OnInit()
    {
        super.OnInit();
        if (!GetGame() || !GetGame().IsDedicatedServer())
            return;
        OZS_Settings.Load();
    }

    override void OnMissionStart()
    {
        super.OnMissionStart();
        if (!GetGame() || !GetGame().IsDedicatedServer())
            return;
        // The ready line of the stand. The world's entities load after this
        // point; the controller writes the real summary a little later.
        OZS_Controller c = OZS_Controller.Get();
        c.OnMissionStarted();
        OZ_Log.Info("storage loaded: boxes=" + c.BoxCount() + " open=" + c.OpenCount());
    }

    override void OnUpdate(float timeslice)
    {
        super.OnUpdate(timeslice);
        if (GetGame() && GetGame().IsDedicatedServer())
            OZS_Controller.Get().OnFrame(timeslice);
    }

    override void OnMissionFinish()
    {
        if (GetGame() && GetGame().IsDedicatedServer())
        {
            // Before anything else: from here the world is coming down, and
            // every box the engine deletes is teardown and not a removal, so
            // EEDelete must not mark their stores as orphaned.
            OZS_Controller.SetShuttingDown(true);
            OZS_Controller.Get().CloseAll();
            OZS_Controller.Reset();
        }
        super.OnMissionFinish();
    }
}
