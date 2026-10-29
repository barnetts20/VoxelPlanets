#include "VoxelPlanets.h"
#include "VoxelLandBake.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"
#include "UObject/UObjectGlobals.h"
#include "Misc/CoreDelegates.h"

#define LOCTEXT_NAMESPACE "FVoxelPlanetsModule"

DEFINE_LOG_CATEGORY(LogVoxelPlanets);

namespace VoxelPlanetsModuleDetail
{
    FDelegateHandle NoiseExitHook;
}

UObject* VoxelPlanets::LoadPluginAsset(UClass* Class, const TCHAR* Name)
{
    const FString Path = FString::Printf(TEXT("/VoxelPlanets/%s.%s"), Name, Name);
    return StaticLoadObject(Class, nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
}

void FVoxelPlanetsModule::StartupModule()
{
	// Where the authored noise sets are (found here, on the game thread, for the workers that
	// read them).
	VoxelLand::NoiseSetDirectory();
	// The shared noise packs dropped before the RHI goes (each pack's GPU copy is released on the
	// render thread). R2 (C1): registered here, on the game thread, not by the first pack made
	// (a pool worker: core delegates aren't thread-safe).
	VoxelPlanetsModuleDetail::NoiseExitHook = FCoreDelegates::OnEnginePreExit.AddStatic(&VoxelLand::ReleaseLandNoise);

	// The module loads at PostConfigInit (VoxelPlanets.uplugin) so its global shaders register
	// before the engine compiles them. Map /Plugin/VoxelPlanets to the plugin's Shaders folder
	// unless the engine already mapped it.
	const FString VirtualDirectory = TEXT("/Plugin/VoxelPlanets");
	if (AllShaderSourceDirectoryMappings().Contains(VirtualDirectory)) return;

	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("VoxelPlanets"));
	if (ensureMsgf(Plugin.IsValid(), TEXT("VoxelPlanets: plugin not found; shaders are not mapped.")))
		AddShaderSourceDirectoryMapping(VirtualDirectory, FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders")));
}

void FVoxelPlanetsModule::ShutdownModule()
{
	// The shared noise packs (VoxelLandBake.cpp), if the engine's pre-exit didn't drop them.
	FCoreDelegates::OnEnginePreExit.Remove(VoxelPlanetsModuleDetail::NoiseExitHook);
	VoxelLand::ReleaseLandNoise();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FVoxelPlanetsModule, VoxelPlanets)
