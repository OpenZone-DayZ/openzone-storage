// The personal stash: the container a player actually opens, one per owner
// per anchor, created at that player's own feet and deleted when the session
// ends. Design: docs/specs/2026-09-23-storage-personal-stash-design.md.
//
// IT IS A STORAGE BOX. Everything a box already knows -- the four-state
// machine, the paced open that reads SQL, the paced close that writes it, the
// audit hooks, the idle clock, the viewer list, the boot reconcile -- is the
// same here, so this class inherits it rather than growing a second copy.
// What differs is exactly two things: WHAT ITS KEY IS (a pair, below) and
// that it is never seen, never taken and never acted on directly.
//
// WHY A CONTAINER PER PLAYER AND NOT ONE SHARED BOX. The engine replicates an
// entity to every client whose bubble covers it, and nothing in the script API
// takes a recipient: RemoteObjectCreate, RemoteObjectTreeCreate and their
// delete twins all act for everyone (3_game/global/game.c:704-709), ECE_LOCAL
// acts on the machine that called it, and SetInvisible hides the model while
// the entity still answers the vicinity scan. Only RPC takes an identity, so
// DATA can be addressed and EXISTENCE cannot. One box showing different cargo
// to different people is therefore impossible, and every shipping mod that
// does this -- X18PersonalChest, Expansion PersonalStorageNew, DBZ_Deposits --
// gives each player a real entity of their own instead.
//
// WHAT PRIVACY MEANS HERE, EXACTLY. The stash is filtered out of other
// players' vicinity panel on THEIR client (OZS_VicinityFilter). That is an
// honour-system filter: the entity and its cargo are still replicated to
// everyone nearby, and the server does not check the owner on an inventory
// move. The owner asked for it this way on 2026-09-23, after the cost was
// stated twice. Do not describe this as protection in a commit message or a
// listing -- it hides the stash from an ordinary player and from nobody else.
class OZ_PersonalStash : OZ_StorageBox
{
    // The owner's SteamID64 does not fit an int and Enforce has no synced
    // string, so it rides as two halves -- the shape WarZ_Locker and
    // Askal_SafeContainer both use. The client needs it to answer OZS_IsMine.
    protected int m_OZS_OwnerHi;
    protected int m_OZS_OwnerLo;
    // Server only: which anchor this stash belongs to. The other half of the
    // key, and the reason a stash at one locker is not the stash at another.
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

    // Server side. Splitting is by decimal halves rather than bit shifts: a
    // SteamID64 is handled as a string everywhere else in the series, and a
    // round trip through a string is what the bridge and the audit already do.
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
    override string OZS_GetId()
    {
        string uid = OZS_OwnerUid();
        if (m_OZS_Anchor == "" || uid == "")
            return "";
        return OZS_Const.StashId(m_OZS_Anchor, uid);
    }

    // ---- lifetime --------------------------------------------------------

    override void EEInit()
    {
        super.EEInit();
        if (GetGame() && !GetGame().IsDedicatedServer())
        {
            // The model never shows. The stash stands at its owner's feet, and
            // a visible crate under every player would be a worse lie than an
            // invisible one: the anchor is the thing players are meant to see.
            SetInvisible(true);
        }
    }

    // The anchor and the owner ride along so a stash that survived a crash
    // still knows its own key when the boot sweep finds it. The owner halves
    // are netsync variables, and the engine does not persist those.
    override void OnStoreSave(ParamsWriteContext ctx)
    {
        super.OnStoreSave(ctx);
        ctx.Write(m_OZS_Anchor);
        ctx.Write(m_OZS_OwnerHi);
        ctx.Write(m_OZS_OwnerLo);
    }

    override bool OnStoreLoad(ParamsReadContext ctx, int version)
    {
        if (!super.OnStoreLoad(ctx, version))
            return false;
        string anchor;
        if (!ctx.Read(anchor))
            return false;
        int hi;
        if (!ctx.Read(hi))
            return false;
        int lo;
        if (!ctx.Read(lo))
            return false;
        m_OZS_Anchor = anchor;
        m_OZS_OwnerHi = hi;
        m_OZS_OwnerLo = lo;
        return true;
    }

    // ---- who it belongs to -----------------------------------------------

    // Client side: is this stash the local player's? Used by the vicinity
    // filter and by the display gates. On the server there is no local player,
    // so it answers true and the server's own code is never gated by it.
    bool OZS_IsMine()
    {
        if (GetGame().IsDedicatedServer())
            return true;
        PlayerBase me = PlayerBase.Cast(GetGame().GetPlayer());
        if (!me || !me.GetIdentity())
            return false;
        return me.GetIdentity().GetPlainId() == OZS_OwnerUid();
    }

    // Belt to the vicinity filter's braces: even if the stash reaches someone
    // else's panel, it opens empty for them. Cosmetic, like the filter. The
    // box's own gates still apply underneath -- a stash that is neither OPEN
    // nor OPENING shows nothing to anybody, its owner included.
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

    // Deliberately empty, and super deliberately NOT called: the box's Open
    // and Close verbs and the deployable's place verbs would all be offered on
    // an invisible thing standing under a player's feet. The anchor carries
    // the only verb there is.
    override void SetActions()
    {
    }
}
