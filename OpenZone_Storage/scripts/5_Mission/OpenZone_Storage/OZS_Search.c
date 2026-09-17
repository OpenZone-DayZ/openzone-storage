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

    static bool Matches(EntityAI e)
    {
        if (s_Text == "")
            return true;
        if (!e)
            return true;
        string n = e.GetDisplayName();
        if (n.IndexOf(s_Q0) >= 0)
            return true;
        if (n.IndexOf(s_Q1) >= 0)
            return true;
        if (n.IndexOf(s_Q2) >= 0)
            return true;
        return n.IndexOf(s_Q3) >= 0;
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

// One bar, belonging to one box's container.
class OZS_BoxBar : ScriptedWidgetEventHandler
{
    protected OZ_StorageBox m_Box;
    protected Widget        m_Root;
    protected EditBoxWidget m_Edit;
    protected ButtonWidget  m_Sort;
    protected TextWidget    m_Label;
    protected TextWidget    m_Loading;
    protected bool          m_WasLoading;
    protected int           m_WasCount = -1;

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
        m_Label = TextWidget.Cast(m_Root.FindAnyWidget("Label"));
        m_Loading = TextWidget.Cast(m_Root.FindAnyWidget("Loading"));
        if (m_Label)
            m_Label.SetText(Widget.TranslateString("#STR_OZS_SEARCH"));
        if (m_Sort)
            m_Sort.SetText(Widget.TranslateString("#STR_OZS_SORT"));
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

    // Called every frame by the container. While the box is OPENING the bar
    // turns into a progress line and the controls go away, so nobody types
    // into a box that is still half there.
    void Refresh()
    {
        if (!m_Root || !m_Box)
            return;
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

        if (m_Edit)
            m_Edit.Show(!loading);
        if (m_Sort)
            m_Sort.Show(!loading);
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
            m_Box.RPCSingleParam(OZS_Const.RPC_SORT_ID, new Param1<bool>(true), true);
            return true;
        }
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
