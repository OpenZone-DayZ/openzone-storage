# The box and locker models

Models, textures and opening animations for OpenZone Storage, by Crystal, contributed on
2026-10-05: the small, medium and large box and the personal locker, in place of the vanilla
wooden crate, sea chest and locker the mod wore until then.

## What is where

| Path | What |
|---|---|
| `OpenZone_Storage/models/<model>/` | what the mod ships: the binarized `.p3d`, its `model.cfg` (bones and animations) and `data/` (`.paa` textures, `.rvmat` materials) |
| `models/<model>/<name>.p3d` | the MLOD source the shipped model is built from |
| `models/fridge/` | the alternate large box, complete (MLOD, `model.cfg`, `data/`) and not shipped |
| `models/prepare_model_root.py` | lays out the binarize root under `build/model-root` |

Every path inside the models and materials is `OpenZone_Storage\models\<model>\data\...`. The
Blender scripts that generate the meshes, the textures and the `model.cfg` files (their
headers name them) are the author's and are not in this repository.

## The models

Size is width x height x depth in metres; triangles are the three visible LODs. Each model
also carries a Geometry LOD with mass, View and Fire Geometry and a Memory LOD with the hinge
axes and the bounding box.

| Folder | Class | Size | Triangles | Openings: `_a` smooth / `_b` snap |
|---|---|---|---|---|
| `small_case` | `OZ_StorageBox_Small` | 0.80 x 0.28 x 0.41 | 332 / 216 / 80 | `lid_a` 1.8 s: the hasp lowered, the lid rises; `lid_b` 1.1 s: the hasp drops, the lid flung open, bounces |
| `medium_crate` | `OZ_StorageBox_Medium` | 1.03 x 0.35 x 0.52 | 872 / 784 / 320 | `case_a` 2.4 s: the hasp tongues lifted off one by one, the lid onto its stop; `case_b` 1.4 s: the tongues knocked off at once, the lid flung open |
| `hardcase_128` | `OZ_StorageBox_Large` | 1.27 x 0.47 x 0.64 | 3716 / 2300 / 528 | `case_a` 2.8 s: four latches one by one, the lid lifted; `case_b` 1.5 s: the latches torn down at once, the lid flung open |
| `fridge` | (alternate large box) | 1.20 x 0.58 x 0.61 | 2408 / 1088 / 450 | `door_a` 2.0 s: the lever, the door smoothly to 100 degrees; `door_b` 1.2 s: the door flung to 110 degrees, bounces |
| `locker` | `OZ_StashAnchor` | 1.00 x 1.90 x 0.52 | 736 / 524 / 218 | `locker_a` 2.0 s: the lever turned, the right door opens; `locker_b` 1.2 s: both doors flung open, they bounce on the stop |

Both openings live in one model, each on its own animation source in `model.cfg`. The config
keeps both sources (`AnimationSources`); the script names the one it drives:
`OZ_StorageBox.OZS_LidSource` per size and `OZ_StashAnchor.OZS_DOOR_SOURCE`. The mod ships
with the snap opening everywhere.

**A box's lid** is up while anybody is looking into the box, and every player around sees
it: the server sets one synchronised bit from the session's watcher list and each client
plays the opening.

**Looks.** The small case comes in two colours on one model: green-teal
(`ozs_case_small_co.paa`, the one in the config) and the paint of the vanilla
`StaticObj_ammoboxes_single` (`ozs_case_small_vanilla_co.paa`). The medium crate has three
states of wear, each a texture and its own material: `ozs_crate_medium`, `_worn`, `_heavy`.
Changing a look is `hiddenSelectionsTextures`, `hiddenSelectionsMaterials` and, for the
crate, `hologramMaterial` in `config.cpp`.

**Markings** are in one style: the OpenZone sign with the word OPENZONE, the name in
Ukrainian, a number of batch 14 (small 0687, hard case 0688, medium 0689, locker 0690) and
the acceptance stamp "ЗП-07 ВТК-3". There is no year: every server has its own. The fridge
carries a chrome nameplate of its own, "ОРКСК", instead.

## The personal locker

A steel cabinet with two mesh doors; inside, a top shelf, a rail for hangers, two hooks and
two lower shelves. As tall as the vanilla `locker_closed_v1` (1.90 m). Olive paint with
flaked patches, chips and rust at the floor.

**The doors open only for the player who opened their stash.** Everybody else sees the
locker closed and the player's gesture at it. The animation phase is set by that client
alone and the server never touches it, so nothing is synchronised
(`OZ_StashAnchor.OZS_ShowOpen`, called from `OZS_Mirror.Begin` and `Destroy`); vanilla does
the same with the fuel dial of a generator (`PowerGenerator.UpdateFuelMeter`).

**The mesh** is a separate tiled texture cut out by alpha (`ozs_locker_mesh_ca.paa`), so its
shadow on the ground and inside the cabinet is a grid. More than half of the tile is metal,
which keeps a door solid from a distance instead of vanishing.

**The collision** is one solid box of the closed cabinet: a door only one client sees open
must not collide with anything. The locker has no `camo` selection: an admin places it, no
kit's hologram projects it, and a whole-LOD `camo` would paint the atlas over the mesh.

## Building

The shipped `.p3d` files are built from the MLOD sources by `binarize`, each together with
the `model.cfg` beside it (binarize takes the class from `CfgModels` by the file's name).
Through the `dayz` MCP server:

```
python models/prepare_model_root.py
asset_build(mod="OpenZone_Storage", source="models/<model>")
```

The first stages the sources under `build/model-root` (`dayz-mcp.toml`, `[build]
project_root`) with links to the mod's `data\` and to the vanilla
`dz\data\data\penetration\`, which the Fire Geometry materials name; it needs the unpacked
vanilla `dz` (`OZ_VANILLA_DZ`, or one of the usual places). The second runs `binarize` from
that root, judges what came out and copies it into the mod.

One run takes minutes whatever the model: 510 to 540 s each for these (2026-10-05). Before
it reads anything, binarize walks the whole drive its working directory is on.

## The alternate large box

`models/fridge` is an old Soviet fridge lying on its back, door up, with rusted-through
holes low on the walls cut out by alpha (`ozs_fridge_ca.paa` with `AlphaTest128` in its
material -- keep the two together) and glass wool behind them. To ship it instead of the
hard case:

1. move `models/fridge/data` and `models/fridge/model.cfg` to
   `OpenZone_Storage/models/fridge/`, then stage and build it as above;
2. in `config.cpp`, point `OZ_StorageBox_Large` and `OZ_StorageBoxKit_LargePlacing` at
   `\OpenZone_Storage\models\fridge\ozs_fridge.p3d`, the texture at `ozs_fridge_ca.paa`, the
   materials at `ozs_fridge` (hologram: `ozs_fridge` in `OpenZone_Storage\models\fridge\data`),
   `soundImpactType = "metal"`, and name the sources `door_a` / `door_b` in
   `AnimationSources`;
3. return `"door_b"` (or `"door_a"`) from `OZ_StorageBox_Large.OZS_LidSource`.

## Checked on the stand (2026-10-05)

Retail server and client, the four shipped models:

- they load with their textures and shadows, and neither log names a model, texture or
  material that was not found (`dayz-mcp.toml` forbids those lines);
- **lids**: a client asking for a box raises its lid, and the last one leaving lowers it --
  the server's phase and the phase the client draws both went 0 -> 1 -> 0, for all three
  sizes, through the probe's `pxopen` and through the ordinary screen;
- **the locker**: with a stash open the client draws the doors at phase 1 while the server
  holds 0, and they shut with the screen;
- **kits**: each "<kit>Placing" projects the box's own model, white where it can stand and
  nearly clear where it cannot (the vanilla sea chest's hologram looks the same there), and
  all three deploy into a box standing where the hologram stood, its face to the player who
  placed it;
- the locker faces the way the vanilla `locker_closed_v1` did (doors at -Z), so a locker
  placed before the models keeps its direction. It is 1.00 m wide against about 0.4 m
  (measured off a picture of the two side by side), so lockers that stood in a row may
  now overlap.

Not checked: a second client watching somebody else's locker (the server's phase staying 0
is what stands for it), and the fridge, which was not built.

## Author and licence

Models, textures and animation: Crystal, made from scratch after references on Sketchfab.
Nothing is copied from them; the sizes were taken from pictures:

- small case: "Wooden Military Case PBR" (odalax), the opening after "MILITARY CASE"
  (Tutjunction);
- medium crate: "USSR War Box" (NEXIC);
- hard case: "Military Hardcase" (Cyril Demetrius), latches and hinges after "Military Cases
  Pack Case 2" (forest_cat);
- fridge: "Old refrigerator of the USSR "ORSK"" (Ruslan Malovsky), the nameplate is its own;
- locker: "Locker Equipment" (LtxxwSibeRia).

The sign is the one on the OpenZone radios. The licence is the series': CC BY-NC-SA 4.0 with
the additional permission in [NOTICE](../NOTICE).
