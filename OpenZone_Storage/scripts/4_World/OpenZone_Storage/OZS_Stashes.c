// The personal stash's own side of the controller: finding one, opening one,
// and the four ways one ends. Design section 4.
//
// Everything below leans on OZS_Controller and the two paced jobs -- a stash
// is a box, so opening it IS the box's open job reading SQL and closing it IS
// the box's close job writing it. The only thing this file adds is the
// lifetime: a box stands in the world forever and a stash exists only while
// somebody is using it.
//
// SERVER ONLY. Nothing here is compiled out on a client, but every entry
// point returns early off the server.
class OZS_Stashes
{
    // ---- finding ---------------------------------------------------------

    // The stash of this owner at this anchor, if it is standing. The
    // controller's own list is the register: every stash is a box and every
    // box registers itself in EEInit.
    static OZ_PersonalStash Find(string anchorKey, string uid)
    {
        array<OZ_StorageBox> boxes = OZS_Controller.Get().Boxes();
        for (int i = 0; i < boxes.Count(); i++)
        {
            OZ_PersonalStash stash = OZ_PersonalStash.Cast(boxes.Get(i));
            if (!stash)
                continue;
            if (stash.OZS_GetAnchor() == anchorKey && stash.OZS_OwnerUid() == uid)
                return stash;
        }
        return null;
    }

    // Every stash of this owner, at any anchor. A player only ever has one
    // open at a time by the rule in Open(), but the sweep and the exits must
    // not assume it.
    static int FindAllOf(string uid, out array<OZ_PersonalStash> into)
    {
        array<OZ_StorageBox> boxes = OZS_Controller.Get().Boxes();
        for (int i = 0; i < boxes.Count(); i++)
        {
            OZ_PersonalStash stash = OZ_PersonalStash.Cast(boxes.Get(i));
            if (stash && stash.OZS_OwnerUid() == uid)
                into.Insert(stash);
        }
        return into.Count();
    }

    // ---- opening ---------------------------------------------------------

    // The whole of task C in one call: the player interacted with an anchor.
    //
    // A stash already standing for this pair is simply used again -- that is
    // the second press of the verb, and it must never cost the player what is
    // inside. A stash of theirs at ANOTHER anchor is closed first, because the
    // owner chose per-anchor binding on 2026-09-23 ("вариант А, лут не
    // телепортируется") and two open at once would give one player two homes
    // for the same close.
    static bool Open(PlayerBase player, OZ_StashAnchor anchor, out string why)
    {
        if (!GetGame() || !GetGame().IsServer())
            return false;
        if (!player || !anchor || !player.GetIdentity())
        {
            why = "#STR_OZS_OPEN_FAILED";
            return false;
        }
        string uid = player.GetIdentity().GetPlainId();
        string key = anchor.OZS_AnchorKey();

        OZ_PersonalStash mine = Find(key, uid);
        if (mine)
        {
            // Standing already. Bring it back under the player's feet -- they
            // may have stepped away -- and let the state decide: an OPEN one
            // needs nothing, a CLOSED one is opened again.
            mine.SetPosition(player.GetPosition());
            if (mine.OZS_GetState() == OZS_Const.STATE_OPEN || mine.OZS_GetState() == OZS_Const.STATE_OPENING)
            {
                OZ_Log.Info("storage: the stash of " + uid + " at " + key + " is open already");
                return true;
            }
            return OZS_Controller.Get().RequestOpen(mine, player, why);
        }

        if (!CloseOthersOf(uid, key))
        {
            why = "#STR_OZS_OPENING";
            return false;
        }

        Object made = GetGame().CreateObjectEx("OZ_PersonalStash", player.GetPosition(), ECE_PLACE_ON_SURFACE);
        OZ_PersonalStash fresh = OZ_PersonalStash.Cast(made);
        if (!fresh)
        {
            OZ_Log.Error("storage: the stash of " + uid + " at " + key + " could not be created");
            why = "#STR_OZS_OPEN_FAILED";
            return false;
        }
        // The key BEFORE the open: the job asks the bridge by id on its first
        // frame, and an id of "" would ask about nothing.
        fresh.OZS_SetAnchor(key);
        fresh.OZS_SetOwner(uid);
        OZ_Log.Info("storage: stash " + fresh.OZS_GetId() + " created for " + OZS_Controller.Who(player));
        return OZS_Controller.Get().RequestOpen(fresh, player, why);
    }

    // True when this player has no stash left open at any OTHER anchor. Ones
    // that are already closing count as gone; one that refuses to close keeps
    // the new open waiting rather than making a second home.
    protected static bool CloseOthersOf(string uid, string exceptAnchor)
    {
        array<OZ_PersonalStash> all = new array<OZ_PersonalStash>();
        FindAllOf(uid, all);
        bool clear = true;
        for (int i = 0; i < all.Count(); i++)
        {
            OZ_PersonalStash other = all.Get(i);
            if (other.OZS_GetAnchor() == exceptAnchor)
                continue;
            int state = other.OZS_GetState();
            if (state == OZS_Const.STATE_CLOSED)
            {
                Drop(other, "moved to another anchor");
                continue;
            }
            if (state == OZS_Const.STATE_CLOSING)
                continue;
            string why;
            if (!Close(other, "anchor", why))
            {
                OZ_Log.Warn("storage: stash " + other.OZS_GetId() + " will not close for another anchor: " + why);
                clear = false;
            }
        }
        return clear;
    }

    // ---- the four exits --------------------------------------------------

    // One exit, one call. `cause` is the bridge's word for why, and it lands
    // in the version row and the event, so it is worth being exact: window,
    // idle, left, died, anchor, boot.
    static bool Close(OZ_PersonalStash stash, string cause, out string why)
    {
        if (!stash)
            return true;
        int state = stash.OZS_GetState();
        if (state == OZS_Const.STATE_CLOSING)
            return true;
        if (state == OZS_Const.STATE_CLOSED)
        {
            Drop(stash, cause);
            return true;
        }
        // No viewer is spared: the stash belongs to one player and the exits
        // fire for that player, so an "except" would only ever exempt the
        // person the exit is about.
        return OZS_Controller.Get().RequestCloseAs(stash, "stash", stash.OZS_OwnerUid(), cause, "", why);
    }

    // Every stash of a player who left, died, or closed their window.
    static void CloseAllOf(string uid, string cause)
    {
        if (uid == "")
            return;
        array<OZ_PersonalStash> all = new array<OZ_PersonalStash>();
        FindAllOf(uid, all);
        for (int i = 0; i < all.Count(); i++)
        {
            string why;
            if (!Close(all.Get(i), cause, why))
                OZ_Log.Warn("storage: stash " + all.Get(i).OZS_GetId() + " (" + cause + ") will not close: " + why);
        }
    }

    // A CLOSED stash has nothing left in the world worth keeping: its contents
    // are in SQL and the entity is an empty invisible crate. This is the only
    // place a stash is removed, and it is why nothing else may call
    // ObjectDelete on one -- doing so before the close would destroy the only
    // copy of its cargo.
    static void Drop(OZ_PersonalStash stash, string cause)
    {
        if (!stash || !GetGame() || !GetGame().IsServer())
            return;
        if (stash.OZS_GetState() != OZS_Const.STATE_CLOSED)
        {
            OZ_Log.Error("storage: refusing to drop stash " + stash.OZS_GetId() + " while it is " + OZS_Const.StateName(stash.OZS_GetState()));
            return;
        }
        int left = stash.OZS_CountEntities();
        if (left > 0)
        {
            OZ_Log.Error("storage: refusing to drop stash " + stash.OZS_GetId() + ": " + left + " entity(ies) are still in it");
            return;
        }
        OZ_Log.Info("storage: stash " + stash.OZS_GetId() + " removed from the world (" + cause + ")");
        GetGame().ObjectDelete(stash);
    }

    // ---- idle and distance -----------------------------------------------

    // Called from the controller's auto-close tick, once per stash. True when
    // this stash should close now. Two reasons, both the owner's absence in
    // some form: they are too far from it, or they have not touched it.
    static bool ShouldClose(OZ_PersonalStash stash, float now, out string cause)
    {
        if (stash.OZS_GetState() != OZS_Const.STATE_OPEN)
            return false;
        OZS_Settings st = OZS_Settings.Get();
        string uid = stash.OZS_OwnerUid();
        PlayerBase owner = OZS_Controller.FindPlayerByUid(uid);
        if (!owner)
        {
            cause = "left";
            return true;
        }
        float away = vector.Distance(owner.GetPosition(), stash.GetPosition());
        if (away > st.StashMaxDistance)
        {
            cause = "away";
            return true;
        }
        float touched = stash.OZS_GetTouchedAt();
        if (touched > 0 && now - touched >= st.StashIdleSeconds)
        {
            cause = "idle";
            return true;
        }
        return false;
    }

    // ---- the boot sweep --------------------------------------------------

    // A stash that outlived a server crash is in the world at boot, because
    // the engine persists any entity. ORDER MATTERS AND IS THE POINT: the
    // contents go to SQL FIRST and the entity goes SECOND, so there is never
    // a moment when neither holds them. That is the same rule the boxes'
    // boot exchange follows.
    //
    // A stash whose key did not survive is the one case that cannot be
    // written anywhere, so it is left standing and shouted about rather than
    // quietly deleted with someone's kit inside.
    static int Sweep()
    {
        if (!GetGame() || !GetGame().IsServer())
            return 0;
        array<OZ_StorageBox> boxes = OZS_Controller.Get().Boxes();
        int swept = 0;
        for (int i = 0; i < boxes.Count(); i++)
        {
            OZ_PersonalStash stash = OZ_PersonalStash.Cast(boxes.Get(i));
            if (!stash)
                continue;
            if (stash.OZS_GetId() == "")
            {
                OZ_Log.Error("storage: a stash survived the restart with no key, holding " + stash.OZS_CountEntities() + " entity(ies) at " + stash.GetPosition().ToString(false) + "; it is left standing for an admin");
                continue;
            }
            string why;
            if (!Close(stash, "boot", why))
                OZ_Log.Warn("storage: stash " + stash.OZS_GetId() + " left from the last session will not close: " + why);
            else
                swept++;
        }
        if (swept > 0)
            OZ_Log.Info("storage: " + swept + " stash(es) left from the last session are being stored and removed");
        return swept;
    }
}
