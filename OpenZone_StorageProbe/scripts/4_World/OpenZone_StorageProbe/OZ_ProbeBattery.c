// One battery of inventory moves, run identically on a CLIENT MIRROR and on a
// SERVER box, so the two answers can be compared line by line.
//
// The question it exists for (2026-09-24): in the design where the server
// holds the authoritative box and one client holds a mirror of it, both are
// the same engine with the same classes -- so they SHOULD judge a move the
// same way. "Should" is not a measurement, and a mirror that accepts what the
// server refuses is a snap-back on every drag the player makes.
//
// Both sides are called with a container holding at least three items in the
// first cells. Every case is reported the same way, so a diff of the two
// reports is the answer.
//
// STAND ONLY.
class OZ_ProbeBattery
{
    static void Run(EntityAI box, out array<string> report)
    {
        if (!box || !box.GetInventory() || !box.GetInventory().GetCargo())
        {
            report.Insert("no container");
            return;
        }
        CargoBase cargo = box.GetInventory().GetCargo();
        if (cargo.GetItemCount() < 2)
        {
            report.Insert("need at least 2 items in the box, have " + cargo.GetItemCount());
            return;
        }
        EntityAI first = EntityAI.Cast(cargo.GetItem(0));
        EntityAI second = EntityAI.Cast(cargo.GetItem(1));
        report.Insert("box " + box.GetType() + " holds " + cargo.GetItemCount() + ", netid " + box.GetNetworkIDString());

        // Each case: a destination cell and what it is meant to exercise. The
        // numbers are deliberately inside and outside a 10-wide grid.
        Case(box, first, 4, 5, "a free cell well inside the grid", report);
        Case(box, first, 0, 0, "back to the cell it came from", report);
        Case(box, first, 9, 9, "the far corner of a 10x10 grid", report);
        Case(box, first, 40, 0, "a row far outside the grid", report);
        Case(box, first, 0, 40, "a column far outside the grid", report);
        Case(box, first, -1, 0, "a negative row", report);
        // The one case where two items meet: onto the cell the second item
        // occupies. Vanilla may swap, refuse, or combine; both sides must do
        // the same thing.
        InventoryLocation where = new InventoryLocation();
        second.GetInventory().GetCurrentInventoryLocation(where);
        Case(box, first, where.GetRow(), where.GetCol(), "onto the cell of another item", report);
    }

    protected static void Case(EntityAI box, EntityAI item, int row, int col, string what, out array<string> report)
    {
        InventoryLocation src = new InventoryLocation();
        item.GetInventory().GetCurrentInventoryLocation(src);
        int fromRow = src.GetRow();
        int fromCol = src.GetCol();
        InventoryLocation dst = new InventoryLocation();
        dst.SetCargo(box, item, 0, row, col, false);
        bool ok = box.GetInventory().TakeToDst(InventoryMode.LOCAL, src, dst);
        InventoryLocation after = new InventoryLocation();
        item.GetInventory().GetCurrentInventoryLocation(after);
        string line = "[" + row + "," + col + "] " + ok.ToString();
        line = line + " from " + fromRow + "," + fromCol + " -> " + after.GetRow() + "," + after.GetCol();
        line = line + "  (" + what + ")";
        report.Insert(line);
    }
}
