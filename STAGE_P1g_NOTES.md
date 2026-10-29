# Stage P1g: the ocean's wave model at a new reach wind without a rebuild (C1)

**The decision on F3 (the atmosphere march):** the user chose to rely on the High preset for the march (its 3×3 march cell runs a ninth of the pixels a frame, against Max's quarter). The march keeps P1f's exact gain. P1e's measurement tools stay in: `r.CloudAtmosphere.March.Ablate` and `CloudAtmosphere.MarchSweep`, which cost nothing at 0.

This stage changes VoxelPlanets only. `StageP1g.zip` is cumulative from R1a (VoxelPlugin + VoxelPlanets: P1a2, P1c and P1d included). CloudAtmosphere stays at `CloudAtmosphere_P1f2.zip`.

## The cost it goes after
**The spike:** in fast flight the ocean actor's tick spiked to **17.9 ms** on the game thread (baseline, `Design/Performance.md`, C1). R1 (OCN-23) found the cause in `FVoxelOceanSurface::UpdateWaveModel`, which rebuilt the whole FFT model synchronously whenever the weather's greatest long-wave speed crossed a 2 m/s step of the reach's wind. The rebuild covers:
- the slots, and their statistics over (2R+1)² modes;
- the short bands' curve (the slots made again at every knot);
- the resolved shares;
- the 393k-entry chart table (`tan`/`atan` per entry);
- seven query cascade prototypes.

**None of that depends on the wind.** The wind enters only the reach: the height and horizontal bounds (`MaxHeightCm`, `MaxHorizontalCm`).

## What changes
**A new constructor, `FVoxelOceanWavesFFT(const FVoxelOceanWavesFFT& Base, double ReachWindSpeed)`:**
- **Shared:** the chart table and the query prototypes (both immutable; the chart table now sits behind a shared pointer). The query frames already computed are shared too, since they don't depend on the wind.
- **Copied:** the slots, the slot table and the table's constants.
- **Recomputed:** only the reach (`SetReach`, the same lines the full constructor runs).

So the model it makes is the one a full build at that wind would make.

**`UpdateWaveModel` uses it** when the held model is the FFT model at the same radius and the only setting that changed is the reach's wind. Anything else still builds a new model, as before. `Cascades->SetSlots` sees the same slot table and does nothing, as it already did.

## Expected
- **The 15–20 ms spike** at each reach-wind step becomes well under a millisecond: a few small copies and the reach's sums.
- **Every other spike in the ocean's tick is unchanged.** If one is left, its trace (`AOceanSphereActor::Tick` and its sections, P1a) will show it.

## Files
- `Source/VoxelPlanets/Private/VoxelOceanWavesFFT.h`, `.cpp`:
  - the reach constructor;
  - `SetReach`;
  - `Charts` as `TSharedPtr<const VoxelOceanCharts::FTable>` (`GetCharts()` unchanged).
- `Source/VoxelPlanets/Private/VoxelOceanSurface.cpp`: `UpdateWaveModel` takes the held model for a reach-only change.
- `Tools/OceanHarness/hook_check.cpp`: the reach constructor's check.

## Checked
- **The harnesses** (`VoxelOceanWavesFFT.cpp` is harness-compiled): the whole default set was run on this tree. 58 pass, 0 fail, 9 skipped as in the baseline, and every output is identical to the R1a baseline's.
- **A new check in `Tools/OceanHarness/hook_check.cpp`, "The reach constructor":** the model made from the held one at a wind two steps up (28 m/s) against a full build at that wind. The reach, the settings, the charts, the slot table and the table's constants are all compared, and so are the shader parameters, the queried height and displacement, and the local reach at 300 random points. **Every value is identical, bit for bit.**
- **Exact by construction:** the shared parts are what a full build computes from the same settings bar the wind. `SetReach` is the full constructor's code, moved into a function.
- Not checked here (no toolchain): the editor build.

## The capture to take with it
1. **Fast, low flight over the sea and along a coast**, where the weather changes (the reach's wind steps there), with `stat unit` up.
   - Optionally an Insights trace (`Trace.Start default`): the ocean actor's tick should show no `UpdateWaveModel` spike.
   - The wave model logs nothing when it changes, so the trace or `stat unit`'s game-thread line is the check.
2. **F8's measurement, in the same session:** hover over open sea at about 1, 3 and 10 m above the water, a few seconds each, with `stat gpu` up. The `VoxelUnderwater` row at each height decides whether F8 has anything left to do. Its probe already skips the pass once the camera stands clear of the local crests.
