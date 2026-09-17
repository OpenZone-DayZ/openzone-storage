// The storage box. One script class for the three sizes; the sizes differ
// only in config (grid and weapon slots).
//
// State machine (netsynced): CLOSED -> OPENING -> OPEN -> CLOSING -> CLOSED.
// While OPEN the cargo and the weapon slots are the engine's ordinary cargo
// and attachments. While CLOSED nothing exists in the world and the count of
// stored items rides along as a netsynced int for the action text. OPENING
// and CLOSING are the paced jobs of OZS_Controller.
//
// The gates below follow the vanilla barrel: cargo and slots display and
// accept only while open; the box is never takeable and never goes into
// cargo or hands. `CanLoadItemIntoCargo` is deliberately NOT gated -- the
// engine calls it when the storage loads and a false there drops the box's
// own cargo at boot (entityai.c:1567).
class OZ_StorageBox : DeployableContainer_Base
{
    protected int    m_OZS_State;
    protected int    m_OZS_StoredCount;
    protected string m_OZS_Id;
    // Server only: the controller's job is creating entities in this box, so
    // the receive gates answer yes although the box is not OPEN yet.
    protected bool   m_OZS_Restoring;
    // Server only: tick time of the last activity in this box, which is the
    // opening itself or any item going in, out or across (0 = not open);
    // the auto-close counts idle time from it. And the time of the last
    // sort, for its cooldown.
    protected float  m_OZS_TouchedAt;
    protected float  m_OZS_LastSort;

    override void InitItemVariables()
    {
        super.InitItemVariables();
        m_OZS_State = OZS_Const.STATE_CLOSED;
        m_OZS_StoredCount = 0;
        m_OZS_Id = "";
        m_OZS_Restoring = false;
        RegisterNetSyncVariableInt("m_OZS_State", 0, 3);
        RegisterNetSyncVariableInt("m_OZS_StoredCount", 0, 100000);
    }

    override void EEInit()
    {
        super.EEInit();
        if (!GetGame() || !GetGame().IsServer())
            return;
        // A fresh box gets an id here; a loaded one is overwritten by OnStoreLoad.
        if (m_OZS_Id == "")
            m_OZS_Id = OZS_Controller.NewId();
        SetTakeable(false);
        // Every boot renews the lifetime, so a box outlives the central
        // economy's cleanup without an entry in types.xml.
        SetLifetime(OZS_Const.BOX_LIFETIME);
        OZS_Controller.Get().Register(this);
    }

    // A box leaving the world is worth a line AND a mark in its own directory:
    // the store stays on disk either way, and an admin looking at the files
    // has no other way to tell an orphan from a box that is merely closed.
    // The mission teardown deletes every entity too, and EEDelete cannot tell
    // the two apart, so the controller's shutdown flag decides.
    override void EEDelete(EntityAI parent)
    {
        if (GetGame() && GetGame().IsServer())
        {
            if (!OZS_Controller.IsShuttingDown())
            {
                string what = "removed from the world as " + OZS_Const.StateName(m_OZS_State);
                what = what + " with " + OZS_CountEntities() + " entities, " + m_OZS_StoredCount + " stored";
                OZ_Log.Warn("storage: box " + m_OZS_Id + " " + what + "; its files are kept");
                OZS_Store.MarkRemoved(m_OZS_Id, GetType() + " " + what + " at " + GetPosition().ToString(false));
            }
            OZS_Controller.Get().Unregister(this);
        }
        super.EEDelete(parent);
    }

    // ---- persistence -----------------------------------------------------

    override void OnStoreSave(ParamsWriteContext ctx)
    {
        super.OnStoreSave(ctx);
        ctx.Write(OZS_Const.SAVE_VERSION);
        ctx.Write(m_OZS_Id);
        ctx.Write(m_OZS_State);
        ctx.Write(m_OZS_StoredCount);
        if (GetGame() && GetGame().IsServer())
            OZS_Controller.Get().OnBoxSaved(this);
    }

    override bool OnStoreLoad(ParamsReadContext ctx, int version)
    {
        if (!super.OnStoreLoad(ctx, version))
            return false;
        int v;
        if (!ctx.Read(v))
            return false;
        string id;
        if (!ctx.Read(id))
            return false;
        int state;
        if (!ctx.Read(state))
            return false;
        int count;
        if (!ctx.Read(count))
            return false;
        if (id != "")
            m_OZS_Id = id;
        m_OZS_State = state;
        m_OZS_StoredCount = count;
        return true;
    }

    // After the whole hierarchy (cargo included) has been loaded.
    override void EEOnAfterLoad()
    {
        super.EEOnAfterLoad();
        if (GetGame() && GetGame().IsServer())
            OZS_Controller.Get().Reconcile(this);
    }

    // ---- state -----------------------------------------------------------

    int OZS_GetState()
    {
        return m_OZS_State;
    }

    string OZS_GetId()
    {
        return m_OZS_Id;
    }

    int OZS_GetStoredCount()
    {
        return m_OZS_StoredCount;
    }

    void OZS_SetState(int state)
    {
        m_OZS_State = state;
        SetSynchDirty();
    }

    void OZS_SetStoredCount(int count)
    {
        m_OZS_StoredCount = count;
        SetSynchDirty();
    }

    void OZS_SetRestoring(bool on)
    {
        m_OZS_Restoring = on;
    }

    bool OZS_IsRestoring()
    {
        return m_OZS_Restoring;
    }

    float OZS_GetTouchedAt()
    {
        return m_OZS_TouchedAt;
    }

    void OZS_SetTouchedAt(float t)
    {
        m_OZS_TouchedAt = t;
    }

    // Any item moving in, out or across the box restarts the idle timer, so
    // AutoCloseSeconds means "nobody has touched it for that long" and not
    // "that long since it was opened" (owner 2026-09-17). The paced jobs do
    // not count: OPENING and CLOSING are not OPEN.
    void OZS_Touch()
    {
        if (!GetGame() || !GetGame().IsServer())
            return;
        if (m_OZS_State != OZS_Const.STATE_OPEN || m_OZS_Restoring)
            return;
        m_OZS_TouchedAt = GetGame().GetTickTime();
    }

    override void EECargoIn(EntityAI item)
    {
        super.EECargoIn(item);
        OZS_Touch();
    }

    override void EECargoOut(EntityAI item)
    {
        super.EECargoOut(item);
        OZS_Touch();
    }

    override void EECargoMove(EntityAI item)
    {
        super.EECargoMove(item);
        OZS_Touch();
    }

    override void EEItemAttached(EntityAI item, string slot_name)
    {
        super.EEItemAttached(item, slot_name);
        OZS_Touch();
    }

    override void EEItemDetached(EntityAI item, string slot_name)
    {
        super.EEItemDetached(item, slot_name);
        OZS_Touch();
    }

    float OZS_GetLastSort()
    {
        return m_OZS_LastSort;
    }

    void OZS_SetLastSort(float t)
    {
        m_OZS_LastSort = t;
    }

    // ---- RPCs from the client's inventory screen ---------------------------

    override void OnRPC(PlayerIdentity sender, int rpc_type, ParamsReadContext ctx)
    {
        if (rpc_type == OZS_Const.RPC_VIEW_ID)
        {
            if (!GetGame() || !GetGame().IsServer())
                return;
            Param1<bool> p = new Param1<bool>(false);
            if (ctx.Read(p))
                OZS_Controller.Get().OnView(this, sender, p.param1);
            return;
        }
        if (rpc_type == OZS_Const.RPC_SORT_ID)
        {
            if (GetGame() && GetGame().IsServer())
                OZS_Controller.Get().RequestSort(this, sender);
            return;
        }
        super.OnRPC(sender, rpc_type, ctx);
    }

    // Entities directly in the box: cargo items plus items in the weapon slots.
    int OZS_CountEntities()
    {
        int n = 0;
        GameInventory inv = GetInventory();
        if (!inv)
            return 0;
        n = inv.AttachmentCount();
        CargoBase cargo = inv.GetCargo();
        if (cargo)
            n = n + cargo.GetItemCount();
        return n;
    }

    // The same entities, listed: weapon slots first, then the cargo in grid
    // order. Returns how many were added.
    int OZS_GetRoots(array<EntityAI> into)
    {
        int n = 0;
        GameInventory inv = GetInventory();
        if (!inv)
            return 0;
        int ac = inv.AttachmentCount();
        for (int a = 0; a < ac; a++)
        {
            EntityAI att = inv.GetAttachmentFromIndex(a);
            if (att)
            {
                into.Insert(att);
                n++;
            }
        }
        CargoBase cargo = inv.GetCargo();
        if (cargo)
        {
            int cc = cargo.GetItemCount();
            for (int c = 0; c < cc; c++)
            {
                EntityAI item = cargo.GetItem(c);
                if (item)
                {
                    into.Insert(item);
                    n++;
                }
            }
        }
        return n;
    }

    // ---- gates -----------------------------------------------------------

    override bool IsOpen()
    {
        return m_OZS_State == OZS_Const.STATE_OPEN;
    }

    override bool CanReceiveItemIntoCargo(EntityAI item)
    {
        if (!IsOpen() && !m_OZS_Restoring)
            return false;
        return super.CanReceiveItemIntoCargo(item);
    }

    override bool CanReceiveAttachment(EntityAI attachment, int slotId)
    {
        if (!IsOpen() && !m_OZS_Restoring)
            return false;
        return super.CanReceiveAttachment(attachment, slotId);
    }

    // Taking is strictly for an OPEN box. The restore job only ever puts
    // things in, so it needs no exception here, and a player must not empty
    // a box that is still filling up.
    override bool CanReleaseCargo(EntityAI cargo)
    {
        if (!IsOpen())
            return false;
        return super.CanReleaseCargo(cargo);
    }

    override bool CanReleaseAttachment(EntityAI attachment)
    {
        if (!IsOpen())
            return false;
        return super.CanReleaseAttachment(attachment);
    }

    // The UI shows the grid and the weapon slots from the first frame of
    // OPENING, so the player watches the box fill instead of staring at a
    // closed panel for six seconds (owner 2026-09-17). Nothing can be taken
    // out until it is OPEN, and the search bar of the panel says how far the
    // loading got.
    override bool CanDisplayCargo()
    {
        return OZS_IsUsable();
    }

    override bool CanDisplayAttachmentCategory(string category_name)
    {
        if (!OZS_IsUsable())
            return false;
        return super.CanDisplayAttachmentCategory(category_name);
    }

    override bool CanDisplayAttachmentSlot(int slot_id)
    {
        if (!OZS_IsUsable())
            return false;
        return super.CanDisplayAttachmentSlot(slot_id);
    }

    // Open, or opening and therefore worth showing.
    bool OZS_IsUsable()
    {
        return m_OZS_State == OZS_Const.STATE_OPEN || m_OZS_State == OZS_Const.STATE_OPENING;
    }

    override bool CanPutInCargo(EntityAI parent)
    {
        return false;
    }

    override bool CanPutIntoHands(EntityAI parent)
    {
        return false;
    }

    // ---- actions ---------------------------------------------------------

    override void SetActions()
    {
        super.SetActions();
        AddAction(OZS_ActionOpenBox);
        AddAction(OZS_ActionCloseBox);
    }
}

class OZ_StorageBox_Small : OZ_StorageBox
{
}

class OZ_StorageBox_Medium : OZ_StorageBox
{
}

class OZ_StorageBox_Large : OZ_StorageBox
{
}
