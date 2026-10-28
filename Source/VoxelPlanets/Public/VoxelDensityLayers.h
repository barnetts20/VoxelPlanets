// VoxelDensityLayers.h - The octave tables of the planet densities: a layer of band-limited
// gradient noise octaves (VoxelDensityCore.ush), fixed at construction, evaluated in double on
// the CPU (FVoxelNoiseSurfaceDensity's surface layers, FVoxelTerrestrialDensity's sampling warp)
// and copied to the GPU's octave table (FVoxelDensityGPUOctave). Public because the densities
// hold their layers by value in public headers; the planet densities' own tables, not an
// interface (nothing exported).

#pragma once

#include "CoreMinimal.h"

struct FVoxelDensityGPUOctave;

namespace VoxelDensityLayers
{
    /** One octave, fixed at construction; both backends read these. With a lacunarity of 2,
     *  at most 32 octaves and the finest coordinate below 2^30 (radius times the finest
     *  frequency), its lattice coordinate is the layer's finest one shifted right by Shift, in
     *  fixed point (VoxelFixedLattice); otherwise Shift is VoxelNoFixedPoint. */
    struct FOctave
    {
        double Frequency = 0.0;       // cycles per cm
        FVector Offset;               // lattice offset (decorrelates octaves)
        float Wavelength = 0.0f;      // cm, for the band weight
        float Amplitude = 1.0f;       // gain^octave, accumulated in float
        uint32 Seed = 0;
        uint32 Shift = 0xffffffffu;
        uint32 OffsetCell[3] = {};    // Offset in 32.32 fixed point
        uint32 OffsetFrac[3] = {};
    };

    struct FLayer
    {
        uint32 Kind = 0;              // VoxelLayerPlain / Ridged / Damped
        double InvNorm = 0.0;         // output scale (see MakeLayer)
        TArray<FOctave> Octaves;
    };

    /** A layer of InOctaves octaves from WavelengthFraction of InPlanetRadius down, InLacunarity
     *  apart, InGain weaker each, seeded from InSeed. */
    FLayer MakeLayer(uint32 Kind, double WavelengthFraction, double InPlanetRadius, double InLacunarity, double InGain, int32 InOctaves, uint32 InSeed);

    /** The layer at SurfacePoint (cm from the planet centre), with octaves finer than Footprint
     *  (cm) left out. RidgeGain and SlopeDamping are the ridged and damped layers' (VoxelLayerOctave;
     *  plain layers ignore them). */
    double EvaluateLayer(const FLayer& Layer, const FVector& SurfacePoint, double Footprint, float RidgeGain, float SlopeDamping);

    /** EvaluateLayer and, when OutGradient is set, its gradient (per cm; plain layers). */
    double EvaluateLayerGradient(const FLayer& Layer, const FVector& SurfacePoint, double Footprint, float RidgeGain, float SlopeDamping, FVector* OutGradient);

    /** Appends the layer's octaves to the GPU's octave table, as VoxelDensitySample.ush reads them. */
    void AppendGPUOctaves(const FLayer& Layer, TArray<FVoxelDensityGPUOctave>& Out);

    /** The finest octave's frequency (cycles per cm; 0 without octaves). */
    double FinestFrequency(const FLayer& Layer);
}
