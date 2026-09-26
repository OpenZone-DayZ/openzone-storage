// The proxy wire: what one item looks like on its way from the authoritative
// container to a player's proxy, and back as an operation.
// Design: docs/specs/2026-09-24-storage-proxy-inventory-design.md §2, §5.
//
// Deliberately flat ints and one string. A Param would be shorter to write and
// a good deal harder to read on the wire, and this travels a thousand times per
// open; ScriptRPC takes primitives directly, so primitives is what it gets.
//
// WHAT IS NOT HERE, ON PURPOSE:
//   · the blob. It buys a correct field nobody reads on a proxy and pays with
//     another mod's OnStoreLoad running on a client, in a context it was never
//     written for (design §10.2);
//   · the network id of anything. Items of an unannounced container have none,
//     which is why every row carries a HANDLE instead.
class OZS_Row
{
    int handle;   // the authority's handle; the only name an operation may use
    int parent;   // the handle of the item this hangs in, 0 = the box itself
    int lt;       // InventoryLocationType: CARGO, ATTACHMENT
    int slot;     // attachment slot id; slot ids CAN be negative
    int row;      // cargo cell
    int col;
    int flip;     // 1 when the item lies across its cell
    int qty;      // rounded quantity, -1 when the item has no quantity
    int qtyMax;   // its maximum, -1 likewise; the screen draws the bar from these
    // ROUNDS IN A MAGAZINE, WHICH IS NOT THE QUANTITY, -1 when not a magazine.
    //
    // The inventory screen draws a magazine's number from `GetAmmoCount()`
    // and nothing else (quantityconversions.c:12-19), so an ammo pile whose
    // rounds never travel draws whatever a FRESH pile of its class holds. The
    // owner had piles of one and two rounds in the box and the screen showed
    // them as 20, 25 and 50 (2026-09-25).
    int ammo;
    int health;   // 0..100, for the damage tint
    string cls;

    void Set(int h, int p, int locType, int slotId, int r, int c, int f, string type)
    {
        handle = h;
        parent = p;
        lt = locType;
        slot = slotId;
        row = r;
        col = c;
        flip = f;
        cls = type;
        qty = -1;
        qtyMax = -1;
        ammo = -1;
        health = 100;
    }

    string Where()
    {
        if (lt == InventoryLocationType.ATTACHMENT)
            return "slot " + slot.ToString();
        // THE WAY ROUND BELONGS TO THE PLACE. A cell alone does not say what
        // rectangle an item covers, and two logs that both read "6,4" while
        // one of them meant the turned can was the whole reason a build could
        // disagree with the authority unseen (2026-09-25).
        if (flip == 1)
            return row.ToString() + "," + col.ToString() + " turned";
        return row.ToString() + "," + col.ToString();
    }
}

class OZS_Wire
{
    // Both sides write and read in this order and nowhere else, so a change
    // here is a change to both halves at once.
    static void WriteRow(ParamsWriteContext ctx, OZS_Row r)
    {
        ctx.Write(r.handle);
        ctx.Write(r.parent);
        ctx.Write(r.lt);
        ctx.Write(r.slot);
        ctx.Write(r.row);
        ctx.Write(r.col);
        ctx.Write(r.flip);
        ctx.Write(r.qty);
        ctx.Write(r.qtyMax);
        ctx.Write(r.ammo);
        ctx.Write(r.health);
        ctx.Write(r.cls);
    }

    static bool ReadRow(ParamsReadContext ctx, OZS_Row r)
    {
        if (!ctx.Read(r.handle)) return false;
        if (!ctx.Read(r.parent)) return false;
        if (!ctx.Read(r.lt)) return false;
        if (!ctx.Read(r.slot)) return false;
        if (!ctx.Read(r.row)) return false;
        if (!ctx.Read(r.col)) return false;
        if (!ctx.Read(r.flip)) return false;
        if (!ctx.Read(r.qty)) return false;
        if (!ctx.Read(r.qtyMax)) return false;
        if (!ctx.Read(r.ammo)) return false;
        if (!ctx.Read(r.health)) return false;
        if (!ctx.Read(r.cls)) return false;
        return true;
    }

    // An operation, client -> server.
    //
    // FOUR NAMES AND A PLACE. Two of the names are HANDLES, which is how items
    // inside a box are named, and two are the halves of a NETWORK ID, which is
    // how anything outside it is named -- the player's own backpack, the item
    // in their hands. Which fields carry what depends on the operation:
    //
    //   MOVE     handle = the item,  other = the container inside the box (0 = the box)
    //   OUT      handle = the item,  net   = WHERE IN THE PLAYER'S INVENTORY it goes
    //   IN       net    = the item,  other = the container inside the box (0 = the box)
    //   COMBINE  handle, other = the two stacks
    //   SWAP     handle, other = the two items
    //
    // `lt`, `slot`, `row`, `col`, `flip` are the place inside whichever
    // container the operation names. `version` is the version of the box the
    // client believed in; the server refuses a stale one and resends (§8.2).
    static void WriteOp(ParamsWriteContext ctx, string id, int op, int handle, int other, int netLow, int netHigh, int lt, int slot, int row, int col, int flip, int version)
    {
        ctx.Write(id);
        ctx.Write(op);
        ctx.Write(handle);
        ctx.Write(other);
        ctx.Write(netLow);
        ctx.Write(netHigh);
        ctx.Write(lt);
        ctx.Write(slot);
        ctx.Write(row);
        ctx.Write(col);
        ctx.Write(flip);
        ctx.Write(version);
    }

    static bool ReadOp(ParamsReadContext ctx, out string id, out int op, out int handle, out int other, out int netLow, out int netHigh, out int lt, out int slot, out int row, out int col, out int flip, out int version)
    {
        if (!ctx.Read(id)) return false;
        if (!ctx.Read(op)) return false;
        if (!ctx.Read(handle)) return false;
        if (!ctx.Read(other)) return false;
        if (!ctx.Read(netLow)) return false;
        if (!ctx.Read(netHigh)) return false;
        if (!ctx.Read(lt)) return false;
        if (!ctx.Read(slot)) return false;
        if (!ctx.Read(row)) return false;
        if (!ctx.Read(col)) return false;
        if (!ctx.Read(flip)) return false;
        if (!ctx.Read(version)) return false;
        return true;
    }
}
