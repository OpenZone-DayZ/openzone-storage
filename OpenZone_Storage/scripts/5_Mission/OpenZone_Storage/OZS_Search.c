// Client: the search bar on the inventory screen and the sort button.
//
// The bar (gui/layouts/ozs_search.layout) hangs off the inventory's root
// widget while the screen is shown. Typing filters live: every item icon
// whose localized display name does not contain the text has its item
// render tinted dark (the icon's "Color" panel sits behind the render and
// cannot shade it -- measured 2026-09-16); the tint is re-applied every
// update while a search is active. The Sort button asks the nearest open
// box in the vicinity list to sort itself (an entity RPC, like the viewer
// heartbeat).
#ifndef NO_GUI
class OZS_Search
{
    static string s_Text;
    static int    s_Version;
    // The first few shades of a session are logged, for the stand.
    static int    s_Diag;
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

    // The shade is a dark panel created inside the icon above its item
    // render (gui/layouts/ozs_shade.layout, priority 500 against the
    // render's 151): the icon's own "Color" panel sits behind the render, and
    // the render ignores its widget colour (both measured 2026-09-16).
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

class OZS_SearchBar : ScriptedWidgetEventHandler
{
    protected Widget        m_Root;
    protected EditBoxWidget m_Edit;
    protected ButtonWidget  m_Sort;
    protected TextWidget    m_Label;

    void OZS_SearchBar(Widget parent)
    {
        m_Root = GetGame().GetWorkspace().CreateWidgets("OpenZone_Storage/gui/layouts/ozs_search.layout", parent);
        if (!m_Root)
        {
            OZ_Log.Warn("storage: the search bar layout could not be created");
            return;
        }
        m_Edit = EditBoxWidget.Cast(m_Root.FindAnyWidget("Search"));
        m_Sort = ButtonWidget.Cast(m_Root.FindAnyWidget("Sort"));
        m_Label = TextWidget.Cast(m_Root.FindAnyWidget("Label"));
        if (m_Label)
            m_Label.SetText(Widget.TranslateString("#STR_OZS_SEARCH"));
        if (m_Sort)
            m_Sort.SetText(Widget.TranslateString("#STR_OZS_SORT"));
        if (m_Edit)
            m_Edit.SetText(OZS_Search.s_Text);
        m_Root.SetHandler(this);
    }

    void Destroy()
    {
        if (m_Root)
        {
            m_Root.Unlink();
            m_Root = null;
        }
        OZS_Search.Set("");
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
        if (w == m_Sort)
        {
            OZS_ClientViewer.Get().RequestSort();
            return true;
        }
        return false;
    }
}

modded class InventoryMenu
{
    protected ref OZS_SearchBar m_OZS_Bar;

    override void OnShow()
    {
        super.OnShow();
        if (!m_OZS_Bar && layoutRoot)
            m_OZS_Bar = new OZS_SearchBar(layoutRoot);
    }

    override void OnHide()
    {
        super.OnHide();
        if (m_OZS_Bar)
        {
            m_OZS_Bar.Destroy();
            m_OZS_Bar = null;
        }
    }
}

// Cargo icons: the match is computed when the query or the item changes,
// the shade is painted on every update while a search is on (vanilla resets
// the colour panel on hover).
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
        {
            m_OZS_ShadeW = OZS_Search.MakeShade(GetMainWidget());
            if (m_OZS_ShadeW && m_Obj && OZS_Search.s_Diag < 3)
            {
                OZS_Search.s_Diag++;
                ErrorEx("[OpenZone] storage: shade on " + m_Obj.GetDisplayName(), ErrorExSeverity.WARNING);
            }
        }
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
