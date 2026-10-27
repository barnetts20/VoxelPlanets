// VoxelPlanets: walkable voxel planets on the VoxelPlugin engine (Design/PlanetPluginSplit.md).

using System.IO;
using UnrealBuildTool;

public class VoxelPlanets : ModuleRules
{
	public VoxelPlanets(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		// The shared HLSL/C++ files (VoxelShaderCompat.h) are included from the C++ too: the
		// planet's here, the engine's through VoxelPlugin's public include path.
		PrivateIncludePaths.Add(Path.Combine(PluginDirectory, "Shaders", "Private"));

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"RenderCore",
				"InputCore",
				"RHI",
				"Renderer",
				"Projects",
				"RealtimeMeshComponent",
				"RealtimeMeshCompute",
				// The voxel engine: the terrain actor, the brick terrain, the mesher, the edits.
				"VoxelPlugin",
				// APlanetGravityZone.
				"GravPlugin",
				// APlanetActor (IPooledActor, IStarLit).
				"UltraLargeScale",
				// The atmosphere glue, the ocean's weather and light, the land climate.
				"CloudAtmosphere",
			}
			);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
				"SlateCore",
			}
			);

		// The authored noise sets (VoxelLand::NoiseSetDirectory, Design/NoiseTiles2D_Handoff.md):
		// read as files at runtime, so staged with a cooked game.
		RuntimeDependencies.Add(Path.Combine(PluginDirectory, "NoiseSets", "*.vxnt"), StagedFileType.UFS);
	}
}
