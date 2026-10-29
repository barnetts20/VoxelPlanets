# Stage K1: caves in the engine density

Design: `Design/EditsAndCaves.md`, "K1 as built". **After delivery: a GPU crash and its fix (2026-10-29), below.** The user then judged the look an ant farm rather than geology; the caves are redesigned for K2 (`Design/EditsAndCaves.md`, "Caves, second design"). K0 (the look test) is `STAGE_K0_NOTES.md`; the user approved its seven decisions on 2026-10-28 ("you suggestions looked good to me"), and K1 builds them as recommended: karst and sea caves, the depth band and sizes as tested, ~25 entrances per 1,000 km² of karst, analytic wall detail, dry caves, meshed depth by level with the brick tree's change.

## What changed

### VoxelPlanets

- **`Shaders/Private/VoxelLandCaves.ush`** (moved from `Tools/CaveHarness/`): the cave SDF, now in the engine. K1 changes to K0's code:
  - primitives keep their radii before the LOD rule and their depth; the LOD rule and the depth gate apply when a sample evaluates them, at its own footprint, so one gathered list serves a brick's samples at its level and the coarser stitched and target samples;
  - regions up to 8 × 8 cells (a brick and its margin), a 4 × 4 texel patch, a 6 × 6 noise lattice; the coarse gates read the patch;
  - the levels' bands carry a margin (4 cells and the blend), so the distance is exact that far past a wall; a brick's list drops primitives that can't change its density above -(12 cells + blend);
  - the cave bake is packed: 8 bytes a cell (the height as a float; base, drainage, relief and province in 32 bits);
  - sea caves keep two cells clear of face edges, as the karst nodes do;
  - new: `VoxelCaveSampleDF` (a sample from the density's double-float position: the land's exact face coordinates), `VoxelCaveBoxRegion` (a brick's face-slot regions), `VoxelCaveBoxMayHold` (the solid-region test), `VoxelCaveSkip`, the evaluation split into begin/primitive/end.
- **`Shaders/Private/VoxelLandCaveCarve.ush`** (new): the caves at a GPU density sample; the mesher's stored path (`VOXEL_CAVE_STORED`) or one gather a sample.
- **`Shaders/Private/VoxelCaveParameters.ush`** (new): the caves' shader inputs.
- **`VoxelDensitySample.ush`**: `VoxelSampleDensityDF` carves the caves where `VOXEL_DENSITY_CAVES` is 1 (the density check: one gather a point); `VoxelSampleDensityBrickDF` for the mesher (the brick's lists). The ocean pages include it without caves.
- **`VoxelDensityCore.ush`**: the placeholder cave slot (`VoxelCaveDensity`, `VoxelComposeTerrain`) is gone (`claude/DeadCode.md` §6).
- **`VoxelTerrestrialMesher.usf`**: `CaveGatherCS` (one thread per brick and face slot: the brick's box with 12 of its cells of margin, its regions gathered into `CaveHeadsOut`/`CavePrimsOut`; after the fix, its node grid in a scratch buffer, `CaveNodesOut`); the sample pass reads them. **`VoxelDensityGPU.usf`**: caves on.
- **`VoxelLandCaves.h/.cpp`** (new): `FVoxelCaveConstants` (the shader's names), `FVoxelLandCaveBake`, `VoxelLand::CaveConstantsFor`, `CaveBakeKey`, `GetCaveBake` (kept on the bake: `FVoxelLandBake::Caves`), `VoxelLandGPU::GetCaveParameters`, `FVoxelCaveSettings::GetHash`.
- **`VoxelLandSettings.h`**: `FVoxelCaveSettings` (24 settings, `Caves` on `FVoxelTerrestrialSettings`, category "Terrestrial|Caves"; on by default). `GetSettingsHash` includes it (`VoxelLandBake.cpp`): a change rebuilds the density, not the bake.
- **`VoxelTerrestrialDensity`**: the CPU sampler includes the cave code (its constants are the sampler's base); `BuildCaves`/`MakeCaveBake` (the bake through the warp at every padded cell, then the base; parallel); `SampleRange` carves the caves, keeping the last cell's list while samples stay in it and 32 m of altitude; `MayHoldSurfaceInSolid`; `BuildGPUData` hands the constants and the cave bake to the GPU.
- **`VoxelPlanetDensity`**: `Sample` runs `SampleRange` per batch (virtual); `SampleOne` lost the cave slot.
- **`VoxelDensityGPU.cpp/.h`, `VoxelLandGPU.h`**: the caves' parameters in the density check and the mesher; the gather pass recorded as the batch's brick pass; the lists' layouts (`FVoxelCavePrimGPU` 64 B, `FVoxelCaveListHeadGPU` 48 B).
- **`Tools/CaveHarness/cave_harness.cpp`**: reads the engine's `VoxelLandCaves.ush`; mode `k1` (below). `K1_results/`.

### VoxelPlugin (engine)

- **`VoxelMesherSourceDensity(uint Brick, …)`**: the brick a point is sampled for (`VoxelBrickMesherPasses.ush`; `VoxelSphereMesher.usf`, `VoxelBrickMesherGPU.usf`, the docs in `VoxelMesherCommon.ush` and `VoxelMesherShaders.h`).
- **`FVoxelMesherSampler::AddBrickPass`** (optional; `VoxelDensitySource.h`), recorded before the batch's first sample pass (`VoxelBrickMesherGPU.cpp`).
- **`IVoxelDensitySource::MayHoldSurfaceInSolid(Region, Footprint)`** (default false) and `FDensitySampleCompositor::MayHoldSurfaceInSolid`: a region a centre sample proved solid is kept when the source may still hold surface in it. Called by the brick tree (`VisitChunk`, the balance's `MayHoldSurface`), the collision descent and the CPU mesher's empty test.
- **The brick tree** (`VoxelBrickTerrain.cpp`): a brick the camera will split is tested at the finest footprint, a leaf at its own (K0's finding: the caves close at coarse levels); the balance tests a culled leaf's children too.

## Why the brick path is a pass of its own

K0 planned a groupshared gather per thread group. The mesher's sample passes lay threads out by lattice point and by hash slot, so a group doesn't map to a brick, and a per-group gather would put the gather's arrays (the 8 × 8 nodes, the 64-primitive list) in every sample thread's scratch memory. The gather pass runs one thread per brick and face slot (3 a brick, for bricks across a cube edge), writes the lists, and the sample shader only reads them: it has no per-sample gather at all.

## Why the solid-region test

The caves' distance is exact near the caves (the margin; horizontally at least 0.12 of a cell, ~15 m), and reads the land's depth beyond. An empty test that culls a region from its centre sample (density below -3.5 × radius) could then cull a region holding a cave the centre sample doesn't see. `MayHoldSurfaceInSolid` asks the caves for the box (a gather with no margin: any primitive open at the footprint within the box) for every region a sample proved solid. In the harness no such hole turned up without it (600 boxes a level near caves), but the test is cheap and makes the culling exact.

## Checks

- **The harness** (`cave_harness n512 512 1e6 k1`, `K1_results/k1.txt`):
  - the sample from a double-float position against K0's: the same cells, fractions within 3e-6 of a cell, altitude within 1.2e-4 m;
  - the mesher's brick lists (levels 0–4, 60 bricks each, 300 samples a brick anywhere in its box, at its footprint and twice it) against the per-sample path: **0 differences near the surface** (|d| < 4 cells: 8,630 samples); deeper they differ (each path's margin misses different far primitives; both read solid); 4–9 primitives a list at levels 0–2, at most 31, none overflowed; regions up to 7 cells; the gather 6–16 µs a brick on the CPU;
  - the CPU density's cell cache against the per-sample path: 0 differences over 49,152 samples; 0.43 µs a sample against 5.0;
  - the solid-region test: no miss; it keeps a few regions near caves;
  - the cave bake's packing: base within 0.25 m, drainage within 0.25, province within 0.008.
- **Unchanged look** (`probe`, `slope`, against K0's binary on the same planet): cave metres in the valley band 113 against 119 (the packing's rounding), uplands 100 and 100; the slope near walls max 1.39, 99% 1.25 (K0's binary on the same planet: 1.39, 1.24).
- **Cost** (CPU, one thread): per sample in the band 4.6 µs (K0 3.7: the margins and the packed 4 × 4 patch); the brick path 0.27 µs a sample plus 4.6 µs a brick (K0 0.20 + 3.7). Out of the band unchanged (0.03–0.05 µs).
- **Shaders**: `Tools/ShaderCheck/shader_check.sh` over both plugins with glslang: every entry point of every `.usf` and the materials, fails=0. **DXC was not run** (its download was refused in this session): please run the check with `DXC` set, or compile in the editor.
- **Not compiled**: the C++ (no UE here). The CPU sampler with the cave code compiles as C++ (`-Wall -Wshadow`, no warnings) in a host built from `VoxelTerrestrialDensity.cpp`'s struct.

## What to look at in the editor

- The log: `[VoxelLand] Caves: 8 cells of 192 m a bake cell, 3 levels 40 m apart; karst above province …` and `[VoxelLand] Cave bake for bake …: … cells in … s (… MB)` (once per bake and warp; ~51 MB at N 1024, kept on the bake).
- Caves are in karst (the top 25% of the land by province) around the water table, 35–500 m down; the entrances are rare by design (≈25 per 1,000 km²). To find some quickly, raise `Caves.MouthShare` to 0.04 and `Caves.DolineShare` to 0.008 for a look (ten times), then put them back. Sea caves are at cliff coasts with relief.
- Inside a cave, the bricks around the camera mesh it (the split test); from above ground the caves cost nothing (K0's brick counts).

## Limits

- A brick whose region (its box and 12 cells of margin) spans more than 8 cave cells gets no caves at its level: only at levels where trunks are still open with 32-cell bricks (level 4, 16 m cells). The default 16-cell bricks fit (up to 7 cells at level 4).
- The cave bake isn't cached on disk: it is remade at load (~1 s at N 1024 over the worker threads).
- The material's underground output and the land surface cache's entrance flags are K2.

## After delivery: the GPU crash and its fix (2026-10-29)

**The crash.** With K1 the editor lost the GPU (DXGI_ERROR_DEVICE_REMOVED) a few seconds after loading a level with a planet, every time. DRED (with `-gpucrashdebugging`) recorded no page fault, so it was a hang or a GPU fault outside resource memory. The breadcrumbs moved around (the mesher's batch on async compute in three crashes, the ocean's surface pass in one), so they didn't name the pass.

**Bisected** with a new switch, `voxel.Caves.GPU` (`VoxelDensityGPU.cpp`; set at startup with `-dpcvars=voxel.Caves.GPU=N`): 0 (no GPU caves) ran clean; 2 (the gather pass runs, the samples carve nothing) crashed. So the gather pass alone, not the carve and not the CPU side.

**The cause (inferred, not proven):** the gather ran one thread per brick slot holding a 144-node grid (44 B a node) and a 64-primitive list (64 B a primitive) in thread-local arrays: ~10 KB of scratch a GPU thread. Every loop and index in the cave code is bounded (checked twice); the per-thread work is milliseconds. The GPU didn't survive the scratch.

**The fix** (`VoxelLandCaves.ush`, `VoxelTerrestrialMesher.usf`, `VoxelDensityGPU.cpp`, `VoxelLandGPU.h`): with `VOXEL_CAVE_GATHER_BUFFERS 1` (the mesher's .usf) the gather keeps its node grid in a scratch buffer (`CaveNodesOut`, `VoxelLandGPU::CaveNodesPerSlot` × `CaveNodeBytes` a slot, written before read by the same thread) and writes its primitives straight into the slot's list (`CavePrimsOut`); the list has no array of its own there, and `VoxelCaveEvaluate`, `VoxelCaveDistance` and `VoxelCaveBoxMayHold` (which read that array) aren't compiled. The node grid goes through `VOXEL_CAVE_NODES_DECL` / `VOXEL_CAVE_NODE_GET` / `VOXEL_CAVE_NODE_SET`, the list through `VOXEL_CAVE_LIST_PUT`; the defaults (the CPU, the harness) are the local arrays as before. The compiled gather keeps only the patch's 16-float and the lattice's 36-float arrays. **The user confirmed: no crash.**

**Also in the fix round:**
- Per-pass breadcrumbs in the mesher: each pass in its own RDG event scope ("VoxelMesher Cave lists", "VoxelMesher Sample lattice", …; `VoxelMesherShaders.h`, `VoxelBrickMesherGPU.cpp`), since pass names alone aren't breadcrumbs.
- `voxel.Caves.GPU` stays: 0 none, 1 on (default), 2 the gather only.
- The stored path clamps a head's count to `VOXEL_CAVE_MAX_PRIMS` (`VoxelLandCaveCarve.ush`).
- **A CPU bug:** `FVoxelTerrestrialDensity::SampleRange` didn't raise footprints below the finest cell to it (`HeightAt` did, and the GPU does), so collision and queries could differ from the meshed terrain at tiny footprints. Fixed.

**Checks:** the harness's `k1` run is identical to the delivery's (`K1_results/k1.txt`, timings aside); the shader check (glslang) fails=0 over both plugins; DXC still not run here.

**Left:** `voxel.Debug.DensityGPUCheck`'s shader (`VoxelDensityGPU.usf`) still gathers per sample with the thread-local arrays: don't run it with caves on (`claude/DeadCode.md` §6). K2 replaces the whole gather.
