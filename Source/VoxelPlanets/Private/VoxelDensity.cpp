#include "VoxelDensity.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "VoxelNoiseSurfaceDensity.h"
#include "VoxelTerrestrialDensity.h"

namespace VoxelDensityDetail
{
    /** The compositor around a planet density: the density as its base and its only sample
     *  layer, EditStore (may be null) on top, with MinFootprint as the edits' floor. */
    TSharedPtr<FDensitySampleCompositor> MakeCompositorFor(TSharedRef<const FVoxelPlanetDensity> Density, TSharedPtr<FSparseEditStore> EditStore, double MinFootprint)
    {
        TSharedPtr<FDensitySampleCompositor> Compositor = MakeShared<FDensitySampleCompositor>(MoveTemp(EditStore));
        Compositor->SetBaseDensity(Density);
        Compositor->SetEditFootprintFloor(MinFootprint);
        Compositor->AddSampleLayer([Density](const FSampleInput& Input, float* DensityOut) {
            Density->Sample(Input, DensityOut);
            });
        return Compositor;
    }
}

uint32 FVoxelSurfaceSettings::GetSettingsHash() const
{
    uint32 H = ::GetTypeHash(Seed);
    for (const double V : { Lacunarity,
        ContinentWavelength, ContinentGain, ContinentWeight,
        MountainWavelength, MountainGain, MountainWeight, MountainStart, MountainBlend, RidgeGain,
        DetailWavelength, DetailGain, DetailWeight, DetailSlopeDamping })
    {
        H = HashCombine(H, ::GetTypeHash(V));
    }
    for (const int32 V : { ContinentOctaves, MountainOctaves, DetailOctaves })
    {
        H = HashCombine(H, ::GetTypeHash(V));
    }
    return H;
}

TSharedPtr<FDensitySampleCompositor> VoxelDensity::MakeCompositor(const FVoxelSurfaceSettings& Settings,
    double PlanetRadius, double NoiseAmplitude, TSharedPtr<FSparseEditStore> EditStore, double MinFootprint)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelDensity::MakeCompositor);
    const TSharedRef<const FVoxelNoiseSurfaceDensity> Density = MakeShared<FVoxelNoiseSurfaceDensity>(Settings, PlanetRadius, NoiseAmplitude, MinFootprint);
    return VoxelDensityDetail::MakeCompositorFor(Density, MoveTemp(EditStore), MinFootprint);
}

TSharedPtr<FDensitySampleCompositor> VoxelDensity::MakeTerrestrialCompositor(const FVoxelTerrestrialSettings& Land,
    TSharedPtr<const FVoxelLandBake, ESPMode::ThreadSafe> Bake, double PlanetRadius, double NoiseAmplitude,
    TSharedPtr<FSparseEditStore> EditStore, double MinFootprint, TSharedPtr<const FVoxelLandClimateField, ESPMode::ThreadSafe> Climate)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelDensity::MakeTerrestrialCompositor);
    const TSharedRef<const FVoxelTerrestrialDensity> Density = MakeShared<FVoxelTerrestrialDensity>(Land, MoveTemp(Bake), PlanetRadius, NoiseAmplitude, MinFootprint,
        MoveTemp(Climate));
    return VoxelDensityDetail::MakeCompositorFor(Density, MoveTemp(EditStore), MinFootprint);
}
