// The two verbs of a box, shaped after ActionOpenBarrel / ActionCloseBarrel
// (interact-once, erect or crouched, target-only within the default 2 m).
// Neither flips the state itself: the controller does, because Open and Close
// are jobs that take frames, not instants.
class OZS_ActionOpenBox : ActionInteractBase
{
    void OZS_ActionOpenBox()
    {
        m_CommandUID = DayZPlayerConstants.CMD_ACTIONMOD_INTERACTONCE;
        m_StanceMask = DayZPlayerConstants.STANCEMASK_ERECT | DayZPlayerConstants.STANCEMASK_CROUCH;
        m_Text = "#STR_OZS_OPEN";
    }

    override void CreateConditionComponents()
    {
        m_ConditionItem = new CCINone;
        m_ConditionTarget = new CCTObject(UAMaxDistances.DEFAULT);
    }

    override bool ActionCondition(PlayerBase player, ActionTarget target, ItemBase item)
    {
        OZ_StorageBox box = OZ_StorageBox.Cast(target.GetObject());
        if (!box)
            return false;
        if (box.OZS_GetState() != OZS_Const.STATE_CLOSED)
            return false;
        // The stored count in the hint, on the side that draws it.
        if (GetGame() && !GetGame().IsDedicatedServer())
            m_Text = Widget.TranslateString("#STR_OZS_OPEN") + " (" + box.OZS_GetStoredCount() + ")";
        return true;
    }

    override void OnExecuteServer(ActionData action_data)
    {
        OZ_StorageBox box = OZ_StorageBox.Cast(action_data.m_Target.GetObject());
        if (!box)
            return;
        string why;
        if (!OZS_Controller.Get().RequestOpen(box, action_data.m_Player, why))
            OZS_Controller.Notify(action_data.m_Player, why);
    }
}

class OZS_ActionCloseBox : ActionInteractBase
{
    void OZS_ActionCloseBox()
    {
        m_CommandUID = DayZPlayerConstants.CMD_ACTIONMOD_INTERACTONCE;
        m_StanceMask = DayZPlayerConstants.STANCEMASK_ERECT | DayZPlayerConstants.STANCEMASK_CROUCH;
        m_Text = "#STR_OZS_CLOSE";
    }

    override void CreateConditionComponents()
    {
        m_ConditionItem = new CCINone;
        m_ConditionTarget = new CCTObject(UAMaxDistances.DEFAULT);
    }

    override bool ActionCondition(PlayerBase player, ActionTarget target, ItemBase item)
    {
        OZ_StorageBox box = OZ_StorageBox.Cast(target.GetObject());
        if (!box)
            return false;
        return box.OZS_GetState() == OZS_Const.STATE_OPEN;
    }

    override void OnExecuteServer(ActionData action_data)
    {
        OZ_StorageBox box = OZ_StorageBox.Cast(action_data.m_Target.GetObject());
        if (!box)
            return;
        string why;
        if (!OZS_Controller.Get().RequestClose(box, action_data.m_Player, why))
            OZS_Controller.Notify(action_data.m_Player, why);
    }
}
