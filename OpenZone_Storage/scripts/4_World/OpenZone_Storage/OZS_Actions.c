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

// The verb of the NEW scheme (design 2026-09-24 §11): a box is a place with a
// button, and the button brings its contents to this one player.
//
// THE SCREEN IS THE ORDINARY INVENTORY (owner, 2026-09-24). The proxy is a
// real container standing beside the player, so the vanilla panel lists it in
// the vicinity like any other box -- with its grid, its slots, its stacking,
// its scrolling and the mod's own search bar already over it. Nothing of that
// had to be written. What had to be written is the four hooks that route a
// drag of a box's item through the server instead of through PREDICTIVE,
// which does not work on a local container (OZS_Player, OZS_Stacking).
//
// It runs entirely on the client, so there is no OnExecuteServer at all.
class OZS_ActionShowBox : ActionInteractBase
{
    void OZS_ActionShowBox()
    {
        m_CommandUID = DayZPlayerConstants.CMD_ACTIONMOD_INTERACTONCE;
        m_StanceMask = DayZPlayerConstants.STANCEMASK_ERECT | DayZPlayerConstants.STANCEMASK_CROUCH;
        m_Text = "#STR_OZS_SHOW";
    }

    override void CreateConditionComponents()
    {
        m_ConditionItem = new CCINone;
        m_ConditionTarget = new CCTObject(UAMaxDistances.DEFAULT);
    }

    override bool IsLockTargetOnUse()
    {
        return false;
    }

    override bool ActionCondition(PlayerBase player, ActionTarget target, ItemBase item)
    {
        OZ_StorageBox box = OZ_StorageBox.Cast(target.GetObject());
        if (!box)
            return false;
        if (GetGame() && !GetGame().IsDedicatedServer())
            m_Text = Widget.TranslateString("#STR_OZS_SHOW") + " (" + box.OZS_GetStoredCount() + ")";
        return true;
    }

    override void OnExecuteClient(ActionData action_data)
    {
        OZS_Mirror.Ask(action_data.m_Target.GetObject());
        // The stream takes a moment; the panel is opened once it has arrived,
        // so the player does not watch an empty box fill up.
        OZS_Mirrors.Get().ShowWhenReady();
    }
}
