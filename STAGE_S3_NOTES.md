# Stage S3: the content

This is the last stage of the planet plugin split (`Design/PlanetPluginSplit.md`). The planet's assets move to VoxelPlanets, and the materials include the planet shaders from their new place.

Unzip StageS3.zip into your `Plugins` folder over StageS2. It holds six source files and these notes.

## The code (this package)

- **VoxelPlanets loads its assets from `/VoxelPlanets/` first**, falling back to the old `/VoxelPlugin/` path. This uses the new `VoxelPlanets::LoadPluginAsset`, for the planet's default terrain material (`MT_TriPlanar`) and ocean material (`MT_Ocean`). The order of the steps below therefore doesn't matter, and the fallback can go once you've moved the assets.
- **The engine's terrain actor no longer defaults to `MT_TriPlanar`**, which is the planet's land material. A standalone terrain given no material now draws Unreal's default material. A planet always sets its own.

## In the editor

1. **Build with this package** first.
2. **Move the planet assets.**
   - In the Content Browser, turn on Settings → Show Plugin Content.
   - Select these in `VoxelPlugin Content`, drag them to `VoxelPlanets Content`, and choose Move Here:
     - `MT_TriPlanar`;
     - `MT_Ocean`;
     - `T_VoxelOceanArrayDefault` (in the same move as `MT_Ocean`, so its reference follows);
     - `VoxelOceanProfile`.
   - **Keep `MPC_VoxelMorph` in VoxelPlugin.** The brick terrain writes it.
   - `MT_Surface`, `MT_TestTransparent` and `TestRT` are yours to keep or delete. Nothing in the code uses them.
3. **Fix the redirectors.** Right-click the VoxelPlugin content folder → Fix Up Redirectors in Folder.
4. **Repoint the Custom nodes** in `MT_TriPlanar` and `MT_Ocean`, and in any other material of yours that uses them. In each Custom node's Include File Paths, change `/Plugin/VoxelPlugin/Private/` to `/Plugin/VoxelPlanets/Private/` for:
   - `VoxelLandMaterial.ush`;
   - `VoxelLandClimateMaterial.ush`;
   - `VoxelOcean.ush`.

   Leave `/Plugin/VoxelPlugin/Private/VoxelMorph.ush` as it is. Apply and save each material, and check it compiles.
5. **Delete the three forwarding stubs** once the materials compile with the new paths:
   - `Plugins/VoxelPlugin/Shaders/Private/VoxelLandMaterial.ush`;
   - `Plugins/VoxelPlugin/Shaders/Private/VoxelLandClimateMaterial.ush`;
   - `Plugins/VoxelPlugin/Shaders/Private/VoxelOcean.ush`.

   Then restart the editor. A material still on an old path now fails to compile, which tells you it missed step 4.
6. **The old cloud material copies** in `VoxelPlugin/Content/Material` (`MT_UCA_*`, `MT_UnifiedCloudAtmosphere`, `MT_Luminous`) and `VolumeTextures`. Check them with the Reference Viewer:
   - if nothing references them, delete them;
   - if something does, they belong to CloudAtmosphere, not to either voxel plugin.
7. **Resave the planet maps** (File → Save All). They then hold the new asset paths and the `/Script/VoxelPlanets` types.

## Done when

- A planet map loads and plays as before: the terrain with its land material and debug views, the ocean, the atmosphere, the climate.
- VoxelPlugin's content holds only `MPC_VoxelMorph`, plus anything you chose to keep.
- No Custom node points at a planet shader under `/Plugin/VoxelPlugin/`.

When you confirm all that, I'll remove two things that are then dead code:
- the `/VoxelPlugin/` fallback in `LoadPluginAsset`;
- the CoreRedirects. Keep these while any unsaved map or asset could still need them; they cost nothing.

## The clean-up (StageS3b)

After the content moved:
- **`VoxelPlanets::LoadPluginAsset`:** it loads from `/VoxelPlanets/` only; the `/VoxelPlugin/` fallback is gone.
- **The forwarding stubs:** the three `.ush` stubs are deleted from VoxelPlugin. Delete them on your side too, if you haven't yet.
- **`README.md`:** VoxelPlanets has one, for its new repository.
- **The CoreRedirects stay.** Unsaved maps or assets elsewhere may still need them, and they cost nothing.
