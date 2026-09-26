#ifndef NO_GUI
// Keeping the player's place in a box that redraws itself.
//
// The vicinity column is where a box is drawn, and it is the column that gets
// thrown back to the top every time the panel is rebuilt -- which, with the
// proxy, happens on every resynchronisation and after operations the screen
// cannot guess. A player scrolled down a long box loses their place and has to
// find it again (owner, 2026-09-25).
//
// `m_LeftArea` is protected, so the position cannot be read from outside the
// class; reopening `Inventory` is the sanctioned way to add the accessor, and
// the scroller itself is vanilla's own (`LeftArea.GetScrollWidget`).
//
// The position is kept in PIXELS rather than as a fraction on purpose: a
// rebuild can change how tall the content is, and a fraction would then point
// at different items. Pixels keep the same rows under the eye, and the widget
// clamps what it cannot honour.
modded class Inventory
{
    ScrollWidget OZS_Scroller()
    {
        if (!m_LeftArea)
            return null;
        return m_LeftArea.GetScrollWidget();
    }
}
#endif
