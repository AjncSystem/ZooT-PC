# Pipeline state list

`zoot-pipeline-states.bin` (when present) lists graphics pipelines recorded while playing
Ocarina of Time on ZooT-PC: for each vertex + fragment pipeline, its draw state (Lichii's
`FixedPipelineState`), the hashes of its two programs and the values their translation read
(texture types, constant buffer values, program header). **It holds no game code**: the
programs themselves come from the player's own copy, extracted by the shader precache.

Status: experimental, not shipped. On the title + attract demo bench (cold caches, RTX 2060
SUPER) a list from one play session cut the mid-game stall from 35.4 s to 27.4 s, but a list
recorded from that very bench did not do better (30.6 s): the pipeline libraries are still
built again in game, so something besides the recorded state differs.

When the list sits next to `eden-cli.exe`, the precache adds one entry per listed pipeline whose
programs are in the game files, and builds that pipeline's libraries with its real draw state
instead of a guess, so the first visit to an area needs fewer pipeline builds mid-game.

Format: `ZOOTPST2`, the pipeline cache version (u32), the key size (u32, 1056), the entry count
(u64), then per entry the key and, for the vertex and the fragment stage, a u32 size and the
env record without its code. A list of another cache version is ignored.

Made with `eden-cli --zoot-export-states <list> [pipeline caches...]`, which merges new keys into
an existing list (without cache files it reads Ocarina's own `lichii-cache.v2.bin`).
