# Stage T5f: rivers in the climate

Rivers through deserts now get green banks. There are two parts, and neither adds a pass or a texture.

1. **The weather sim's moisture.**
   - The atmosphere's surface map already carries the land's share; the sim's land evaporates less than water. That share now counts:
     - a kept lake as water;
     - a river's cells as part water, by their drainage.
   - So large rivers and lakes add moisture to the air around them, at the sim's scale.
   - The map is built once per bake on the CPU. The cost is a few extra lookups per point.
2. **The riparian ground, in the land climate.**
   - Water from upstream wets the land along a river whatever the rain falls there, as with the Nile's valley through the Sahara.
   - The climate's aridity (log2 of Holdridge's PET ratio) is reduced by up to 6 by the drainage at the point. It starts at 200 km² of drainage (the default `RiverMinArea`) and is full at 10,000 km².
   - At full it turns the driest desert humid: no desert and full plant cover where the summer is warm, along the river.
   - The drainage comes from the land node's existing `Fields` output, already computed per pixel, so the cost is one ramp in the climate node.
   - The drainage falls off within about one bake cell of a channel, and more slowly along larger rivers. So the green band is about a cell wide, wider along large rivers. That is about 1.5 km on a 1,000 km planet, and about 10 km at Earth's size.

**Not changed:** the dunes. They are placed by the climate map's desert at the bake's height (T5c), so sand seas still reach a river's valley. The rivers' own carve already flattens the valley floor.

Unzip StageT5f.zip into your `Plugins` folder.

## In the editor: one rewire in the terrain material

In `MT_TriPlanar`, on the climate Custom node (the one that includes `VoxelLandClimateMaterial.ush`):
1. **Add an input** named `Fields`.
2. **Connect it** to the land node's `Fields` output (the land Custom node's additional output of that name).
3. **Change the Code line** to:
   ```
   return VoxelLandClimateMaterial(Parameters, ClimateTex, OriginHigh, OriginLow, AxisX, AxisY, AxisZ, NormalWS, Fields, BaseColor, ClimateParams, ClimateShape, ClimateTint, Climate, Cover);
   ```

The old call, without `Fields`, still compiles. It just leaves the rivers out, so the material works before and after the rewire.

## Settings

**`APlanetActor::AtmosphereRiverWater`** (Planet | Atmosphere, default 0.5). In the sim's moisture, a river cell counts as this share of water at 50 times the bake's `RiverMinArea` of drainage, ramping up from `RiverMinArea`. 0 counts only the sea.
- A kept lake always counts as water.
- Changing it captures the climate again, as a change of the terrain height does.

**The riparian term's constants** are in `Shaders/Private/VoxelLandClimate.ush`:
- `VOXEL_LAND_CLIMATE_RIPARIAN_START` and `_FULL`: the drainage range, log2 m².
- `_WETTING`: the aridity taken off at full.

Change them there if the band should start at smaller rivers or green less.

## What changed

- **`VoxelLandClimate.ush`:**
  - `VoxelLandClimateAt` takes the drainage (log2 m², 0 none);
  - the point has `Riparian` (0..1), already applied to `Aridity`;
  - the constants above.
- **`VoxelLandClimateMaterial.ush`:** the node takes `Fields`; the old call is kept as an overload without it.
- **`VoxelLandClimate::PointAt`:** takes the drainage (default 0). The planet's `GetClimateAt` passes the bake's at the point, so gameplay sees the same green banks.
- **`APlanetActor`:**
  - `ConfigureAtmosphereSurface` counts lakes and rivers as water;
  - the new `AtmosphereRiverWater` property;
  - the surface map's and the climate capture's keys follow it and `RiverMinArea`.
- **`Tools/LandClimateHarness/climate_check.cpp`:** a riparian check.
- **`Tools/ShaderCheck/Material/climate_material.hlsl`:** checks both calls.

## What to check in the engine

1. **Rewire the climate node** as above.
2. **Find a large river through a desert.** `voxel.Debug.LandView 19` shows the rivers. With the climate on, its banks should be green and not desert, over about a cell either side.
3. **Check the aridity view.** `voxel.Debug.LandClimate 4` (aridity) shows the river's line humid through the dry belt.
4. **Wait for the climate capture.** Once the climate captures again (the surface map changed), the precipitation map (`voxel.Debug.LandClimate 3`) may show slightly more rain downwind of large rivers and lakes. That part is subtle by design; the bands come from the riparian term.

## Checks run here

- **Climate harness:** all OK. It includes the new riparian check:
  - a hot desert cell has aridity 4.8 without a river;
  - 2.6 at 1,000 km² of drainage;
  - −1.2 at 10,000 km², with full plant cover and no desert;
  - no change below 200 km².
- **Shader check:** fails=0 on the land cache writer, the terrestrial mesher, the density compute and the materials (land, cached land, climate with and without `Fields`, ocean).
- **Harness syntax:** the gully and dune harnesses compile unchanged.
- **Not checked here:** the UE compile.
