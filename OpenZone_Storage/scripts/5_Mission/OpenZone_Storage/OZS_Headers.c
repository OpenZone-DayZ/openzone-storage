// The inventory screen uppercases every container name before it reaches a
// header, with string.ToUpper -- and the engine's ToUpper turns every
// non-ASCII byte into a space (stand, 2026-09-16), so "Мала скриня" reads
// as " (0/500)" on a Ukrainian client, and a fix in the header alone found
// nothing but spaces to fix (2026-09-17). The places that do it are reopened
// here to fold case through OZS_Case, which knows the Ukrainian and Russian
// alphabets; the headers themselves fold the same way, so a name that
// reaches them intact stays intact. Nothing here is specific to the box:
// every container on the screen gets its name back.
#ifndef NO_GUI
modded class Header
{
    // The vanilla body is exactly the destructive ToUpper, so no super.
    override void SetName(string name)
    {
        m_HeaderText.SetText(OZS_Case.Upper(name));
    }
}

modded class ClosableHeader
{
    override void SetName(string name)
    {
        m_HeaderText.SetText(OZS_Case.Upper(name));
        m_HeaderText.Update();
        float x;
        float y;
        m_HeaderText.GetScreenSize(x, y);
        m_PanelWidget.SetSize(1, y + InventoryMenu.GetHeightMultiplied(10));
    }
}

modded class CargoContainer
{
    // Vanilla's UpdateHeaderText with the casing changed and nothing else.
    override void UpdateHeaderText()
    {
        string name = OZS_Case.Upper(m_Entity.GetDisplayName());
        if (m_Entity.CanDisplayCargo() && m_Entity.GetInventory().GetCargoFromIndex(m_CargoIndex))
        {
            name = name + " (" + GetCargoCapacity().ToString() + "/" + GetMaxCargoCapacity() + ")";
            if (m_IsAttachment && m_CargoHeader)
            {
                m_FalseHeaderTextWidget.SetText(name);
                float x;
                float y;
                m_FalseHeaderTextWidget.Update();
                m_FalseHeaderTextWidget.GetScreenSize(x, y);
                m_CargoHeader.FindAnyWidget("grid_container_header").SetSize(1, y + InventoryMenu.GetHeightMultiplied(10));
                m_CargoHeader.Update();
                if (m_AlternateFalseHeaderTextWidget)
                    m_AlternateFalseHeaderTextWidget.SetText(name);
                return;
            }
        }
        Container owner = Container.Cast(GetParent());
        if (owner && owner.GetHeader())
            owner.GetHeader().SetName(name);
    }
}

modded class Attachments
{
    // The attachment rows carry the owner's name as a false header; vanilla
    // builds the grid with the spoiled name, this sets the intact one after.
    override void InitAttachmentGrid(int att_row_index)
    {
        super.InitAttachmentGrid(att_row_index);
        if (m_AttachmentsContainer && m_Entity)
            m_AttachmentsContainer.SetFalseAttachmentsHeaderText(OZS_Case.Upper(m_Entity.GetDisplayName()));
    }
}

modded class HandsPreview
{
    // An item in the hands without cargo names the hands header itself.
    override void CreateNewIcon(ItemBase item)
    {
        super.CreateNewIcon(item);
        if (!m_Item || m_Item.GetInventory().GetCargo())
            return;
        HandsContainer holder = HandsContainer.Cast(m_Parent);
        if (!holder)
            return;
        HandsHeader header = HandsHeader.Cast(holder.GetHeader());
        if (header)
            header.SetName(m_Item.GetDisplayName());
    }
}
#endif
