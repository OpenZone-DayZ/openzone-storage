// Writing one turn to SQL, as it happens.
// Design: docs/specs/2026-09-24-storage-proxy-inventory-design.md §7.
//
// WHY PER OPERATION AND NOT AT CLOSE. In this scheme the authoritative
// container is not saved by the engine, so nothing but SQL remembers the box.
// A commit at close would make a crash cost the whole session; a commit per
// operation costs the one turn that did not reach the base.
//
// THE UNIT IS A ROOT, NOT THE BOX. A record is a list of roots, each a blob of
// its whole subtree. Rewriting the box after every drag would be 0.17 ms per
// entity -- a fifth of a second on a full box. Rewriting the one root that
// changed is a few dozen bytes. A root is named by its POSITION in the record;
// the box keeps that order from the open (OZS_RootOrder) and the bridge
// numbers the rows of a version the same way, so neither side has to trust an
// index the other invented. A dropped root leaves the list on both sides and a
// new one joins the end: that is the whole agreement.
//
// THE VERSION IS NOT CUT PER TURN. The bridge forks the current version once,
// on the first change of a session, and mutates it after that. History stays
// readable -- one version per session rather than one per drag (§7).
class OZS_Commit
{
    protected static int s_Serial;

    static int NextSerial()
    {
        s_Serial++;
        if (s_Serial > 999999)
            s_Serial = 1;
        return s_Serial;
    }

    // The top-level item `e` hangs under INSIDE THE BOX, or null when `e` is
    // not in the box at all.
    //
    // THE NULL IS THE WHOLE POINT. This used to walk up and return whatever it
    // reached, which for an item still in a player's inventory is THE PLAYER.
    // The caller then wrote the player into the box's record as a root, with
    // their clothes and everything in them -- seven entities a time, and the
    // next open would have materialised copies of somebody's gear inside the
    // box. Measured against the owner 2026-09-24: two drags into the box grew
    // the record from 10 entities to 24 while the box stayed at 10.
    static EntityAI TopOf(OZS_Session s, EntityAI e)
    {
        if (!s || !s.m_Auth || !e)
            return null;
        EntityAI up = e;
        while (up && up.GetHierarchyParent() && up.GetHierarchyParent() != s.m_Auth)
            up = up.GetHierarchyParent();
        if (!up || up.GetHierarchyParent() != s.m_Auth)
            return null;
        return up;
    }

    // Which root of the record `e` belongs to. MUST BE TAKEN BEFORE A MOVE
    // when the move can change it: afterwards the entity belongs somewhere
    // else and the old root is no longer reachable from it.
    static int RootOf(OZS_Session s, EntityAI e)
    {
        EntityAI top = TopOf(s, e);
        if (!top)
            return -1;
        return s.m_Auth.OZS_RootPosition(top);
    }

    // ---- what the operations call ----------------------------------------

    // An item moved inside the box. `wasRoot` is the position it belonged to
    // before the move; pass -2 when the move cannot have changed it.
    static void Moved(OZS_Session s, EntityAI e, int wasRoot = -2)
    {
        if (!Ready(s))
            return;
        EntityAI top = TopOf(s, e);
        if (!top)
        {
            string gone = "nothing";
            if (e)
                gone = e.GetType();
            OZ_Log.Error("storage: proxy: box " + s.m_Id + ": " + gone + " moved but is no longer in the box; nothing is written");
            return;
        }
        int now = s.m_Auth.OZS_RootPosition(top);
        OZS_Letter letter = s.Letter();
        if (wasRoot >= 0 && wasRoot != now)
            letter.Rewrite(wasRoot);
        if (now >= 0)
        {
            letter.Rewrite(now);
        }
        else
        {
            // A MOVE THAT ADDS A ROOT IS ALMOST ALWAYS A DUPLICATE. It means
            // the item's top-level parent is not in the box's record order --
            // and if the record already has it under another position, this
            // writes a second copy of it. Loud, with everything needed to see
            // which item and which box.
            string what = "nothing";
            if (top)
                what = top.GetType();
            OZ_Log.Error("storage: proxy: box " + s.m_Id + ": " + what + " moved but is not in the record's order (" + s.m_Auth.OZS_RootOrder().Count().ToString() + " root(s) known); it is being ADDED, which duplicates it if the record already had it");
            letter.Add(top);
        }
        letter.Post();
    }

    static void Quantity(OZS_Session s, EntityAI e)
    {
        Moved(s, e, -2);
    }

    // TWO ITEMS MOVED IN ONE TURN, ONE LETTER. A swap used to post one letter
    // per item, and the core's client posts every call at once, so the two
    // were concurrent requests the bridge applied in arrival order -- each
    // numbered against a different picture of the record (review 2026-09-26,
    // B5). Neither item changes its root in a swap inside the box, so both
    // are rewrites of where they stand now.
    static void MovedPair(OZS_Session s, EntityAI a, EntityAI b)
    {
        if (!Ready(s))
            return;
        OZS_Letter letter = s.Letter();
        Standing(s, letter, a);
        Standing(s, letter, b);
        letter.Post();
    }

    // The root `e` stands in now, into the letter as a rewrite -- or, loudly,
    // as an addition when the order does not know it (see Moved).
    protected static void Standing(OZS_Session s, OZS_Letter letter, EntityAI e)
    {
        EntityAI top = TopOf(s, e);
        if (!top)
        {
            string gone = "nothing";
            if (e)
                gone = e.GetType();
            OZ_Log.Error("storage: proxy: box " + s.m_Id + ": " + gone + " moved but is no longer in the box; nothing is written for it");
            return;
        }
        int now = s.m_Auth.OZS_RootPosition(top);
        if (now >= 0)
        {
            letter.Rewrite(now);
            return;
        }
        OZ_Log.Error("storage: proxy: box " + s.m_Id + ": " + top.GetType() + " moved but is not in the record's order (" + s.m_Auth.OZS_RootOrder().Count().ToString() + " root(s) known); it is being ADDED, which duplicates it if the record already had it");
        letter.Add(top);
    }

    // TWO STACKS BECAME ONE, IN ONE LETTER: the emptied stack's root dropped
    // (rewritten, when it was nested in a container), the receiving stack's
    // root rewritten, together. Written as two letters they were two
    // concurrent requests, and a pair applied in the wrong order rewrote the
    // wrong root whenever the neighbour was of the same class -- which is
    // exactly two piles of one ammunition, and which the identity check
    // cannot tell apart (review 2026-09-26, B5). `fromRoot`, `fromWasRoot`
    // and `fromType` are taken before the merge, like Left's; `fromGone`
    // says whether the emptied stack was deleted.
    static void Combined(OZS_Session s, EntityAI into, int fromRoot, bool fromWasRoot, string fromType, bool fromGone)
    {
        if (!Ready(s))
            return;
        OZS_Letter letter = s.Letter();
        if (fromGone && fromWasRoot)
            letter.Drop(fromRoot, fromType);
        else
            letter.Rewrite(fromRoot);
        Standing(s, letter, into);
        letter.Post();
    }

    // A whole item arrived from outside the box.
    static void Added(OZS_Session s, EntityAI e)
    {
        if (!Ready(s))
            return;
        EntityAI top = TopOf(s, e);
        if (!top)
        {
            string what = "nothing";
            if (e)
                what = e.GetType();
            OZ_Log.Error("storage: proxy: box " + s.m_Id + " was told " + what + " arrived, but it is not in the box; nothing is written");
            return;
        }
        OZS_Letter letter = s.Letter();
        letter.Add(top);
        letter.Post();
    }

    // An item is no longer in the box: emptied into another stack, taken out,
    // destroyed. Called AFTER the entity is gone, so both numbers must have
    // been taken before it went: `wasRoot` is the root it belonged to, and
    // `wasItself` says whether it WAS that root rather than something nested
    // inside it.
    // `wasType` is the class of the item that left, and it has to be taken
    // BEFORE it goes: by the time this is called the entity is deleted and
    // the order's slot reads as nothing, so the letter could not otherwise
    // say which root it means the bridge to drop.
    static void Left(OZS_Session s, int wasRoot, bool wasItself, string wasType)
    {
        if (!Ready(s))
            return;
        OZS_Letter letter = s.Letter();
        if (wasItself)
            letter.Drop(wasRoot, wasType);
        else
            letter.Rewrite(wasRoot);
        letter.Post();
    }

    // ONE STACK BECAME TWO, AND BOTH HALVES GO IN ONE LETTER. Written
    // separately they would be two turns, and a server that died between them
    // would leave the record holding a stack that had already given its
    // contents away -- the rounds counted twice or not at all, depending on
    // the order.
    static void Split(OZS_Session s, EntityAI source, EntityAI made)
    {
        if (!Ready(s))
            return;
        OZS_Letter letter = s.Letter();
        int was = RootOf(s, source);
        if (was >= 0)
            letter.Rewrite(was);
        // A new stack standing in the box is a root of its own; one made
        // inside a container that is already in the record is part of THAT
        // root, and adding it would write the container twice.
        EntityAI top = TopOf(s, made);
        if (top == made)
        {
            letter.Add(made);
        }
        else
        {
            int host = RootOf(s, made);
            if (host >= 0)
                letter.Rewrite(host);
        }
        letter.Post();
    }

    // EVERY ROOT THE BOX HOLDS, WRITTEN AS THE WHOLE TRUTH.
    //
    // Used when a relative letter cannot help: after the two sides have been
    // caught disagreeing, and when a session ends. Within a session the
    // authority IS the contents -- a root the record has that the box does not
    // was parked out of the version before the box was ever filled -- so
    // saying "the roots are these" is saying what is there.
    //
    // Returns false when it could not even be sent, so a caller that was
    // repairing knows the repair did not happen.
    // `closing` marks the session's LAST letter: the bridge shuts the box in
    // the same step as it writes the roots, so nothing an admin writes in
    // between can be overwritten (review 2026-09-26, C4). Without the flag
    // this is a repair, and the box stays open.
    static bool Whole(OZS_Session s, string why, bool closing = false)
    {
        if (!Ready(s))
            return false;
        // FROM THE BOX, NOT FROM THE ORDER -- and the difference is the whole
        // point. `OZS_RootOrder` is this side's own bookkeeping, and it is
        // exactly what is in doubt when a repair is needed: sourcing the
        // repair from it rewrites the record with the same wrong list, the
        // next turn disagrees again, and the box repairs itself forever while
        // nothing the player does appears to take (measured 2026-09-25: the
        // record kept being set to 122 roots while the box held 121, once per
        // turn, and combining ammo looked like it did nothing but redraw).
        //
        // `OZS_GetRoots` asks the container: the weapon slots, then the cargo
        // in grid order. That is what is there.
        array<EntityAI> real = new array<EntityAI>();
        s.m_Auth.OZS_GetRoots(real);
        OZS_Letter letter = new OZS_Letter(s);
        for (int i = 0; i < real.Count(); i++)
        {
            if (real.Get(i))
                letter.Add(real.Get(i));
        }
        return letter.PostWhole(why, closing);
    }

    // A SORT: the same roots, new cells, one absolute letter.
    //
    // Nothing moves inside the authority here, and that is deliberate. The
    // record is rewritten with the cells the planner chose, and the authority
    // is then rebuilt FROM the record by the same open job that filled it in
    // the first place -- item by item, on the same frame budget. Moving a
    // hundred roots around a live container instead would be a hundred
    // verified moves with a parking problem in the middle of it, and the open
    // job already does exactly this work safely.
    static bool Sorted(OZS_Session s, array<EntityAI> roots, array<int> rows, array<int> cols)
    {
        if (!Ready(s))
            return false;
        OZS_Letter letter = new OZS_Letter(s);
        for (int i = 0; i < roots.Count(); i++)
        {
            if (!roots.Get(i))
                continue;
            int row = -1;
            int col = -1;
            if (i < rows.Count())
            {
                row = rows.Get(i);
                col = cols.Get(i);
            }
            letter.AddAt(roots.Get(i), row, col);
        }
        return letter.PostWhole("sorted");
    }

    static bool Ready(OZS_Session s)
    {
        if (!s || !s.m_Auth)
            return false;
        if (!OZS_Bridge.Up())
        {
            // §9: buffering in memory is exactly the window a per-operation
            // commit exists to close. The session ends instead.
            OZ_Log.Error("storage: proxy: the bridge is down; box " + s.m_Id + " cannot take another turn");
            s.Fail("the bridge is down");
            return false;
        }
        return true;
    }
}

// One letter to the bridge: the roots that changed, the roots that are gone,
// the roots that are new. An operation builds one and posts it once, so a turn
// that touches two roots is one round trip and cannot half-happen.
class OZS_Letter
{
    protected OZS_Session m_S;
    protected ref array<int> m_Rewrite;
    protected ref array<int> m_Drop;
    // The class that stood at each dropped position, taken while it was still
    // there. Parallel to m_Drop.
    protected ref array<string> m_DropName;
    protected ref array<EntityAI> m_Add;

    void OZS_Letter(OZS_Session s)
    {
        m_S = s;
        m_Rewrite = new array<int>();
        m_Drop = new array<int>();
        m_DropName = new array<string>();
        m_Add = new array<EntityAI>();
        m_Row = new array<int>();
        m_Col = new array<int>();
    }

    void Rewrite(int pos)
    {
        if (pos >= 0 && m_Rewrite.Find(pos) < 0)
            m_Rewrite.Insert(pos);
    }

    void Drop(int pos, string was)
    {
        if (pos >= 0 && m_Drop.Find(pos) < 0)
        {
            m_Drop.Insert(pos);
            m_DropName.Insert(was);
        }
    }

    void Add(EntityAI top)
    {
        if (top && m_Add.Find(top) < 0)
        {
            m_Add.Insert(top);
            m_Row.Insert(-1);
            m_Col.Insert(-1);
        }
    }

    // WHERE EACH ROOT IS TO STAND, when the turn is moving them all.
    //
    // A sort does not touch a single item: it says "these roots, in these
    // cells". The writer has always been able to put a root down at a cell
    // other than the one it occupies (OZS_StoreWriter.WriteRoot takes a row
    // and a column); this is how a letter says so. Parallel to m_Add, -1 for
    // "wherever it is now", which is what every other kind of letter sends.
    protected ref array<int> m_Row;
    protected ref array<int> m_Col;

    void AddAt(EntityAI top, int row, int col)
    {
        if (!top || m_Add.Find(top) >= 0)
            return;
        m_Add.Insert(top);
        m_Row.Insert(row);
        m_Col.Insert(col);
    }

    // The absolute form: the adds collected are not additions, they are the
    // whole record. An EMPTY one is still a statement -- "the box holds
    // nothing" -- so unlike Post it does not return early on having nothing
    // to say.
    bool PostWhole(string why, bool closing = false)
    {
        return Send(true, why, closing);
    }

    void Post()
    {
        // A batch is posted by the operation that opened it, once, at its
        // end (OZS_Session.EndBatch); a commit writing into it says nothing.
        if (m_S.m_Batch == this)
            return;
        if (m_Rewrite.Count() == 0 && m_Drop.Count() == 0 && m_Add.Count() == 0)
            return;
        Send(false, "", false);
    }

    // The class at a position of the order, or "" when there is nothing there
    // to name. An empty name is not a mismatch -- it is this side having
    // nothing to say -- so the bridge skips it rather than refusing.
    protected string NameAt(array<EntityAI> order, int pos)
    {
        if (pos < 0 || pos >= order.Count() || !order.Get(pos))
            return "";
        return order.Get(pos).GetType();
    }

    protected bool Send(bool whole, string note, bool closing)
    {
        if (!whole && m_Rewrite.Count() == 0 && m_Drop.Count() == 0 && m_Add.Count() == 0)
            return false;
        array<EntityAI> order = m_S.m_Auth.OZS_RootOrder();
        // The blobs, in the order the bridge reads them: first the rewrites,
        // then the additions. The letter says how many of each.
        array<EntityAI> blobs = new array<EntityAI>();
        array<int> at = new array<int>();
        for (int i = 0; i < m_Rewrite.Count(); i++)
        {
            int pos = m_Rewrite.Get(i);
            if (pos < 0 || pos >= order.Count() || !order.Get(pos))
                continue;
            at.Insert(pos);
            blobs.Insert(order.Get(pos));
        }
        int rewrites = blobs.Count();
        for (int a = 0; a < m_Add.Count(); a++)
            blobs.Insert(m_Add.Get(a));
        int entities = 0;
        for (int c = 0; c < blobs.Count(); c++)
            entities = entities + OZS_Records.CountTree(blobs.Get(c));

        string name = "";
        if (blobs.Count() > 0)
        {
            OZS_StoreWriter writer = new OZS_StoreWriter();
            string why;
            if (!writer.OpenOp(m_S.m_Auth, blobs.Count(), entities, OZS_Commit.NextSerial(), why))
            {
                OZ_Log.Error("storage: proxy: a turn of box " + m_S.m_Id + " cannot be written: " + why);
                m_S.Fail("the turn could not be written");
                return false;
            }
            for (int b = 0; b < blobs.Count(); b++)
            {
                // A rewrite keeps its place; only an add can carry a new one,
                // and only a sort ever sets it.
                int putRow = -1;
                int putCol = -1;
                int addAt = b - rewrites;
                if (addAt >= 0 && addAt < m_Row.Count())
                {
                    putRow = m_Row.Get(addAt);
                    putCol = m_Col.Get(addAt);
                }
                writer.WriteRoot(blobs.Get(b), putRow, putCol);
            }
            if (!writer.Finish(why))
            {
                OZ_Log.Error("storage: proxy: a turn of box " + m_S.m_Id + " was written short: " + why);
                m_S.Fail("the turn could not be written");
                return false;
            }
            name = writer.Name();
        }

        OZS_OpLetter letter = new OZS_OpLetter();
        letter.id = m_S.m_Id;
        letter.file = name;
        letter.rewrite = new array<int>();
        letter.drop = new array<int>();
        letter.expect = new array<string>();
        for (int r = 0; r < at.Count(); r++)
            letter.rewrite.Insert(at.Get(r));
        for (int d = 0; d < m_Drop.Count(); d++)
            letter.drop.Insert(m_Drop.Get(d));
        // The names go in the same order the positions do: the rewrites
        // first, then the drops. A position whose entity is already gone --
        // which is every drop, since a drop is written after the item left --
        // is named by the class it was, which the order still holds.
        for (int er = 0; er < letter.rewrite.Count(); er++)
            letter.expect.Insert(NameAt(order, letter.rewrite.Get(er)));
        for (int ed = 0; ed < letter.drop.Count(); ed++)
            letter.expect.Insert(m_DropName.Get(ed));
        letter.adds = m_Add.Count();
        letter.entities = entities;
        if (whole)
            letter.replace = 1;
        if (whole && closing)
            letter.close = 1;

        if (whole)
        {
            // THE ORDER BECOMES THE LETTER, not the other way round. The whole
            // point of the absolute form is that the positions we had are the
            // ones in doubt, so they are thrown away and rebuilt from what the
            // box actually holds -- in exactly the order the bridge is about
            // to store.
            order.Clear();
            for (int w = 0; w < blobs.Count(); w++)
                order.Insert(blobs.Get(w));
            OZ_Log.Info("storage: proxy: box " + m_S.m_Id + ": the record is being set to what the box holds -- " + blobs.Count().ToString() + " root(s), " + entities.ToString() + " entity(ies) (" + note + ")");
        }
        else
        {
            // The record's order follows the letter on this side too, and the
            // bridge renumbers its rows the same way. Drops are applied from
            // the back so the earlier positions keep their meaning while we go.
            for (int dd = m_Drop.Count() - 1; dd >= 0; dd--)
            {
                int gone = m_Drop.Get(dd);
                if (gone >= 0 && gone < order.Count())
                    order.RemoveOrdered(gone);
            }
            for (int aa = 0; aa < m_Add.Count(); aa++)
                order.Insert(m_Add.Get(aa));
            OZ_Log.Dbg("storage: proxy: turn for " + m_S.m_Id + ": rewrite " + letter.rewrite.Count().ToString() + " drop " + letter.drop.Count().ToString() + " add " + letter.adds.ToString() + ", the order now has " + order.Count().ToString() + " root(s), the box " + m_S.m_Auth.OZS_CountEntities().ToString());
        }
        string json;
        string err;
        if (!JsonFileLoader<OZS_OpLetter>.MakeData(letter, json, err, false))
        {
            OZ_Log.Error("storage: proxy: the turn letter cannot be written: " + err);
            m_S.Fail("the turn could not be written");
            return false;
        }
        m_S.OnCommitSent();
        // A REPAIR IS NOT REPAIRED AGAIN. The reply knows whether the letter
        // it answers was the absolute form: if THAT is refused there is
        // nothing left to try, and the session ends rather than looping.
        OZS_Bridge.Post(OZS_Const.ROUTE_OP, json, new OZS_OpReply(m_S, whole));
        return true;
    }
}

class OZS_OpLetter
{
    string id;
    string file;
    ref array<int> rewrite;
    ref array<int> drop;
    // WHAT THIS SIDE BELIEVES STANDS AT EACH OF THOSE POSITIONS: the class
    // name of `rewrite[i]`, then of `drop[i]`, in that order. The whole
    // positional protocol rests on "position k means the same root on both
    // sides", and nothing used to check it -- so an order that had come apart
    // by one wrote a rewrite and a drop onto the wrong roots, and the counts
    // still matched afterwards, which is why the count check never saw it
    // (measured 2026-09-25: a drifted turn dropped an Ammo_556x45 and a
    // Mag_AKM_30Rnd and added an Ammo_9x19, for a combine of two Ammo_9x19).
    //
    // The bridge compares before it applies anything and refuses the whole
    // letter on a mismatch, which is what turns silent corruption into a
    // refusal the session answers with an absolute rewrite.
    ref array<string> expect;
    int adds;
    int entities;
    // THE ABSOLUTE FORM. Every other field is relative -- this position, that
    // one -- and a relative letter is written in the numbering it is trying to
    // correct. With this set, the roots of the record become exactly the ones
    // in the file, in that order, and the other fields are not read.
    int replace;
    // THE SESSION'S LAST WORD. With this set on an absolute letter the bridge
    // marks the box closed in the same transaction as it writes the roots.
    // A `closed` sent separately was a second, concurrent request, and an
    // admin's write that landed between the two was overwritten by this
    // letter (review 2026-09-26, C4).
    int close;
}

class OZS_OpAnswer
{
    bool ok;
    string why;
    int version;
    int roots;
    int entities;
}

class OZS_OpReply : OZ_BridgeReply
{
    protected OZS_Session m_S;
    // Was the letter this answers the absolute form? A refused ordinary turn
    // can be put straight by one; a refused repair cannot.
    protected bool m_Whole;

    void OZS_OpReply(OZS_Session s, bool whole = false)
    {
        m_S = s;
        m_Whole = whole;
    }

    override void OnBody(string json)
    {
        if (!m_S)
            return;
        OZS_OpAnswer a = new OZS_OpAnswer();
        string err;
        if (!JsonFileLoader<OZS_OpAnswer>.LoadData(json, a, err) || !a)
        {
            OZ_Log.Error("storage: proxy: the bridge's answer to a turn cannot be read: " + err);
            m_S.Fail("the bridge's answer to a turn could not be read");
            return;
        }
        if (!a.ok)
        {
            OZ_Log.Error("storage: proxy: the bridge refused a turn of box " + m_S.m_Id + ": " + a.why);
            // Off the wire before anything else: the repair below sends a
            // letter of its own, and a refusal that never gave its flight
            // back would leave the session unable to close.
            m_S.OnCommitRefused();
            // A REFUSAL IS PROOF THE WRITE DID NOT HAPPEN -- the bridge applies
            // a turn inside one transaction and answers with the refusal only
            // after it has rolled back. So the record is exactly as it was and
            // the box has moved on without it: two pictures that no longer
            // agree, which is the one thing a relative letter cannot mend.
            //
            // It used to end the session instead, and the session took the
            // authority with it -- items included. A pair of trousers put into
            // a brand-new stash was refused, and destroyed, on 2026-09-25.
            if (!m_Whole)
            {
                if (OZS_Commit.Whole(m_S, "a turn was refused: " + a.why))
                    return;
            }
            m_S.Fail("the bridge refused a turn: " + a.why);
            return;
        }
        m_S.OnCommitted(a.version, a.roots, a.entities);
    }

    override void OnFail(int code)
    {
        if (!m_S)
            return;
        OZ_Log.Error("storage: proxy: a turn of box " + m_S.m_Id + " never reached the bridge (code " + code.ToString() + ")");
        m_S.Fail("a turn never reached the bridge");
    }
}
