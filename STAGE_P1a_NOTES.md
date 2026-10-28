# Stage P1a: instrumentation for the performance pass (measurement only)

**P1a2 (2026-10-28, 11:05):** P1a crashed on loading a planet. RDG asserts when an event scope is still open at `GraphBuilder.Execute()`, and the new GPU-stat scopes in five render commands that build their own graph were opened at function level: the bake's plate step, the river job (`TickRiverJob`, the reported stack), the land cache write (`FVoxelLandCache::Dispatch`, the other one), the shore field and the coast write. Each is now closed in a block before `Execute()`, as `SampleCoastDepths` already did. The atmosphere's two readback scopes (`CloudAtmosphereReadback`) are dropped. The other new rows were existing `RDG_EVENT_SCOPE`s inside pass functions, which close before their callers execute, so they are unchanged. `StageP1a2.zip` and `CloudAtmosphere_P1a2.zip` are cumulative.

P1's first stage (`claude/KICKOFF_P1_Performance.md`: "measure before changing anything"). Before it, an Unreal Insights trace showed the three plugins almost blind: no `TRACE_CPUPROFILER_EVENT_SCOPE` anywhere, no `stat gpu` row for the atmosphere, the sim, the land cache or the ocean's shore field, and the spawn's gates (bake → climate → field/shelf → `Initialize`) as one opaque `APlanetActor` tick. P1a adds the scopes and a per-build spawn timeline. **Nothing it adds changes what is computed or drawn.** Its cost is near zero: a trace scope is one branch when the CPU channel is off, and the timeline does a few comparisons per planet tick.

Two zips, full files, CRLF, cumulative from R1a: `StageP1a.zip` (VoxelPlugin + VoxelPlanets) and `CloudAtmosphere_P1a.zip`.

## What it adds

**1. CPU trace scopes (Insights' CPU track), ~210 of them.** Every function on the spawn path and the per-frame paths in `Design/CodeMap.md` §3–4 is named after itself:
- **The planet:** `APlanetActor::Tick`, `TickLandBake`, `UpdateClimateCapture`, `EnsureLandClimateBase`, `RebuildLandClimateTarget`, `UpdateIceShelf`, `Initialize`, `BuildCompositor`, `ConfigureOcean`/`Atmosphere`/`AtmosphereSurface`, `TickLandClimate`.
- **The bake:** `FVoxelLandBaker::Tick`/`StartGpu`/`Finish`, `VoxelLandCache::Load`/`Save`, `VoxelLandNoise::Read`/`MakePackLocked`, `VoxelLand::TickRivers`, `FVoxelLandBake::BuildSeaConnected`/`BuildRelief`, `VoxelLandClimate::BuildBase`/`BuildMap`/`MakeField`/`MakeIceShelfBake`.
- **The terrain and its material:** `AAdaptiveVoxelActor::Tick`/`Rebuild`/`TickCollision`, `FVoxelBrickPages::*` (`Flush`, `PreparePages`, `Commit`, `Stage`, `DispatchBatches`, …), `FVoxelBrickPageGPU::Run_RenderThread`, `FVoxelCollisionBricks::*`, `FVoxelEditGPUData::Build`, the terrain material and land cache.
- **The ocean:** `AOceanSphereActor::Tick` and its sections, `FVoxelOceanSurface::*`, `FVoxelOceanPages::UpdateClipmap`/`ProbeDepth`/`SampleCoastDepths`/`Flush`, the shore and coast builders, the cascades, the surface pass and the underwater pass.
- **The atmosphere:** `APlanetAtmosphereActor::Tick` and its sections, `UFlowSimSubsystem::Advance`/`BuildParams`/`StepSimulation`/`ResetSimulation`/`ResolveClaims`, the generator, the view extension's render functions, the readbacks.
- **Render commands:** every substantial `ENQUEUE_RENDER_COMMAND` body carries an `RT <name>` scope, so render-thread time is attributed.
- The harness-compiled files (`VoxelOceanWavesFFT.cpp`, `VoxelOceanWeatherTable.cpp`) are left alone; their callers are scoped.

**2. `stat gpu` rows (`DECLARE_GPU_STAT` + `RDG_EVENT_SCOPE_STAT`).**
- **New:** `CloudAtmosphereMarch`, `CloudAtmosphereFlowSim`, `CloudAtmosphereShadowBake`, `CloudAtmosphereTransmittance`, `VoxelLandCacheWrite`, `VoxelOceanShoreField`, `VoxelOceanCoastWrite`, `VoxelLandBakeGPU`.
- **Existing:** the mesher, page writes, cascades, clipmap, rings, surface and underwater rows.

**3. The spawn timeline** (`PlanetSpawnTimeline.h/.cpp`, a member of `APlanetActor`). It starts on the tick a build starts (a first build, or a rebuild) and ends when the terrain's first fill has settled and, on a terrestrial planet with an ocean, the shore field is in. It also ends 90 s after `Initialize`, saying so, if the camera keeps the terrain busy. It logs one line:

```
[Planet] <name> spawn timeline (first build), seconds from the start: bake 0.71 (disk cache), rivers 0.86, noise pack 0.71,
climate capture started 0.88, atmosphere on 0.88, climate 12.40 (snapshot), initialized 13.62, first terrain 13.90,
terrain settled 16.20, shore field 19.05, coast field 21.30. Game thread ms: bake gate 45, climate base 52,
atmosphere config 120, climate field 80, ice shelf 640, compositor 30, terrain init 220, ocean init 150,
Initialize in all 410; the planet's ticks 900, 1600 ms in all, the longest 1050 ms.
```

Each milestone is also a **bookmark** in Insights (`Planet <name>: <milestone>`), so the trace's timeline is labelled.

**Engine additions** (read-only accessors, for the timeline):
- `FVoxelBrickTerrain::NumOnScreen()`;
- `FVoxelBrickTerrain::IsSettled()`: the latest launch check found nothing to do, and nothing is committing, held, running or backlogged; a new `bIdle` flag;
- `AOceanSphereActor::HasShoreField()`/`HasCoastField()`;
- `FVoxelLandBaker::GetSource()` (`disk cache` / `GPU bake` / `CPU bake`).

## Checked
- No shader, no shared `.ush` and no harness-compiled file is touched: the harnesses and the shader check are unaffected by construction.
- Every scope was placed by a script and its placement reviewed in the diff. One GPU-stat declaration that landed inside a struct (`VoxelLandBake.cpp`, after an `.ush` include inside `FVoxelLandPlatesCPU`) was moved to file scope.
- Unity build: the planet's helpers are in `PlanetActorTimelineDetail`, the timeline's in `PlanetSpawnTimelineDetail`. The GPU stat names are unique across the plugins.
- Not checked here (no toolchain): the build itself. The likeliest compile risk is a missing include for `DECLARE_GPU_STAT` in a CloudAtmosphere file. Each file that gets one already uses RDG event scopes, whose header declares it.

## The captures to take with it
1. **Insights:** launch with `-trace=default,gpu,task -statnamedevents` (or in the editor: `Trace.Start default,gpu,task` and `stat namedevents`), spawn a planet, then fly fast and low along a coast for ~20 s; `Trace.Stop`.
2. **The log of two spawns:** one with the bake cached, one cold (delete `Saved/VoxelLand/Bake_*.vlb` or change the seed). The timeline line is the summary; keep the `LogPSOHitching` lines.
3. **Frame stats** at the four spots (A: 2–5 m over sea near a coast; B: ~100 m, fast over land; C: along a coast at ~100 m; D: 5–10 km up): `stat unit`, `stat gpu` (now with the atmosphere's rows), `stat rhi`, `stat VoxelTerrain`; a `ProfileGPU` at A and B; `voxel.Ocean.PerfSweep 120 3` at A; `voxel.Debug.BrickPerf 1` during B.
