// Stand diagnostic: why a hologram refuses to deploy. The retail build has
// no DIAG_DEVELOPER collision details, so this re-runs the individual checks
// after every evaluation of a storage kit's hologram and prints them to the
// client's .RPT once a second.
modded class Hologram
{
    protected float m_OZP_LastPrint;

    override void EvaluateCollision(ItemBase action_item = null)
    {
        super.EvaluateCollision(action_item);
        if (!m_Parent || !m_Parent.IsInherited(OZ_StorageBoxKit_Base))
            return;
        if (GetGame().IsDedicatedServer())
            return;
        float now = GetGame().GetTickTime();
        if (now - m_OZP_LastPrint < 1.0)
            return;
        m_OZP_LastPrint = now;
        string s = "[OpenZone] probe hologram " + m_Parent.GetType() + " colliding=" + IsColliding();
        s = s + " floating=" + IsFloating() + " hidden=" + IsHidden() + " bbox=" + IsCollidingBBox(action_item);
        s = s + " proxy=" + IsCollidingGeometryProxy(action_item) + " player=" + IsCollidingPlayer() + " roof=" + IsClippingRoof();
        s = s + " base=" + IsBaseViable() + " gplot=" + IsCollidingGPlot() + " zero=" + IsCollidingZeroPos();
        s = s + " angle=" + IsCollidingAngle() + " permitted=" + IsPlacementPermitted() + " height=" + HeightPlacementCheck();
        s = s + " water=" + IsUnderwater() + " terrain=" + IsInTerrain() + " canplace=" + m_Player.CanPlaceItem(m_Projection);
        s = s + " pos=" + GetProjectionPosition().ToString() + " proj=" + m_Projection.GetType();
        // Print reaches no file on the retail client (no script log there);
        // ErrorEx lands in the .RPT.
        ErrorEx(s, ErrorExSeverity.WARNING);
    }
}
