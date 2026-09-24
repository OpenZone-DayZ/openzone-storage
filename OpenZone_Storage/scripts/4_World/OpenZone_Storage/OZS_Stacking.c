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
}
