// Every action class must be known to the ActionConstructor before an item
// can AddAction it: ItemBase.AddAction asks ActionManagerBase.GetAction,
// which only answers registered classes, and drops the action with a
// Debug.LogError that lands in the .RPT, not the script log (measured
// 2026-09-16: the box's actions were silently absent until this file).
modded class ActionConstructor
{
    override void RegisterActions(TTypenameArray actions)
    {
        super.RegisterActions(actions);
        actions.Insert(OZS_ActionOpenBox);
        actions.Insert(OZS_ActionCloseBox);
    }
}
