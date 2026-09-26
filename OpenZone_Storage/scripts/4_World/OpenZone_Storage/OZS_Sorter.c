// The sorted layout of a box: the cargo items ordered by display name, then
// class, then quantity (fullest first), packed row by row into the grid.
// Used by the close job of a sort request: the records get the new cells,
// and the reopen materialises them in that order. The weapon slots are not
// touched. A native string sort does the ordering (Enforce has no
// comparator sort), so the key is one string per item.
class OZS_Sorter
{
    // Fills rows/cols aligned with `roots`: -1 for anything that keeps its
    // place (attachments, and cargo that found no cell in the new layout --
    // the open job then takes any free cell). Returns how many were placed.
    static int Plan(OZ_StorageBox box, array<EntityAI> roots, array<int> rows, array<int> cols)
    {
        rows.Clear();
        cols.Clear();
        for (int i = 0; i < roots.Count(); i++)
        {
            rows.Insert(-1);
            cols.Insert(-1);
        }
        CargoBase cargo = box.GetInventory().GetCargo();
        if (!cargo)
            return 0;
        int width = cargo.GetWidth();
        int height = cargo.GetHeight();
        if (width <= 0 || height <= 0)
            return 0;

        // One key per cargo root: name | class | 999999 - quantity | index.
        array<string> keys = new array<string>();
        for (int r = 0; r < roots.Count(); r++)
        {
            EntityAI e = roots.Get(r);
            if (!e)
                continue;
            InventoryLocation loc = new InventoryLocation();
            if (!e.GetInventory() || !e.GetInventory().GetCurrentInventoryLocation(loc))
                continue;
            if (loc.GetType() != InventoryLocationType.CARGO)
                continue;
            keys.Insert(KeyOf(e, r));
        }
        keys.Sort();

        // Greedy packing: each item takes the first free block scanning the
        // grid row by row; 1x1 items advance a cursor, bigger ones scan.
        array<bool> taken = new array<bool>();
        for (int c = 0; c < width * height; c++)
            taken.Insert(false);
        int cursor = 0;
        int placed = 0;
        for (int k = 0; k < keys.Count(); k++)
        {
            int index = IndexOf(keys.Get(k));
            if (index < 0 || index >= roots.Count())
                continue;
            EntityAI item = roots.Get(index);
            if (!item)
                continue;
            // THE SHAPE IT ACTUALLY HAS, TURN AND ALL.
            //
            // `GetInventoryItemSize` answers the CONFIG's size and knows
            // nothing about a turned item: a rag lying across three cells came
            // back as one wide and three tall, so the planner reserved a
            // narrow column for it and the restore then stood it sideways
            // across two neighbours. The player saw a cell that looked empty,
            // refused every drop, and the refusal blamed the exchange
            // (owner, 2026-09-26).
            //
            // OZS_Ops.SizeOf reads the cargo's own numbers and swaps them when
            // the item is flipped, which is the same shape every other check
            // in this mod measures against.
            int w = 1;
            int h = 1;
            if (!OZS_Ops.SizeOf(item, w, h))
                GetGame().GetInventoryItemSize(InventoryItem.Cast(item), w, h);
            if (w < 1)
                w = 1;
            if (h < 1)
                h = 1;
            int cell = FindBlock(taken, width, height, w, h, cursor);
            if (cell < 0)
                continue;
            int row = cell / width;
            int col = cell % width;
            Mark(taken, width, row, col, w, h);
            rows.Set(index, row);
            cols.Set(index, col);
            placed++;
            if (w == 1 && h == 1)
            {
                cursor = cell + 1;
            }
        }
        return placed;
    }

    protected static string KeyOf(EntityAI e, int index)
    {
        string name = OZS_Case.Lower(e.GetDisplayName());
        float q = 0;
        ItemBase item = ItemBase.Cast(e);
        if (item && item.HasQuantity())
            q = item.GetQuantity();
        int qi = 999999 - q;
        if (qi < 0)
            qi = 0;
        return name + "|" + e.GetType() + "|" + Pad(qi, 6) + "|" + Pad(index, 6);
    }

    protected static int IndexOf(string key)
    {
        int at = key.LastIndexOf("|");
        if (at < 0)
            return -1;
        return key.Substring(at + 1, key.Length() - at - 1).ToInt();
    }

    protected static string Pad(int v, int digits)
    {
        string s = v.ToString();
        while (s.Length() < digits)
            s = "0" + s;
        return s;
    }

    // First cell index whose w x h block is free, scanning from `from`.
    protected static int FindBlock(array<bool> taken, int width, int height, int w, int h, int from)
    {
        if (w > width || h > height)
            return -1;
        int cells = width * height;
        for (int cell = from; cell < cells; cell++)
        {
            int row = cell / width;
            int col = cell % width;
            if (col + w > width || row + h > height)
                continue;
            if (BlockFree(taken, width, row, col, w, h))
                return cell;
        }
        // Bigger items may fit before the 1x1 cursor.
        for (int back = 0; back < from; back++)
        {
            int brow = back / width;
            int bcol = back % width;
            if (bcol + w > width || brow + h > height)
                continue;
            if (BlockFree(taken, width, brow, bcol, w, h))
                return back;
        }
        return -1;
    }

    protected static bool BlockFree(array<bool> taken, int width, int row, int col, int w, int h)
    {
        for (int y = 0; y < h; y++)
        {
            for (int x = 0; x < w; x++)
            {
                if (taken.Get((row + y) * width + col + x))
                    return false;
            }
        }
        return true;
    }

    protected static void Mark(array<bool> taken, int width, int row, int col, int w, int h)
    {
        for (int y = 0; y < h; y++)
        {
            for (int x = 0; x < w; x++)
                taken.Set((row + y) * width + col + x, true);
        }
    }
}
