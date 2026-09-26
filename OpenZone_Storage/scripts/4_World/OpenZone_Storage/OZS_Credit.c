// WHAT A STACK IN THE BOX GAVE TO A STACK OUTSIDE IT, WHILE THE RECORD IS
// BEING TOLD. The outbound half of cross-boundary stacking (OZS_Boundary.
// StackOut): the rounds -- or the quantity -- leave the box's stack first,
// the letter that says so goes to the bridge, and only then does the
// player's stack receive them. Between the two this object holds them.
//
// It is the hand-over of a take-out (OZS_Handover) for contents instead of an
// entity, and it exists for the same reason: the record must let go BEFORE
// the player's save can hold what it let go of, or a server that dies in
// between duplicates it (design section 7). With WaitForRecord on the
// credit waits for the bridge's answer; with it off the credit is given in
// the same frame as the letter, exactly as a take-out is handed over.
//
// TAKING IS UNDONE BY RESTORING, NOT BY DELETING. The giver is left standing
// in the box -- at nothing, if it gave everything -- until the answer comes
// back, so a refused turn can put the contents straight back where the
// record still says they are (see OZS_Session.OnCommitRefused). The empty
// stack is removed only once the credit has been given.
//
// What crosses is what vanilla's own combine would move, in vanilla's own
// words: cartridges one by one with their damage (Magazine.CombineItems,
// magazine.c:292), or the quantity the receiver has room for with the health
// averaged in (ItemBase.CombineItems, itembase.c:2308).
class OZS_Credit
{
    string m_Uid;
    int m_Handle;
    // The stack in the box that gave, and the player's stack that receives.
    // The receiver is the player's real, announced entity; if the player is
    // gone by the time the credit is given, so is it, and the contents go
    // back into the box (OZS_Boundary.Credited).
    ItemBase m_Giver;
    ItemBase m_Taker;
    // Whether the giver was a root of the record and gave everything: then
    // its letter was a drop, and writing it back is an addition.
    bool m_WasItself;
    bool m_Gone;
    string m_Type;
    // Rounds, each with its damage and its class -- or quantity.
    bool m_Rounds;
    ref array<float> m_Damage;
    ref array<string> m_Cartridge;
    float m_Quantity;
    // The giver's health at the moment of taking, for the average the
    // receiver's health becomes. Absolute for rounds (as Magazine has it),
    // 0..1 for quantity (as ItemBase has it).
    float m_GiverHealth;

    void OZS_Credit(string uid, int handle, ItemBase giver, ItemBase taker, bool wasItself)
    {
        m_Uid = uid;
        m_Handle = handle;
        m_Giver = giver;
        m_Taker = taker;
        m_WasItself = wasItself;
        m_Type = giver.GetType();
        m_Damage = new array<float>();
        m_Cartridge = new array<string>();
    }

    // Takes from the giver what the taker has room for. Null when nothing
    // would move -- the taker is full, or the two are not the same kind of
    // stack, which CanBeCombined has already refused.
    static OZS_Credit Take(string uid, int handle, ItemBase giver, ItemBase taker, bool wasItself)
    {
        Magazine from = Magazine.Cast(giver);
        Magazine into = Magazine.Cast(taker);
        if (from && into)
        {
            int room = into.GetAmmoMax() - into.GetAmmoCount();
            int n = from.GetAmmoCount();
            if (room < n)
                n = room;
            if (n <= 0)
                return null;
            OZS_Credit rounds = new OZS_Credit(uid, handle, giver, taker, wasItself);
            rounds.m_Rounds = true;
            rounds.m_GiverHealth = from.GetHealth();
            for (int i = 0; i < n; i++)
            {
                float damage;
                string cartridge;
                if (!from.ServerAcquireCartridge(damage, cartridge))
                    break;
                rounds.m_Damage.Insert(damage);
                rounds.m_Cartridge.Insert(cartridge);
            }
            if (rounds.m_Damage.Count() == 0)
                return null;
            from.SetSynchDirty();
            return rounds;
        }
        if (from || into)
            return null;
        float q = taker.ComputeQuantityUsedEx(giver, true);
        if (q <= 0)
            return null;
        OZS_Credit quantity = new OZS_Credit(uid, handle, giver, taker, wasItself);
        quantity.m_Quantity = q;
        quantity.m_GiverHealth = giver.GetHealth01("", "");
        // NEITHER DESTROYED BY CONFIG NOR BY FORCE: an emptied giver stays,
        // at nothing, for Restore or for the deletion that follows the
        // credit. (Vanilla's combine lets the config decide and deletes a
        // rag stack on the spot; here the stack must outlive the wire.)
        giver.AddQuantity(-q, false, false);
        return quantity;
    }

    // The receiver gets what was taken. `m_Giver` is passed to the
    // receiver's OnCombine as vanilla does -- rag, bandage and sewing kit
    // read the other stack's cleanness there -- and it is still a live
    // entity at this point on every path (it is deleted after, never
    // before).
    void Give()
    {
        if (!m_Taker)
            return;
        Magazine into = Magazine.Cast(m_Taker);
        if (m_Rounds)
        {
            if (!into)
                return;
            int had = into.GetAmmoCount();
            int stored = 0;
            for (int i = 0; i < m_Damage.Count(); i++)
            {
                if (into.ServerStoreCartridge(m_Damage.Get(i), m_Cartridge.Get(i)))
                    stored++;
            }
            if (into.GetAmmoCount() > 0)
            {
                float health = (had * into.GetHealth() + stored * m_GiverHealth) / into.GetAmmoCount();
                into.SetHealth("", "", health);
            }
            if (m_Giver)
                into.OnCombine(m_Giver);
            into.SetSynchDirty();
            return;
        }
        float hp1 = m_Taker.GetHealth01("", "");
        float total = m_Taker.GetQuantity() + m_Quantity;
        if (total > 0)
        {
            float hpResult = ((hp1 * m_Taker.GetQuantity()) + (m_GiverHealth * m_Quantity)) / total;
            hpResult = hpResult * m_Taker.GetMaxHealth();
            m_Taker.SetHealth("", "Health", hpResult);
        }
        m_Taker.AddQuantity(m_Quantity);
        if (m_Giver)
            m_Taker.OnCombine(m_Giver);
    }

    // What was taken goes back into the giver: the turn was refused, or
    // there is nobody left to give it to.
    void Restore()
    {
        if (!m_Giver)
            return;
        Magazine from = Magazine.Cast(m_Giver);
        if (m_Rounds)
        {
            if (!from)
                return;
            for (int i = 0; i < m_Damage.Count(); i++)
                from.ServerStoreCartridge(m_Damage.Get(i), m_Cartridge.Get(i));
            from.SetSynchDirty();
            return;
        }
        m_Giver.AddQuantity(m_Quantity, false, false);
    }

    // How much crossed, for the log: rounds or quantity.
    float Amount()
    {
        if (m_Rounds)
            return m_Damage.Count();
        return m_Quantity;
    }
}
