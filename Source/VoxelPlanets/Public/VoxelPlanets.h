// VoxelPlanets.h - The planet plugin's module and log category (Design/PlanetPluginSplit.md, S2).

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "Logging/LogMacros.h"
#include "Templates/Casts.h"

/** The planet plugin's log category: the planet, the land, the climate, the ocean. */
VOXELPLANETS_API DECLARE_LOG_CATEGORY_EXTERN(LogVoxelPlanets, Log, All);

namespace VoxelPlanets
{
    /** Game thread. The plugin's asset Name (a package at the content root, the object of the same
     *  name): /VoxelPlanets/Name. Null when it isn't there or the object isn't a Class. */
    VOXELPLANETS_API UObject* LoadPluginAsset(UClass* Class, const TCHAR* Name);

    template<typename T>
    T* LoadPluginAsset(const TCHAR* Name) { return Cast<T>(LoadPluginAsset(T::StaticClass(), Name)); }
}

class FVoxelPlanetsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
