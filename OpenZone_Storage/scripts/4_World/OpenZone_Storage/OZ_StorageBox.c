// The storage box. One script class for the three sizes; the sizes differ
// only in config (grid and weapon slots).
//
// TWO KINDS OF INSTANCE, ONE CLASS. A PLACED box stands in the world as the
// anchor a player walks up to: it is always CLOSED, holds nothing, and its
// stored count rides along as a netsynced int for the action text. An
// AUTHORITY (OZS_Authority) is a box of the same class nobody is told about,
// standing for a placed box's contents while somebody looks into it; it is
// OPENING while the open job fills it from SQL and OPEN after. (A third, the
// client's proxy, is a client-local instance the mirror builds.)
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
    // Server only: this box is being torn down, so the items leaving it are
    // leaving because it is ending -- not because anybody took them. Without
    // this every session close wrote one `take` per item into the admin
    // history, and a box of 121 items closed looked exactly like a box of 121
    // items emptied by a player (measured 2026-09-25).
    protected bool   m_OZS_Releasing;
    // Server only: this box is an authority -- unannounced, unsaved, standing
    // for a box somewhere else. See OZS_Authority.
    protected bool   m_OZS_Authority;
    // Server only: the top-level items in the order the stored record lists
    // them. See OZS_RootOrder.
    protected ref array<EntityAI> m_OZS_RootOrder;
    // Server only: the time of the last sort, for its cooldown.
    protected float  m_OZS_LastSort;

    override void InitItemVariables()
    {
        super.InitItemVariables();
        m_OZS_State = OZS_Const.STATE_CLOSED;
        m_OZS_StoredCount = 0;
        m_OZS_Id = "";
        m_OZS_Restoring = false;
        m_OZS_Releasing = false;
        RegisterNetSyncVariableInt("m_OZS_State", 0, 3);
        RegisterNetSyncVariableInt("m_OZS_StoredCount", 0, 100000);
    }

    override void EEInit()
    {
        super.EEInit();
        if (!GetGame() || !GetGame().IsServer())
            return;
        // An authority is not a box in the world and must not be treated as
        // one: no id of its own (it will be told whose contents it holds), no
        // place in the controller's register, no lifetime, no audit when it
        // goes. OZS_Authority holds the latch up for the one CreateObjectEx
        // call, because EEInit runs inside it and there is no earlier moment
        // to say so.
        if (OZS_Authority.IsMaking())
        {
            m_OZS_Authority = true;
            SetTakeable(false);
            return;
        }
        // The engine's persistent id is the box id (owner, 2026-09-19): it is
        // valid the frame the box is created and the same after every boot.
        // OZS_GetId() computes it lazily as well, in case it is not there yet.
        string pid = OZS_Store.PersistentIdOf(this);
        if (pid != "")
            m_OZS_Id = pid;
        else if (m_OZS_Id == "")
            m_OZS_Id = OZS_Controller.NewId();
        SetTakeable(false);
        // Every boot renews the lifetime, so a box outlives the central
        // economy's cleanup without an entry in types.xml.
        SetLifetime(OZS_Const.BOX_LIFETIME);
        OZS_Controller.Get().Register(this);
    }

    // A box leaving the world is worth a line and an event: the bridge marks
    // it removed and keeps its versions by the retention. The mission
    // teardown deletes every entity too, and EEDelete cannot tell the two
    // apart, so the controller's shutdown flag decides.
    override void EEDelete(EntityAI parent)
    {
        if (GetGame() && GetGame().IsServer() && !m_OZS_Authority)
        {
            if (!OZS_Controller.IsShuttingDown())
            {
                string what = "removed from the world as " + OZS_Const.StateName(m_OZS_State);
                what = what + " with " + OZS_CountEntities() + " entities, " + m_OZS_StoredCount + " stored";
                OZ_Log.Warn("storage: box " + m_OZS_Id + " " + what);
                // WHO IS DELETING THIS BOX. Four boxes vanished from the world
                // at boot on 2026-09-24/25, every one of them a CLOSED box
                // with contents in SQL, and nothing in this mod deletes a
                // placed box except the admin's `remove`. The stack says which
                // side of the engine the call came from; without it the only
                // evidence is that the box is gone.
                // The stack under an unexplained deletion is worth a lot when
                // one is being hunted and nothing the rest of the time.
                if (OZ_Log.IsDebug())
                    DumpStack();
                OZS_Audit.Log("removed", m_OZS_Id, "", "", GetType(), 0, -1, -1, "", what + " at " + GetPosition().ToString(false));
                // AND ANYBODY LOOKING INTO IT IS SENT AWAY. The contents are
                // in an authority of their own and outlive this entity, so a
                // session went on trading through a box that was no longer
                // in the world (review 2026-09-26, E2). Closing it writes
                // what the authority holds into the record -- which the
                // bridge keeps as the archive of a removed box -- and that is
                // what an admin restores from.
                OZS_Session inIt = OZS_Proxies.Get().Find(OZS_GetId());
                if (inIt)
                    inIt.Close("the box was removed from the world");
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
        {
            // A box saved under an older id (a stamp) takes the persistent
            // id now; the bridge learns the new one at the boot exchange.
            string pid = OZS_Store.PersistentIdOf(this);
            if (pid != "" && pid != m_OZS_Id)
            {
                OZ_Log.Info("storage: box " + m_OZS_Id + " is " + pid + " by its persistent id from now on");
                m_OZS_Id = pid;
            }
            // HOW LONG THE ENGINE THINKS THIS BOX HAS LEFT.
            //
            // Boxes kept vanishing at world load, and the stack under the
            // deletion had NOTHING between `main()` and this class -- the
            // engine takes them, not any script (measured 2026-09-25). The
            // first suspect is the central economy's cleanup: a class with no
            // types.xml entry gets a default lifetime, and the `SetLifetime`
            // in EEInit runs BEFORE the engine restores the saved one and
            // overwrites it. So the number is printed, and then set again --
            // here, where it is the last word.
            OZ_Log.Info("storage: box " + m_OZS_Id + " comes back with " + GetLifetime().ToString() + " s of lifetime left of " + GetLifetimeMax().ToString() + "; renewing it");
            SetLifetime(OZS_Const.BOX_LIFETIME);
            OZS_Controller.Get().Reconcile(this);
        }
    }

    // ---- state -----------------------------------------------------------

    int OZS_GetState()
    {
        return m_OZS_State;
    }

    string OZS_GetId()
    {
        if (m_OZS_Id == "" && GetGame() && GetGame().IsServer())
        {
            string pid = OZS_Store.PersistentIdOf(this);
            if (pid != "")
                m_OZS_Id = pid;
        }
        return m_OZS_Id;
    }

    // An AUTHORITY stands for a box it is not: it is created unannounced and
    // unsaved, and must answer with the id of the box whose contents it holds,
    // not with a persistent id of its own (design 2026-09-24 §2). Nothing else
    // may call this -- a placed box's id is the engine's and is not ours to
    // change.
    void OZS_StandForId(string id)
    {
        m_OZS_Id = id;
    }

    // The ROOTS in the order the record lists them, filled by the open. A
    // commit names a root by its POSITION here and the bridge numbers the
    // roots of a version the same way, so the two stay in step without either
    // side sending an index the other has to trust (design 2026-09-24 §7).
    // Empty on a box that is not being used through a proxy.
    array<EntityAI> OZS_RootOrder()
    {
        if (!m_OZS_RootOrder)
            m_OZS_RootOrder = new array<EntityAI>();
        return m_OZS_RootOrder;
    }

    void OZS_NoteRoot(EntityAI e)
    {
        if (e)
            OZS_RootOrder().Insert(e);
    }

    void OZS_ForgetRoots()
    {
        OZS_RootOrder().Clear();
    }

    // -1 when this entity is not a root of the record: it is nested in one,
    // or the box was never opened through a proxy.
    int OZS_RootPosition(EntityAI e)
    {
        array<EntityAI> order = OZS_RootOrder();
        for (int i = 0; i < order.Count(); i++)
        {
            if (order.Get(i) == e)
                return i;
        }
        return -1;
    }

    // True for a container created by OZS_Authority. Everything built into
    // this box must be built LOCAL, because the box itself is unannounced and
    // a networked child of an unannounced parent is a contradiction the
    // clients would resolve badly.
    bool OZS_IsAuthority()
    {
        return m_OZS_Authority;
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

    // TWO CASES, AND ONLY ONE OF THEM IS FOR GOOD.
    //
    // A DISCARD raises this and never lowers it: the box is going away with
    // everything in it. A SORT raises it to empty the authority and lowers it
    // again before the refill, because the box goes on serving the player
    // afterwards -- and a flag left up there would silence the audit for the
    // rest of the session, which is how a box stops telling an admin anything.
    void OZS_Releasing(bool on = true)
    {
        m_OZS_Releasing = on;
    }

    // An item moving in or out while the box is open and not being restored
    // is a player's doing and becomes an event. The restore moves hundreds
    // of items through these hooks and is not one. (The idle clock these
    // hooks used to restart went with the auto-close on 2026-09-26; a
    // session has its own, OZS_Session.m_Empty.)
    protected bool OZS_Live()
    {
        if (!GetGame() || !GetGame().IsServer())
            return false;
        if (m_OZS_State != OZS_Const.STATE_OPEN || m_OZS_Restoring || m_OZS_Releasing)
            return false;
        // NOT ON AN AUTHORITY. Every crossing of its boundary is written by
        // the boundary itself, with the player's name (OZS_Boundary's `in`
        // and `out`); the cargo events wrote a second row for each, and a
        // `take` for a move into a container standing inside the box, which
        // never left it (review 2026-09-26, E4).
        if (m_OZS_Authority)
            return false;
        return true;
    }

    override void EECargoIn(EntityAI item)
    {
        super.EECargoIn(item);
        if (OZS_Live())
            OZS_Audit.Item("put", this, item, "");
    }

    override void EECargoOut(EntityAI item)
    {
        super.EECargoOut(item);
        if (OZS_Live())
            OZS_Audit.Item("take", this, item, "");
    }

    override void EEItemAttached(EntityAI item, string slot_name)
    {
        super.EEItemAttached(item, slot_name);
        if (OZS_Live())
            OZS_Audit.Item("put", this, item, slot_name);
    }

    override void EEItemDetached(EntityAI item, string slot_name)
    {
        super.EEItemDetached(item, slot_name);
        if (OZS_Live())
            OZS_Audit.Item("take", this, item, slot_name);
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

    // NO RPC OF ITS OWN ANY MORE (owner, 2026-09-26).
    //
    // Two branches lived here, and both belonged to the old scheme: RPC_VIEW_ID
    // (a client saying "I am looking at this box", so the server would not
    // close it) and RPC_SORT_ID (the Sort button, which needed the PLACED box
    // to be open). Under the proxy the server knows its watchers and the
    // placed box is never open, so the first had nothing to protect and the
    // second could not fire at all. A sort is now an ordinary turn of the
    // session (OZS_Ops.Sort), asked for over the proxy's own wire.

    // Entities directly in the box: cargo items plus items in the weapon slots.
    int OZS_CountEntities()
    {
        int n = 0;
        GameInventory inv = GetInventory();
        if (!inv)
            return 0;
        // NOT WHAT IS ON ITS WAY OUT. An item deleted this frame stands in
        // the cargo until the frame ends, and the record this count is held
        // against has already let it go -- a stack emptied into the player's
        // is deleted in the same frame as the bridge's answer is judged
        // (OZS_Boundary.Credited). OZS_Records.CountTree skips the same.
        int ac = inv.AttachmentCount();
        for (int a = 0; a < ac; a++)
        {
            EntityAI att = inv.GetAttachmentFromIndex(a);
            if (att && !att.IsSetForDeletion())
                n++;
        }
        CargoBase cargo = inv.GetCargo();
        if (cargo)
        {
            int cc = cargo.GetItemCount();
            for (int c = 0; c < cc; c++)
            {
                EntityAI it = cargo.GetItem(c);
                if (it && !it.IsSetForDeletion())
                    n++;
            }
        }
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
        // ONE LOGIC ON A BOX, NOT TWO (owner, 2026-09-24). The old verbs --
        // Open, which materialised the contents into this very entity, and
        // Close, which captured them back -- are gone from the menu. They
        // cannot share a box with the new scheme: an Open would fill the
        // placed box from SQL while an authority holds the same items, and
        // the same loot would exist twice in the world.
        //
        // The machinery behind them stays: the personal stash still uses it,
        // and so do the admin commands and the boot reconciliation. It is
        // only the player's verbs on a placed box that are down to one.
        AddAction(OZS_ActionShowBox);
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
