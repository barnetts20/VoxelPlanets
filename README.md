# VoxelPlanets

Walkable voxel planets for Unreal Engine 5.7, built on the **VoxelPlugin** voxel engine (SDF terrain, dual contouring in LOD bricks on the GPU, edits and collision).

This plugin holds everything specific to planets:
- `APlanetActor`: spawns and drives the terrain, the ocean, the gravity zone and the atmosphere.
- The terrestrial archetype:
  - the land bake (plates, rivers, lakes, landforms);
  - the climate and the climate-shaped surfaces (dunes, glaciers, ice sheets, the polar ice shelf);
  - the land material and its surface cache;
  - the land debug views;
  - the planet density and its GPU passes.
- The ocean: FFT waves, the clipmap and pages, the shore and coast fields, foam, spray, and the underwater pass.

The noise surface is the other surface generator. Further planet archetypes go here as well.

## Dependencies (project plugins)

- **VoxelPlugin:** the engine.
- **RealtimeMeshComponent.**
- **CloudAtmosphere:** the atmosphere, the weather and the climate snapshot.
- **GravPlugin:** gravity zones.
- **UltraLargeScale:** pooling and star light.

## Layout

| Path | Contents |
|---|---|
| `Source/VoxelPlanets` | the module |
| `Shaders/Private` | the planet shaders, mapped to `/Plugin/VoxelPlanets/Private`; they include the engine's from `/Plugin/VoxelPlugin/Private` |
| `Content` | the planet materials (`MT_TriPlanar`, `MT_Ocean`), the ocean's default texture array, the ocean profile |
| `NoiseSets` | the authored noise sets (`*.vxnt`, not in git; see `NoiseSets/README.txt`) |
| `Config/DefaultVoxelPlanets.ini` | redirects for maps and assets saved before the split |
| `Tools` | plain C++ harnesses and the offline shader check |

For the tools:
- `Tools/include_flags.sh` gives the include paths of both plugins.
- `Tools/ShaderCheck/shader_check.sh <Plugins folder>` compiles both plugins' shaders and the materials' Custom-node code with DXC and glslang.

The design notes live in the project's docs. `Design/PlanetPluginSplit.md` covers the split; the `STAGE_*_NOTES.md` files record each delivered stage.
