// THE HANDLE LIVES ON THE ITEM.
//
// An authority names every item in it by a number of its own
// (OZS_Authority.Handle), and the table that mapped entities to numbers was
// walked from the top for every lookup: a snapshot of a thousand-item box
// asked it once per node and once per parent -- on the order of a million
// steps on one server frame, on every open and on every restream, which is
// a hitch every player on the server feels (review 2026-09-26, D1). The
// number on the item itself is one read.
//
// NOT ON EntityAI. "Engine class 'EntityAI' cannot be modded" is the
// compile error that answers a `modded class EntityAI` (measured
// 2026-09-26), so the field sits on ItemBase -- which OZS_Stacking.c reopens
// already, and which carries the field there. Weapon_Base descends from
// ItemBase through InventoryItemSuper, so weapons carry it too (measured the
// same day: declaring it on Weapon_Base as well is "Multiple declaration").
// This class reads and writes it through the cast, and answers "not
// taggable" for an entity that is no ItemBase -- for which the authority
// walks its table as it always did.
//
// Zero means "no handle". The authority's own table stays the word on which
// entity a number names (OZS_AuthRec), so a number left on an item that has
// moved on -- see OZS_Authority.Forget -- is checked against it and never
// trusted on its own. Nothing here is saved: an authority is not, and a
// handle is good for one session only.
class OZS_HandleTag
{
    static bool Taggable(EntityAI e)
    {
        return ItemBase.Cast(e) != null;
    }

    static int Read(EntityAI e)
    {
        ItemBase item = ItemBase.Cast(e);
        if (item)
            return item.m_OZS_Handle;
        return 0;
    }

    static void Write(EntityAI e, int handle)
    {
        ItemBase item = ItemBase.Cast(e);
        if (item)
            item.m_OZS_Handle = handle;
    }
}
