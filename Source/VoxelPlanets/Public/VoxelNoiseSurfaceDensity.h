// VoxelNoiseSurfaceDensity.h - The noise surface: a planet density (FVoxelPlanetDensity) whose
// heightfield is three layers of gradient noise over the planet radius (continents, ridged
// mountains, slope-damped detail; FVoxelSurfaceSettings), the CPU evaluation of
// VoxelDensityCore.ush's composition.

#pragma once

#include "CoreMinimal.h"
#include "VoxelPlanetDensity.h"
#include "VoxelDensityLayers.h"

/** The noise surface on the CPU. Immutable after construction; safe to call from any thread. */
class VOXELPLANETS_API FVoxelNoiseSurfaceDensity : public FVoxelPlanetDensity
{
public:
    /** S1a (Design/PlanetPluginSplit.md): the voxel engine's density source. */
    static FName SourceTypeName();
    virtual FName GetSourceType() const override { return SourceTypeName(); }

    /** InMinFootprint (cm) is the smallest footprint any sample is evaluated at: octaves finer
     *  than it are always left out, whatever footprint a caller passes (0 keeps all). */
    FVoxelNoiseSurfaceDensity(const FVoxelSurfaceSettings& InSettings, double InPlanetRadius, double InNoiseAmplitude, double InMinFootprint = 0.0);

    virtual double HeightAt(const FVector& Dir, double Footprint) const override;

    /** The base's, then the three layers' octaves, their normalisation and the settings' weights
     *  (LandMode 0). */
    virtual void BuildGPUData(FVoxelDensityGPUData& Out) const override;

    const FVoxelSurfaceSettings& GetSettings() const { return Settings; }

private:
    FVoxelSurfaceSettings Settings;
    VoxelDensityLayers::FLayer Continent;
    VoxelDensityLayers::FLayer Mountain;
    VoxelDensityLayers::FLayer Detail;
};
