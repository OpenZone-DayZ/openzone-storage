// A TAKE-OUT THAT HAS NOT BEEN WRITTEN YET. See OZS_Session.m_Handover: with
// `WaitForRecord` on, the item stays in the box while its letter is on the
// wire, and this is everything needed to finish the hand-over when the answer
// comes back.
//
// The destination is kept field by field rather than as an InventoryLocation,
// because a location holds references to its parent and its item and this
// object outlives the frame that made it. The GROUND case keeps no position
// at all: a place at the player's feet is computed from the player, and by
// the time this fires they have had a round trip to walk.
//
// (This file held OZS_Late until 2026-09-26 -- a ScriptRPC held back for the
// stand's fake ping. A built message does not survive the frame it was
// written in; the ping now holds the OPERATION instead, see
// OZS_Session.OperateAs.)
class OZS_Handover
{
    string m_Uid;
    EntityAI m_Item;
    int m_Handle;
    protected int m_Type;
    protected EntityAI m_Parent;
    protected int m_Slot;
    protected int m_Row;
    protected int m_Col;
    protected bool m_Flip;

    void OZS_Handover(string uid, EntityAI item, InventoryLocation dst, int handle)
    {
        m_Uid = uid;
        m_Item = item;
        m_Handle = handle;
        m_Type = dst.GetType();
        m_Parent = dst.GetParent();
        m_Slot = dst.GetSlot();
        m_Row = dst.GetRow();
        m_Col = dst.GetCol();
        m_Flip = dst.GetFlip();
    }

    // Rebuilt rather than stored, so a destination whose parent has gone in
    // the meantime comes back empty and is caught as "not a place" by the
    // engine's own gate instead of further down.
    bool Where(PlayerBase player, out InventoryLocation dst)
    {
        if (m_Type == InventoryLocationType.GROUND)
            return GameInventory.SetGroundPosByOwner(player, m_Item, dst);
        if (!m_Parent || !m_Item)
            return false;
        if (m_Type == InventoryLocationType.HANDS)
            dst.SetHands(m_Parent, m_Item);
        else if (m_Type == InventoryLocationType.ATTACHMENT)
            dst.SetAttachment(m_Parent, m_Item, m_Slot);
        else
            dst.SetCargo(m_Parent, m_Item, 0, m_Row, m_Col, m_Flip);
        return true;
    }
}
