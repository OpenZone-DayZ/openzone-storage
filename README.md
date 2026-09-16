# OpenZone Storage

Virtual storage boxes for the OpenZone series: a box whose contents do not exist
in the game world while it is closed. Part of the [OpenZone](../README.md) family;
prefix `OZ`, runs with `OZ_Core` alone.

## Status: research (2026-09-16)

No storage mod yet. The owner chose model C of the survey on 2026-09-16 (real cargo,
items exist only while the box is open, paced materialisation), three boxes of
500 / 1000 / 1500 cells and weapon slots on the box. The comparison of existing
implementations and the stand measurements behind the recommendation are in
[docs/2026-09-16-virtual-storage-survey.md](docs/2026-09-16-virtual-storage-survey.md);
the design, with the decisions still open, in
[docs/2026-09-16-storage-box-spec.md](docs/2026-09-16-storage-box-spec.md).

What exists is the **measurement probe**, two pbos that are built but never
published:

| pbo | side | what |
|---|---|---|
| `OpenZone_StorageProbe` | client + server | `OZ_ProbeCrate` (a SeaChest with a 10 x 500 cargo grid), `ItemBase.EEInit`/`EEDelete` counters, per-frame monitors in `MissionServer.OnUpdate` and `MissionGameplay.OnUpdate` |
| `OpenZone_StorageProbe_Bridge` | server only | the `oz_probe` verb for the MCP bridge (`world_exec`) |

Results land as JSON lines in `$profile:OpenZone_StorageProbe/results.log`
(server) and `$profile:OpenZone_StorageProbe/client.log` (client). The runs of
2026-09-16 are kept in `docs/measurements/2026-09-16/` (as `.jsonl` / `.txt`,
because the repository ignores `*.log` as build output).

## Build and run

Through the `dayz` MCP server: `project_open` -> `mod_build` -> `server_start`
-> `world_ready` -> `world_exec verb=oz_probe args={"op":"crate","pos":"..."}`.
The stand profile is `dayz-mcp.toml` plus the machine half `dayz-mcp.local.toml`
(retail server, so `tools/profiler/script-profile.ps1` from openzone-radio can
read the script VM).

## Licence

CC BY-NC-SA 4.0 with the server-operator permission in `NOTICE`.
