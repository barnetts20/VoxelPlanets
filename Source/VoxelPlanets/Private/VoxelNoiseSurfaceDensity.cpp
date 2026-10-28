#include "VoxelNoiseSurfaceDensity.h"
#include "VoxelDensityGPU.h"
#include "VoxelShaderCompat.h"

namespace VoxelShader
{
#include "VoxelDensityCore.ush"
}

namespace VoxelNoiseSurfaceDensityDetail
{
    constexpr uint32 SaltContinent = 1u;
    constexpr uint32 SaltMountain = 2u;
    constexpr uint32 SaltDetail = 3u;
}

FVoxelNoiseSurfaceDensity::FVoxelNoiseSurfaceDensity(const FVoxelSurfaceSettings& InSettings, double InPlanetRadius, double InNoiseAmplitude, double InMinFootprint)
    : FVoxelPlanetDensity(InPlanetRadius, InNoiseAmplitude, InMinFootprint)
    , Settings(InSettings)
{
    using namespace VoxelNoiseSurfaceDensityDetail;
    using VoxelDensityLayers::MakeLayer;

    const uint32 Seed = (uint32)Settings.Seed;
    Continent = MakeLayer(VoxelShader::VoxelLayerPlain, Settings.ContinentWavelength, PlanetRadius, Settings.Lacunarity, Settings.ContinentGain, Settings.ContinentOctaves, VoxelShader::VoxelHashSeed(Seed, SaltContinent));
    Mountain = MakeLayer(VoxelShader::VoxelLayerRidged, Settings.MountainWavelength, PlanetRadius, Settings.Lacunarity, Settings.MountainGain, Settings.MountainOctaves, VoxelShader::VoxelHashSeed(Seed, SaltMountain));
    Detail = MakeLayer(VoxelShader::VoxelLayerDamped, Settings.DetailWavelength, PlanetRadius, Settings.Lacunarity, Settings.DetailGain, Settings.DetailOctaves, VoxelShader::VoxelHashSeed(Seed, SaltDetail));
}

FName FVoxelNoiseSurfaceDensity::SourceTypeName()
{
    static const FName Name(TEXT("VoxelNoiseSurfaceDensity"));
    return Name;
}

double FVoxelNoiseSurfaceDensity::HeightAt(const FVector& Dir, double Footprint) const
{
    const double Len = Dir.Size();
    if (Len <= 1e-12 || NoiseAmplitude <= 0.0) return 0.0;

    // Footprints below the finest cell are raised to it, as Sample does (and the GPU).
    const double Band = FMath::Max(Footprint, MinFootprint);
    const FVector SurfacePoint = Dir * (PlanetRadius / Len);
    const float RidgeGain = (float)Settings.RidgeGain;
    const float SlopeDamping = (float)Settings.DetailSlopeDamping;
    const double C = VoxelDensityLayers::EvaluateLayer(Continent, SurfacePoint, Band, RidgeGain, SlopeDamping);
    const double R = VoxelDensityLayers::EvaluateLayer(Mountain, SurfacePoint, Band, RidgeGain, SlopeDamping);
    const double D = VoxelDensityLayers::EvaluateLayer(Detail, SurfacePoint, Band, RidgeGain, SlopeDamping);

    // In double: a float height in [0, 1] would quantise the surface to NoiseAmplitude * 6e-8
    // (several cm on an Earth-sized planet). VoxelSampleDensityDF mirrors this in double-float.
    const double Base = 0.5 + 0.5 * Settings.ContinentWeight * C;
    const double MaskT = FMath::Clamp((C - Settings.MountainStart) / FMath::Max(Settings.MountainBlend, 1e-6), 0.0, 1.0);
    const double Mountains = Settings.MountainWeight * MaskT * MaskT * (3.0 - 2.0 * MaskT) * R;
    const double Details = Settings.DetailWeight * D;
    return NoiseAmplitude * FMath::Clamp(Base + Mountains + Details, 0.0, 1.0);
}

void FVoxelNoiseSurfaceDensity::BuildGPUData(FVoxelDensityGPUData& Out) const
{
    FVoxelPlanetDensity::BuildGPUData(Out);
    // Continent, mountain, then detail octaves (the terrestrial generator's warp would follow).
    for (const VoxelDensityLayers::FLayer* Layer : { &Continent, &Mountain, &Detail })
        VoxelDensityLayers::AppendGPUOctaves(*Layer, Out.Octaves);
    Out.ContinentOctaves = (uint32)Continent.Octaves.Num();
    Out.MountainOctaves = (uint32)Mountain.Octaves.Num();
    Out.DetailOctaves = (uint32)Detail.Octaves.Num();
    Out.InvNormContinent = VoxelDoubleFloat(Continent.InvNorm);
    Out.InvNormMountain = VoxelDoubleFloat(Mountain.InvNorm);
    Out.InvNormDetail = VoxelDoubleFloat(Detail.InvNorm);
    Out.ContinentWeight = VoxelDoubleFloat(Settings.ContinentWeight);
    Out.MountainWeight = VoxelDoubleFloat(Settings.MountainWeight);
    Out.MountainStart = VoxelDoubleFloat(Settings.MountainStart);
    Out.MountainBlend = VoxelDoubleFloat(FMath::Max(Settings.MountainBlend, 1e-6));
    Out.DetailWeight = VoxelDoubleFloat(Settings.DetailWeight);
    Out.RidgeGain = (float)Settings.RidgeGain;
    Out.SlopeDamping = (float)Settings.DetailSlopeDamping;
    // LandMode stays 0: the noise surface.

    double MaxFrequency = 0.0;
    for (const VoxelDensityLayers::FLayer* Layer : { &Continent, &Mountain, &Detail })
        MaxFrequency = FMath::Max(MaxFrequency, VoxelDensityLayers::FinestFrequency(*Layer));
    CheckLatticeRange(PlanetRadius, MaxFrequency);
}
