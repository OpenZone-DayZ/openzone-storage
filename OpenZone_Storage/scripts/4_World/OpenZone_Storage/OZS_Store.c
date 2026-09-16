// The file layer of a box: $profile:OpenZone/Storage/<box id>/items.bin and
// items.list, written together on every Close, read on Open and at boot.
//
// Writing is paced: the FileSerializer writes about 0.17 ms per entity
// (measured 2026-09-16: 1430 entities = 238 ms in one frame), so the close
// job writes a frame's budget of roots at a time through OZS_StoreWriter.
// Both files are written as <name>.new and copied over the live names at the
// end, and the OLD live files are deleted when the writer opens -- so while a
// close is under way the box has no files, and the boot rule "no files: the
// engine's own cargo is the truth" (spec section 7) covers a crash at any
// point of it.
class OZS_Store
{
    static string BoxDir(string id)
    {
        return OZS_Const.DIR + "\\" + id;
    }

    static string BinPath(string id)
    {
        return BoxDir(id) + "\\" + OZS_Const.FILE_BIN;
    }

    static string ListPath(string id)
    {
        return BoxDir(id) + "\\" + OZS_Const.FILE_LIST;
    }

    static bool HasFiles(string id)
    {
        return FileExist(BinPath(id)) || FileExist(ListPath(id));
    }

    // Removes both files (the engine holds the truth again).
    static void Delete(string id)
    {
        if (FileExist(BinPath(id)))
            DeleteFile(BinPath(id));
        if (FileExist(ListPath(id)))
            DeleteFile(ListPath(id));
    }

    // "2026-09-16 17:31:18" in UTC.
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

    // "20260916-173118": the stamp without the characters a file name refuses.
    static string FileStamp()
    {
        int y;
        int mo;
        int d;
        int h;
        int mi;
        int s;
        GetYearMonthDayUTC(y, mo, d);
        GetHourMinuteSecondUTC(h, mi, s);
        string t = y.ToString() + Pad2(mo) + Pad2(d) + "-" + Pad2(h) + Pad2(mi) + Pad2(s);
        return t;
    }

    static string Pad2(int v)
    {
        if (v < 10)
            return "0" + v;
        return "" + v;
    }

    // .new over the live name. CopyFile refuses to overwrite on some builds;
    // then the live file goes first.
    static bool Commit(string tmp, string live, out string why)
    {
        bool ok = CopyFile(tmp, live);
        if (!ok && FileExist(live))
        {
            DeleteFile(live);
            ok = CopyFile(tmp, live);
        }
        if (!ok)
        {
            why = "cannot copy " + tmp + " over " + live;
            return false;
        }
        DeleteFile(tmp);
        return true;
    }

    // The first line of items.list, or "" -- the cheap way to say what a
    // store holds without opening the blob.
    static string ListHeader(string id)
    {
        FileHandle fh = OpenFile(ListPath(id), FileMode.READ);
        if (fh == 0)
            return "";
        string line;
        FGets(fh, line);
        CloseFile(fh);
        return line;
    }

    // The root count from the items.list header, or -1.
    static int HeaderRoots(string id)
    {
        string head = ListHeader(id);
        if (head == "")
            return -1;
        array<string> p = new array<string>();
        head.Split("|", p);
        if (p.Count() < 8 || p.Get(0) != OZS_Const.LIST_HEAD)
            return -1;
        return p.Get(6).ToInt();
    }

    // Lines in items.list beyond the header, or -1.
    static int ListLines(string id)
    {
        FileHandle fh = OpenFile(ListPath(id), FileMode.READ);
        if (fh == 0)
            return -1;
        string line;
        int n = -1;
        while (FGets(fh, line) > 0)
            n++;
        CloseFile(fh);
        return n;
    }
}

// One store being written, a root at a time, across frames.
class OZS_StoreWriter
{
    protected string m_Id;
    protected string m_BinLive;
    protected string m_ListLive;
    protected string m_BinNew;
    protected string m_ListNew;
    protected ref FileSerializer m_Bin;
    protected FileHandle m_List;
    protected int m_Expected;
    protected int m_Written;
    protected bool m_Open;

    // Deletes the old live files, opens the .new pair and writes the
    // headers. False leaves nothing behind and says why.
    bool Open(OZ_StorageBox box, int roots, int entities, out string why)
    {
        m_Id = box.OZS_GetId();
        if (m_Id == "")
        {
            why = "the box has no id";
            return false;
        }
        MakeDirectory(OZS_Const.DIR);
        MakeDirectory(OZS_Store.BoxDir(m_Id));
        m_BinLive = OZS_Store.BinPath(m_Id);
        m_ListLive = OZS_Store.ListPath(m_Id);
        m_BinNew = m_BinLive + OZS_Const.FILE_NEW;
        m_ListNew = m_ListLive + OZS_Const.FILE_NEW;
        m_Expected = entities;
        m_Written = 0;

        int saveVer = GetGame().SaveVersion();
        string stamp = OZS_Store.Stamp();

        m_Bin = new FileSerializer();
        if (!m_Bin.Open(m_BinNew, FileMode.WRITE))
        {
            why = "cannot write " + m_BinNew;
            return false;
        }
        m_List = OpenFile(m_ListNew, FileMode.WRITE);
        if (m_List == 0)
        {
            m_Bin.Close();
            DeleteFile(m_BinNew);
            why = "cannot write " + m_ListNew;
            return false;
        }
        m_Open = true;

        // From here on the box has no store: the engine's cargo is the truth
        // until Commit.
        OZS_Store.Delete(m_Id);

        m_Bin.Write(OZS_Const.BIN_VERSION);
        m_Bin.Write(saveVer);
        m_Bin.Write(stamp);
        m_Bin.Write(box.GetType());
        m_Bin.Write(m_Id);
        m_Bin.Write(roots);
        m_Bin.Write(entities);

        string head = OZS_Const.LIST_HEAD + "|1|" + saveVer + "|" + stamp + "|" + box.GetType();
        head = head + "|" + m_Id + "|" + roots + "|" + entities;
        FPrintln(m_List, head);
        return true;
    }

    // One root entity with everything under it, into both files.
    void WriteRoot(EntityAI e)
    {
        if (!m_Open || !e)
            return;
        m_Written = m_Written + OZS_Records.WriteEntity(m_Bin, e);
        OZS_Records.WriteListEntity(m_List, e, 0);
    }

    int Written()
    {
        return m_Written;
    }

    // Trailer, close, and the .new pair over the live names.
    bool Commit(out string why)
    {
        if (!m_Open)
        {
            why = "the store is not open";
            return false;
        }
        if (m_Written != m_Expected)
            OZ_Log.Warn("storage: box " + m_Id + " counted " + m_Expected + " entities but wrote " + m_Written);
        m_Bin.Write(OZS_Const.BIN_END);
        m_Bin.Close();
        CloseFile(m_List);
        m_Open = false;
        if (!OZS_Store.Commit(m_BinNew, m_BinLive, why))
            return false;
        if (!OZS_Store.Commit(m_ListNew, m_ListLive, why))
            return false;
        return true;
    }

    // Closes and removes the .new pair; the live names stay absent.
    void Abort()
    {
        if (!m_Open)
            return;
        m_Bin.Close();
        CloseFile(m_List);
        m_Open = false;
        if (FileExist(m_BinNew))
            DeleteFile(m_BinNew);
        if (FileExist(m_ListNew))
            DeleteFile(m_ListNew);
    }
}
