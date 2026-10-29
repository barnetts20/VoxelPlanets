# STAGE K0 NOTES: caves, the look test (2026-10-28)

K0 builds the cave network as a harness prototype, measures it and renders it, before any engine work (`claude/KICKOFF_K0_Caves.md`). The design, the measures and the decisions for the user are in `Design/EditsAndCaves.md`, section "K0 as built". **No engine file changed.**

## Files (all new, `VoxelPlanets/Tools/CaveHarness/`)

| File | What |
|---|---|
| `VoxelLandCaves.ush` | The cave SDF in the shared HLSL/C++ subset: the cell lattice on the bake's, the levels, the branchwork tree, the primitives (passages, keyholes, chambers, shafts, dolines, sea caves), the wall detail, the LOD, depth and cover gates, the brick path (`VoxelCaveGather`, `VoxelCaveEvaluate`), and `VoxelCaveCarve`. K1 moves it to `Shaders/Private` |
| `cave_harness.cpp` | The harness: `gully_harness.cpp`'s planet loader and land chain, the cave bake, and the modes `info`, `probe`, `slope`, `cost`, `brickcost`, `micro`, `mouths`, `seaprobe`, `bricks`, `views` (its head comment) |
| `K0_looks/*.png` | 13 images: sections, plan views of the lowest level, ray-marched views |
| `K0_results/*.txt` | The runs the measures come from |

## Build and run

    g++ -O2 -std=c++20 -ffp-contract=off -pthread $(<VoxelPlanets>/Tools/include_flags.sh) -I. cave_harness.cpp -o cave_harness
    solve_harness 512 n512 dump 1e6        # LandSolveHarness: the 1,000 km planet at 3 km bake cells (~70 s)
    cave_harness n512 512 1e6 cost         # and the other modes; every Cave* parameter can be set from the environment

`n128` works too (64 cave cells a bake cell).

## Results (details in the design doc)

- **Cost (CPU, one thread):** out of the band 0.03 µs a sample (1% of the land chain); outside karst 0.18 µs; in the karst band one sample at a time 3.7 µs (~100%); **the brick path 0.20 µs a sample (5%)** plus 3.7 µs a brick. K1 uses the brick path.
- **Slope:** measured max 1.43 near walls and mouths; analytic worst case 2.8 if every term aligns (under `SlopeBound` 3.5).
- **Entrances:** about 25 per 1,000 km² of karst at the defaults (13 passage mouths, 12 dolines), plus sea caves.
- **Land surface cache column:** flagged texels only at mouths; none elsewhere.
- **Bricks:** camera above ground +0; camera inside a cave +45% wanted bricks (the cave around it). The brick tree must test bricks it will split with the caves at the finest footprint, or it never meshes the cave (K1).

## Checks

- The delivered files build from the plugin folder with the include flags, and run.
- `VoxelLandCaves.ush` compiles as C++ inside a host struct (the CPU density's form) with `-Wall -Wshadow`: no warnings. No unsuffixed float literals, swizzles or out parameters.
- `brickcost`: the brick path and the per-sample path give the same carved density at every sample.
- The exact skips leave the cave volume unchanged (`probe`, before and after).
- **Not run:** the DXC compile of the `.ush` (the session couldn't fetch DXC), and anything on a GPU.

## Also changed (the Project)

- `Design/EditsAndCaves.md`: "K0 as built", the decisions, the stages table.
- `claude/DeadCode.md`: section 6, the cave placeholders K1 replaces (`VoxelCaveDensity`, `VoxelComposeTerrain`).

## For the user

- Look at `K0_looks/` and answer the seven decisions in the design doc.
- Nothing to build in the engine. Commit `Tools/CaveHarness/` if you want the harness kept.
- The transfer folder `ReviewStage/_claude/` holds the snapshot tarball and the K0 working copies (`k0/`); delete it when convenient.
