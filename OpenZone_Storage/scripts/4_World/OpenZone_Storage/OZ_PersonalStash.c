// The personal stash: one record per player per anchor, shown to that one
// player through the proxy. Design: docs/specs/2026-09-23-storage-personal-
// stash-design.md, as rebuilt on the proxy (2026-09-24 §11).
//
// IT IS A STORAGE BOX, and it is never placed in the world. Exactly two
// things of this class ever exist: the AUTHORITY of a stash session on the
// server (OZS_Authority.Create, unannounced and unsaved) and its PROXY on
// the owner's client (OZS_Mirror.Begin, client-local). Both are told whose
// stash they stand for through OZS_StandForId, and both answer with the
// pair as their id. The config gives the class the equipment slots a kit
// needs; everything else -- the gates, the audit hooks, the root order -- is
// the box's.
//
// THE OLD SCHEME stood a real, replicated container of this class under the
// owner's feet, filled it from SQL and kept it hidden from other players by
// an honour-system vicinity filter. That container, its persistence of the
// key across a crash, its idle and distance exits and the boot sweep that
// stored the ones a crash left standing went on 2026-09-26: nothing of this
// class is saved any more, so there is nothing to sweep and nothing to
// migrate.
class OZ_PersonalStash : OZ_StorageBox
{
    // The owner's SteamID64 does not fit an int and Enforce has no synced
    // string, so it rides as two halves -- the shape WarZ_Locker and
    // Askal_SafeContainer both use. The client's proxy needs it to answer
    // OZS_IsMine, which the display gates below ask.
    protected int m_OZS_OwnerHi;
    protected int m_OZS_OwnerLo;
    // Which anchor this stash belongs to: the other half of the key, and the
    // reason a stash at one locker is not the stash at another.
    protected string m_OZS_Anchor;

    override void InitItemVariables()
    {
        super.InitItemVariables();
        m_OZS_OwnerHi = 0;
        m_OZS_OwnerLo = 0;
        m_OZS_Anchor = "";
        RegisterNetSyncVariableInt("m_OZS_OwnerHi");
        RegisterNetSyncVariableInt("m_OZS_OwnerLo");
    }

    // ---- the key ---------------------------------------------------------

    // Splitting is by decimal halves rather than bit shifts: a SteamID64 is
    // handled as a string everywhere else in the series, and a round trip
    // through a string is what the bridge and the audit already do.
    void OZS_SetOwner(string uid)
    {
        int cut = uid.Length() - 9;
        if (cut < 0)
            cut = 0;
        string hi = uid.Substring(0, cut);
        string lo = uid.Substring(cut, uid.Length() - cut);
        m_OZS_OwnerHi = hi.ToInt();
        m_OZS_OwnerLo = lo.ToInt();
        SetSynchDirty();
    }

    string OZS_OwnerUid()
    {
        if (m_OZS_OwnerHi == 0 && m_OZS_OwnerLo == 0)
            return "";
        string lo = m_OZS_OwnerLo.ToString();
        while (lo.Length() < 9)
            lo = "0" + lo;
        if (m_OZS_OwnerHi == 0)
            return lo;
        return m_OZS_OwnerHi.ToString() + lo;
    }

    void OZS_SetAnchor(string key)
    {
        m_OZS_Anchor = key;
    }

    string OZS_GetAnchor()
    {
        return m_OZS_Anchor;
    }

    // THE KEY IS A PAIR, not the engine's persistent id a box uses: the same
    // player at two lockers has two stashes, and the same locker holds one per
    // player. It is written as ONE STRING because that is what the bridge's
    // box_id column is -- opaque text -- so the pair needs no schema of its
    // own; OZS_Const.StashId is the single place that spells it, and the
    // bridge's own splitter is its mirror.
    //
    // Being told an id takes the pair APART again, because a stash answers
    // with its pair rather than with a stored string. Without this the
    // authority answered with an empty id and the bridge refused the open
    // with "bad box id" (measured 2026-09-25).
    override void OZS_StandForId(string id)
    {
        super.OZS_StandForId(id);
        if (!OZS_Const.IsStashId(id))
            return;
        OZS_SetAnchor(OZS_Const.StashAnchorOf(id));
        OZS_SetOwner(OZS_Const.StashOwnerOf(id));
    }

    override string OZS_GetId()
    {
        string uid = OZS_OwnerUid();
        if (m_OZS_Anchor == "" || uid == "")
            return "";
        return OZS_Const.StashId(m_OZS_Anchor, uid);
    }

    // ---- what a client sees ----------------------------------------------

    override void EEInit()
    {
        super.EEInit();
        if (GetGame() && !GetGame().IsDedicatedServer())
        {
            // The model never shows: the proxy stands beside the player, and
            // the anchor is the thing players are meant to see.
            SetInvisible(true);
        }
    }

    // Client side: is this the local player's own? The proxy is, always --
    // it was built from their record -- so this is belt to the braces of the
    // vicinity filter. On the server there is no local player, so it answers
    // true and the server's own code is never gated by it.
    bool OZS_IsMine()
    {
        if (GetGame().IsDedicatedServer())
            return true;
        PlayerBase me = PlayerBase.Cast(GetGame().GetPlayer());
        if (!me || !me.GetIdentity())
            return false;
        return me.GetIdentity().GetPlainId() == OZS_OwnerUid();
    }

    override bool CanDisplayCargo()
    {
        return OZS_IsMine() && super.CanDisplayCargo();
    }

    override bool CanDisplayAttachmentSlot(int slot_id)
    {
        return OZS_IsMine() && super.CanDisplayAttachmentSlot(slot_id);
    }

    override bool CanDisplayAttachmentCategory(string category_name)
    {
        return OZS_IsMine() && super.CanDisplayAttachmentCategory(category_name);
    }

    // ---- no verbs of its own ---------------------------------------------

    // Deliberately empty, and super deliberately NOT called: the box's verb
    // and the deployable's place verbs would all be offered on an invisible
    // thing beside a player. The anchor carries the only verb there is.
    override void SetActions()
    {
    }
}
