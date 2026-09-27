// A test bag that takes only bandages into its cargo (owner, 2026-09-27):
// what the boxes do with a container whose script refuses some items --
// the mirror's drag, the server's boundary and the record's restore.
//
// The vanilla pattern (Pot, PlateCarrierPouches): the RECEIVE gate is
// strict and the LOAD gate is left as it is, because the engine loses what
// the load gate refuses (ItemBase.CanReceiveItemIntoCargo, the comment of
// 15.06 about items lost after a load from storage).
class OZ_ProbeBandageBag : TaloonBag_Blue
{
    override bool CanReceiveItemIntoCargo(EntityAI item)
    {
        if (!super.CanReceiveItemIntoCargo(item))
            return false;
        return item.IsKindOf("BandageDressing");
    }
}
