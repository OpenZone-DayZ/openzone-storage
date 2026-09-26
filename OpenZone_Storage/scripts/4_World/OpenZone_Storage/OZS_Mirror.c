// The client half of the proxy design: the PROXY itself -- a real container in
// this one client's memory, holding a copy of what the authoritative container
// on the server holds.
// Design: docs/specs/2026-09-24-storage-proxy-inventory-design.md §2.
//
// It is a real entity on purpose. A table of records could not answer "does
// this fit here", and the screen needs a live item to draw an icon from. It is
// created with ECE_LOCAL, so it exists nowhere but in this process: no other
// client has heard of it, the server has not either, and nothing about it is
// ever saved.
//
// WHERE IT LIVES. Far under the terrain, because a container standing next to
// the player would show up in the vanilla vicinity panel and be rendered in the
// world. Nothing about a proxy needs a sensible position: nobody walks up to it
// and the screen draws its contents from the entities, not from the scene.
//
// WHAT IT IS NOT. It is never the last word. It may be more permissive than the
// authority but never stricter (§5), and where the two disagree -- which has
// been measured, on a negative cell both returned true and did different things
// -- the authority wins and the proxy is rebuilt.
class OZS_Mirrors
{
    protected static ref OZS_Mirrors s_Inst;
    protected ref array<ref OZS_Mirror> m_Mirrors;
    // A box was asked for and the panel should open when it is whole.
    protected bool m_Waiting;
    // The anchor the last ask went through. The client is never told a box's
    // id, so this is the only thing that pairs a proxy with the box in the
    // world it stands for.
    protected Object m_Asked;
    // WHICH OF THE SCREEN'S METHODS AN OPERATION CAME IN THROUGH. Only ever
    // read into a log line. The screen has a dozen ways of saying "move this
    // there", and a report of the shape "I was not taking anything out" is
    // unanswerable without knowing which one the gesture used.
    static string s_Via = "?";
    // THE PANEL DOES NOT NOTICE A LOCAL MOVE.
    //
    // A proxy is moved with `InventoryMode.LOCAL`, and the vanilla inventory
    // learns of a container's contents changing from the engine's own
    // inventory events -- which a local move on a client-only entity does not
    // raise. So the server performed the swap, the proxy applied it, and the
    // screen went on drawing the old picture: no refusal, no rebuild, nothing
    // in any log (owner, 2026-09-25 -- "it still does not drag").
    //
    // The redraw itself belongs to 5_Mission, which is the only tier that can
    // see the menu; this side just says that something changed.
    // WHERE THE PLAYER WAS LOOKING WHILE THE BOX IS REBUILT.
    //
    // A resynchronisation throws away every proxy item and makes them again,
    // so the panel rebuilds from nothing and starts at the top. In a box five
    // hundred cells deep that costs the player their place every time somebody
    // else touches the box. Below zero means nothing is being kept.
    //
    // 4_World cannot see a widget, so the number is only stored here; the
    // screen puts it in and takes it out (OZS_ClientViewer).
    static float s_ScrollWas = -1;

    static bool s_Redraw;

    static void Changed()
    {
        s_Redraw = true;
    }

    static bool TakeRedraw()
    {
        bool was = s_Redraw;
        s_Redraw = false;
        return was;
    }

    static OZS_Mirrors Get()
    {
        if (!s_Inst)
            s_Inst = new OZS_Mirrors();
        return s_Inst;
    }

    // Mission finish on the client. Statics survive a reconnect inside one
    // process; a mirror kept across one holds a container the world took
    // with it (review 2026-09-26, D4).
    static void Reset()
    {
        if (s_Inst)
            s_Inst.DropAll();
        s_Inst = null;
    }

    void OZS_Mirrors()
    {
        m_Mirrors = new array<ref OZS_Mirror>();
    }

    void Asking(Object anchor)
    {
        m_Asked = anchor;
    }

    // The box in the world a proxy stands for, paired at the moment of asking.
    Object Asked()
    {
        return m_Asked;
    }

    // THE ONE MIRROR THERE USUALLY IS. A client holds one open box at a time
    // in practice, and the stand's own control file needs a way to name it
    // without knowing its id.
    OZS_Mirror First()
    {
        for (int i = 0; i < m_Mirrors.Count(); i++)
        {
            if (m_Mirrors.Get(i))
                return m_Mirrors.Get(i);
        }
        return null;
    }

    OZS_Mirror Find(string id)
    {
        for (int i = 0; i < m_Mirrors.Count(); i++)
        {
            if (m_Mirrors.Get(i).m_Id == id)
                return m_Mirrors.Get(i);
        }
        return null;
    }

    array<ref OZS_Mirror> All()
    {
        return m_Mirrors;
    }

    // The one a stream has just built. A screen asks for a box before it knows
    // the box's id and binds to whatever arrives; only one screen is open at a
    // time, so "the newest" is the one that was asked for.
    OZS_Mirror Newest()
    {
        if (m_Mirrors.Count() == 0)
            return null;
        return m_Mirrors.Get(m_Mirrors.Count() - 1);
    }

    void Keep(OZS_Mirror m)
    {
        m_Mirrors.Insert(m);
    }

    void Drop(string id)
    {
        for (int i = m_Mirrors.Count() - 1; i >= 0; i--)
        {
            if (m_Mirrors.Get(i).m_Id != id)
                continue;
            m_Mirrors.Get(i).Destroy();
            m_Mirrors.RemoveOrdered(i);
        }
    }

    void DropAll()
    {
        for (int i = m_Mirrors.Count() - 1; i >= 0; i--)
            m_Mirrors.Get(i).Destroy();
        m_Mirrors.Clear();
    }

    // The ordinary inventory, opened when the box has arrived rather than
    // when it was asked for.
    void ShowWhenReady()
    {
        m_Waiting = true;
    }

    void Update(float timeslice)
    {
        if (m_Waiting)
        {
            OZS_Mirror m = Newest();
            if (!m || !m.m_Whole)
                return;
            m_Waiting = false;
#ifndef NO_GUI
            if (GetGame().GetMission())
            {
                GetGame().GetMission().ShowInventory();
                m_Shown = true;
            }
#endif
            return;
        }
        Watch();
    }

    // THE PROXY LIVES EXACTLY AS LONG AS THE SCREEN THAT NEEDS IT.
    //
    // It used to live until the client process exited: nothing on either side
    // ever said "let it go", so every box a player opened stayed in their
    // vicinity panel for the rest of the session, under the box's own name.
    // The player then had two entries called СЕРЕДНЯ СКРИНЯ, one of them a
    // ghost -- and opening the wrong one showed an empty box (owner,
    // 2026-09-24).
    //
    // The screen closing is the honest end of a session: the server is told,
    // which lets the authority go and the box be closed, and the proxy is
    // deleted here. The server also says `GONE` of its own accord when a
    // session ends for any other reason.
    protected bool m_Shown;

    protected void Watch()
    {
#ifndef NO_GUI
        if (!m_Shown || m_Mirrors.Count() == 0)
            return;
        UIManager ui = GetGame().GetUIManager();
        if (ui && ui.IsMenuOpen(MENU_INVENTORY))
            return;
        m_Shown = false;
        for (int i = 0; i < m_Mirrors.Count(); i++)
            m_Mirrors.Get(i).Shut();
        DropAll();
#endif
    }

    string Status()
    {
        string s = "mirrors=" + m_Mirrors.Count();
        for (int i = 0; i < m_Mirrors.Count(); i++)
            s = s + " | " + m_Mirrors.Get(i).Status();
        return s;
    }

    // ---- "is this about a box at all?" -----------------------------------
    //
    // THE FIRST QUESTION OF EVERY HOOK, AND IT MUST BE CHEAP AND EXACT.
    // The hooks sit on the player's own move methods, which run on every drag
    // the player makes anywhere -- their pockets, their backpack, a car, some
    // other mod's crate. None of that is ours. So:
    //
    //   1. no proxy open at all -> one integer test and straight to super;
    //   2. a proxy open -> compare the HIERARCHY ROOT of each end against the
    //      proxy ENTITIES we made ourselves, by pointer. Not by class, not by
    //      position: an ordinary crate of the same class must never match.
    //
    // Everything that is not a box goes to super untouched.
    static bool None()
    {
        if (!s_Inst)
            return true;
        return s_Inst.m_Mirrors.Count() == 0;
    }

    // IS EVERY BOX ON THIS CLIENT FINISHED BEING BUILT?
    //
    // Asked before the screen's own state is worth saving: a box halfway
    // through a rebuild has nothing drawn, and everything measured off the
    // panel at that moment describes an empty column rather than the player's
    // place in a full one.
    bool Whole()
    {
        for (int i = 0; i < m_Mirrors.Count(); i++)
        {
            if (!m_Mirrors.Get(i).m_Whole)
                return false;
        }
        return true;
    }

    // The proxy this entity belongs to, or null. An item nested three
    // containers deep inside a proxy belongs to it; an item in the player's
    // backpack belongs to the player and answers null.
    static OZS_Mirror Of(EntityAI e)
    {
        if (None() || !e)
            return null;
        EntityAI root = e;
        while (root.GetHierarchyParent())
            root = root.GetHierarchyParent();
        array<ref OZS_Mirror> all = s_Inst.m_Mirrors;
        for (int i = 0; i < all.Count(); i++)
        {
            if (all.Get(i).m_Box == root)
                return all.Get(i);
        }
        return null;
    }

    // The proxy an inventory location is in: its parent's root. A location
    // with no parent -- the ground, the hands of nobody -- is never ours.
    static OZS_Mirror At(InventoryLocation il)
    {
        if (None() || !il)
            return null;
        return Of(il.GetParent());
    }

    // The one proxy a move touches, or null when it touches none. A move
    // between two different proxies is not a thing: a player has one box open.
    static OZS_Mirror Touching(InventoryLocation src, InventoryLocation dst)
    {
        OZS_Mirror from = At(src);
        if (from)
            return from;
        return At(dst);
    }

    // ---- the wire, client side -------------------------------------------

    // THE CLIENT LISTENS AT THE GAME, NOT AT ITS PLAYER (measured 2026-09-24).
    // A ScriptRPC sent server -> one client arrives with its TARGET NULL, even
    // when the server addressed it to that player's own entity and logged the
    // entity's network id doing so. With a null target the engine never calls
    // Object.OnRPC at all, so a handler on PlayerBase is never reached.
    // DayZGame.Event_OnRPC fires for every message, target or no target, and
    // every message of ours carries the box id in its body anyway.
    //
    // (The other direction is fine: client -> server keeps the target, which
    // is why the asking half still rides on the player.)
    // ---- the quick gesture and the ground swap ---------------------------

    // ALT HELD, read off the keyboard itself: DayZGame keeps Alt in a private
    // field with no getter, and the fast-transfer inputs vanilla defines are
    // bound on consoles only (bin.pbo: ps4X / x1X, nothing for a keyboard).
    static bool AltHeld()
    {
        return KeyState(KeyCode.KC_LMENU) != 0 || KeyState(KeyCode.KC_RMENU) != 0;
    }

    // ALT + CLICK ON AN ITEM: out of the box into the player's inventory, or
    // the player's own into the open box (owner, 2026-09-26). Anything else
    // under the cursor -- the ground, somebody else's crate -- is not this
    // gesture and answers false, so the click goes on to vanilla.
    static bool QuickMove(EntityAI item)
    {
        if (None() || !item)
            return false;
        PlayerBase player = PlayerBase.Cast(GetGame().GetPlayer());
        if (!player)
            return false;
        OZS_Mirror m = Of(item);
        if (m)
        {
            s_Via = "AltClick";
            return m.TakeInto(item, player, FindInventoryLocationType.CARGO);
        }
        if (item.GetHierarchyRootPlayer() != player)
            return false;
        if (!item.GetInventory().CanRemoveEntity() || !player.CanManipulateInventory())
            return false;
        m = s_Inst.First();
        if (!m || !m.m_Whole)
            return false;
        s_Via = "AltClick";
        return m.PutInto(item);
    }

    // A LOOSE ITEM DROPPED ON AN ITEM IN THE BOX, when the screen found
    // nothing to do with the pair: its own swap test (CanSwapEntitiesEx)
    // does not pass a ground item. The exchange the drop means is asked for
    // as what it is -- an Across -- and the server decides (owner,
    // 2026-09-26: "a swap between the ground and the box").
    static bool GroundSwap(EntityAI selected, EntityAI target)
    {
        if (None() || !selected || !target)
            return false;
        if (selected.GetHierarchyParent() || Of(selected))
            return false;
        OZS_Mirror m = Of(target);
        if (!m)
            return false;
        int handle = m.HandleOf(target);
        if (handle == 0)
            return false;
        s_Via = "GroundSwap";
        m.Across(selected, handle);
        return true;
    }

    static void Listen()
    {
        if (!GetGame() || !GetGame().IsClient())
            return;
        if (s_Ears)
            return;
        s_Ears = new OZS_MirrorEars();
        DayZGame.Event_OnRPC.Insert(s_Ears.OnGameRPC);
    }

    protected static ref OZS_MirrorEars s_Ears;

    // Returns true when the message was ours.
    static bool OnWire(int type, ParamsReadContext ctx)
    {
        if (!GetGame() || !GetGame().IsClient())
            return false;
        if (type == OZS_Const.RPC_PX_BEGIN)
        {
            string id;
            string cls;
            int total;
            int version;
            if (!ctx.Read(id) || !ctx.Read(cls) || !ctx.Read(total) || !ctx.Read(version))
                return true;
            OZS_Mirrors.Get().Drop(id);
            // `ref` on the local: a `new` held by a plain local can be
            // collected before it reaches the array that owns it.
            ref OZS_Mirror m = new OZS_Mirror(id, cls, total, version);
            m.m_Anchor = OZS_Mirrors.Get().Asked();
            OZS_Mirrors.Get().Keep(m);
            m.Begin();
            return true;
        }
        if (type == OZS_Const.RPC_PX_ROWS)
        {
            string rowsId;
            int n;
            if (!ctx.Read(rowsId) || !ctx.Read(n))
                return true;
            OZS_Mirror into = OZS_Mirrors.Get().Find(rowsId);
            for (int i = 0; i < n; i++)
            {
                OZS_Row r = new OZS_Row();
                if (!OZS_Wire.ReadRow(ctx, r))
                    return true;
                if (into)
                    into.Add(r);
            }
            return true;
        }
        if (type == OZS_Const.RPC_PX_GONE)
        {
            string goneId;
            if (!ctx.Read(goneId))
                return true;
            // A GONE for a box this client never got to build is the end of
            // an open that failed on the server: the screen was waiting for
            // it, and must stop.
            if (!OZS_Mirrors.Get().Find(goneId))
                OZS_Mirrors.Get().Unwait();
            OZS_Mirrors.Get().Drop(goneId);
            return true;
        }
        if (type == OZS_Const.RPC_PX_END)
        {
            string endId;
            int endTotal;
            int endVersion;
            if (!ctx.Read(endId) || !ctx.Read(endTotal) || !ctx.Read(endVersion))
                return true;
            OZS_Mirror done = OZS_Mirrors.Get().Find(endId);
            if (done)
                done.End(endTotal, endVersion);
            return true;
        }
        if (type == OZS_Const.RPC_PX_CHANGE)
        {
            string chId;
            int what;
            int chVersion;
            OZS_Row cr = new OZS_Row();
            if (!ctx.Read(chId) || !ctx.Read(what) || !ctx.Read(chVersion))
                return true;
            if (!OZS_Wire.ReadRow(ctx, cr))
                return true;
            OZS_Mirror changed = OZS_Mirrors.Get().Find(chId);
            if (changed)
                changed.Change(what, cr, chVersion);
            return true;
        }
        if (type == OZS_Const.RPC_PX_NO)
        {
            string noId;
            int noHandle;
            int noVersion;
            string noWhy;
            if (!ctx.Read(noId) || !ctx.Read(noHandle) || !ctx.Read(noVersion) || !ctx.Read(noWhy))
                return true;
            OZS_Mirror said = OZS_Mirrors.Get().Find(noId);
            if (said)
                said.Refused(noHandle, noVersion, noWhy);
            else
                OZS_Mirrors.Get().Told(noWhy);
            return true;
        }
        return false;
    }

    // ---- a refusal with no box behind it ---------------------------------

    // The open itself was turned away -- the bridge down, the box in a
    // transition, the fill failed -- and there is no mirror to be refused
    // through. The wait for a stream ends and the player is told why, in
    // their own language when the reason is one of the stringtable's
    // (review 2026-09-26, C2, C3).
    void Told(string why)
    {
        OZ_Log.Warn("storage: proxy: the box could not be shown: " + why);
        Unwait();
        Tell(why);
    }

    void Unwait()
    {
        m_Waiting = false;
    }

    // A refusal, in the player's own language, where they are looking. Only
    // the reasons written as a stringtable key are shown; the rest are our
    // own words to ourselves.
    static void Tell(string why)
    {
#ifndef NO_GUI
        if (why == "" || why.Get(0) != "#")
            return;
        string text = Widget.TranslateString(why);
        if (text == "")
            return;
        NotificationSystem.AddNotificationExtended(4, text, "", "set:dayz_gui_icon image:missing");
#endif
    }
}

// The client's ear on DayZGame.Event_OnRPC. A ScriptInvoker wants a method of
// an object, so this is the object.
class OZS_MirrorEars
{
    void OnGameRPC(PlayerIdentity sender, Object target, int rpc_type, ParamsReadContext ctx)
    {
        OZS_Mirrors.OnWire(rpc_type, ctx);
    }
}

// ---------------------------------------------------------------------------

class OZS_Mirror
{
    string m_Id;
    string m_Class;
    // The box in the world this stands for. The client is never told a box's
    // id, so the pairing is made when the player asks through it.
    Object m_Anchor;

    EntityAI m_Box;
    ref array<int> m_Handles;
    ref array<EntityAI> m_Items;
    int m_Version;
    int m_Expected;
    int m_Missed;
    bool m_Whole;
    // Did this proxy draw a guess of its own that a refusal would have to
    // undo? A MOVE is shown at once and the server's word follows; a SWAP is
    // not drawn at all until the server answers. Only the first kind needs the
    // box to ask for itself again when the answer is no -- asking anyway is
    // the blink the player sees on every refusal (owner, 2026-09-25).
    protected bool m_Guessed;
    float m_Started;
    float m_Spent;
    // Wall time from the first chunk to the end: the stream is paced by the
    // server, so the work and the wait are two different numbers and both
    // matter.
    float m_Wall;
    // The screen hangs on this: it is told when the contents changed rather
    // than polling a thousand cells every frame.
    ref ScriptInvoker m_OnChanged;

    void OZS_Mirror(string id, string cls, int total, int version)
    {
        m_Id = id;
        m_Class = cls;
        m_Expected = total;
        m_Version = version;
        m_Handles = new array<int>();
        m_Items = new array<EntityAI>();
        m_OnChanged = new ScriptInvoker();
        m_Whole = false;
        m_Missed = 0;
    }

    ScriptInvoker OnChanged()
    {
        return m_OnChanged;
    }

    void Begin()
    {
        // The box is about to be built from nothing. Whatever the screen has
        // remembered of the player's place stands until it is whole again.
        OZS_Mirrors.Changed();
        m_Started = GetGame().GetTickTime();
        m_Spent = 0;
        Man me = GetGame().GetPlayer();
        vector at = Vector(0, 0, 0);
        if (me)
            at = me.GetPosition();
        // BESIDE THE PLAYER, ON PURPOSE (owner, 2026-09-24). The vanilla
        // inventory finds containers by proximity, and a client-local
        // container standing next to the player is listed there like any
        // other -- with its grid, its slots, its stacking and its scrolling,
        // none of which we then have to write. The model is hidden: the player
        // should see the box they are standing at, not a second one at their
        // feet.
        at[0] = at[0] + OZS_Const.PROXY_ASIDE;
        Object made = GetGame().CreateObjectEx(m_Class, at, ECE_LOCAL | ECE_NOLIFETIME);
        m_Box = EntityAI.Cast(made);
        if (!m_Box)
        {
            OZ_Log.Error("storage: proxy: this client cannot create " + m_Class + " for box " + m_Id);
            return;
        }
        m_Box.SetInvisible(true);
        OZ_StorageBox asBox = OZ_StorageBox.Cast(m_Box);
        if (asBox)
        {
            // A proxy answers with the id of the box it shows, and its gates
            // must not refuse the items this fills it with.
            asBox.OZS_StandForId(m_Id);
            asBox.OZS_SetState(OZS_Const.STATE_OPEN);
            asBox.OZS_SetRestoring(true);
        }
    }

    // One row into the container. Parents always come before their children,
    // so the parent of a row is already built when the row arrives.
    void Add(OZS_Row r)
    {
        if (!m_Box)
            return;
        float t0 = GetGame().GetTickTime();
        EntityAI parent = m_Box;
        if (r.parent != 0)
        {
            parent = ByHandle(r.parent);
            if (!parent)
            {
                m_Missed++;
                return;
            }
        }
        EntityAI made = null;
        if (r.lt == InventoryLocationType.ATTACHMENT)
        {
            made = parent.GetInventory().CreateAttachmentEx(r.cls, r.slot);
        }
        else
        {
            bool flip = r.flip == 1;
            if (r.row >= 0)
                made = parent.GetInventory().CreateEntityInCargoEx(r.cls, 0, r.row, r.col, flip);
            if (!made)
                made = parent.GetInventory().CreateEntityInCargo(r.cls);
        }
        if (!made)
        {
            m_Missed++;
            m_Spent = m_Spent + (GetGame().GetTickTime() - t0);
            return;
        }
        // WHERE IT ACTUALLY LANDED. `CreateEntityInCargoEx` answers with the
        // entity whether or not it honoured the cell: asked for 6,4 turned, in
        // a box where 6,4 was still empty, it made the can at 6,3 -- and the
        // next can then landed on top of it. That is why a box the client had
        // just built from the authority, ten rows for ten items, already
        // disagreed with the server about two of them, with nothing in either
        // log to say so (measured 2026-09-25).
        //
        // The authority's layout never overlaps, so an item put exactly where
        // it is told can never block a later one. Correcting each one the
        // moment it is made keeps that true for the whole build.
        if (r.lt == InventoryLocationType.CARGO && r.row >= 0)
        {
            InventoryLocation got = new InventoryLocation();
            bool knows = made.GetInventory().GetCurrentInventoryLocation(got);
            if (!knows || !Matches(got, parent, r))
            {
                string landed = "nowhere it will say";
                if (knows)
                    landed = got.GetRow().ToString() + "," + got.GetCol().ToString() + " flip " + got.GetFlip().ToString();
                OZ_Log.Warn("storage: proxy: made " + r.cls + " for " + r.Where() + " and the engine put it at " + landed + "; moving it");
                Place(made, r);
            }
        }
        Dress(made, r);
        if (r.lt == InventoryLocationType.ATTACHMENT)
            SyncWeapon(parent);
        m_Handles.Insert(r.handle);
        m_Items.Insert(made);
        m_Spent = m_Spent + (GetGame().GetTickTime() - t0);
    }

    // A WEAPON DRAWS ITS MAGAZINE ONLY WHEN TOLD TO. The magazine selection
    // of a weapon is switched by Weapon_Base.ShowMagazine/HideMagazine
    // (SelectionMagazineShow, or the simple hidden selection on the weapons
    // that have one), and the only callers in the game are the weapon's own
    // state machine and ForceSyncSelectionState. A magazine created straight
    // into the slot of a local weapon runs neither: EEItemAttached on a
    // weapon only refreshes its property modifiers (weapon_base.c:1116). So
    // the proxy held the drum, the panel listed it, and the AKM was drawn
    // without it (owner, 2026-09-26). ForceSyncSelectionState reads what is
    // attached now and shows or hides accordingly; the chamber it also
    // reads is empty on a proxy, and stays hidden, which is right.
    static void SyncWeapon(EntityAI e)
    {
        Weapon_Base wpn = Weapon_Base.Cast(e);
        if (wpn)
            wpn.ForceSyncSelectionState();
    }

    // What of an item's state the proxy is given. Not the blob: it would buy
    // a correct field nobody on a client reads and pay with another mod's
    // OnStoreLoad running where it was never meant to (§10.2).
    static void Dress(EntityAI e, OZS_Row r)
    {
        // WHICH WAY ROUND, TOLD TO THE ITEM AND NOT ONLY TO ITS CELL.
        //
        // `CreateEntityInCargoEx` takes the turn as part of the PLACE, and the
        // panel draws from the place -- so a turned rag was drawn lying across
        // correctly. But everything that works out WHICH CELLS an item covers
        // asks the ITEM (GameInventory.GetFlipCargo), and that stayed false.
        // So the vanilla screen drew the rag across three columns while
        // believing it reached three rows DOWN: a drop onto an empty cell
        // looked to it like a drop onto whatever sat below, and it offered an
        // exchange instead of a move. The player reads that as "I am putting
        // it on an empty cell and it tells me the two cannot trade places"
        // (owner, 2026-09-26).
        if (r.lt == InventoryLocationType.CARGO && e.GetInventory())
            e.GetInventory().SetFlipCargo(r.flip == 1);
        if (r.health >= 0)
            e.SetHealth01("", "", r.health / 100.0);
        ItemBase item = ItemBase.Cast(e);
        if (item && r.qty >= 0)
        {
            // THE FOURTH ARGUMENT IS `allow_client`, AND WITHOUT IT THIS DOES
            // NOTHING HERE.
            //
            //   SetQuantity(float value, bool destroy_config = true,
            //               bool destroy_forced = false,
            //               bool allow_client = false,
            //               bool clamp_to_stack_max = true)
            //
            // A proxy lives on a client, so the default `false` made every
            // call return without touching the item -- silently, the way this
            // engine prefers. Every stack in the box then drew the quantity
            // its CONFIG gives a fresh one, not the one in the record: the
            // owner saw ammo piles counting 20, 25, 50 and 70 rounds that
            // nobody had put there (2026-09-25).
            item.SetQuantity(r.qty, false, false, true);
        }
        // THE ROUNDS ARE SET SEPARATELY, because the screen reads them
        // separately: `QuantityConversions.GetItemQuantityText` returns
        // `GetAmmoCount()` for a magazine and never looks at the quantity at
        // all (quantityconversions.c:12-19). `LocalSetAmmoCount` is the
        // client-side half of the pair; the server-side one would do nothing
        // here.
        Magazine mag = Magazine.Cast(e);
        if (mag && r.ammo >= 0)
        {
            mag.LocalSetAmmoCount(r.ammo);
            // READ BACK WHAT THE SCREEN WILL READ. `GetAmmoCount` is the one
            // number the panel draws for a magazine, so asking it here is the
            // same question the player's eyes ask -- and a setter that did
            // nothing would otherwise be invisible until somebody counted
            // rounds by hand.
            int got = mag.GetAmmoCount();
            if (got != r.ammo)
                OZ_Log.Warn("storage: proxy: " + r.cls + " was given " + r.ammo.ToString() + " round(s) and reports " + got.ToString());
        }
    }

    void End(int total, int version)
    {
        m_Whole = true;
        m_Version = version;
        m_Expected = total;
        m_Wall = GetGame().GetTickTime() - m_Started;
        float wall = m_Wall * 1000;
        OZ_StorageBox asBox = OZ_StorageBox.Cast(m_Box);
        if (asBox)
            asBox.OZS_SetRestoring(false);
        string s = "storage: proxy: box " + m_Id + " built with " + m_Items.Count().ToString() + " of " + total.ToString() + " item(s)";
        s = s + " in " + (m_Spent * 1000).ToString() + " ms of work over " + wall.ToString() + " ms";
        if (m_Missed > 0)
            s = s + ", " + m_Missed.ToString() + " REFUSED BY THIS CLIENT";
        OZ_Log.Info(s);
        Announce();
    }

    // ---- the server's word -----------------------------------------------

    void Change(int what, OZS_Row r, int version)
    {
        m_Version = version;
        if (what == OZS_Const.CH_GONE)
        {
            Forget(r.handle, true);
            Announce();
            return;
        }
        if (what == OZS_Const.CH_ADDED)
        {
            // It may be here already: this client's own proxy guessed it, and
            // the server is confirming. Rebuilt from the authority's word
            // either way, so the two can never drift apart.
            Forget(r.handle, true);
            Add(r);
            Announce();
            return;
        }
        if (what == OZS_Const.CH_MOVED)
        {
            EntityAI e = ByHandle(r.handle);
            // THE LAST SILENT STRETCH: what the server said, and what the
            // proxy did about it. A swap the server performed and the screen
            // never showed left no trace at all on this side -- no refusal, no
            // rebuild, nothing to tell "the message never came" from "it came
            // and changed nothing" (owner, 2026-09-25).
            string told = "#" + r.handle.ToString() + " " + r.cls + " to " + r.Where();
            if (!e)
            {
                OZ_Log.Dbg("storage: proxy: told " + told + ", which this proxy does not have; adding it");
                Add(r);
                Announce();
                return;
            }
            OZ_Log.Dbg("storage: proxy: told " + told);
            m_Guessed = false;
            // A CHANGE FROM THE AUTHORITY IS NOT A GUESS. When the proxy
            // cannot do what the server did, the two are out of step by
            // definition, and the only honest answer is to ask for the box
            // again -- silently keeping the old picture is what let the screen
            // show the cans unswapped while both logs said they had swapped.
            if (!Place(e, r))
            {
                Resync();
                return;
            }
            Announce();
            return;
        }
        if (what == OZS_Const.CH_QTY)
        {
            EntityAI q = ByHandle(r.handle);
            if (q)
                Dress(q, r);
            Announce();
            return;
        }
    }

    // One change, one repaint. Every branch above ended with the same pair of
    // calls -- the panel's own redraw and this box's listeners -- and a
    // scripted edit had already doubled one of them in every branch.
    void Announce()
    {
        OZS_Mirrors.Changed();
        m_OnChanged.Invoke(this);
    }

    // The authority says this item is here. The proxy is moved to match --
    // LOCAL, because PREDICTIVE does not work on a client-local container
    // (measured 2026-09-24).
    bool Place(EntityAI e, OZS_Row r)
    {
        if (!e || !m_Box)
            return false;
        EntityAI parent = m_Box;
        if (r.parent != 0)
        {
            parent = ByHandle(r.parent);
            if (!parent)
                return false;
        }
        InventoryLocation src = new InventoryLocation();
        if (!e.GetInventory().GetCurrentInventoryLocation(src))
            return false;
        // ALREADY THERE IS ALREADY DONE. Asking the engine to move an item to
        // the cell it is already in returns false, and most changes a proxy is
        // told are the echo of a move it made itself a moment ago.
        if (Matches(src, parent, r))
            return true;
        // Whose slot a magazine leaves: told below, once it has (SyncWeapon).
        EntityAI before = e.GetHierarchyParent();
        InventoryLocation dst = new InventoryLocation();
        if (r.lt == InventoryLocationType.ATTACHMENT)
            dst.SetAttachment(parent, e, r.slot);
        else
            dst.SetCargo(parent, e, 0, r.row, r.col, r.flip == 1);
        // THE PLACE IS EMPTIED BEFORE IT IS ASKED FOR. A move onto a cell
        // another item stands on answers true and does nothing -- measured in
        // the same client log where a move to an EMPTY cell reported itself
        // correctly, so it is the taken cell that the engine refuses, not the
        // reading that is unreliable (2026-09-25). Whoever stands there is
        // parked out of the way first; a swap always brings that item's own
        // change in the same burst, so the parking place is never drawn.
        if (r.lt == InventoryLocationType.CARGO && r.row >= 0)
            Vacate(parent, e, r);
        bool took = e.GetInventory().TakeToDst(InventoryMode.LOCAL, src, dst);
        // A local move is immediate, so both weapons -- the one the magazine
        // left and the one it landed on -- read their slots right now.
        if (Magazine.Cast(e))
        {
            SyncWeapon(before);
            SyncWeapon(parent);
        }
        // WHERE IT ACTUALLY WENT, NOT WHAT THE CALL SAID. `TakeToDst` has been
        // seen to answer true and leave the item where it was, and on a drop
        // onto a TAKEN cell it can leave two items lying over one another: the
        // panel then draws one of them and the other is gone from the screen
        // while still holding its cells, so nothing can be put there either.
        // That is what the owner saw as "the second item disappears and I
        // cannot use its cell" (2026-09-25).
        InventoryLocation now = new InventoryLocation();
        bool read = e.GetInventory().GetCurrentInventoryLocation(now);
        bool landed = false;
        if (read)
            landed = Matches(now, parent, r);
        // THE SAME WITNESS THE SERVER'S `Put` HAS, ON THIS SIDE TOO. Both
        // boxes logged "told #2 to 6,4" and neither complained, yet the client
        // still had the two cans the way round they were before: the proxy
        // reported a move it had not made, and there was nothing in the log
        // to say so (measured 2026-09-25). What was asked, what the call
        // answered, where the item really is.
        if (!landed)
        {
            string at = "unreadable";
            if (read)
                at = now.GetRow().ToString() + "," + now.GetCol().ToString() + " flip " + now.GetFlip().ToString();
            string want = r.row.ToString() + "," + r.col.ToString() + " flip " + (r.flip == 1).ToString();
            OZ_Log.Warn("storage: proxy: " + r.cls + " would not go to " + want + " (the move said " + took.ToString() + "); it is at " + at);
        }
        return landed;
    }

    // Move whoever stands on the rectangle this item is about to occupy.
    void Vacate(EntityAI parent, EntityAI e, OZS_Row r)
    {
        int w;
        int h;
        if (!OZS_Ops.SizeOf(e, w, h))
            return;
        // `SizeOf` answers for the way the item lies NOW. The row names the
        // way it is going to lie, and a can turned the other way covers a
        // different rectangle -- which is the pair the owner found broken:
        // two items turned the same way exchanged places, two turned
        // differently did not (2026-09-25).
        bool now = OZS_Ops.Flipped(e);
        bool then = r.flip == 1;
        if (now != then)
        {
            int turned = w;
            w = h;
            h = turned;
        }
        for (int row = r.row; row < r.row + h; row++)
        {
            for (int col = r.col; col < r.col + w; col++)
            {
                EntityAI sitting = OZS_Ops.Occupant(parent, row, col, e);
                if (!sitting)
                    continue;
                InventoryLocation from = new InventoryLocation();
                if (!sitting.GetInventory().GetCurrentInventoryLocation(from))
                    continue;
                InventoryLocation park = new InventoryLocation();
                if (!OZS_Ops.Somewhere(parent, sitting, r.row, r.col, w, h, park))
                    continue;
                sitting.GetInventory().TakeToDst(InventoryMode.LOCAL, from, park);
            }
        }
    }

    // Is this location the one the row describes?
    static bool Matches(InventoryLocation il, EntityAI parent, OZS_Row r)
    {
        if (il.GetParent() != parent)
            return false;
        if (il.GetType() != r.lt)
            return false;
        if (r.lt == InventoryLocationType.ATTACHMENT)
            return il.GetSlot() == r.slot;
        if (r.lt == InventoryLocationType.CARGO)
        {
            if (il.GetRow() != r.row)
                return false;
            if (il.GetCol() != r.col)
                return false;
            // AND THE SAME WAY ROUND. A cell is not a place on its own: an
            // item lying turned covers a different rectangle, so a proxy that
            // accepts "right cell, wrong orientation" believes it agrees with
            // the authority while the two boxes have different shapes in them.
            // The can was the only item in the test that flips, and the can
            // was the one that went out of step (owner, 2026-09-25).
            bool turned = il.GetFlip();
            bool wanted = r.flip == 1;
            return turned == wanted;
        }
        return true;
    }

    // Ask the server for this box from the beginning. The authority is the
    // truth; when the proxy's own guess was wrong, rebuilding from it is the
    // only way back to agreement, and one rebuild is cheaper than a picture
    // the player cannot trust.
    void Resync()
    {
        if (!m_Anchor)
            return;
        Ask(m_Anchor);
    }

    // ---- what the screen asks of the server ------------------------------

    // Move something inside the box. The proxy has already shown the player
    // its own guess; this is the word that will count.
    void Move(int handle, int into, int lt, int slot, int row, int col, int flip)
    {
        Send(OZS_Const.OP_MOVE, handle, into, 0, 0, lt, slot, row, col, flip);
    }

    // OUT CARRIES WHERE THE PLAYER PUT IT. The place they dropped it is the
    // whole point of the drag: a coat dragged into a backpack must land in
    // that backpack, not in the hands. The destination's container is one of
    // the player's own entities, so it travels by network id; lt/slot/row/col
    // say where inside it.
    void Out(int handle, EntityAI into, int lt, int slot, int row, int col, int flip)
    {
        int low = 0;
        int high = 0;
        if (into)
            into.GetNetworkID(low, high);
        Send(OZS_Const.OP_OUT, handle, 0, low, high, lt, slot, row, col, flip);
    }

    void Combine(int handle, int other)
    {
        Send(OZS_Const.OP_COMBINE, handle, other, 0, 0, InventoryLocationType.CARGO, -1, -1, -1, 0);
    }

    // TIDY THE WHOLE BOX. It names no item, so every argument is the empty
    // one; the server reads the layout it wants off the planner.
    void Sort()
    {
        Send(OZS_Const.OP_SORT, 0, 0, 0, 0, InventoryLocationType.CARGO, -1, -1, -1, 0);
    }

    // THE PLACE THE SCREEN CHOSE FOR THE DISPLACED ITEM TRAVELS WITH THE
    // OPERATION. Vanilla's forced swap is `ForceSwapEntities(item1, item2,
    // item2_dst)` -- the screen works out where the item being replaced
    // should go and hands it over. Throwing that argument away and letting
    // the server pick is why a bandage dropped on the middle of a rifle
    // landed on the rifle's CORNER: the only places the server knew were the
    // two root cells (owner, 2026-09-25). `row` below -1 means "the screen
    // did not name one", which is the ordinary swap.
    // THE CELL THE PLAYER AIMED AT TRAVELS TOO, IN `net`.
    //
    // An exchange used to carry no cell at all, so the only place the server
    // could put the dragged item was the OTHER ITEM'S ROOT -- and a can
    // dropped on the middle of a rifle jumped to the rifle's corner (owner,
    // 2026-09-25: "you move the item by its root and put it on a root"). For a
    // swap the two network id halves carry no network id -- there is none
    // inside a box -- so they carry the aimed row and column instead. Below
    // zero means the screen did not tell us, which is every swap the panel
    // decided on by itself.
    void Swap(int handle, int other, int aimRow, int aimCol, int lt, int slot, int row, int col, int flip)
    {
        Send(OZS_Const.OP_SWAP, handle, other, aimRow, aimCol, lt, slot, row, col, flip);
    }

    // ONE STACK BECOMES TWO, BOTH INSIDE THIS BOX.
    //
    // `into` is the container the new stack goes in -- the box itself, or
    // something standing in it. The kind says WHICH of vanilla's two splits
    // the screen asked for; see OZS_Const.SPLIT_HALF.
    //
    // Nothing is guessed on this side and nothing is drawn ahead of the
    // answer: a split makes an entity that only the authority can make, so
    // there is nothing for a proxy to guess AT. The new stack arrives as an
    // ordinary addition a moment later.
    void Split(EntityAI item, EntityAI into, int kind, int lt, int slot, int row, int col, int flip)
    {
        int handle = HandleOf(item);
        if (handle == 0)
            return;
        int where = 0;
        if (into && into != m_Box)
        {
            where = HandleOf(into);
            if (where == 0)
                return;
        }
        Send(OZS_Const.OP_SPLIT, handle, where, kind, 0, lt, slot, row, col, flip);
    }

    // An item of the player's own trades places with one in the box. The two
    // are named in different words on purpose: the one inside has no network
    // id to give, and the one outside has no handle in this box's record.
    void Across(EntityAI mine, int handle)
    {
        if (!mine)
            return;
        int low;
        int high;
        mine.GetNetworkID(low, high);
        Send(OZS_Const.OP_XSWAP, handle, 0, low, high, InventoryLocationType.CARGO, -1, -1, -1, 0);
    }

    // An item of the player's own goes in. It is named by its NETWORK id,
    // because on this side it is a real announced entity.
    void In(EntityAI mine, int into, int lt, int slot, int row, int col, int flip)
    {
        if (!mine)
            return;
        int low;
        int high;
        mine.GetNetworkID(low, high);
        Send(OZS_Const.OP_IN, 0, into, low, high, lt, slot, row, col, flip);
    }

    // TWO STACKS, ONE EACH SIDE (owner, 2026-09-26). The one in the box by
    // handle, the player's -- or the one at their feet -- by network id.
    // Nothing is drawn ahead of the answer: what changes is a count, and the
    // server tells the box's stack's count by a change and the player's own
    // is told by the engine.
    void StackIn(EntityAI giver, EntityAI taker)
    {
        int handle = HandleOf(taker);
        if (handle == 0 || !giver)
            return;
        int low;
        int high;
        giver.GetNetworkID(low, high);
        Send(OZS_Const.OP_STACK_IN, handle, 0, low, high, InventoryLocationType.CARGO, -1, -1, -1, 0);
    }

    void StackOut(EntityAI giver, EntityAI taker)
    {
        int handle = HandleOf(giver);
        if (handle == 0 || !taker)
            return;
        int low;
        int high;
        taker.GetNetworkID(low, high);
        Send(OZS_Const.OP_STACK_OUT, handle, 0, low, high, InventoryLocationType.CARGO, -1, -1, -1, 0);
    }

    // EVERY message of ours leaves on this client's own player. Not on the
    // object a message arrived on: a server -> client message arrives with its
    // target NULL (measured), so there is nothing there to answer to.
    protected void Send(int op, int handle, int other, int netLow, int netHigh, int lt, int slot, int row, int col, int flip)
    {
        Man me = GetGame().GetPlayer();
        if (!me)
            return;
        string asking = "op " + op.ToString() + " #" + handle.ToString() + "/" + other.ToString();
        asking = asking + " lt " + lt.ToString() + " slot " + slot.ToString() + " at " + row.ToString() + "," + col.ToString();
        OZ_Log.Dbg("storage: proxy: asking " + asking + " via " + OZS_Mirrors.s_Via);
        ScriptRPC rpc = new ScriptRPC();
        OZS_Wire.WriteOp(rpc, m_Id, op, handle, other, netLow, netHigh, lt, slot, row, col, flip, m_Version);
        rpc.Send(me, OZS_Const.RPC_PX_OP, true, null);
    }

    void Shut()
    {
        Man me = GetGame().GetPlayer();
        if (!me)
            return;
        ScriptRPC rpc = new ScriptRPC();
        rpc.Write(m_Id);
        rpc.Send(me, OZS_Const.RPC_PX_SHUT, true, null);
    }

    // Ask the server to show a box. Static, because there is no mirror yet.
    //
    // THE ANCHOR IS NAMED BY ITS NETWORK ID, NOT THE BOX BY ITS OWN. A box id
    // is a server-side thing -- the engine's persistent id, or a stash's pair
    // -- and the client has never been told it. The anchor is an object both
    // sides have, and asking through it also means a player can only ask for a
    // box that is in front of them.
    static void Ask(Object anchor)
    {
        Man me = GetGame().GetPlayer();
        if (!me || !anchor)
            return;
        OZS_Mirrors.Get().Asking(anchor);
        int low;
        int high;
        anchor.GetNetworkID(low, high);
        ScriptRPC rpc = new ScriptRPC();
        rpc.Write(low);
        rpc.Write(high);
        rpc.Send(me, OZS_Const.RPC_PX_OPEN, true, null);
    }

    // ---- what a drag of the vanilla screen means -------------------------

    // One move the player made with the ordinary inventory screen, once it is
    // known to touch this proxy. Three cases, and they are not symmetrical:
    //
    //   inside the box   the proxy moves AT ONCE and the server is asked to
    //                    agree -- this is the responsive case, and the one
    //                    §5 is about;
    //   out of the box   nothing is done here. The real item is not in this
    //                    proxy, it is on the server; it appears in the
    //                    player's hands when the server has moved and
    //                    announced it. Guessing would mean showing a copy.
    //   into the box     likewise: the item is the player's real, announced
    //                    entity, and the server unannounces it. A local guess
    //                    would be a second one.
    bool Drag(InventoryLocation src, InventoryLocation dst)
    {
        EntityAI item = src.GetItem();
        if (!item)
            return false;
        bool fromMe = OZS_Mirrors.Of(item) == this;
        bool toMe = OZS_Mirrors.At(dst) == this;
        if (fromMe && toMe)
            return Inside(item, dst);
        if (fromMe)
            return TakeOut(item, dst);
        if (toMe)
            return PutIn(item, dst);
        return false;
    }

    // WHICH WAY ROUND THE ITEM IS GOING, asked of the item and not only of the
    // place.
    //
    // The turn lives on the ITEM's inventory (GameInventory.SetFlipCargo), and
    // vanilla's own MakeDstForSwap reads it from there -- "the flip comes from
    // the item that is moving, never from the location it is moving into".
    // A drop carries it in the destination only once the item has been PUT
    // somewhere turned; a player who turns it in mid-drag and drops it
    // straight into the box hands us a destination with no flip at all, so we
    // wrote the item down as upright while their screen drew it lying across.
    // The two pictures then disagreed about which cells it covers, and every
    // drop near it was refused for reasons that made no sense on screen
    // (owner, 2026-09-26: "turn it without putting it down first, and it
    // counts as vertical").
    protected int FlipOf(EntityAI item, InventoryLocation dst)
    {
        if (dst && dst.GetFlip())
            return 1;
        if (item && item.GetInventory() && item.GetInventory().GetFlipCargo())
            return 1;
        return 0;
    }

    protected bool Inside(EntityAI item, InventoryLocation dst)
    {
        int handle = HandleOf(item);
        if (handle == 0)
            return false;
        int into = 0;
        if (dst.GetParent() != m_Box)
            into = HandleOf(dst.GetParent());
        int flip = FlipOf(item, dst);
        // A CELL SOMETHING ELSE IS STANDING ON IS NOT A PLACE TO DROP.
        //
        // The engine will happily put one item on top of another inside a
        // client-local container: both then hold the same cells, the panel
        // draws whichever it meets first, and the other is GONE from the
        // screen while still occupying its cells -- so nothing can be put
        // there either. The owner hit it twice in a row, with a can and then
        // with a rifle (2026-09-25). The server refuses such a move anyway, so
        // the guess was never going to survive; refusing it here means nothing
        // moves and nothing disappears.
        // A DROP ONTO A CELL SOMEBODY IS STANDING ON IS A SWAP. That is what
        // it means in the vanilla inventory, and the screen would ask
        // `CanSwapEntities` about it -- a native that refuses everything in a
        // container the engine has never been told about, so for a box the
        // screen never offers the swap at all and falls back to a plain move
        // onto a taken cell. The meaning is recognised from the CELL instead,
        // and sent as what it is. The server decides which of the two vanilla
        // swaps applies and may still refuse; the proxy draws nothing until it
        // answers, because a swap guessed here would take two moves to undo.
        if (dst.GetType() == InventoryLocationType.CARGO)
        {
            // WHOEVER IS UNDER THE ITEM'S WHOLE BODY, not just under the
            // cursor. A bandage dropped with its head on a free cell and its
            // tail over a can is a swap with the CAN; asking only about the
            // first cell sent it as a plain move and the can was pushed out.
            // In the orientation it is being DROPPED in, not the one it is
            // lying in now -- see OZS_Ops.SizeFor.
            EntityAI sitting = OZS_Ops.InTheWay(dst.GetParent(), item, dst.GetRow(), dst.GetCol(), flip);
            if (sitting)
            {
                int sits = HandleOf(sitting);
                if (sits == 0)
                    return true;
                // The cell under the item's own top-left corner, which is
                // where the player let go -- not the corner of whatever is
                // standing there.
                Swap(handle, sits, dst.GetRow(), dst.GetCol(), InventoryLocationType.CARGO, -1, -1, -1, 0);
                return true;
            }
        }
        // The picture first, the word from the server after. The proxy may be
        // wrong here and the server will say so (§5).
        OZS_Row want = new OZS_Row();
        want.Set(handle, into, dst.GetType(), dst.GetSlot(), dst.GetRow(), dst.GetCol(), flip, item.GetType());
        if (!Place(item, want))
            return true;
        m_Guessed = true;
        Move(handle, into, dst.GetType(), dst.GetSlot(), dst.GetRow(), dst.GetCol(), flip);
        return true;
    }


    // THE PLACE THE PLAYER DROPPED IT TRAVELS WITH THE OPERATION. `dst` is
    // already exactly what the vanilla screen computed -- the backpack they
    // aimed at, the vest pocket, the cell, the hands. Throwing it away and
    // letting the server find "somewhere" is how an item ends up in the hands
    // when the player put it into a backpack.
    protected bool TakeOut(EntityAI item, InventoryLocation dst)
    {
        int handle = HandleOf(item);
        if (handle == 0)
            return false;
        int flip = FlipOf(item, dst);
        Out(handle, dst.GetParent(), dst.GetType(), dst.GetSlot(), dst.GetRow(), dst.GetCol(), flip);
        return true;
    }

    protected bool PutIn(EntityAI item, InventoryLocation dst)
    {
        int into = 0;
        if (dst.GetParent() != m_Box)
            into = HandleOf(dst.GetParent());
        int flip = FlipOf(item, dst);
        In(item, into, dst.GetType(), dst.GetSlot(), dst.GetRow(), dst.GetCol(), flip);
        return true;
    }

    // The other shape the vanilla screen moves things in: "put this item into
    // that target", with the place either named or left to the engine. Every
    // PredictiveTakeEntityTo* method lands here, and from here it is the same
    // three cases as a drag.
    bool DragTo(EntityAI item, EntityAI target, int lt, int slot, int row, int col)
    {
        if (!item || !target)
            return false;
        InventoryLocation src = new InventoryLocation();
        if (!item.GetInventory().GetCurrentInventoryLocation(src))
            return false;
        InventoryLocation dst = new InventoryLocation();
        if (lt == InventoryLocationType.ATTACHMENT)
            dst.SetAttachment(target, item, slot);
        else if (row >= 0 && col >= 0)
            dst.SetCargo(target, item, 0, row, col, false);
        else if (!target.GetInventory().FindFreeLocationFor(item, FindInventoryLocationType.ANY, dst))
            return false;
        return Drag(src, dst);
    }

    // OUT OF THE BOX TO WHEREVER IT FITS ON THE PLAYER: the quick gesture
    // (Alt+click, and vanilla's PredictiveTakeEntityToInventory). This side
    // proposes a place the way vanilla's own call would -- the kind the
    // caller asked for first, then any -- and the server checks it as it
    // checks every named destination. No place this side can see is still
    // asked, with none named, so the answer is the server's word (a refusal
    // the player can read, or the hands) rather than silence.
    bool TakeInto(EntityAI item, EntityAI target, FindInventoryLocationType flags)
    {
        if (!item || !target)
            return false;
        int handle = HandleOf(item);
        if (handle == 0)
            return false;
        InventoryLocation src = new InventoryLocation();
        if (!item.GetInventory().GetCurrentInventoryLocation(src))
            return false;
        InventoryLocation dst = new InventoryLocation();
        if (target.GetInventory().FindFreeLocationFor(item, flags, dst))
            return Drag(src, dst);
        if (target.GetInventory().FindFreeLocationFor(item, FindInventoryLocationType.ANY, dst))
            return Drag(src, dst);
        Out(handle, target, InventoryLocationType.CARGO, -1, -1, -1, 0);
        return true;
    }

    // INTO THE BOX, WHEREVER IT FITS: the other half of the quick gesture.
    // The proxy proposes a place -- a cell, or a weapon slot for a rifle --
    // and the server checks it; a proxy that sees no room sends the item
    // anyway with no cell named, and the authority finds one or says the box
    // is full. Never the ground: with a box open the gesture means the box.
    bool PutInto(EntityAI item)
    {
        if (!item || !m_Box)
            return false;
        InventoryLocation src = new InventoryLocation();
        if (!item.GetInventory().GetCurrentInventoryLocation(src))
            return false;
        InventoryLocation dst = new InventoryLocation();
        if (m_Box.GetInventory().FindFreeLocationFor(item, FindInventoryLocationType.ANY, dst))
            return Drag(src, dst);
        In(item, 0, InventoryLocationType.CARGO, -1, -1, -1, 0);
        return true;
    }

    // Out of the box and onto the ground, in one operation. The destination is
    // named by its TYPE alone -- the server puts it at the player's feet,
    // because a ground position computed on a client is one more thing that
    // can be wrong by the time it arrives.
    bool DropOut(EntityAI item)
    {
        int handle = HandleOf(item);
        if (handle == 0)
            return false;
        Out(handle, null, InventoryLocationType.GROUND, -1, -1, -1, 0);
        return true;
    }

    // Two items changing places, both inside this proxy.
    // An ordinary swap: equal footprints, each into the other's place, and
    // the screen names no destination because none is needed.
    bool DragSwap(EntityAI a, EntityAI b)
    {
        int ha = HandleOf(a);
        int hb = HandleOf(b);
        if (ha == 0 || hb == 0)
            return false;
        // NO CELL IS CARRIED, AND VANILLA CARRIES NONE EITHER. `MakeDstForSwap`
        // gives each item the OTHER one's location and its own orientation
        // (itemmanager.c); the point the cursor was over never enters it. A
        // cell was carried here for a while, read off the screen, and it was
        // wrong -- measured from the container's root widget, which in this
        // mod's panel holds a title, a search field and buttons above the
        // grid, so a drop aimed at row 3 came out as row 10 (2026-09-25).
        Swap(ha, hb, -1, -1, InventoryLocationType.CARGO, -1, -1, -1, 0);
        return true;
    }

    // A forced swap: `a` takes `b`'s place and `b` goes where the SCREEN said,
    // which is the whole difference from the one above.
    bool DragForceSwap(EntityAI a, EntityAI b, InventoryLocation forB)
    {
        int ha = HandleOf(a);
        int hb = HandleOf(b);
        if (ha == 0 || hb == 0)
            return false;
        // THE SCREEN'S PLACE IS ONLY WORTH TAKING WHEN IT IS IN THIS BOX.
        //
        // Dropping a small item on a rifle makes the vanilla panel offer a
        // forced swap whose place for the rifle is a RESERVED SLOT ON THE
        // PLAYER: it does not know the box has weapon slots of its own, so it
        // looks where it can and finds your back. A swap inside a box must not
        // fling anything out of it (owner, 2026-09-25), so that answer is
        // dropped -- and nothing is put in its place. The operation goes with
        // NO place named, and the server, which is the only side that may
        // decide, finds one inside the box.
        if (!forB || OZS_Mirrors.At(forB) != this)
            return DragSwap(a, b);
        // fall through with the screen's own place for the displaced item, in
        // the orientation `b` is actually in (see FlipOf).
        int flip = FlipOf(b, forB);
        Swap(ha, hb, -1, -1, forB.GetType(), forB.GetSlot(), forB.GetRow(), forB.GetCol(), flip);
        return true;
    }

    // Two stacks becoming one, both inside this proxy. Never instant: the
    // client's own CombineItemsClient names items by network id and a proxy's
    // items have none, so the engine on the server does it and tells us.
    bool DragCombine(EntityAI into, EntityAI from)
    {
        int hi = HandleOf(into);
        int hf = HandleOf(from);
        if (hi == 0 || hf == 0)
            return false;
        Combine(hi, hf);
        return true;
    }

    void Refused(int handle, int version, string why)
    {
        OZ_Log.Warn("storage: proxy: box " + m_Id + " refused #" + handle.ToString() + " (" + why + "), the server is at v" + version.ToString());
        // THE PLAYER IS TOLD, NOT THE LOG. A refusal nobody sees is a box that
        // "just does not work": the owner dropped a bandage on a rifle, the
        // right thing happened -- nothing -- and there was no way to tell that
        // from a broken screen (2026-09-25). Only the reasons written as a
        // stringtable key are shown; the rest are our own words to ourselves.
        Say(why);
        // THE PROXY GUESSED AND THE SERVER SAID NO. Whatever the proxy drew
        // for that operation is now a picture of a box that does not exist,
        // and there is no undo: the only way back to agreement is to ask the
        // authority again. Refusals are rare; a wrong picture the player keeps
        // acting on is not.
        //
        // But only when it DID guess. A refused swap leaves nothing to undo.
        if (m_Guessed)
        {
            m_Guessed = false;
            Resync();
        }
        Announce();
    }

    // A refusal, in the player's own language, where they are looking.
    protected void Say(string why)
    {
        OZS_Mirrors.Tell(why);
    }

    // ---- handles ---------------------------------------------------------

    EntityAI ByHandle(int handle)
    {
        for (int i = 0; i < m_Handles.Count(); i++)
        {
            if (m_Handles.Get(i) == handle)
                return m_Items.Get(i);
        }
        return null;
    }

    int HandleOf(EntityAI e)
    {
        for (int i = 0; i < m_Items.Count(); i++)
        {
            if (m_Items.Get(i) == e)
                return m_Handles.Get(i);
        }
        return 0;
    }

    // `andDelete` also removes the entity: an item that left the box is not
    // just unnamed, it is not there.
    void Forget(int handle, bool andDelete)
    {
        for (int i = m_Handles.Count() - 1; i >= 0; i--)
        {
            EntityAI e = m_Items.Get(i);
            // A row whose entity the engine already took -- a child deleted
            // with its container -- goes whenever the table is walked
            // (review 2026-09-26, D5).
            if (!e)
            {
                m_Handles.RemoveOrdered(i);
                m_Items.RemoveOrdered(i);
                continue;
            }
            if (m_Handles.Get(i) != handle)
                continue;
            m_Handles.RemoveOrdered(i);
            m_Items.RemoveOrdered(i);
            if (andDelete)
            {
                // Hidden by name, not resynced: the deletion is deferred to
                // the end of the frame, and until then the weapon would
                // still answer that the magazine is attached (SyncWeapon).
                Weapon_Base wpn = Weapon_Base.Cast(e.GetHierarchyParent());
                if (wpn && Magazine.Cast(e))
                    wpn.HideMagazine();
                GetGame().ObjectDelete(e);
            }
        }
    }

    // Everything directly in the box, in the order the screen draws it.
    int Roots(array<EntityAI> into)
    {
        if (!m_Box || !m_Box.GetInventory())
            return 0;
        GameInventory inv = m_Box.GetInventory();
        int n = 0;
        int ac = inv.AttachmentCount();
        for (int a = 0; a < ac; a++)
        {
            EntityAI att = inv.GetAttachmentFromIndex(a);
            if (att)
            {
                into.Insert(att);
                n++;
            }
        }
        CargoBase cargo = inv.GetCargo();
        if (!cargo)
            return n;
        int cc = cargo.GetItemCount();
        for (int c = 0; c < cc; c++)
        {
            EntityAI item = cargo.GetItem(c);
            if (item)
            {
                into.Insert(item);
                n++;
            }
        }
        return n;
    }

    void Destroy()
    {
        for (int i = m_Items.Count() - 1; i >= 0; i--)
        {
            if (m_Items.Get(i))
                GetGame().ObjectDelete(m_Items.Get(i));
        }
        m_Items.Clear();
        m_Handles.Clear();
        if (m_Box)
            GetGame().ObjectDelete(m_Box);
        m_Box = null;
    }

    string Status()
    {
        string s = m_Id + " v" + m_Version.ToString() + " " + m_Items.Count().ToString() + "/" + m_Expected.ToString();
        s = s + " built in " + (m_Spent * 1000).ToString() + " ms over " + (m_Wall * 1000).ToString() + " ms";
        if (!m_Whole)
            s = s + " streaming";
        if (m_Missed > 0)
            s = s + " missed " + m_Missed.ToString();
        if (!m_Box)
            return s + " NO CONTAINER";
        return s + " netid " + m_Box.GetNetworkIDString();
    }
}
