// DROPPED ONTO AN OCCUPIED SLOT: the exchange the player meant, sent even
// when the screen's own test would not offer it (owner, 2026-09-27: "it must
// be possible to trade the weapon in the hands for the one on the hook, the
// grid against the hooks, the player's inventory and worn gear against the
// box's slots").
//
// Every slot drop of the vanilla panel goes through one of two handlers --
// `ContainerWithCargoAndAttachments.TakeAsAttachment` for a container's
// own slots (the box's rack and clothing hooks among them) and
// `PlayerContainer.OnDropReceivedFromGhostArea` for the worn gear -- and
// both offer a swap for an occupied slot only when
// `GameInventory.CanSwapEntitiesEx` says yes. That native answers as it
// pleases about an item in a container the engine was never told about,
// and a refusal there is silent: the drop does nothing, or the item lands
// on some other hook. When one end of the pair is in a box, the pair is
// sent to the server as the exchange it is, and the server decides.
//
// Vanilla's own meanings for the drop come first, in vanilla's order, so
// nothing that worked is taken away: two stacks combine, a pair the native
// passes swaps by vanilla's own call (which this mod's hooks then carry), a
// scope dropped on the hung rifle attaches to it, a can dropped on the hung
// bag goes inside it. Only a drop none of those claim, of an item that could
// hang on that very hook, is the exchange.
#ifndef NO_GUI
class OZS_SlotSwap
{
    // True when the drop was an exchange with a box's item and has been sent;
    // false hands it on to vanilla.
    static bool Try(EntityAI item, Widget receiver)
    {
        if (OZS_Mirrors.None() || !item || !receiver)
            return false;
        SlotsIcon icon;
        receiver.GetUserData(icon);
        if (!icon || icon.IsReserved())
            return false;
        EntityAI sitter = icon.GetEntity();
        if (!sitter || sitter == item)
            return false;
        OZS_Mirror a = OZS_Mirrors.Of(item);
        OZS_Mirror b = OZS_Mirrors.Of(sitter);
        if (!a && !b)
            return false;
        PlayerBase player = PlayerBase.Cast(GetGame().GetPlayer());
        if (!player || !player.CanManipulateInventory())
            return false;
        if (!item.GetInventory().CanRemoveEntity() || !sitter.GetInventory().CanRemoveEntity())
            return false;
        ItemBase ib = ItemBase.Cast(item);
        ItemBase sb = ItemBase.Cast(sitter);
        if (ib && sb && sb.CanBeCombined(ib))
            return false;
        if (GameInventory.CanSwapEntitiesEx(sitter, item))
            return false;
        if (sitter.GetInventory().CanAddAttachment(item))
            return false;
        bool inside = sitter.GetInventory().CanAddEntityInCargo(item, item.GetInventory().GetFlipCargo());
        if (inside)
            inside = !sitter.GetInventory().HasEntityInCargo(item);
        if (inside)
            return false;
        // An item that could never hang on this hook is not asking to trade
        // with the one that does: a can dropped on a hung vest goes on to
        // vanilla, which puts it where it can.
        if (!item.GetInventory().HasInventorySlot(icon.GetSlotID()))
            return false;
        if (!OZS_Mirrors.SwapAnyway(item, sitter))
            return false;
        ItemManager.GetInstance().HideDropzones();
        ItemManager.GetInstance().SetIsDragging(false);
        return true;
    }
}

modded class ContainerWithCargoAndAttachments
{
    override void TakeAsAttachment(Widget w, Widget receiver)
    {
        if (OZS_SlotSwap.Try(GetItemPreviewItem(w), receiver))
            return;
        super.TakeAsAttachment(w, receiver);
    }
}

modded class PlayerContainer
{
    override void OnDropReceivedFromGhostArea(Widget w, int x, int y, Widget receiver)
    {
        EntityAI dragged = null;
        ItemPreviewWidget ipw = ItemPreviewWidget.Cast(GetItemPreviewWidget(w));
        if (ipw)
            dragged = ipw.GetItem();
        if (OZS_SlotSwap.Try(dragged, receiver))
            return;
        super.OnDropReceivedFromGhostArea(w, x, y, receiver);
    }
}
#endif
