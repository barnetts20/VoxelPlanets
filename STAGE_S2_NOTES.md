# Stage S2: VoxelPlanets

This is the third stage of the planet plugin split (`Design/PlanetPluginSplit.md`). The planet code now lives in its own plugin, `Plugins/VoxelPlanets`, built on the engine in `Plugins/VoxelPlugin`. Nothing should look or play any differently.

**VoxelPlugin**, the engine, keeps:
- the SDF edits and the dual-contouring mesher, on the CPU and the GPU;
- the brick terrain and its pages, and the collision bricks;
- the terrain actor `AAdaptiveVoxelActor`;
- the density seam, the owner interface, the lattice helpers and the test sphere.

**VoxelPlanets** gets everything else:
- the planet actor, its terrain actor and its gravity zone;
- the terrestrial archetype: the land bake, the climate, the land material and its cache, the land debug views, the planet density and its GPU passes;
- the ocean and the underwater pass;
- the noise sets;
- the planet tools.

## Install

Unzip **StageS2.zip into your `Plugins` folder**. It holds `VoxelPlugin\` and `VoxelPlanets\`, plus `StageS2_Integrate.ps1` next to them. Then:

1. **Run the integration script** from the Plugins folder:
   - `powershell -ExecutionPolicy Bypass -File .\StageS2_Integrate.ps1`
   - Add `-WhatIf` first if you want to see what it would do. You can delete the script once it has run.

   It does three things:
   - **Moves your own copies, which the package doesn't carry:**
     - `VoxelGPUCommon.h` goes from the engine's `Private` to its `Public` folder;
     - the `NoiseSets` folder (the land and foam sets) goes from VoxelPlugin to VoxelPlanets. A set already there is left alone, with a warning.
   - **Deletes the 236 files that moved** from VoxelPlugin (the list is in the script, and it touches nothing else).
   - **Removes the folders left empty.**
2. **Delete `Plugins\VoxelPlugin\Binaries` and `Plugins\VoxelPlugin\Intermediate` before the first build.** This step is required. The old module's generated code for the moved classes (`PlanetActor.gen.cpp` and the rest) would otherwise linger there and include headers VoxelPlugin no longer has.
3. **Check your project's references:**
   - If your `.uproject` lists plugins explicitly, add `{ "Name": "VoxelPlanets", "Enabled": true }`. The plugin is also enabled by default.
   - If any of your own modules use the planet types in C++, add `"VoxelPlanets"` to their `Build.cs`. Those types are `APlanetActor`, `AOceanSphereActor`, `UVoxelOceanProfile` and so on.
4. **Regenerate the project files and build.**

Your `Shaders/Private/VoxelDF64.ush` stays in VoxelPlugin as it is.

## Loading old maps and assets

`VoxelPlanets/Config/DefaultVoxelPlanets.ini` redirects every reflected type that moved (16 of them) from `/Script/VoxelPlugin` to `/Script/VoxelPlanets`:
- **Classes:** `APlanetActor`, `APlanetTerrainActor`, `APlanetGravityZone`, `AOceanSphereActor`, `UVoxelOceanProfile`;
- **Structs:** the settings, climate and sample structs;
- **Enums:** the three enums.

A plugin's own self-named ini is where Unreal reads its core redirects. Saved planets, ocean profiles and Blueprints should load without changes.
- **Resave the planet maps and the ocean profiles once** after this works.
- **If a level warns that it can't find `/Script/VoxelPlugin.PlanetActor`,** or a similar class, copy the `[CoreRedirects]` section into your project's `Config/DefaultEngine.ini`, then tell me.

## The materials

The materials' Custom nodes include `/Plugin/VoxelPlugin/Private/VoxelLandMaterial.ush`, `VoxelLandClimateMaterial.ush` and `VoxelOcean.ush`. Those files moved. To keep the materials compiling, VoxelPlugin keeps a one-line forwarding stub of each, which includes the real file from `/Plugin/VoxelPlanets/Private/`.
- S3 points the Custom nodes at the new paths and deletes the stubs.
- The content (`MT_TriPlanar`, `MT_Ocean`, `MPC_VoxelMorph` and the rest) stays in VoxelPlugin until S3, so the code still loads it from `/VoxelPlugin/...`.
- **Until S3, keep VoxelPlanets enabled whenever VoxelPlugin is.** The stubs and the materials that use them need VoxelPlanets' shaders.

## What changed

**VoxelPlugin:**
- **`VoxelPlugin.uplugin`:** depends on RealtimeMeshComponent only.
- **`VoxelPlugin.Build.cs`:**
  - no GravPlugin, UltraLargeScale or CloudAtmosphere, and no noise-set staging;
  - its `Shaders/Private` is now a public include path, because VoxelPlanets' C++ includes `VoxelDF64.ush` and `VoxelCubeSphere.ush`.
- **`VoxelPlugin.cpp`:** no planet calls left. The noise-set directory and the noise packs' release moved to VoxelPlanets' module.
- **`VoxelEditCore.h` and `VoxelGPUCommon.h`:** now in `Public`.
- **`CVarVoxelDebugLevelColor`:** exported, because the ocean pages read it.
- **The three forwarding stubs** described above.
- **`Tools`:** only `EditHarness/edit_harness.cpp` remains; the rest moved.

**VoxelPlanets:**
- **New:**
  - `VoxelPlanets.uplugin`: depends on VoxelPlugin, RealtimeMeshComponent, CloudAtmosphere, GravPlugin and UltraLargeScale;
  - `VoxelPlanets.Build.cs`;
  - the module (`VoxelPlanets.h/.cpp`): it maps `/Plugin/VoxelPlanets` to its shaders and finds the noise sets at startup;
  - the redirects.
- **The moved files, renamed:**
  - `VOXELPLANETS_API`;
  - the log category `LogVoxelPlanets`. Filter by it in the output log; the message tags (`[VoxelLand]`, `[VoxelOcean]` and so on) are the same.
  - the planet shaders' virtual paths, `/Plugin/VoxelPlanets/Private/...`. Planet shaders still include the engine's shaders from `/Plugin/VoxelPlugin/Private/...`.
  - the noise sets are found in VoxelPlanets' folder.
- **Tools:**
  - **`Tools/include_flags.sh`** prints both plugins' include paths, and the harnesses' build lines use it.
  - **`Tools/ShaderCheck/shader_check.sh`** now takes the Plugins folder and checks both plugins' shaders and the materials' code.

## What to check in the engine

- **Build:** both plugins build, with no link errors in VoxelPlanets.
- **Shaders and noise sets:**
  - the log shows the noise sets found under `Plugins/VoxelPlanets/NoiseSets`;
  - the shaders compile, including the material stubs;
  - the land, ocean and climate look as before.
- **A planet map loads:** the planet, the terrain, the ocean and the gravity zone all as before, with nothing missing in the details panels (the settings structs carried over).
- **An ocean profile asset still opens.**

## Checks run here

- **Shader check:** DXC and glslang over every `.usf` of both plugins, plus the material stand-ins (land, cached land, climate, ocean vertex and pixel): fails=0.
- **Harnesses, built with the new include flags:**
  - all of them were syntax-checked before and after the move, and give the same results;
  - the edit and cache-edit harnesses ran and passed;
  - so did the climate check and the ocean weather check.
- **Independent review:** found no link, compile or load problems. Four small fixes went in:
  - explicit includes for four files that might have relied on the old unity grouping (`Misc/ScopeLock.h`, `RenderingThread.h`);
  - a `.gitignore` for VoxelPlanets;
  - stale comments;
  - the edit harness's include line.
- **Not checked here:** the UE compile and link.
