// Stacking, when the stacks are in a box.
// Design: docs/specs/2026-09-24-storage-proxy-inventory-design.md §5, §10.3.
//
// The vanilla inventory merges two stacks with `entity.CombineItemsClient(other)`
// (icon.c:495, :531, :750). That call serialises THE ENTITIES THEMSELVES into
// `ScriptInputUserData` -- which is to say by network id -- and an item in a
// proxy has none. So it cannot work there, and the server is asked instead.
//
// AS EVERYWHERE ELSE, THIS DECIDES ONLY FOR ITEMS IN A BOX. With no box open
// the override is one integer test and a call to super; with a box open it
// compares hierarchy roots by pointer. Two stacks of bandages in the player's
// own pockets go to vanilla untouched.
//
// The merge is not instant even when it is ours: the engine does it on the
// authoritative container, with every mod's `CanBeCombined` in force, and the
// proxy is told the result. That is the trade §10.3 records -- a round trip
// for correctness that no script of ours could reproduce.
modded class ItemBase
{
    override void CombineItemsClient(EntityAI entity2, bool use_stack_max = true)
    {
        if (!OZS_Mirrors.None())
        {
            OZS_Mirror mine = OZS_Mirrors.Of(this);
            OZS_Mirror theirs = OZS_Mirrors.Of(entity2);
            if (mine && mine == theirs)
            {
                mine.DragCombine(this, entity2);
                return;
            }
            if (mine || theirs)
            {
                // One stack in the box and one outside. Crossing the boundary
                // is a move, and a move of a part of a stack is a split; both
                // are their own operations and neither is this one.
                return;
            }
        }
        super.CombineItemsClient(entity2, use_stack_max);
    }

    // A SPLIT INTO A BOX MOVES THE WHOLE STACK INSTEAD, FOR NOW.
    // `SplitItemUtils.TakeOrSplitToInventory` reaches for this when the target
    // will not take the whole quantity; the box's cargo always will, so this
    // fires mostly for slots with a cap. Splitting properly needs a quantity
    // on the wire and a new stack made on the authority -- until then, moving
    // the whole thing is the honest approximation, and far better than the
    // nothing that happened before this override existed.
    override void SplitIntoStackMaxClient(EntityAI destination_entity, int slot_id)
    {
        if (!OZS_Mirrors.None())
        {
            OZS_Mirror m = OZS_Mirrors.Of(destination_entity);
            if (!m)
                m = OZS_Mirrors.Of(this);
            if (m)
            {
                int lt = InventoryLocationType.CARGO;
                if (slot_id >= 0)
                    lt = InventoryLocationType.ATTACHMENT;
                m.DragTo(this, destination_entity, lt, slot_id, -1, -1);
                return;
            }
        }
        super.SplitIntoStackMaxClient(destination_entity, slot_id);
    }

    override void SplitItemToInventoryLocation(notnull InventoryLocation dst)
    {
        if (!OZS_Mirrors.None())
        {
            OZS_Mirror m = OZS_Mirrors.At(dst);
            if (!m)
                m = OZS_Mirrors.Of(this);
            if (m)
            {
                InventoryLocation src = new InventoryLocation();
                if (GetInventory().GetCurrentInventoryLocation(src))
                    m.Drag(src, dst);
                return;
            }
        }
        super.SplitItemToInventoryLocation(dst);
    }
}
