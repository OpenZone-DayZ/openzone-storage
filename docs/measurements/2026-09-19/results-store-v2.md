# Store version 2: one file per root (2026-09-19, stand)

Same stand as `results-nested-restore-load.md`. Version 1 = one `items.bin` with every
record inline; version 2 = `items.bin` header only plus `roots/NNNN.bin` per root, the
list unchanged. The close writes at the 5 ms per frame budget, the open restores at 500
entities per second; "work" is the job's own accounting of its steps.

## The same content, both formats

| box | roots / entities | close, write | close, delete | open, work | open, longest step | open, wall |
|---|---|---|---|---|---|---|
| Large, 100 nested chains, v1 (2026-09-19 01:1x) | 100 / 500 | 78-83 ms, 14-15 frames | 26-34 ms, 10 frames | 105-119 ms | 2-3 ms | 1 s |
| the same, v2 | 100 / 500 | 205 ms, 35 frames | 26 ms, 10 frames | 142 ms (one root from the list) | 9 ms | 1 s |
| Large, 1443 flat items, v1 (2026-09-17) | 1443 / 1468 | 283 ms | 72 ms | 20 ms | 2 ms | 2.9 s |
| Large, 1400 flat items, v2 | 1400 / 1400 | 1108 ms, 211 frames | 86 ms, 28 frames | 196 ms | 8 ms | 2.8 s |

So a root file costs about 0.8 ms to write and 0.13 ms to open on this disk, paced over
the frames of the job; the wall time of an open is the rate, not the files. The
`OZ_ProbeState.DeferredTick` and the ghost watcher were idle during these runs.

## The isolation

`roots/0007.bin` of the 100-chain box had its `SmallProtectorCase` renamed to
`SmallProtectorCasX` (same length, the stamp untouched). The open: `root 7 of 100 cannot
be read (the record cannot be followed); kept as roots\0007.bin.failed-90919-000809; it
comes from items.list`, then `100 items (500 entities) ... 1 root(s) from items.list`;
501 entities around the box, 0 zombies. The list still knew the real class, so root 7 came
back whole with list-level state; with the mod really gone it comes back as the pouch
alone (section 22).

## The archive

The Small box, version 2 store with one root, deleted with `world_delete`: `store
archived: 4 file(s) in $profile:OpenZone\Storage\removed\90918-165558-11-5860, the emptied
directory removed=true` -- index, list, a `.failed` copy and `roots/0000.bin` moved, the
live directory gone. `DeleteFile` on an emptied directory returns true and removes it.

## Engine facts measured on the way

- `FileSerializer` has `Open`, `IsOpen`, `Close`, `Read`, `Write` and nothing else: no
  position, no seek, no byte skip. A record that cannot be parsed ends the stream for good.
- `FindFile(dir + "\\*", name, attr, FindFileFlags.DIRECTORIES)` lists a `$profile:`
  directory; the subdirectory shows up as a name among the files.
- `DeleteFile` removes an empty directory.
- A version-1 index refused with `format version 1 is not 2` sends the whole box through
  `items.list`, as designed.
