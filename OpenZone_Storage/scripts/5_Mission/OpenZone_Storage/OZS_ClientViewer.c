// Client: tells the server which boxes the inventory screen is showing.
// The server has no idea what a player is browsing (measured 2026-09-16:
// OnInventoryMenuOpen/Close are empty client-side declarations, the
// VicinityItemManager is a client singleton), so this scans the vicinity
// list twice a second while the screen is open, sends "looking" when a box
// enters it and "gone" when it leaves or the screen closes, and repeats
// "looking" every VIEW_HEARTBEAT seconds so a lost packet cannot pin a box.
#ifndef NO_GUI
class OZS_ClientViewer
{
    protected static ref OZS_ClientViewer s_Inst;

    protected ref array<OZ_StorageBox> m_Viewing;
    protected float m_Scan;
    protected float m_Beat;

    static OZS_ClientViewer Get()
    {
        if (!s_Inst)
            s_Inst = new OZS_ClientViewer();
        return s_Inst;
    }

    void OZS_ClientViewer()
    {
        m_Viewing = new array<OZ_StorageBox>();
    }

    void Update(float dt)
    {
        m_Scan = m_Scan + dt;
        if (m_Scan < OZS_Const.VIEW_SCAN)
            return;
        m_Beat = m_Beat + m_Scan;
        m_Scan = 0;

        // The inventory menu stays in the manager while hidden (measured
        // 2026-09-16: FindMenu answered it with the screen closed), so the
        // question is whether it is SHOWN.
        array<OZ_StorageBox> now = new array<OZ_StorageBox>();
        InventoryMenu menu = InventoryMenu.Cast(GetGame().GetUIManager().FindMenu(MENU_INVENTORY));
        if (menu && menu.IsOpened())
        {
            array<EntityAI> items = VicinityItemManager.GetInstance().GetVicinityItems();
            if (items)
            {
                for (int i = 0; i < items.Count(); i++)
                {
                    OZ_StorageBox b = OZ_StorageBox.Cast(items.Get(i));
                    if (b && now.Find(b) < 0)
                        now.Insert(b);
                }
            }
        }

        bool beat = m_Beat >= OZS_Const.VIEW_HEARTBEAT;
        if (beat)
            m_Beat = 0;

        for (int o = 0; o < m_Viewing.Count(); o++)
        {
            OZ_StorageBox old = m_Viewing.Get(o);
            if (old && now.Find(old) < 0)
                Send(old, false);
        }
        for (int n = 0; n < now.Count(); n++)
        {
            OZ_StorageBox b2 = now.Get(n);
            if (m_Viewing.Find(b2) < 0 || beat)
                Send(b2, true);
        }
        m_Viewing = now;
    }

    protected void Send(OZ_StorageBox box, bool viewing)
    {
        box.RPCSingleParam(OZS_Const.RPC_VIEW_ID, new Param1<bool>(viewing), true);
    }
}

modded class MissionGameplay
{
    override void OnUpdate(float timeslice)
    {
        super.OnUpdate(timeslice);
        if (GetGame() && GetGame().IsClient())
            OZS_ClientViewer.Get().Update(timeslice);
    }
}
#endif
