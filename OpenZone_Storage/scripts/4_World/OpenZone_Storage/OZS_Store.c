// The wire of a box (design 2026-09-19, sections 1 and 2): one file per
// close in the exchange directory, written by OZS_StoreWriter and handed to
// the bridge by name; one cache per closed box, written by the bridge and
// read by the open job. The truth of a closed box is the bridge's SQL, the
// truth of an open one is the engine's cargo; a file is only the wire,
// because an OnStoreSave body leaves the script VM through FileSerializer
// and no other way.
class OZS_Store
{
    static string XchgPath(string name)
    {
        return OZS_Const.DIR_XCHG + "\\" + name;
    }

    // "<boxId>-<YYYYMMDD-HHMMSS>.bin": the only close-file name the bridge
    // accepts. Windows forbids ':' in a name, hence the second stamp format.
    static string CloseName(string id)
    {
        return id + "-" + FileStamp() + ".bin";
    }

    static void EnsureDirs()
    {
        MakeDirectory(OZS_Const.DIR);
        MakeDirectory(OZS_Const.DIR_XCHG);
    }

    // The engine's persistent id as "b1-b2-b3-b4": valid the frame the
    // entity is created and identical after a save and a boot (measured
    // 2026-09-19). Empty while the engine has not given one.
    static string PersistentIdOf(EntityAI e)
    {
        if (!e)
            return "";
        int b1;
        int b2;
        int b3;
        int b4;
        e.GetPersistentID(b1, b2, b3, b4);
        if (b1 == 0 && b2 == 0 && b3 == 0 && b4 == 0)
            return "";
        return b1.ToString() + "-" + b2.ToString() + "-" + b3.ToString() + "-" + b4.ToString();
    }

    // UTC "YYYY-MM-DD HH:MM:SS": headers, events and the bridge's SQL.
    static string Stamp()
    {
        int y;
        int mo;
        int d;
        int h;
        int mi;
        int s;
        GetYearMonthDayUTC(y, mo, d);
        GetHourMinuteSecondUTC(h, mi, s);
        string t = y.ToString() + "-" + Pad2(mo) + "-" + Pad2(d);
        t = t + " " + Pad2(h) + ":" + Pad2(mi) + ":" + Pad2(s);
        return t;
    }

    // UTC "YYYYMMDD-HHMMSS": file names. Derived from Stamp() by dropping
    // the punctuation, so the two stamps of one close can never disagree
    // (a build of this engine once printed the year of the direct call as
    // one digit, which the bridge's name check refused).
    static string FileStamp()
    {
        string t = Stamp();
        t.Replace("-", "");
        t.Replace(":", "");
        t.Replace(" ", "-");
        return t;
    }

    static string Pad2(int v)
    {
        if (v < 10)
            return "0" + v;
        return "" + v;
    }

    static int ReadHeader(FileSerializer f, out int saveVer, out string stamp, out string boxClass, out string boxId, out int roots, out int entities, out string why)
    {
        int ver;
        if (!f.Read(ver))
        {
            why = "the header cannot be read";
            return 0;
        }
        if (ver != OZS_Const.BIN_VERSION)
        {
            why = "format version " + ver.ToString() + " is not " + OZS_Const.BIN_VERSION.ToString();
            return 0;
        }
        if (!f.Read(saveVer) || !f.Read(stamp) || !f.Read(boxClass) || !f.Read(boxId) || !f.Read(roots) || !f.Read(entities))
        {
            why = "the header is cut short";
            return 0;
        }
        if (roots < 0 || entities < 0)
        {
            why = "the header counts are negative";
            return 0;
        }
        return 1;
    }
}

// Writes one close file: the header with a fresh random marker, every root
// followed by the marker, BIN_END. Finish() closes the file and the bridge
// is told its name; Abort() deletes it. Nothing here is atomic on purpose:
// while the file is being written nobody knows it exists, and once the
// bridge is told, the writer never touches it again (design section 3).
class OZS_StoreWriter
{
    protected string m_Id;
    protected string m_Name;
    protected string m_Path;
    protected string m_Stamp;
    protected ref FileSerializer m_File;
    protected int m_M0;
    protected int m_M1;
    protected int m_M2;
    protected int m_M3;
    protected int m_Expected;
    protected int m_Written;
    protected int m_RootsWritten;
    protected bool m_Open;

    bool Open(OZ_StorageBox box, int roots, int entities, out string why)
    {
        m_Id = box.OZS_GetId();
        if (m_Id == "")
        {
            why = "the box has no id";
            return false;
        }
        OZS_Store.EnsureDirs();
        m_Stamp = OZS_Store.Stamp();
        m_Name = OZS_Store.CloseName(m_Id);
        m_Path = OZS_Store.XchgPath(m_Name);
        m_File = new FileSerializer();
        if (!m_File.Open(m_Path, FileMode.WRITE))
        {
            m_File = null;
            why = "cannot write " + m_Path;
            return false;
        }
        m_M0 = Math.RandomInt(1, 2147483647);
        m_M1 = Math.RandomInt(1, 2147483647);
        m_M2 = Math.RandomInt(1, 2147483647);
        m_M3 = Math.RandomInt(1, 2147483647);
        m_Expected = entities;
        m_Written = 0;
        m_RootsWritten = 0;
        m_Open = true;
        m_File.Write(OZS_Const.BIN_VERSION);
        m_File.Write(GetGame().SaveVersion());
        m_File.Write(m_Stamp);
        m_File.Write(box.GetType());
        m_File.Write(m_Id);
        m_File.Write(roots);
        m_File.Write(entities);
        WriteMarker();
        return true;
    }

    protected void WriteMarker()
    {
        m_File.Write(m_M0);
        m_File.Write(m_M1);
        m_File.Write(m_M2);
        m_File.Write(m_M3);
    }

    void WriteRoot(EntityAI e, int newRow = -1, int newCol = -1)
    {
        if (!m_Open || !e)
            return;
        m_RootsWritten++;
        m_Written = m_Written + OZS_Records.WriteRoot(m_File, e, newRow, newCol);
        WriteMarker();
    }

    string Stamp()
    {
        return m_Stamp;
    }

    string Name()
    {
        return m_Name;
    }

    int Written()
    {
        return m_Written;
    }

    int Roots()
    {
        return m_RootsWritten;
    }

    bool Finish(out string why)
    {
        if (!m_Open)
        {
            why = "the store is not open";
            return false;
        }
        if (m_Written != m_Expected)
            OZ_Log.Warn("storage: box " + m_Id + " counted " + m_Expected.ToString() + " entities but wrote " + m_Written.ToString());
        m_File.Write(OZS_Const.BIN_END);
        m_File.Close();
        m_File = null;
        m_Open = false;
        return true;
    }

    // The file is ours until the bridge is told about it; a close that
    // fails before or after that point removes it (after: only when the
    // bridge refused, so it never read it into SQL).
    void Abort()
    {
        if (m_File)
        {
            m_File.Close();
            m_File = null;
        }
        m_Open = false;
        if (m_Path != "" && FileExist(m_Path))
            DeleteFile(m_Path);
    }
}
