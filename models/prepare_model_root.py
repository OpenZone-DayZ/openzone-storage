"""Lay out the binarize root for the box models (dayz-mcp.toml, [build] project_root).

    python models/prepare_model_root.py

binarize has no root switch: the root is its working directory, and every path
baked into a model resolves against it. The models name their textures as
OpenZone_Storage\\models\\<model>\\data\\... and their Fire Geometry materials as
the vanilla dz\\data\\data\\penetration\\*.rvmat, so the root needs both trees:

    build/model-root/OpenZone_Storage/models/<model>/<name>.p3d   the MLOD, copied from models/<model>/
    build/model-root/OpenZone_Storage/models/<model>/model.cfg    copied from the mod
    build/model-root/OpenZone_Storage/models/<model>/data         junction to the mod's data
    build/model-root/dz/data/data/penetration                     junction to the unpacked vanilla folder

Then one model is built and put into the mod by
asset_build(mod="OpenZone_Storage", source="models/<model>").

Only the models the mod ships are staged: a source folder with no
OpenZone_Storage/models/<model>/model.cfg beside it is left alone.

The vanilla tree is found at OZ_VANILLA_DZ, or at the first of the usual places.
Windows only: the links are NTFS junctions, which need no elevation.

HOW LONG ONE RUN TAKES IS THE SIZE OF THE DISK, NOT OF THE MODEL (measured
2026-10-05). Before it reads anything binarize walks the WHOLE DRIVE its working
directory is on -- its busy thread sits in FindNextFile with the drive's root open.
Started from this folder that was 510 to 540 s a model. asset_build has since learned
to start it from a drive letter substituted for this root, where the walk has only
this tree to cover: a tenth of a second a model. The walk goes through the junctions
too, so the one vanilla folder the models name is linked and not the whole tree
(with all 103,359 files of it linked in, a run took half a second).
"""
import _winapi
import glob
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
MOD = "OpenZone_Storage"
MOD_MODELS = os.path.join(REPO, MOD, "models")
ROOT = os.path.join(REPO, "build", "model-root")
PENETRATION = os.path.join("data", "data", "penetration")
VANILLA_CANDIDATES = ["E:/pdrive/dz", "P:/dz", "D:/modding/PDrive/dz"]


def vanilla_dz():
    wanted = [os.environ.get("OZ_VANILLA_DZ", "")] + VANILLA_CANDIDATES
    for path in wanted:
        if path and os.path.isdir(os.path.join(path, PENETRATION)):
            return os.path.normpath(path)
    sys.exit("no unpacked vanilla dz with data/data/penetration found; set OZ_VANILLA_DZ")


def junction(target, link):
    if os.path.exists(link):
        return
    _winapi.CreateJunction(os.path.normpath(target), os.path.normpath(link))


def main():
    # The one vanilla folder the models name, not the whole tree.
    os.makedirs(os.path.join(ROOT, "dz", "data", "data"), exist_ok=True)
    junction(os.path.join(vanilla_dz(), PENETRATION), os.path.join(ROOT, "dz", PENETRATION))
    for mlod in sorted(glob.glob(os.path.join(HERE, "*", "*.p3d"))):
        model = os.path.basename(os.path.dirname(mlod))
        shipped = os.path.join(MOD_MODELS, model)
        if not os.path.isfile(os.path.join(shipped, "model.cfg")):
            print("left alone", model, "(the mod does not ship it)")
            continue
        staged = os.path.join(ROOT, MOD, "models", model)
        os.makedirs(staged, exist_ok=True)
        shutil.copy2(mlod, staged)
        shutil.copy2(os.path.join(shipped, "model.cfg"), staged)
        junction(os.path.join(shipped, "data"), os.path.join(staged, "data"))
        print("staged", model)


if __name__ == "__main__":
    main()
