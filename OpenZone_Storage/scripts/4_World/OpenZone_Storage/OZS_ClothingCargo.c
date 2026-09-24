// Clothing hung in the stash keeps working pockets.
//
// THE RULE THIS WIDENS. Vanilla clothing refuses cargo unless it is lying
// loose or worn by a person -- literally:
//
//     // 4_world/entities/itembase/clothing_base.c:66
//     bool CanReceiveItemIntoCargoClothingConditions(EntityAI item)
//     {
//         EntityAI hierarchyParent = GetHierarchyParent();
//         return !hierarchyParent || hierarchyParent.IsMan() || SmershException(hierarchyParent);
//     }
//
// A container is not a Man, so a jacket attached to one can be emptied but
// never filled -- taking out is not gated by this method, putting in is. That
// asymmetry is exactly what the owner reported on 2026-09-23: "забрать могу,
// положить нет".
//
// This is NOT the AreChildrenAccessible() depth rule (entityai.c:1662). That
// one is about ancestors sitting in CARGO and it passes here: an attachment
// costs one step of a budget of two. Two separate gates, and the clothing one
// is the one that was shutting the pocket.
//
// THE PRECEDENT. Vanilla makes this exception for its own personal stash in
// the sibling method, same file, line 93:
//
//     if (parent && parent.IsInherited(UndergroundStash))
//         return true;
//
// So a container that is meant to hold a kit is expected to say so. Ours says
// so here, for the personal stash and nothing else: every other parent, and
// every case that is not a stash, falls through to vanilla untouched.
//
// Deliberately NOT extended to OZ_StorageBox: the boxes are published and a
// player-visible rule change there is the owner's call, not a side effect of
// this feature.
modded class Clothing
{
    override bool CanReceiveItemIntoCargoClothingConditions(EntityAI item)
    {
        if (OZS_HangingInStash())
            return true;
        return super.CanReceiveItemIntoCargoClothingConditions(item);
    }

    // The twin used when the server loads a saved tree back in. Without it the
    // engine would accept a filled jacket into the stash and then drop its
    // contents on the next restart, which is the worst of both answers.
    override bool CanLoadItemIntoCargoClothingConditions(EntityAI item)
    {
        if (OZS_HangingInStash())
            return true;
        return super.CanLoadItemIntoCargoClothingConditions(item);
    }

    // ANYWHERE above this garment, at any depth. A vest hangs in the stash's
    // Vest slot, its pouches hang on the vest, and something could hang on
    // those in turn: the pouches' own parent is the vest, which is no more a
    // Man than the stash is, so a check of the DIRECT parent alone would
    // allow the vest and refuse the pouches. Vanilla's SmershException walks
    // exactly one step up for its own version of this shape; this asks the
    // engine for the top of the chain instead of counting steps, so there is
    // no depth here to get wrong and none to raise later.
    //
    // The engine's own limit still applies on top and is not ours to widen:
    // AreChildrenAccessible() (entityai.c:1662) spends a budget of
    // INVENTORY_MAX_REACHABLE_DEPTH_ATT = 2 attachment steps, so
    // stash -> garment -> pouch is reachable and a fourth level is not -- on
    // a player's own body exactly as here. Anything in CARGO cuts the chain
    // outright, at any depth.
    protected bool OZS_HangingInStash()
    {
        return OZ_PersonalStash.Cast(GetHierarchyRoot()) != null;
    }
}
