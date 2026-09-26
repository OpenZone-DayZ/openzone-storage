// Client: the box's own panel in the inventory screen -- a search field, a
// Sort button, and a loading line while the box is still filling up.
//
// The bar belongs to the BOX, not to the player (owner 2026-09-17): it is
// created inside the body of the container the vicinity builds for an
// OZ_StorageBox, above the cargo grid, and the Sort button asks that box and
// no other. Typing shades every icon whose localized name does not contain
// the text; the shade is a dark panel created inside the icon above its item
// render, because the icon's own "Color" panel sits behind the render, the
// render ignores its widget colour, and a panel with style "blank" paints
// nothing at all (all three measured 2026-09-16).
#ifndef NO_GUI
class OZS_Search
{
    static string s_Text;
    static int    s_Version;
    // The query in the four spellings a name may use (typed, lower,
    // Capitalized, UPPER): the names are never case-folded themselves, so a
    // match costs a few IndexOf per icon and no engine casing touches
    // Cyrillic (see OZS_Case).
    protected static string s_Q0;
    protected static string s_Q1;
    protected static string s_Q2;
    protected static string s_Q3;

    static void Set(string text)
    {
        string t = text.Trim();
        if (t == s_Text)
            return;
        s_Text = t;
        s_Q0 = t;
        s_Q1 = OZS_Case.Lower(t);
        s_Q2 = OZS_Case.Capitalize(s_Q1);
        s_Q3 = OZS_Case.Upper(t);
        s_Version++;
    }

    static bool Active()
    {
        return s_Text != "";
    }

    // Re-shades every icon on the next frame without changing the query;
    // used when a container's contents changed instead of its text.
    static void Bump()
    {
        s_Version++;
    }

    // The query against one name, in the four spellings (see Set).
    protected static bool NameMatches(string n)
    {
        if (n.IndexOf(s_Q0) >= 0)
            return true;
        if (n.IndexOf(s_Q1) >= 0)
            return true;
        if (n.IndexOf(s_Q2) >= 0)
            return true;
        return n.IndexOf(s_Q3) >= 0;
    }

    // An entity matches by its own name, or by the name of anything it
    // holds, however deep: the pouch stays lit for the rag inside it, and
    // the player opens the pouch instead of the wrong one. PREORDER lists
    // the entity itself first; it is skipped, its name was asked already.
    static bool Matches(EntityAI e)
    {
        if (s_Text == "")
            return true;
        if (!e)
            return true;
        if (NameMatches(e.GetDisplayName()))
            return true;
        // A leaf holds nothing: no walk, no allocation.
        if (e.IsEmpty())
            return false;
        GameInventory inv = e.GetInventory();
        if (!inv)
            return false;
        ref array<EntityAI> inside = new array<EntityAI>();
        inv.EnumerateInventory(InventoryTraversalType.PREORDER, inside);
        for (int i = 0; i < inside.Count(); i++)
        {
            EntityAI k = inside.Get(i);
            if (k && k != e && NameMatches(k.GetDisplayName()))
                return true;
        }
        return false;
    }

    static Widget MakeShade(Widget parent)
    {
        if (!parent)
            return null;
        Widget w = GetGame().GetWorkspace().CreateWidgets("OpenZone_Storage/gui/layouts/ozs_shade.layout", parent);
        if (!w)
            ErrorEx("[OpenZone] storage: the shade layout could not be created", ErrorExSeverity.WARNING);
        return w;
    }
}

// One line of the count list: a class, how many entities of it, and for
// stackables (canBeSplit) the pieces they add up to.
class OZS_CountRow
{
    string name;
    int    n;
    bool   stack;
    float  qty;
}

// One bar, belonging to one box's container.
class OZS_BoxBar : ScriptedWidgetEventHandler
{
    protected OZ_StorageBox m_Box;
    protected Widget        m_Root;
    protected EditBoxWidget m_Edit;
    protected ButtonWidget  m_Sort;
    // The button is drawn by its children, the way the PDA's buttons are:
    // an edge, a face that lights up under the mouse, and the text.
    protected Widget        m_SortBg;
    protected TextWidget    m_SortText;
    protected TextWidget    m_Label;
    protected TextWidget    m_Loading;
    protected bool          m_WasLoading;
    protected int           m_WasCount = -1;
    protected ButtonWidget         m_Count;
    protected Widget               m_CountBg;
    protected TextWidget           m_CountText;
    protected Widget               m_CountPanel;
    protected MultilineTextWidget  m_CountList;
    protected bool                 m_CountOpen;
    // CountInventory() the list was built from; -1 = build on the next frame.
    protected int                  m_CountSeen = -1;
    // CountInventory() as of the last frame, open or not; -1 = not seen yet.
    protected int                  m_LastCount = -1;

    void OZS_BoxBar(OZ_StorageBox box, Widget parent)
    {
        m_Box = box;
        m_Root = GetGame().GetWorkspace().CreateWidgets("OpenZone_Storage/gui/layouts/ozs_search.layout", parent);
        if (!m_Root)
        {
            ErrorEx("[OpenZone] storage: the search bar layout could not be created", ErrorExSeverity.WARNING);
            return;
        }
        // Above the attachments (sort 1) and the cargo grid (sort 2).
        m_Root.SetSort(0);
        m_Edit = EditBoxWidget.Cast(m_Root.FindAnyWidget("Search"));
        m_Sort = ButtonWidget.Cast(m_Root.FindAnyWidget("Sort"));
        m_SortBg = m_Root.FindAnyWidget("SortBg");
        m_SortText = TextWidget.Cast(m_Root.FindAnyWidget("SortText"));
        m_Label = TextWidget.Cast(m_Root.FindAnyWidget("Label"));
        m_Loading = TextWidget.Cast(m_Root.FindAnyWidget("Loading"));
        m_Count = ButtonWidget.Cast(m_Root.FindAnyWidget("Count"));
        m_CountBg = m_Root.FindAnyWidget("CountBg");
        m_CountText = TextWidget.Cast(m_Root.FindAnyWidget("CountText"));
        m_CountPanel = m_Root.FindAnyWidget("CountPanel");
        m_CountList = MultilineTextWidget.Cast(m_Root.FindAnyWidget("CountList"));
        if (m_CountText)
            m_CountText.SetText(Widget.TranslateString("#STR_OZS_COUNT"));
        if (m_Label)
            m_Label.SetText(Widget.TranslateString("#STR_OZS_SEARCH"));
        if (m_SortText)
            m_SortText.SetText(Widget.TranslateString("#STR_OZS_SORT"));
        if (m_Edit)
            m_Edit.SetText(OZS_Search.s_Text);
        m_Root.SetHandler(this);
        Refresh();
    }

    void Destroy()
    {
        if (m_Root)
        {
            m_Root.Unlink();
            m_Root = null;
        }
    }

    // Called every frame by the container.
    void Refresh()
    {
        if (!m_Root || !m_Box)
            return;
        RefreshLoading();
        RefreshCount();
    }

    // While the box is OPENING the bar turns into a progress line and the
    // controls go away, so nobody types into a box that is still half there.
    protected void RefreshLoading()
    {
        bool loading = m_Box.OZS_GetState() == OZS_Const.STATE_OPENING;
        int have = 0;
        if (loading)
        {
            CargoBase cargo = m_Box.GetInventory().GetCargo();
            if (cargo)
                have = cargo.GetItemCount();
        }
        if (loading == m_WasLoading && have == m_WasCount)
            return;
        m_WasLoading = loading;
        m_WasCount = have;

        if (loading && m_CountOpen)
            ShowCount(false);
        if (m_Edit)
            m_Edit.Show(!loading);
        if (m_Sort)
            m_Sort.Show(!loading);
        if (m_Count)
            m_Count.Show(!loading);
        if (m_Label)
            m_Label.Show(!loading);
        if (m_Loading)
        {
            m_Loading.Show(loading);
            if (loading)
            {
                string s = Widget.TranslateString("#STR_OZS_LOADING");
                s = s + " " + have + " / " + m_Box.OZS_GetStoredCount();
                m_Loading.SetText(s);
            }
        }
    }

    // The panel opens and closes on the button; open, it is rebuilt only
    // when the box's inventory changed size (CountInventory is one native
    // call), never on a frame it did not.
    protected void ShowCount(bool open)
    {
        m_CountOpen = open;
        m_CountSeen = -1;
        if (!m_CountPanel)
            return;
        if (!open)
        {
            m_CountPanel.Show(false);
            m_Root.SetSize(1, OZS_Const.UI_BAR_PX);
            m_Root.Update();
            Widget above = m_Root.GetParent();
            if (above)
                above.Update();
        }
    }

    // Every frame: one native walk of the box (CountInventory) tells
    // whether anything moved. A live query is then re-applied, so a
    // pouch that lost its rag goes dark and one that gained it lights
    // up; the open panel is rebuilt on the same signal.
    protected void RefreshCount()
    {
        int now = m_Box.GetInventory().CountInventory();
        if (now != m_LastCount)
        {
            m_LastCount = now;
            if (OZS_Search.Active())
                OZS_Search.Bump();
        }
        if (!m_CountOpen || !m_CountPanel || !m_CountList)
            return;
        if (now == m_CountSeen)
            return;
        m_CountSeen = now;
        BuildCount();
    }

    // Most numerous first, then by name.
    protected bool Before(OZS_CountRow a, OZS_CountRow b)
    {
        if (a.n != b.n)
            return a.n > b.n;
        return NameLess(a.name, b.name);
    }

    // string has no Compare() and the engine has no comparator sort (see
    // OZS_Sorter), so the order is read out character by character through
    // ToAscii(), which the engine documents on the string's first character.
    protected bool NameLess(string a, string b)
    {
        int len = a.Length();
        if (b.Length() < len)
            len = b.Length();
        for (int i = 0; i < len; i++)
        {
            int ca = a.Substring(i, 1).ToAscii();
            int cb = b.Substring(i, 1).ToAscii();
            if (ca != cb)
                return ca < cb;
        }
        return a.Length() < b.Length();
    }

    protected void BuildCount()
    {
        ref array<EntityAI> all = new array<EntityAI>();
        m_Box.GetInventory().EnumerateInventory(InventoryTraversalType.PREORDER, all);
        ref map<string, ref OZS_CountRow> byType = new map<string, ref OZS_CountRow>();
        ref array<ref OZS_CountRow> rows = new array<ref OZS_CountRow>();
        int total = 0;
        for (int i = 0; i < all.Count(); i++)
        {
            EntityAI e = all.Get(i);
            // The weapons in the box's slots count too: they are in the box.
            if (!e || e == m_Box)
                continue;
            string t = e.GetType();
            ref OZS_CountRow row = byType.Get(t);
            if (!row)
            {
                row = new OZS_CountRow();
                row.name = e.GetDisplayName();
                byType.Insert(t, row);
                rows.Insert(row);
            }
            row.n = row.n + 1;
            total = total + 1;
            // Stackables by config (canBeSplit), never by the momentary state
            // of the stack; a magazine or an ammo pile keeps its pieces in the
            // ammo count, everything else in the quantity.
            ItemBase item = ItemBase.Cast(e);
            if (item && item.IsSplitable())
            {
                row.stack = true;
                Magazine mag = Magazine.Cast(e);
                if (mag)
                    row.qty = row.qty + mag.GetAmmoCount();
                else
                    row.qty = row.qty + item.GetQuantity();
            }
        }
        // Insertion sort: the list is a few dozen lines at most.
        for (int a = 1; a < rows.Count(); a++)
        {
            OZS_CountRow moving = rows.Get(a);
            int b = a - 1;
            while (b >= 0 && Before(moving, rows.Get(b)))
            {
                rows.Set(b + 1, rows.Get(b));
                b = b - 1;
            }
            rows.Set(b + 1, moving);
        }

        string text;
        int lines;
        if (rows.Count() == 0)
        {
            text = Widget.TranslateString("#STR_OZS_COUNT_EMPTY");
            lines = 1;
        }
        else
        {
            text = Widget.TranslateString("#STR_OZS_COUNT_TOTAL") + ": " + total.ToString();
            lines = 1;
            string pieces = Widget.TranslateString("#STR_OZS_COUNT_PIECES");
            for (int r = 0; r < rows.Count(); r++)
            {
                if (r == OZS_Const.UI_COUNT_LINES)
                {
                    int rest = rows.Count() - r;
                    text = text + "\n" + Widget.TranslateString("#STR_OZS_COUNT_MORE") + " " + rest.ToString();
                    lines = lines + 1;
                    break;
                }
                OZS_CountRow x = rows.Get(r);
                string line = x.n.ToString() + " x " + x.name;
                if (x.stack)
                    line = line + " (" + Math.Round(x.qty).ToString() + " " + pieces + ")";
                text = text + "\n" + line;
                lines = lines + 1;
            }
        }
        m_CountList.SetText(text);
        int tw = 0;
        int th = 0;
        m_CountList.GetTextSize(tw, th);
        int height = 8 + Math.Max(th, lines * OZS_Const.UI_LINE_PX);
        m_CountList.SetSize(440, height - 8);
        m_CountPanel.SetSize(1, height);
        m_CountPanel.Show(true);
        m_Root.SetSize(1, OZS_Const.UI_BAR_PX + height);
        m_CountList.Update();
        m_CountPanel.Update();
        m_Root.Update();
        Widget above = m_Root.GetParent();
        if (above)
            above.Update();
    }

    override bool OnChange(Widget w, int x, int y, bool finished)
    {
        if (w == m_Edit)
        {
            OZS_Search.Set(m_Edit.GetText());
            return true;
        }
        return false;
    }

    override bool OnClick(Widget w, int x, int y, int button)
    {
        if (w == m_Sort && m_Box)
        {
            // THROUGH THE PROXY'S OWN WIRE, not at the box on the ground.
            //
            // The placed box is never open under this scheme, and the sort's
            // first test was exactly "is it open" -- so a press on this button
            // was answered with "opening" and nothing happened, silently
            // (2026-09-26). The mirror is the thing that holds the contents,
            // so it is the thing that is asked to tidy them.
            OZS_Mirror mine = OZS_Mirrors.Of(m_Box);
            if (mine)
                mine.Sort();
            return true;
        }
        if (w == m_Count)
        {
            ShowCount(!m_CountOpen);
            return true;
        }
        return false;
    }

    override bool OnMouseEnter(Widget w, int x, int y)
    {
        if (w == m_Sort && m_SortBg)
            m_SortBg.SetColor(ARGBF(1, 0.22, 0.3, 0.38));
        if (w == m_Count && m_CountBg)
            m_CountBg.SetColor(ARGBF(1, 0.22, 0.3, 0.38));
        return false;
    }

    override bool OnMouseLeave(Widget w, Widget enterW, int x, int y)
    {
        if (w == m_Sort && m_SortBg)
            m_SortBg.SetColor(ARGBF(1, 0.135, 0.18, 0.225));
        if (w == m_Count && m_CountBg)
            m_CountBg.SetColor(ARGBF(1, 0.135, 0.18, 0.225));
        return false;
    }
}

// The two containers the vicinity builds for a ground container: with cargo
// only, and with cargo plus attachment slots. A storage box has weapon slots,
// so it is normally the second, but both are hooked.
modded class ContainerWithCargo
{
    protected ref OZS_BoxBar m_OZS_Bar;

    override void SetEntity(EntityAI entity, int cargo_index = 0, bool immedUpdate = true)
    {
        super.SetEntity(entity, cargo_index, immedUpdate);
        if (m_OZS_Bar)
            return;
        OZ_StorageBox box = OZ_StorageBox.Cast(entity);
        if (box)
            m_OZS_Bar = new OZS_BoxBar(box, GetMainWidget());
    }

    override void UpdateInterval()
    {
        super.UpdateInterval();
        if (m_OZS_Bar)
            m_OZS_Bar.Refresh();
    }
}

modded class ContainerWithCargoAndAttachments
{
    protected ref OZS_BoxBar m_OZS_Bar;

    override void SetEntity(EntityAI entity, bool immedUpdate = true)
    {
        super.SetEntity(entity, immedUpdate);
        // An entity without a cargo grid names the closable header from
        // vanilla's spoiled uppercase (see OZS_Headers.c); name it again.
        if (m_Entity && m_ClosableHeader && !(m_Entity.CanDisplayCargo() && m_Entity.GetInventory().GetCargo()))
            m_ClosableHeader.SetName(m_Entity.GetDisplayName());
        if (m_OZS_Bar)
            return;
        OZ_StorageBox box = OZ_StorageBox.Cast(entity);
        if (box)
            m_OZS_Bar = new OZS_BoxBar(box, GetMainWidget());
    }

    override void UpdateInterval()
    {
        super.UpdateInterval();
        if (m_OZS_Bar)
            m_OZS_Bar.Refresh();
    }
}

// The query belongs to one visit to the inventory screen.
modded class InventoryMenu
{
    override void OnHide()
    {
        super.OnHide();
        OZS_Search.Set("");
    }
}

// Cargo icons: the match is computed when the query or the item changes,
// the shade is painted above the item render.
modded class Icon
{
    protected int    m_OZS_Applied = -1;
    protected Widget m_OZS_ShadeW;

    override void Init(EntityAI obj)
    {
        super.Init(obj);
        m_OZS_Applied = -1;
        OZS_Shade();
    }

    override void UpdateInterval()
    {
        super.UpdateInterval();
        if (m_OZS_Applied != OZS_Search.s_Version)
            OZS_Shade();
    }

    protected void OZS_Shade()
    {
        m_OZS_Applied = OZS_Search.s_Version;
        bool shaded = OZS_Search.Active() && !OZS_Search.Matches(m_Obj);
        if (shaded && !m_OZS_ShadeW)
            m_OZS_ShadeW = OZS_Search.MakeShade(GetMainWidget());
        if (m_OZS_ShadeW)
            m_OZS_ShadeW.Show(shaded);
    }
}

// Attachment slots (the box's weapon slots, worn gear): the same shade,
// inside the slot's own panel.
modded class SlotsIcon
{
    protected int    m_OZS_Applied = -1;
    protected Widget m_OZS_ShadeW;

    override void Init(EntityAI obj, bool reservation = false)
    {
        super.Init(obj, reservation);
        m_OZS_Applied = -1;
        OZS_Shade();
    }

    override void Clear()
    {
        super.Clear();
        if (m_OZS_ShadeW)
            m_OZS_ShadeW.Show(false);
    }

    override void UpdateInterval()
    {
        super.UpdateInterval();
        if (m_OZS_Applied != OZS_Search.s_Version)
            OZS_Shade();
    }

    protected void OZS_Shade()
    {
        m_OZS_Applied = OZS_Search.s_Version;
        bool shaded = m_Obj && OZS_Search.Active() && !OZS_Search.Matches(m_Obj);
        if (shaded && !m_OZS_ShadeW)
            m_OZS_ShadeW = OZS_Search.MakeShade(m_PanelWidget);
        if (m_OZS_ShadeW)
            m_OZS_ShadeW.Show(shaded);
    }
}
#endif
