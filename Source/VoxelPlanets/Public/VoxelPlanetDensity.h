// VoxelPlanetDensity.h - The planet densities' shared base: the voxel engine's density source
// (IVoxelDensitySource) as a planet makes it, a heightfield above the planet radius sampled
// in double (VoxelDensityCore.ush's composition: surface term, cave slot), with the GPU twin
// (FVoxelDensityGPUSource) built once from BuildGPUData. The two surfaces are
// FVoxelNoiseSurfaceDensity (the noise surface) and FVoxelTerrestrialDensity (the land bake);
// VoxelDensity.h's factories build the compositor around one of them.

#pragma once

#include "CoreMinimal.h"
#include "VoxelDensitySource.h"
#include "VoxelDensity.h"   // FVoxelWaterSample (reflected: stays there)

class FDensitySampleCompositor;
struct FSampleInput;
struct FVoxelDensityGPUData;

/** A planet's density without edits: the surface height in a direction (HeightAt), composed
 *  with the cave slot into the engine's density (Sample). Positions are double and each noise
 *  octave is evaluated on an integer lattice split in double, so results are precise at any
 *  radius. Immutable after construction; safe to call from any thread. */
class VOXELPLANETS_API FVoxelPlanetDensity : public IVoxelDensitySource
{
public:
    /** The GPU twin (S1a, Design/PlanetPluginSplit.md): GetGPUData's data, with the planet's
     *  mesher passes (FVoxelDensityGPUSource, VoxelTerrestrialMesher.usf). */
    virtual TSharedPtr<const IVoxelDensitySourceGPU> MakeGPUSource() const override final;
    /** The compositor's source when it is a planet density of either kind (null otherwise). */
    static TSharedPtr<const FVoxelPlanetDensity> Of(const FDensitySampleCompositor& Compositor);

    /** Height above the planet radius (cm, 0..NoiseAmplitude) in direction Dir (any length),
     *  with octaves finer than Footprint (cm) left out; footprints below MinFootprint are
     *  raised to it, as Sample does (and the GPU). */
    virtual double HeightAt(const FVector& Dir, double Footprint) const = 0;

    /** Terrain density (negative = solid) at each input position, without edits. */
    void Sample(const FSampleInput& Input, float* DensityOut) const;

    /** R2: the water at Direction from the planet's centre (any length; planet frame), at the
     *  finest footprint. This base: None, with the ground's height above the planet radius (its
     *  sea level is the planet's: APlanetActor::GetWaterAt). FVoxelTerrestrialDensity: the sea,
     *  the lakes and the rivers from the shared carving code. */
    virtual FVoxelWaterSample WaterAt(const FVector& Direction) const;

    /** The octave tables and constants the GPU evaluation (VoxelDensitySample.ush) needs to
     *  compute this density. This base resets Out and fills the radius, the amplitude and
     *  MinFootprint; the derived density calls it first and fills the rest. Prefer GetGPUData:
     *  built once, shared. */
    virtual void BuildGPUData(FVoxelDensityGPUData& Out) const;

    /** BuildGPUData's, built on the first call and kept (the GPU twin and every consumer of the
     *  density's GPU data share it). Any thread. */
    TSharedRef<const FVoxelDensityGPUData> GetGPUData() const;

    double GetPlanetRadius() const { return PlanetRadius; }
    double GetNoiseAmplitude() const { return NoiseAmplitude; }
    /** The smallest footprint (cm) any sample is evaluated at (0: every octave). */
    double GetMinFootprint() const { return MinFootprint; }

protected:
    /** InMinFootprint (cm) is the smallest footprint any sample is evaluated at: octaves finer
     *  than it are always left out, whatever footprint a caller passes (0 keeps all). */
    FVoxelPlanetDensity(double InPlanetRadius, double InNoiseAmplitude, double InMinFootprint);

    /** The 2^31 lattice warning, for BuildGPUData: the GPU converts lattice coordinates to int32
     *  before wrapping; the CPU wraps through int64. They agree while every coordinate (at most
     *  SampleRadius, the sphere the layers are sampled on, times MaxFrequency, the finest
     *  octave's, plus an offset below 750) stays below 2^31. */
    static void CheckLatticeRange(double SampleRadius, double MaxFrequency);

    double PlanetRadius = 1.0;
    double NoiseAmplitude = 0.0;
    double MinFootprint = 0.0;

private:
    float SampleOne(const FVector& Position, double Footprint) const;

    /** GetGPUData's, made on the first call. */
    mutable FCriticalSection GPUDataLock;
    mutable TSharedPtr<const FVoxelDensityGPUData> GPUData;
};
