#include "VoxelPlanetDensity.h"
#include "VoxelDensityGPU.h"
#include "VoxelNoiseSurfaceDensity.h"
#include "VoxelTerrestrialDensity.h"
#include "VoxelPlanets.h"
#include "VoxelShaderCompat.h"
#include "FDensitySampleCompositor.h"
#include "Async/ParallelFor.h"
#include "Misc/ScopeLock.h"

namespace VoxelShader
{
#include "VoxelDensityCore.ush"
}

FVoxelPlanetDensity::FVoxelPlanetDensity(double InPlanetRadius, double InNoiseAmplitude, double InMinFootprint)
    : PlanetRadius(FMath::Max(InPlanetRadius, 1.0))
    , NoiseAmplitude(FMath::Max(InNoiseAmplitude, 0.0))
    , MinFootprint(FMath::Max(InMinFootprint, 0.0))
{
}

FVoxelWaterSample FVoxelPlanetDensity::WaterAt(const FVector& Direction) const
{
    FVoxelWaterSample Out;
    const double Len = Direction.Size();
    if (Len < 1.0e-6) return Out;
    // The noise surface: the ground above the planet radius (the planet knows its sea).
    Out.GroundHeight = Out.SurfaceHeight = (float)(HeightAt(Direction, MinFootprint) * 0.01);
    return Out;
}

float FVoxelPlanetDensity::SampleOne(const FVector& Position, double Footprint) const
{
    using namespace VoxelShader;

    const double Radius = Position.Size();
    const double Surface = Radius - (PlanetRadius + HeightAt(Position, Footprint));
    return VoxelComposeTerrain((float)Surface, VoxelCaveDensity());
}

void FVoxelPlanetDensity::Sample(const FSampleInput& Input, float* DensityOut) const
{
    const int32 Count = Input.Num();
    if (Count <= 0) return;
    const double Footprint = FMath::Max(Input.Footprint, MinFootprint);

    constexpr int32 BatchSize = 64;
    if (Count > 2 * BatchSize)
    {
        const int32 NumBatches = (Count + BatchSize - 1) / BatchSize;
        ParallelFor(NumBatches, [&](int32 Batch) {
            const int32 End = FMath::Min(Count, (Batch + 1) * BatchSize);
            for (int32 i = Batch * BatchSize; i < End; i++)
                DensityOut[i] = SampleOne(Input.GetPosition(i), Footprint);
            });
    }
    else
    {
        for (int32 i = 0; i < Count; i++)
            DensityOut[i] = SampleOne(Input.GetPosition(i), Footprint);
    }
}

void FVoxelPlanetDensity::BuildGPUData(FVoxelDensityGPUData& Out) const
{
    Out = FVoxelDensityGPUData();
    Out.PlanetRadius = VoxelDoubleFloat(PlanetRadius);
    Out.NoiseAmplitude = VoxelDoubleFloat(NoiseAmplitude);
    Out.MinFootprint = MinFootprint;
}

void FVoxelPlanetDensity::CheckLatticeRange(double SampleRadius, double MaxFrequency)
{
    // The GPU converts lattice coordinates to int32 before wrapping; the CPU wraps through
    // int64. They agree while every coordinate (at most the planet radius times the highest
    // frequency, plus an offset below 750) stays below 2^31.
    if (SampleRadius * MaxFrequency + 750.0 >= 2147483648.0)
    {
        UE_LOG(LogVoxelPlanets, Warning, TEXT("[VoxelDensity] The finest octave reaches lattice coordinate %.3g (limit 2^31): GPU and CPU densities will differ. Use fewer octaves or a longer wavelength."),
            SampleRadius * MaxFrequency + 750.0);
    }
}

TSharedRef<const FVoxelDensityGPUData> FVoxelPlanetDensity::GetGPUData() const
{
    FScopeLock Lock(&GPUDataLock);
    if (!GPUData.IsValid())
    {
        const TSharedRef<FVoxelDensityGPUData> Data = MakeShared<FVoxelDensityGPUData>();
        BuildGPUData(*Data);
        GPUData = Data;
    }
    return GPUData.ToSharedRef();
}

TSharedPtr<const IVoxelDensitySourceGPU> FVoxelPlanetDensity::MakeGPUSource() const
{
    return MakeShared<FVoxelDensityGPUSource>(GetGPUData());
}

TSharedPtr<const FVoxelPlanetDensity> FVoxelPlanetDensity::Of(const FDensitySampleCompositor& Compositor)
{
    const TSharedPtr<const IVoxelDensitySource> Base = Compositor.GetBaseDensity();
    if (!Base.IsValid()) return nullptr;
    const FName Type = Base->GetSourceType();
    const bool bPlanet = Type == FVoxelNoiseSurfaceDensity::SourceTypeName() || Type == FVoxelTerrestrialDensity::SourceTypeName();
    return bPlanet ? StaticCastSharedPtr<const FVoxelPlanetDensity>(Base) : nullptr;
}
