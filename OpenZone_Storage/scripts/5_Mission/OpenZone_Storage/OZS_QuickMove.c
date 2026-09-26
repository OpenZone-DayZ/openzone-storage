// ALT + LEFT CLICK: an item in the box goes to the player's inventory, an
// item of the player's goes into the open box (owner, 2026-09-26).
//
// THERE IS NO SUCH GESTURE IN VANILLA ON A KEYBOARD. The screen has one --
// UAUIFastTransferItem, the console's X -- but the keyboard preset binds it
// to nothing (bin.pbo: ps4X and x1X only), and the one modifier click a PC
// player has is Ctrl+LMB, which drops the item on the ground and is written
// straight into the click handlers rather than read off an input. So this
// is written the same way: at the top of every handler a click on an item
// can reach, ahead of vanilla's own cases, and only while Alt is down and a
// box is open. With no box open the test is one integer and the click goes
// on to vanilla untouched.
//
// Five handlers, because the panel keeps one per kind of icon: cargo icons
// (Icon), the player's worn slots (PlayerContainer), a container's own slots
// -- the box's weapon rack among them (ContainerWithCargoAndAttachments), a
// weapon's attachment rows (AttachmentCategoriesRow) and the hands
// (HandsContainer). WidgetEventHandler keeps ONE handler per widget, so a
// sixth registration on the slot icons would silently replace vanilla's.
#ifndef NO_GUI
class OZS_QuickMove
{
    // True when the click was this gesture and has been sent; false hands
    // it on to vanilla.
    static bool Try(int button, EntityAI item)
    {
        if (button != MouseState.LEFT || !item)
            return false;
        if (OZS_Mirrors.None())
            return false;
        if (!OZS_Mirrors.AltHeld())
            return false;
        return OZS_Mirrors.QuickMove(item);
    }

    // The item under a slot icon, the way every slot handler reads it.
    static EntityAI OfSlot(Widget w)
    {
        SlotsIcon icon;
        w.GetUserData(icon);
        if (!icon)
            return null;
        return icon.GetEntity();
    }

    // The item under a preview widget, the way the hands handler reads it.
    static EntityAI OfPreview(Widget w)
    {
        string name = w.GetName();
        name.Replace("PanelWidget", "Render");
        ItemPreviewWidget ipw = ItemPreviewWidget.Cast(w.FindAnyWidget(name));
        if (!ipw)
            return null;
        return ipw.GetItem();
    }
}

modded class Icon
{
    override void MouseClick(Widget w, int x, int y, int button)
    {
        if (!m_Lock && OZS_QuickMove.Try(button, m_Item))
            return;
        super.MouseClick(w, x, y, button);
    }

    // A LOOSE ITEM DROPPED ON AN ITEM IN THE BOX. The screen asks its own
    // swap test about the pair and that test does not pass a ground item, so
    // the drop arrives here with nothing chosen and vanilla would do nothing
    // (owner, 2026-09-26: "a swap between the ground and the box"). The
    // exchange the drop means is sent as what it is; see OZS_Mirrors.
    // GroundSwap. Every pair the screen DID choose something for -- two
    // stacks, a magazine on a rifle, an item into a bag in the box -- goes
    // on to vanilla, whose choice reaches this mod by its own hooks.
    override bool PerformCombination(EntityAI selectedEntity, EntityAI targetEntity, int combinationFlag, InventoryLocation ilSwapDst = null)
    {
        if (combinationFlag == InventoryCombinationFlags.NONE && OZS_Mirrors.GroundSwap(selectedEntity, targetEntity))
            return true;
        return super.PerformCombination(selectedEntity, targetEntity, combinationFlag, ilSwapDst);
    }
}

modded class PlayerContainer
{
    override void MouseClick(Widget w, int x, int y, int button)
    {
        if (OZS_QuickMove.Try(button, OZS_QuickMove.OfSlot(w)))
            return;
        super.MouseClick(w, x, y, button);
    }
}

modded class ContainerWithCargoAndAttachments
{
    override void MouseClick2(Widget w, int x, int y, int button)
    {
        if (OZS_QuickMove.Try(button, OZS_QuickMove.OfSlot(w)))
            return;
        super.MouseClick2(w, x, y, button);
    }
}

modded class AttachmentCategoriesRow
{
    override void MouseClick(Widget w, int x, int y, int button)
    {
        if (OZS_QuickMove.Try(button, OZS_QuickMove.OfSlot(w)))
            return;
        super.MouseClick(w, x, y, button);
    }
}

modded class HandsContainer
{
    override void MouseClick2(Widget w, int x, int y, int button)
    {
        if (OZS_QuickMove.Try(button, OZS_QuickMove.OfPreview(w)))
            return;
        super.MouseClick2(w, x, y, button);
    }
}
#endif
