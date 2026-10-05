// The thing players walk up to: a locker that holds nothing itself and only
// carries the verb. Design: docs/specs/2026-09-23-storage-personal-stash-design.md §2.
//
// A PLACED ITEM SINCE 2026-09-26 (owner: "give it a model that does not
// vanish by lifetime"). It was the vanilla static locker itself before -- a
// House -- and a House made by script is never saved: the world save holds
// items, not buildings, so every restart lost the anchor, and with it the
// way to every stash keyed at that spot. Now it is an item, exactly as a
// placed box is: saved with the world, its lifetime renewed to 45 days on
// every boot, wearing the locker's model by its path (config.cpp).
//
// What the House gave for free is given back by hand below: it is in
// nobody's vicinity panel (IsInventoryVisible), nobody can pick it up or
// pocket it, and its health is a million. The key of a stash under it is
// still where it STANDS (OZS_AnchorKey), so an anchor placed again on the
// same whole metre after being lost finds every stash again.
class OZ_StashAnchor : ItemBase
{
    // The engine's own id of this item, as a box has one: what the bridge's
    // admin page names the locker by (owner, 2026-09-27), while the stashes
    // under it stay keyed by where it stands (OZS_AnchorKey). Valid the
    // frame the item is created and the same after every boot, as a box's.
    protected string m_OZS_Id;

    string OZS_GetId()
    {
        if (m_OZS_Id == "" && GetGame() && GetGame().IsServer())
            m_OZS_Id = OZS_Store.PersistentIdOf(this);
        return m_OZS_Id;
    }

    // The opening the doors play, one of the two the model carries
    // (config.cpp, AnimationSources):
    //   "locker_a"  smooth: the lever turned, the right door opens
    //   "locker_b"  snap:   both doors flung open, they bounce on the stop
    static const string OZS_DOOR_SOURCE = "locker_b";

    // THE DOORS, ON THIS CLIENT ONLY AND NEVER SYNCHRONISED: what the player
    // whose stash is open sees. The server never calls this, so the phase
    // every other client holds stays 0 and they go on seeing a closed locker
    // with a player gesturing at it -- nobody walks up to their locker to
    // find somebody else's doors standing open. Vanilla moves an item's
    // animation on the client alone the same way (PowerGenerator.
    // UpdateFuelMeter). A locker that streams in again starts closed.
    void OZS_ShowOpen(bool open)
    {
        if (!GetGame() || GetGame().IsDedicatedServer())
            return;
        float phase = 0;
        if (open)
            phase = 1;
        SetAnimationPhase(OZS_DOOR_SOURCE, phase);
    }

    override void EEInit()
    {
        super.EEInit();
        if (!GetGame() || !GetGame().IsServer())
            return;
        SetTakeable(false);
        // Every boot renews the lifetime, so the anchor outlives the central
        // economy's cleanup without an entry in types.xml -- as a box does.
        SetLifetime(OZS_Const.BOX_LIFETIME);
        OZS_Controller.Get().RegisterAnchor(this);
    }

    // A locker leaving the world is news to the bridge, as a box is. The
    // mission teardown deletes everything too, and the controller's
    // shutdown flag tells the two apart (OZ_StorageBox.EEDelete).
    override void EEDelete(EntityAI parent)
    {
        if (GetGame() && GetGame().IsServer())
        {
            string id = OZS_GetId();
            if (!OZS_Controller.IsShuttingDown() && id != "")
            {
                OZ_Log.Warn("storage: locker " + id + " removed from the world at " + GetPosition().ToString(false));
                OZS_Audit.Log("removed", id, "", "", GetType(), 0, -1, -1, OZS_AnchorKey(), "removed from the world at " + GetPosition().ToString(false));
            }
            OZS_Controller.Get().UnregisterAnchor(this);
        }
        super.EEDelete(parent);
    }

    // Set again after the load, where it is the last word: the engine
    // restores the saved lifetime AFTER EEInit and would overwrite the
    // renewal above (measured on the boxes, 2026-09-25).
    override void EEOnAfterLoad()
    {
        super.EEOnAfterLoad();
        if (GetGame() && GetGame().IsServer())
            SetLifetime(OZS_Const.BOX_LIFETIME);
    }

    // Not a thing anybody's inventory screen lists, holds, carries or wears.
    // The action still finds it: it aims by the cursor, not by the panel.
    override bool IsInventoryVisible()
    {
        return false;
    }

    override bool IsTakeable()
    {
        return false;
    }

    override bool CanPutInCargo(EntityAI parent)
    {
        return false;
    }

    override bool CanPutIntoHands(EntityAI parent)
    {
        return false;
    }

    override bool CanPutAsAttachment(EntityAI parent)
    {
        return false;
    }

    override bool CanDisplayCargo()
    {
        return false;
    }

    override void SetActions()
    {
        super.SetActions();
        AddAction(OZS_ActionOpenStash);
    }

    // HALF OF EVERY STASH KEY AT THIS LOCKER. It has to be the same string
    // after a restart, or every player's kit would land under a new key and
    // look lost, so it is derived from where the anchor STANDS rather than
    // from an engine id -- and it survives the anchor itself: one placed
    // again on the same whole metre answers to the same stashes.
    string OZS_AnchorKey()
    {
        return OZS_Const.AnchorKeyAt(GetPosition());
    }
}

// Opening a stash at this anchor. Nothing is created in the world: the client
// asks for the anchor it is looking at, the server pairs it with the asker
// and streams that one player's record to their proxy (OZS_Proxy,
// RPC_PX_OPEN). Two people at one locker get two sessions, and neither can
// name the other's -- the pairing is made on the server from who sent the
// message, which is where the privacy comes from.
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

    // NOTHING HAPPENS ON THE SERVER FROM THE ACTION ANY MORE.
    //
    // The stash used to be a real container spawned under the player's feet,
    // and this is where it was made. With the proxy it is a session like a
    // box's: the client asks for it by the anchor it is looking at, and the
    // server pairs that anchor with the asker (OZS_Proxy, RPC_PX_OPEN). The
    // pairing has to happen there rather than here, because that is the one
    // place that knows who sent the message.
    override void OnExecuteServer(ActionData action_data)
    {
    }

    // ASK FOR IT, AND OPEN THE SCREEN WHEN IT HAS ARRIVED.
    //
    // The same two lines a storage box uses. The half-second wait this used to
    // need is gone with the container it was waiting for: nothing has to reach
    // the client through the world any more, and `ShowWhenReady` opens the
    // panel on the stream rather than on a timer.
    override void OnExecuteClient(ActionData action_data)
    {
        OZS_Mirror.Ask(action_data.m_Target.GetObject());
        OZS_Mirrors.Get().ShowWhenReady();
    }
}
