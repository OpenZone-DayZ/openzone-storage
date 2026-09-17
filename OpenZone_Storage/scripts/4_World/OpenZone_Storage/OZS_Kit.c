// The kits: what a player carries and deploys into a stationary box. The
// vanilla placement path is used as is (ActionTogglePlaceObject shows the
// hologram, the next/previous-action inputs turn it by 15 degrees,
// ActionDeployObject holds for GetDeployTime seconds); on completion the kit
// creates the box where the hologram stood and the action deletes the kit
// (IsBasebuildingKit). The hologram projects the class "<kit>Placing", which
// carries the box's model and hologram material in config.
class OZ_StorageBoxKit_Base : ItemBase
{
    // The box this kit becomes; each size overrides it.
    string OZS_BoxType()
    {
        return "";
    }

    override bool IsBasebuildingKit()
    {
        return true;
    }

    override bool IsDeployable()
    {
        return true;
    }

    override bool CanProxyObstruct()
    {
        return false;
    }

    override float GetDeployTime()
    {
        return OZS_Const.DEPLOY_SECONDS;
    }

    override string GetDeploySoundset()
    {
        return "putDown_FenceKit_SoundSet";
    }

    override string GetLoopDeploySoundset()
    {
        return "Shelter_Site_Build_Loop_SoundSet";
    }

    override void SetActions()
    {
        super.SetActions();
        AddAction(ActionTogglePlaceObject);
        AddAction(ActionDeployObject);
    }

    override void OnPlacementComplete(Man player, vector position = "0 0 0", vector orientation = "0 0 0")
    {
        super.OnPlacementComplete(player, position, orientation);
        if (!g_Game.IsServer())
            return;
        string type = OZS_BoxType();
        OZ_StorageBox box = OZ_StorageBox.Cast(g_Game.CreateObjectEx(type, GetPosition(), ECE_PLACE_ON_SURFACE));
        if (!box)
        {
            OZ_Log.Error("storage: kit " + GetType() + " could not create " + type + " at " + position.ToString());
            return;
        }
        box.SetPosition(position);
        box.SetOrientation(orientation);
        box.SetLifetime(OZS_Const.BOX_LIFETIME);
        string who = "a player";
        PlayerBase pb = PlayerBase.Cast(player);
        if (pb)
            who = OZS_Controller.Who(pb);
        OZ_Log.Info("storage: " + who + " placed " + type + " id=" + box.OZS_GetId() + " at " + position.ToString());
        // The kit stays invisible until the deploy action deletes it.
        HideAllSelections();
    }
}

class OZ_StorageBoxKit_Small : OZ_StorageBoxKit_Base
{
    override string OZS_BoxType()
    {
        return "OZ_StorageBox_Small";
    }
}

class OZ_StorageBoxKit_Medium : OZ_StorageBoxKit_Base
{
    override string OZS_BoxType()
    {
        return "OZ_StorageBox_Medium";
    }
}

class OZ_StorageBoxKit_Large : OZ_StorageBoxKit_Base
{
    override string OZS_BoxType()
    {
        return "OZ_StorageBox_Large";
    }
}

// The hologram projections: the box's look, a kit's behaviour, no box logic.
class OZ_StorageBoxKit_SmallPlacing : OZ_StorageBoxKit_Small
{
}

class OZ_StorageBoxKit_MediumPlacing : OZ_StorageBoxKit_Medium
{
}

class OZ_StorageBoxKit_LargePlacing : OZ_StorageBoxKit_Large
{
}
