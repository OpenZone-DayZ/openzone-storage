// THE BOX HAS ONE VERB, AND IT IS THE PROXY'S.
//
// `OZS_ActionOpenBox` and `OZS_ActionCloseBox` stood here until 2026-09-26:
// the old scheme, where an Open materialised the record INTO the placed box
// and every player nearby saw the loot. They had already been detached from
// the box (see OZ_StorageBox.SetActions), because the two schemes cannot
// share one container -- an Open while an authority holds the same items puts
// the same loot in the world twice. Detached but still registered, they were
// one AddAction away from coming back, and none of the guarantees built since
// -- a commit per turn, the identity check on every position, the closing
// write -- applies to that path. Owner's decision: the old scheme goes
// (2026-09-26).
//
// What stayed is the one piece the proxy is built from: RequestOpenAs, the
// paced open job that fills an authority from SQL. The close job went on
// 2026-09-26 -- a session writes per turn and closes with its last letter.

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
