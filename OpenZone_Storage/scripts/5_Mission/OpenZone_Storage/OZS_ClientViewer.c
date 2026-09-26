// Client: the mission hooks the proxy needs -- the wire's client end, and the
// redraw that keeps the player's scroll position.
//
// WHAT WAS HERE UNTIL 2026-09-26: a scan of the vicinity list twice a second
// that told the server which boxes the inventory screen was showing, so the
// server would not close one under a player who was browsing it. That was the
// old scheme's problem: there the contents lived in the PLACED box, and the
// server had no other way to know anybody was looking.
//
// Under the proxy the server knows exactly who is looking -- a session has its
// watchers -- and the placed box is never open, so there was nothing left for
// the scan to protect. What it still did was cost every client two vicinity
// walks a second and an RPC per box in reach. Gone with the scheme that needed
// it (owner, 2026-09-26).
#ifndef NO_GUI
modded class MissionGameplay
{
    override void OnInit()
    {
        super.OnInit();
        // The proxy wire's client end. See OZS_Mirrors.Listen: it has to be
        // the game's own invoker, because a server -> client message arrives
        // with no target and never reaches an Object.OnRPC.
        OZS_Mirrors.Listen();
    }

    override void OnMissionFinish()
    {
        // The proxies go with the world they mirrored; see OZS_Mirrors.Reset.
        OZS_Mirrors.Reset();
        super.OnMissionFinish();
    }

    override void OnUpdate(float timeslice)
    {
        super.OnUpdate(timeslice);
        if (GetGame() && GetGame().IsClient())
        {
            OZS_Mirrors.Get().Update(timeslice);
            // Something in a proxy moved: tell the open inventory to draw
            // itself again. Once per frame at most, and only while a box is
            // open -- with no box the flag is never raised.
            if (OZS_Mirrors.TakeRedraw())
            {
                InventoryMenu open = InventoryMenu.Cast(GetGame().GetUIManager().FindMenu(MENU_INVENTORY));
                if (open && open.m_Inventory)
                {
                    // WHERE THE PLAYER WAS LOOKING, KEPT ACROSS THE REDRAW.
                    // A rebuilt panel starts at the top, and a box five
                    // hundred cells tall makes that expensive to the person
                    // who had scrolled down to the thing they were moving.
                    ScrollWidget bar = open.m_Inventory.OZS_Scroller();
                    float was = -1;
                    if (bar)
                        was = bar.GetVScrollPos();
                    // A BOX BEING REBUILT READS AS ZERO, AND ZERO IS A LIE.
                    //
                    // While a resynchronisation is in flight the column has
                    // nothing in it, so the scroller honestly answers 0 -- and
                    // saving that would throw away the very place this is
                    // meant to keep. So the last position taken while the box
                    // was WHOLE is held aside, and only that one is put back.
                    if (OZS_Mirrors.Get().Whole())
                    {
                        if (was >= 0)
                            OZS_Mirrors.s_ScrollWas = was;
                    }
                    open.m_Inventory.Refresh();
                    if (bar && OZS_Mirrors.s_ScrollWas >= 0)
                        bar.VScrollToPos(OZS_Mirrors.s_ScrollWas);
                }
            }
        }
    }
}
#endif
