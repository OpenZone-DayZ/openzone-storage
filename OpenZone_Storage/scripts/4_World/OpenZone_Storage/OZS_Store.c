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

    static string RootsDir(string id)
    {
        return BoxDir(id) + "\\" + OZS_Const.DIR_ROOTS;
    }

    // roots/0000.bin, roots/0001.bin ... one per root, in the order of the
    // list.
    static string RootPath(string id, int n)
    {
        return RootsDir(id) + "\\" + n.ToStringLen(4) + ".bin";
    }

    // Root files present, counted from 0 up to the first gap.
    static int RootCount(string id)
    {
        int n = 0;
        while (FileExist(RootPath(id, n)))
            n++;
        return n;
    }

    static bool HasFiles(string id)
    {
        return FileExist(BinPath(id)) || FileExist(ListPath(id));
    }

    // Removes the index, the list and every root file (the engine holds the
    // truth again).
    static void Delete(string id)
    {
        if (FileExist(BinPath(id)))
            DeleteFile(BinPath(id));
        if (FileExist(ListPath(id)))
            DeleteFile(ListPath(id));
        DeleteRoots(id);
    }

    static int DeleteRoots(string id)
    {
        int n = 0;
        while (FileExist(RootPath(id, n)))
        {
            DeleteFile(RootPath(id, n));
            n++;
        }
        return n;
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

    // The stamp from the items.list header, or "" when there is no readable
    // header. Both files are written with the SAME stamp in one OZS_StoreWriter
    // pass, so a pair whose stamps differ is not a pair: it is a new file next
    // to a survivor of an older commit, which can only happen when the delete
    // of the old live file failed. Splicing such a list onto a truncated blob
    // would duplicate or lose items, so the open job refuses it.
    static string ListStamp(string id)
    {
        string head = ListHeader(id);
        if (head == "")
            return "";
        array<string> p = new array<string>();
        head.Split("|", p);
        if (p.Count() < 8 || p.Get(0) != OZS_Const.LIST_HEAD)
            return "";
        return p.Get(3);
    }

    // A box that left the world leaves its files behind; this marks the
    // directory so an admin can tell an orphan store from a live one without
    // reading the log. Appends, so a directory reused later keeps its history.
    // Moves the store of a box that left the world into DIR_REMOVED/<id>/
    // and writes removed.txt on top of it there (owner 2026-09-19): the
    // files stay readable for an admin, and the live tree holds live boxes
    // only. Returns how many files went; 0 for a box that never had a store.
    static int Archive(string id, string text)
    {
        if (id == "")
            return 0;
        string from = BoxDir(id);
        string to = OZS_Const.DIR_REMOVED + "\\" + id;
        MakeDirectory(OZS_Const.DIR_REMOVED);
        int moved = MoveFiles(from, to, id);
        moved = moved + MoveFiles(RootsDir(id), to + "\\" + OZS_Const.DIR_ROOTS, id);
        if (moved == 0)
            return 0;
        FileHandle mark = OpenFile(to + "\\" + OZS_Const.FILE_REMOVED, FileMode.APPEND);
        if (mark != 0)
        {
            FPrintln(mark, Stamp() + " " + text);
            CloseFile(mark);
        }
        // DeleteFile removes an emptied directory too (measured 2026-09-19).
        DeleteFile(RootsDir(id));
        bool dirGone = DeleteFile(from);
        OZ_Log.Info("storage: box " + id + " store archived: " + moved + " file(s) in " + to + ", the emptied directory removed=" + dirGone);
        return moved;
    }

    // Every file of `from` copied into `to` (made on demand) and deleted
    // where it was; how many went. Subdirectories are left alone.
    protected static int MoveFiles(string from, string to, string id)
    {
        array<string> names = new array<string>();
        string name;
        FileAttr attr;
        FindFileHandle fh = FindFile(from + "\\*", name, attr, FindFileFlags.DIRECTORIES);
        if (name != "" && name != "." && name != ".." && name != OZS_Const.DIR_ROOTS)
            names.Insert(name);
        while (FindNextFile(fh, name, attr))
        {
            if (name != "." && name != ".." && name != OZS_Const.DIR_ROOTS)
                names.Insert(name);
        }
        CloseFindFile(fh);
        if (names.Count() == 0)
            return 0;
        MakeDirectory(to);
        int moved = 0;
        for (int i = 0; i < names.Count(); i++)
        {
            string src = from + "\\" + names.Get(i);
            string dst = to + "\\" + names.Get(i);
            if (CopyFile(src, dst))
            {
                DeleteFile(src);
                moved++;
            }
            else
            {
                OZ_Log.Warn("storage: box " + id + ": " + names.Get(i) + " could not be copied into the archive; it stays in " + from);
            }
        }
        return moved;
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

// One store being written, a root at a time, across frames: the index and
// the list as .new files committed at the end, every root straight into its
// own file under roots/ -- the old files went at Open, so until Commit the
// engine's cargo is the truth and a crash leaves nothing half-live.
class OZS_StoreWriter
{
    protected string m_Id;
    protected string m_BinLive;
    protected string m_ListLive;
    protected string m_BinNew;
    protected string m_ListNew;
    protected string m_Stamp;
    protected FileHandle m_List;
    protected int m_Expected;
    protected int m_Written;
    protected int m_RootsWritten;
    protected bool m_Open;

    // Deletes the old files, writes the .new index whole and opens the .new
    // list with its header. False leaves nothing behind and says why.
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
        MakeDirectory(OZS_Store.RootsDir(m_Id));
        m_BinLive = OZS_Store.BinPath(m_Id);
        m_ListLive = OZS_Store.ListPath(m_Id);
        m_BinNew = m_BinLive + OZS_Const.FILE_NEW;
        m_ListNew = m_ListLive + OZS_Const.FILE_NEW;
        m_Expected = entities;
        m_Written = 0;
        m_RootsWritten = 0;

        int saveVer = GetGame().SaveVersion();
        m_Stamp = OZS_Store.Stamp();

        FileSerializer index = new FileSerializer();
        if (!index.Open(m_BinNew, FileMode.WRITE))
        {
            why = "cannot write " + m_BinNew;
            return false;
        }
        m_List = OpenFile(m_ListNew, FileMode.WRITE);
        if (m_List == 0)
        {
            index.Close();
            DeleteFile(m_BinNew);
            why = "cannot write " + m_ListNew;
            return false;
        }
        m_Open = true;

        // From here on the box has no store: the engine's cargo is the truth
        // until Commit.
        OZS_Store.Delete(m_Id);

        index.Write(OZS_Const.BIN_VERSION);
        index.Write(saveVer);
        index.Write(m_Stamp);
        index.Write(box.GetType());
        index.Write(m_Id);
        index.Write(roots);
        index.Write(entities);
        index.Write(OZS_Const.BIN_END);
        index.Close();

        string head = OZS_Const.LIST_HEAD + "|1|" + saveVer + "|" + m_Stamp + "|" + box.GetType();
        head = head + "|" + m_Id + "|" + roots + "|" + entities;
        FPrintln(m_List, head);
        return true;
    }

    // One root entity with everything under it: its own file, and its lines
    // in the list. A cell override (>= 0) puts a cargo root elsewhere in the
    // grid (the sort).
    void WriteRoot(EntityAI e, int newRow = -1, int newCol = -1)
    {
        if (!m_Open || !e)
            return;
        int n = m_RootsWritten;
        m_RootsWritten++;
        string path = OZS_Store.RootPath(m_Id, n);
        FileSerializer f = new FileSerializer();
        if (!f.Open(path, FileMode.WRITE))
        {
            // The list still carries this root; the open reads it from there.
            OZ_Log.Error("storage: box " + m_Id + " cannot write " + path + "; root " + n + " will come from items.list");
            OZS_Records.WriteListEntity(m_List, e, 0, newRow, newCol);
            return;
        }
        f.Write(OZS_Const.BIN_VERSION);
        f.Write(m_Stamp);
        f.Write(n);
        m_Written = m_Written + OZS_Records.WriteEntity(f, e, newRow, newCol);
        f.Write(OZS_Const.BIN_END);
        f.Close();
        OZS_Records.WriteListEntity(m_List, e, 0, newRow, newCol);
    }

    int Written()
    {
        return m_Written;
    }

    // Close the list and put the .new pair over the live names; the root
    // files are already where they belong.
    bool Commit(out string why)
    {
        if (!m_Open)
        {
            why = "the store is not open";
            return false;
        }
        if (m_Written != m_Expected)
            OZ_Log.Warn("storage: box " + m_Id + " counted " + m_Expected + " entities but wrote " + m_Written);
        CloseFile(m_List);
        m_Open = false;
        if (!OZS_Store.Commit(m_BinNew, m_BinLive, why))
            return false;
        if (!OZS_Store.Commit(m_ListNew, m_ListLive, why))
            return false;
        return true;
    }

    // Closes and removes the .new pair and the root files written so far;
    // the live names stay absent.
    void Abort()
    {
        if (!m_Open)
            return;
        CloseFile(m_List);
        m_Open = false;
        if (FileExist(m_BinNew))
            DeleteFile(m_BinNew);
        if (FileExist(m_ListNew))
            DeleteFile(m_ListNew);
        OZS_Store.DeleteRoots(m_Id);
    }
}
