# packaging

What goes into a `@Mod` folder besides the packed pbo.

The `@Mod` folders are **build output and are not in git**. `mod_build` writes
`addons/` into them and signs it with the series key (`keys/ZoneProtocol.biprivatekey`,
never committed); everything else a published mod needs lives here and is copied in by
[`package.ps1`](../package.ps1).

```
packaging/<Mod>/mod.cpp     the name, author, version and description DayZ shows in
                            the launcher and the mod list
packaging/<Mod>/meta.cpp    the Workshop item id, for mods that have been published
packaging/<Mod>.workshop.bbcode
                            the Workshop listing: the description the item's page
                            shows, English and Ukrainian in one field, Steam BBCode.
                            Beside the folder, not inside it, so package.ps1 never
                            ships it as a file of the mod
packaging/<Mod>/types-example.xml
                            an example economy entry for a mission's own types.xml;
                            copied into @Mod like every other file of the folder
packaging/<Mod>.workshop.bbcode
                            the Workshop listing: the description the item's page
                            shows, English and Ukrainian in one field, Steam BBCode.
                            Beside the folder, not inside it, so package.ps1 never
                            ships it as a file of the mod
```

`meta.cpp` ties a local folder to a Workshop item; without it the next upload creates a
**second** item instead of updating the one that exists. Publisher writes it into the
`@Mod` folder on the first publish -- **copy it back here and commit it**, otherwise the
next `package.ps1` cannot restore it after a deleted build folder. `timestamp` is left
to Publisher: a number invented to fill it would be false (it is the .NET
`DateTime.ToBinary()` of the moment Publisher wrote the file; the `dayz` MCP writes the
three lines above, and Steam needs no more).

Publishing, in order: `mod_build` (packs and signs) -> `.\package.ps1` (puts the rest in
place; `-Check` only reports) -> `workshop_publish("<Mod>")` from the `dayz` MCP, which
uploads the `@Mod` folder to the item `meta.cpp` names (Publisher, pointed at the same
folder, still works). The listing goes up separately and without touching the files:
`workshop_publish("<Mod>", description=<the text of packaging/<Mod>.workshop.bbcode>,
tags=[...], content=False)`. Steam replaces the whole tag list on every such call, so pass
the full set; the tags each item carries:

| item | tags |
|---|---|
| OZ_Storage (3803455084) | Mod, Mechanics, Props |

The procedure and its reasons are written out in `openzone-radio/docs/publishing.md`.

`packaging/<Mod>.workshop.png` is the preview image the item's page shows: 1024x512 PNG, under
1 MB, sent with `workshop_publish("<Mod>", preview="packaging/<Mod>.workshop.png", content=False)`
-- like the listing text, beside the folder so it never ships as a file of the mod.
