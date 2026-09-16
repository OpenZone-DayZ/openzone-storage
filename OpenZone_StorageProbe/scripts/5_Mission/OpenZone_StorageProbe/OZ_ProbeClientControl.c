// Stand only, client side: a file the stand writes and the client polls once
// a second, because the bridge cannot reach the client and a headless stand
// has no mouse for the inventory screen's widgets.
//
//   $profile:OpenZone_StorageProbe/control.txt, first line:
//     search <text>   -- as if typed into the storage search bar
//     clear           -- empty the search
//     sort [n]        -- press the Sort button (change n to press again)
//     inventory [n]   -- open the inventory screen
#ifndef NO_GUI
class OZ_ProbeClientControl
{
    static const string FILE = "$profile:OpenZone_StorageProbe/control.txt";

    protected float  m_Timer;
    protected string m_Last;

    void OnFrame(float timeslice)
    {
        m_Timer = m_Timer + timeslice;
        if (m_Timer < 1.0)
            return;
        m_Timer = 0;
        if (!FileExist(FILE))
            return;
        FileHandle fh = OpenFile(FILE, FileMode.READ);
        if (fh == 0)
            return;
        string line;
        FGets(fh, line);
        CloseFile(fh);
        line = line.Trim();
        if (line == m_Last)
            return;
        m_Last = line;
        Apply(line);
    }

    protected void Apply(string line)
    {
        // WARNING, because INFO never reaches the retail client's .RPT.
        if (line.IndexOf("search ") == 0)
        {
            OZS_Search.Set(line.Substring(7, line.Length() - 7));
            ErrorEx("[OpenZone] probe control: search [" + OZS_Search.s_Text + "] version " + OZS_Search.s_Version, ErrorExSeverity.WARNING);
        }
        else if (line == "clear")
        {
            OZS_Search.Set("");
            ErrorEx("[OpenZone] probe control: search cleared", ErrorExSeverity.WARNING);
        }
        else if (line.IndexOf("sort") == 0)
        {
            OZS_ClientViewer.Get().RequestSort();
            ErrorEx("[OpenZone] probe control: sort requested", ErrorExSeverity.WARNING);
        }
        else if (line.IndexOf("inventory") == 0)
        {
            GetGame().GetMission().ShowInventory();
            ErrorEx("[OpenZone] probe control: inventory shown", ErrorExSeverity.WARNING);
        }
    }
}
#endif
