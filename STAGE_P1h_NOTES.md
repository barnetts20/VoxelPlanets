# Stage P1h: the atmosphere from a terrestrial planet's first tick (S6); F8 closed

This stage changes VoxelPlanets only. `StageP1h.zip` is cumulative from R1a: VoxelPlugin and VoxelPlanets, with P1a2, P1c, P1d and P1g included. CloudAtmosphere stays at `CloudAtmosphere_P1f2.zip`.

## F8: nothing left to do
The baseline measured the underwater pass at 2.4 ms when the camera was 2–5 m over the sea, 2.0 ms of it the 256²×5 surface map. On P1g, hovering over open sea:

| Camera height | `VoxelUnderwater` |
|---|---|
| ~1 m | **0.17 ms** (the camera probe alone) |
| 3 m | not run |
| 10 m | not run |

The pass's own skips already cover it:
- the local reach (W6i);
- the camera probe's reading of the crests round the camera's foot (W8b).

The baseline's 2.4 ms was a camera within reach of a rough sea's crests, where the pass is needed. **F8 is closed with no change.**

## S6: what it goes after
A terrestrial planet's first build showed nothing until its land bake was in:
- the bake took 0.8 s from the disk cache, 1.7–1.9 s cold;
- the atmosphere was configured only when the climate capture started, after the bake, at **1.0–1.9 s**.

The kickoff (`claude/KICKOFF_P1_Performance.md`) asks for the atmosphere first, as the placeholder. Nothing the atmosphere draws needs the bake except its surface map, the ground under its weather.

## What changes
**On the first build of a terrestrial planet**, `APlanetActor::Tick` now spawns the children and calls `ConfigureAtmosphere` on the very first tick, before the bake gate.
- **The floor:** the sea's sphere, which is the actor's radius. That is the floor the capture's configuration gives it later (the terrestrial generator's floor is the sea), so it doesn't move.
- **The surface map:** none yet. `ConfigureAtmosphereSurface` clears it without a bake.

When the bake is in, the climate capture does what it did before:
- it configures the atmosphere again, which is now the same seed, so there's no regeneration;
- it gives it the bake's surface map;
- it restarts its sim for the snapshot.

**Off for:** rebuilds and reseeds (the atmosphere is already on), noise planets (no bake to wait for), and a planet with its atmosphere off. `voxel.Planet.EarlyAtmosphere 0` restores R1a's order for the A/B.

## Expected and the look trade
- **The timeline:** "atmosphere on" at ~0 s on a first build, instead of 1.0–1.9 s. `Initialize` and the first terrain should land as before.
  - Watch the cold bake's time, though. The atmosphere's sim now spins up on the GPU while the bake runs, so a cold bake could take a little longer.
- **The look, for your sign-off:** in the first second or two, the sky and clouds show over a planet with no terrain yet. When the capture starts, the clouds roll anew, because the sim restarts on the real ground.
  - That restart already happened before. It's just now visible, because there's a sky to see it in.
  - If the change of clouds at ~1–2 s bothers you, `voxel.Planet.EarlyAtmosphere 0` is R1a's order.

## Files
`Source/VoxelPlanets/Private/PlanetActor.cpp`: the cvar, and the early configuration in `Tick`.

## Checked
- No shader and no harness-compiled file is touched.
- Not checked here (no toolchain): the build.

## The capture to take with it
1. **Spawn a planet fresh** (a first build, not a reseed): once from the cache, once cold (a new seed, or delete the bake). I'll read the spawn timeline lines from your log: "atmosphere on", "bake" and "initialized" against the baseline.
2. **The look** in those first seconds, with `voxel.Planet.EarlyAtmosphere` at 1, then at 0.
