// The thing players walk up to: a locker that holds nothing itself and only
// carries the verb. Design: docs/specs/2026-09-23-storage-personal-stash-design.md §2.
//
// It is a House (BuildingSuper is a typedef of House, 4_world/entities/game/
// super/building.c:93), so it is never in anyone's vicinity panel and never
// takes damage: Building.IsInventoryVisible() already returns false
// (3_game/entities/building.c:264), and the config parent
// StaticObj_Furniture_locker_closed_v1 descends from HouseNoDestruct. Neither
// needs an override here -- both come with the base.
//
// A building keeps its OWN action map instead of the player's: BuildingBase
// builds one per type in InitializeActions() and hands it out through
// GetActions(). SetActions() below is the only place a verb can be attached
// to it -- registering the action in ActionConstructor is necessary but not
// sufficient.
class OZ_StashAnchor : BuildingSuper
{
    override void SetActions()
    {
        super.SetActions();
        AddAction(OZS_ActionOpenStash);
    }

    // HALF OF EVERY STASH KEY AT THIS LOCKER. It has to be the same string
    // after a restart, or every player's kit would land under a new key and
    // look lost, so it is derived from where the anchor STANDS rather than
    // from an engine id: anchors are placed by an admin and do not move,
    // while a script-created building gets a fresh identity every boot.
    //
    // When the JSON placement of section 2 lands, an anchor will be able to
    // carry a name from its entry and this becomes the fallback.
    string OZS_AnchorKey()
    {
        return OZS_Const.AnchorKeyAt(GetPosition());
    }
}

// Opening a stash at this anchor. The stash itself is created at the PLAYER's
// feet rather than at the anchor: two people at one locker then get one each,
// under themselves, and both privacy and concurrency fall out of the geometry
// instead of out of a trick.
//
// All the verb does is ask OZS_Stashes, which finds or creates the container
// and hands it to the controller's ordinary open job -- the same job that
// fills a storage box from SQL, because a stash is a box with a pair for a key.
class OZS_ActionOpenStash : ActionInteractBase
{
    void OZS_ActionOpenStash()
    {
        m_CommandUID = DayZPlayerConstants.CMD_ACTIONMOD_OPENDOORFW;
        m_StanceMask = DayZPlayerConstants.STANCEMASK_ERECT | DayZPlayerConstants.STANCEMASK_CROUCH;
        m_Text = "#STR_OZS_STASH_OPEN";
    }

    // By the cursor rather than by the object's origin: a locker is two metres
    // tall and its origin sits on the floor, so aiming at the top shelf from
    // in front would otherwise measure out of reach (CCTObject compares
    // GetPosition(), CCTCursor compares where you are actually looking).
    override void CreateConditionComponents()
    {
        m_ConditionItem = new CCINone;
        m_ConditionTarget = new CCTCursor(UAMaxDistances.DEFAULT);
    }

    override bool ActionCondition(PlayerBase player, ActionTarget target, ItemBase item)
    {
        return OZ_StashAnchor.Cast(target.GetObject()) != null;
    }

    // Several players at one locker is the point, so the anchor must not be
    // locked for the duration -- vanilla's own underground lever refuses the
    // lock for the same reason (actionuseundergroundlever.c:41).
    override bool IsLockTargetOnUse()
    {
        return false;
    }

    override void OnExecuteServer(ActionData action_data)
    {
        PlayerBase player = action_data.m_Player;
        OZ_StashAnchor anchor = OZ_StashAnchor.Cast(action_data.m_Target.GetObject());
        if (!player || !anchor)
            return;
        string why;
        if (!OZS_Stashes.Open(player, anchor, why))
            OZS_Controller.Notify(player, why);
    }

    // The player's own inventory window, the ordinary one. Half a second late
    // on purpose: the stash is made on the server and has to reach this client
    // before the vicinity panel can list it. The panel then fills in front of
    // the player while the open job works, the way a box does.
    override void OnExecuteClient(ActionData action_data)
    {
        if (GetGame() && GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY))
            GetGame().GetCallQueue(CALL_CATEGORY_GAMEPLAY).CallLater(OZS_ShowInventory, 500, false);
    }

    void OZS_ShowInventory()
    {
        if (GetGame() && GetGame().GetMission())
            GetGame().GetMission().ShowInventory();
    }
}
