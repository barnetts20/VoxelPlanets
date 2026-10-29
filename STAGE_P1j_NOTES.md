# Stage P1j: the noise packs a reseed replaced are let go (memory)

**Measured (2026-10-28, 16:20: two memreports after six reseeds):**
- `Voxel.LandNoise`: **1 buffer**, where there were 4.
- **Structured buffers:** 1,085 MB → 410 MB in orbit and 497 MB at the coast (the coast's extra is the mesher's vertex cache).
- **Tracked RHI resources:** 4,175 / 4,050 MB → 3,657 / 3,272 MB.
- **Each reseed logged one "released" line** for the pack it replaced.
- **"noise pack" read the bake's time on every build**, so spawn time is unchanged.

The memory pass of P1. This stage changes VoxelPlanets only. `StageP1j.zip` is cumulative from R1a (VoxelPlugin + VoxelPlanets: P1a2, P1c, P1d, P1g, P1h and P1i included). CloudAtmosphere stays at `CloudAtmosphere_P1f2.zip`.

## What the two memreports showed (2026-10-28, 15:39 at the coast, 15:40 in orbit)
**The two are within a few percent of each other:** 4.18 and 4.05 GB of tracked RHI resources (3.49 and 3.65 GB of it not transient). The largest of the plugins' resources:

| Resource | Size | What it is |
|---|---|---|
| `Voxel.LandNoise` × **4** | 224 MB each | the noise pack's GPU copy (the peaks', hills', strata', detail, dunes', sand, glacial, ice and polar sets) |
| `Texture2DDynamic_33` | 172 MB | the land material's copy of the same pack (the chains it reads) |
| `VoxelOceanShore_12` | 82 MB | the shore field and the coastal field's slices |
| `TextureRenderTarget2D_0..2` | 18 + 36 + 36 MB | the land surface cache's atlases (R1's #30) |
| `Texture2DDynamic_30, _31` | 49 MB each | the land material's bake textures |
| `VoxelOceanFoamSet` | 43 MB | the foam set |
| `Voxel.LandRiverData`, `LandHeight`, `LandWater` | 27, 24, 24 MB | the bake's fields for the density |
| `Voxel.LandFlow`, `LandForm`, `LandAttr` | 12 MB each | likewise |

**One pack is in use, and four were alive.** The log shows four reseeds before the reports (19:36:43, 19:37:09, 19:37:15 and 19:37:21 UTC), each with its own pack (peaks_a, then peaks_c with strata_a, then peaks_c with strata_b, then peaks_b). The registry kept the four most recently made alive whatever used them (R1a's `Recent`, four).

So three superseded packs stayed after the reseeds that replaced them, each with:
- **its words in memory:** 224 MB of RAM;
- **its GPU copy:** 224 MB of VRAM.

That is about **670 MB of VRAM and 670 MB of RAM** held for nothing. The material's texture (172 MB) was already only the current pack's.

## What changes
**The keep-alive list existed so that a pack made in the background survives until the planet asks for it.** It now does exactly that:
- **Pending:** a pack made and not yet taken stays (at most four, as before).
- **Taken:** when `TryGetLandNoise` (the planet) or `GetLandNoise` (a density) hands it out, it leaves the list. From then on it lives exactly as long as a planet or a density holds it, and goes with its last holder: the words freed, the GPU copy released on the render thread, as before.
- **The prewarm's worker** makes the pack without taking it (it holds nothing).

**New cvar, `voxel.Land.NoisePacksKept`:** default 0. It keeps that many recently taken packs alive after everyone let them go, for a workflow that keeps going back to the same seeds.

**Why 0 costs no spawn time:** a pack takes about 0.5 s to read on a worker. It is read beside the bake (0.9 s from the disk cache, 1.8 s on the GPU), and the log's timelines show it ready with the bake. A reseed back to an earlier pack reads it again in that same shadow.

**A log line** when a pack goes: `[VoxelLand] Noise pack <key> released (N MB of words and its GPU copy).`

## Expected
- **After a few reseeds:** a single `Voxel.LandNoise` (224 MB) in `rhi.DumpResourceMemory`, where there were four. That is −670 MB of VRAM (the structured buffers: 1,085 → ~410 MB) and −670 MB of RAM.
- **On a fresh spawn with no reseed:** nothing changes (one pack, as before).
- **During a reseed:** the old and the new pack are both alive until the new terrain is up, as before.
- **The look:** unchanged. The same pack is read for the same seed. Only how long an unused one lives changes.
- **Spawn time:** unchanged (the read is beside the bake).

## Looked at and left
- **The land material's noise texture (172 MB)** holds the same rows as the GPU buffer (224 MB). The compute passes could read the texture instead, but that touches every noise fetch in the shared land shaders. That is not a small exact stage; it's a later candidate (about −224 MB of VRAM, or −172 MB the other way round).
- **The land surface cache's atlases (90 MB, R1's #30):** in use. Dropping its coarsest levels is C4's work, not an exact cut.
- **The shore texture (82 MB):** in use. A replaced texture's GPU copy goes at the next GC (up to a minute in the editor). Releasing it sooner would race the material's reference, so it stays as is.
- **The coastal field's CPU texels (R1's #33):** the CPU samples them (`FVoxelOceanCoastField::Sample`). Compacting the mostly-zero atlas changes the shared core's layout, so it is not small.
- **The ocean's clip textures and the old sim textures, "unused for 1978 / 3443 frames"** in the render-target pool: the engine's pool keeps unused targets while its total is under `r.RenderTargetPoolMin` (400 MB; it was at 397 MB). They aren't held by the plugins.
- **Outside the three plugins:**
  - **15 volume textures, 681 MB:** UltraLargeScale's 3D noise (`VT_PerlinCurl_S2` and `_S4` at 146 MB each, `VT_MultiNoise_1_S4` and `_S8`, `VT_Perlin_S4`, `VT_Gradient_Turbulence_l5_256` at 73 MB each), referenced by its galaxy and star assets. 128³ versions or a smaller format would be that plugin's look change.
  - **Nanite's streaming pool (512 MB):** the engine's default `r.Nanite.Streaming.StreamingPoolSize`, a project setting.

## Files
- `Source/VoxelPlanets/Private/VoxelLandBake.cpp`:
  - the registry's `Pending` and `Kept` lists (were `Recent`);
  - `TakeLocked`/`Take`, `FindOrMake`;
  - `GetLandNoise`, `TryGetLandNoise`, `PrewarmLandNoise`, `ReleaseLandNoise`;
  - the cvar;
  - the release log line.
- `Source/VoxelPlanets/Public/VoxelLandBake.h`: `GetLandNoise`'s comment.

## Checked
- No shader and no harness-compiled file is touched.
- Not checked here (no toolchain): the build.

## The capture to take with it
1. **Reseed three or four times, as before the reports.** Expect a "Noise pack ... released" line in the log for each pack a reseed replaced, once the new terrain is up.
2. **A memreport** (`memreport -full`) at the same spots. I'll read:
   - the `Voxel.LandNoise` count in `rhi.DumpResourceMemory`: 1, where there were 4;
   - the structured buffers in `rhi.DumpMemory`;
   - the process's physical memory.
3. **The spawn timeline lines** of those reseeds: "noise pack" should still read at the bake's time.
