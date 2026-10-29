# Stage P1i: the spawn's last long ticks off the game thread (S2), and F6

S6 was signed off (2026-10-28): the atmosphere from a terrestrial planet's first tick stays, `voxel.Planet.EarlyAtmosphere 1`. The user scoped the rest of P1 to S2, F6 and a memory pass; F4/F5 (ocean shading) are not taken.

This stage changes VoxelPlanets only. `StageP1i.zip` is cumulative from R1a (VoxelPlugin + VoxelPlanets: P1a2, P1c, P1d, P1g and P1h included). CloudAtmosphere stays at `CloudAtmosphere_P1f2.zip`.

## The cost it goes after
**After P1d** (the ice shelf on a worker), a build's longest game-thread tick was **74–119 ms**, in two places:
- **The climate tick:** the snapshot lands, then `RebuildLandClimateTarget`, which is `VoxelLandClimate::BuildMap` + `MakeField` at **61–122 ms**.
- **The `Initialize` tick:** `BuildCompositor`, which is `VoxelDensity::MakeTerrestrialCompositor` at **44–85 ms** (the terrestrial density packs the bake's flow, form, relief and province fields), plus the terrain's and the ocean's initialisation, 15–40 ms.

## What changes
**The climate field is made on the thread pool.** `BuildMap` and `MakeField` are documented as any-thread, and they run on copies of their inputs:
- **copied:** the climate base, a few hundred KB;
- **held:** the sim's climate map;
- **by value:** the settings.

`RebuildLandClimateTarget` now launches the job. `TickClimateFieldJob` applies the result when it lands, on the game thread, with what the old function did after the two calls: the map shown at once or faded in, the field, its base key, the upload.
- **Repeat requests:** a request with the same inputs as the job under way waits for it rather than starting another (the key covers the base, the map's settings, the sim's map, the lapse rate, the ice sheets, the shelf).
- **Superseded jobs:** they are kept until they finish, so their fields are released on the game thread (a field's GPU holder goes through a render command).
- **The build:** it waits for the field (the shelf and the dunes are placed by it), as it waits for the bake and the shelf.
- **The snapshot's status line** ("the sim's climate…" or "no precipitation") is written and logged when the field lands, because only the map knows which it is.

**The compositor is made on the thread pool too, ahead of `Initialize`.** `PrebuildCompositor`, the build's last gate, makes it with exactly `BuildCompositor`'s inputs:
- the terrain's bake (the shelf's, when there is one);
- the climate;
- the planet's shape, root box and lattice depth;
- the same edit-store rule (`HeldEditStore`: the held store for the same planet and lattice, else a fresh one).

`BuildCompositor` takes it when its inputs' key matches, and otherwise builds as before. The density reads only shared, immutable data; the noise pack's registry locks, and the compositor holds the edit store without writing it.

**On a steady planet**, a recapture or a climate-settings change now lands its field a few frames later than before, and the fade starts then. The terrain's rebuild for the dunes follows the field, as before.

**F6, the small exact waste.** The profiles put the rest of F6 at about 0.1–0.3 ms, so one item was taken:
- **Taken:** the clip map's wave pass with no level due (most frames between cascade steps) was dispatched as a full layer of 8×8 groups that all returned at once, only to write the info rows. It is now one group, whose first eight threads write them, exactly as before (R1's P1-2).
- **Left, with reasons:**
  - **The foam's zero slices (OCW-3, about 0.04 ms):** they need slice views threaded through the cascades.
  - **The sim's surface and resample passes at zero substeps (ATM-6):** the resample is the display-time output the clouds move by, so it isn't waste.
  - **The idle polls and the per-frame `BuildParams` (ATM-7, P1-14):** microseconds of CPU on a GPU-bound frame.

## Expected
- **The longest spawn tick:** ~75–120 ms down to about **20–40 ms**. What stays on the game thread is the climate base (25–37 ms), the atmosphere's configuration (~10 ms) and the terrain's and the ocean's initialisation (15–40 ms), each in its own tick.
- **The timeline:** "climate field" and "compositor" ms read ~0 (only the launches). The build takes a few frames longer to reach `Initialize`, because it now waits for the field and the compositor, in parallel with the frames that keep rendering.
- **The terrain and the climate are bit-identical:** the same functions on the same inputs.

## Files
- `Source/VoxelPlanets/Public/PlanetActor.h`:
  - `TickClimateFieldJob`, `FClimateFieldResult`, the field job's members;
  - `PrebuildCompositor`, `HeldEditStore`, `CompositorKey`, the compositor job's members.
- `Source/VoxelPlanets/Private/PlanetActor.cpp`:
  - the build's gates;
  - the asynchronous `RebuildLandClimateTarget` and its apply;
  - the snapshot's status;
  - the climate's off path;
  - `BuildCompositor` taking the prebuild.
- `Source/VoxelPlanets/Private/VoxelOceanPages.cpp`: the clip wave pass's group count with no level due.

## Checked
- No shader and no harness-compiled file is touched.
- Not checked here (no toolchain): the build.

## The capture to take with it
1. **Reseed a few times, cached and cold, and spawn a first build once.** I'll read the timeline lines: "the longest" tick, "climate field" and "compositor" ms, and when "initialized" lands.
2. **The look:** the terrain, the ice shelf and the land's climate (the material's colours, the dunes) should be exactly as before.
