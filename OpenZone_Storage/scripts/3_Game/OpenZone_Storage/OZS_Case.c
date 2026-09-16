// Case folding that survives Cyrillic. The engine's string.ToLower/ToUpper
// were seen to turn non-ASCII bytes into spaces (stand, 2026-09-16), so the
// search and the sort fold case through replacement tables instead: ASCII
// A..Z and the Ukrainian and Russian alphabets, in place, no engine casing.
class OZS_Case
{
    protected static ref array<string> s_Upper;
    protected static ref array<string> s_Lower;

    protected static void Build()
    {
        if (s_Upper)
            return;
        s_Upper = new array<string>();
        s_Lower = new array<string>();
        Pair("A", "a"); Pair("B", "b"); Pair("C", "c"); Pair("D", "d"); Pair("E", "e"); Pair("F", "f");
        Pair("G", "g"); Pair("H", "h"); Pair("I", "i"); Pair("J", "j"); Pair("K", "k"); Pair("L", "l");
        Pair("M", "m"); Pair("N", "n"); Pair("O", "o"); Pair("P", "p"); Pair("Q", "q"); Pair("R", "r");
        Pair("S", "s"); Pair("T", "t"); Pair("U", "u"); Pair("V", "v"); Pair("W", "w"); Pair("X", "x");
        Pair("Y", "y"); Pair("Z", "z");
        Pair("А", "а"); Pair("Б", "б"); Pair("В", "в"); Pair("Г", "г"); Pair("Ґ", "ґ"); Pair("Д", "д");
        Pair("Е", "е"); Pair("Є", "є"); Pair("Ё", "ё"); Pair("Ж", "ж"); Pair("З", "з"); Pair("И", "и");
        Pair("І", "і"); Pair("Ї", "ї"); Pair("Й", "й"); Pair("К", "к"); Pair("Л", "л"); Pair("М", "м");
        Pair("Н", "н"); Pair("О", "о"); Pair("П", "п"); Pair("Р", "р"); Pair("С", "с"); Pair("Т", "т");
        Pair("У", "у"); Pair("Ф", "ф"); Pair("Х", "х"); Pair("Ц", "ц"); Pair("Ч", "ч"); Pair("Ш", "ш");
        Pair("Щ", "щ"); Pair("Ъ", "ъ"); Pair("Ы", "ы"); Pair("Ь", "ь"); Pair("Э", "э"); Pair("Ю", "ю");
        Pair("Я", "я");
    }

    protected static void Pair(string upper, string lower)
    {
        s_Upper.Insert(upper);
        s_Lower.Insert(lower);
    }

    static string Lower(string s)
    {
        Build();
        string r = s;
        for (int i = 0; i < s_Upper.Count(); i++)
            r.Replace(s_Upper.Get(i), s_Lower.Get(i));
        return r;
    }

    static string Upper(string s)
    {
        Build();
        string r = s;
        for (int i = 0; i < s_Lower.Count(); i++)
            r.Replace(s_Lower.Get(i), s_Upper.Get(i));
        return r;
    }

    // The first letter upper, the rest as given ("папір" -> "Папір").
    static string Capitalize(string s)
    {
        Build();
        if (s == "")
            return s;
        for (int i = 0; i < s_Lower.Count(); i++)
        {
            string l = s_Lower.Get(i);
            if (s.IndexOf(l) == 0)
                return s_Upper.Get(i) + s.Substring(l.Length(), s.Length() - l.Length());
        }
        return s;
    }
}
