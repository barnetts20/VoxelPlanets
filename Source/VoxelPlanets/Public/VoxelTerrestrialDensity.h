// VoxelTerrestrialDensity.h - The terrestrial generator: a planet density (FVoxelPlanetDensity)
// whose heightfield is the land bake's heights (sampled through a warp, VoxelLandSample.ush)
// plus the hillside layers (hills, peaks, strata, dunes, the glacial and polar surfaces, the
// ice sheets), the gullies (VoxelLandGully.ush), the detail layer (VoxelLandDetail.ush, N1) and
// the rivers' and lakes' carve (VoxelLandCarve.ush), from the shared land code the GPU runs too.

#pragma once

#include "CoreMinimal.h"
#include "VoxelPlanetDensity.h"
#include "VoxelDensityLayers.h"

struct FVoxelLandBake;
struct FVoxelLandClimateField;
struct FVoxelLandNoiseSet;
struct FVoxelLandRiverSet;
struct FVoxelTerrestrialSettings;
namespace VoxelShader { struct FVoxelLandSamplerCPU; }

/** The terrestrial generator on the CPU. Immutable after construction; safe to call from any
 *  thread. */
class VOXELPLANETS_API FVoxelTerrestrialDensity : public FVoxelPlanetDensity
{
public:
    /** S1a (Design/PlanetPluginSplit.md): the voxel engine's density source. */
    static FName SourceTypeName();
    virtual FName GetSourceType() const override { return SourceTypeName(); }
    /** The compositor's source when it is a terrestrial density (null otherwise: the noise
     *  surface, or none). */
    static TSharedPtr<const FVoxelTerrestrialDensity> Of(const FDensitySampleCompositor& Compositor);

    /** InPlanetRadius is the base radius and InNoiseAmplitude the height range above it that
     *  hold every height (VoxelLand::ShapeFor), so the contract of HeightAt and the lattice
     *  sizing are the noise surface's. InMinFootprint (cm) is the smallest footprint any sample
     *  is evaluated at (0 keeps every octave). InClimate (T5c): the land climate the dunes are
     *  placed by (null: no dunes). */
    FVoxelTerrestrialDensity(const FVoxelTerrestrialSettings& InLand, TSharedPtr<const FVoxelLandBake, ESPMode::ThreadSafe> InBake,
        double InPlanetRadius, double InNoiseAmplitude, double InMinFootprint = 0.0,
        TSharedPtr<const FVoxelLandClimateField, ESPMode::ThreadSafe> InClimate = nullptr);
    virtual ~FVoxelTerrestrialDensity() override;

    virtual double HeightAt(const FVector& Dir, double Footprint) const override;

    /** R2: the water at Direction, from the same shared code as the carving (VoxelLandCarve.ush),
     *  at the finest footprint: the sea where the ground is below sea level, a kept lake's level,
     *  a river's water line in its channel. FlowVelocity is in the planet frame (cm/s);
     *  PointDepth is left 0 (the planet's query fills it in). */
    virtual FVoxelWaterSample WaterAt(const FVector& Direction) const override;

    /** The base's, then the warp's octaves and the Land* block (LandMode 1). */
    virtual void BuildGPUData(FVoxelDensityGPUData& Out) const override;

    /** The generator's bake (never null). */
    const TSharedPtr<const FVoxelLandBake, ESPMode::ThreadSafe>& GetLandBake() const { return LandBake; }

    /** The peaks' noise set (VoxelLandPeaks.ush; null without peaks). */
    const TSharedPtr<const FVoxelLandNoiseSet, ESPMode::ThreadSafe>& GetLandNoise() const { return LandNoise; }

    /** T5c: the climate the dunes were placed by (null: no dunes). */
    const TSharedPtr<const FVoxelLandClimateField, ESPMode::ThreadSafe>& GetLandClimate() const { return LandClimate; }

private:
    /** HeightAt for the terrestrial generator, from the surface point on the sea-level sphere
     *  (the bake's SeaRadius; VoxelDensitySample.ush samples the same sphere). */
    double LandHeightAt(const FVector& SurfacePoint, double Footprint) const;
    /** The same, and with OutWater the water there (WaterAt). */
    double LandSurfaceAt(const FVector& SurfacePoint, double Footprint, FVoxelWaterSample* OutWater) const;
    /** The land sampler (LandSampler) from the constants and buffers above, once the
     *  constructor has them all. */
    void BuildLandSampler();

    TSharedPtr<const FVoxelLandBake, ESPMode::ThreadSafe> LandBake;
    VoxelDensityLayers::FLayer LandWarp[3];   // the sampling warp's components (no band limit)
    double LandHeightOffset = 0.0;      // cm: the bake's sea-level heights to heights above PlanetRadius
    double LandDetailAmplitude = 0.0;   // cm: the detail layer's (VoxelLandDetail.ush, N1)
    double LandWarpAmplitude = 0.0;     // cm
    double LandSampleRadius = 1.0;      // cm: the sea-level sphere the warp and detail are sampled on
    float LandCellM = 1.0f;             // the bake cell's arc at the face centre (m)
    float LandGullyStrength = 0.0f;     // VoxelLandGully.ush
    uint32 LandGullyOctaves = 0;
    float LandGullyFlowGain = 0.0f;
    float LandGullyDetail = 1.0f;
    uint32 LandGullySeed = 0;
    float LandHillRelief = 0.0f;        // VoxelLandHills.ush
    double LandHillFrequency = 0.0;     // cycles per cm (the finest octave's)
    uint32 LandHillOctaves = 0;
    uint32 LandHillSeed = 0;
    TSharedPtr<const FVoxelLandNoiseSet, ESPMode::ThreadSafe> LandNoise;   // the noise pack: the peaks', detail, hills' and strata's sets (null: none used)
    // VoxelLandPeaks.ush's inputs: the set's placement and normalisation (the shader's header).
    float LandPeakStrength = 0.0f;
    float LandPeakTexelsPerCell = 0.0f; // 0: no peaks
    uint32 LandPeakSeed = 0;
    float LandPeakSpacingM = 0.0f;
    float LandPeakReliefM = 0.0f;
    float LandPeakReliefStart = 500.0f;   // T4b: the peaks' fade-in over the local relief (m)
    float LandPeakReliefEnd = 1500.0f;
    float LandPeakMeanR = 0.0f;
    float LandPeakMeanG = 0.0f;
    float LandPeakScaleR = 1.0f;
    float LandPeakScaleG = 1.0f;
    float LandPeakScaleB = 1.0f;
    // The peaks' detail (VoxelLandPeaks.ush, T3e): the pack's detail set (N1; LandNoise's Detail*),
    // hard rock its G, soft rock its R, with those channels' means.
    float LandPeakDetailTexelsPerCell = 0.0f;  // 0: no detail
    float LandDetailMeanHard = 0.0f;
    float LandDetailMeanSoft = 0.0f;
    float LandPeakDetailScaleHard = 0.0f;
    float LandPeakDetailScaleSoft = 0.0f;
    float LandPeakDetailFade = 0.0f;
    // The hills' set (VoxelLandHillSet.ush, T4a; its chain: LandNoise's Hill*). 0 texels: none
    // (the procedural hills, LandHillFrequency and LandHillOctaves).
    float LandHillTexelsPerCell = 0.0f;
    float LandHillScaleR = 1.0f;
    float LandHillScaleG = 1.0f;
    float LandHillScaleB = 1.0f;
    float LandHillMeanR = 0.0f;       // N1b: R's and G's means (unit), for the face blend
    float LandHillMeanG = 0.0f;
    float LandHillSpacingM = 0.0f;
    float LandHillSwellTexelsPerCell = 0.0f;   // 0: no swells
    float LandHillSwellM = 0.0f;
    float LandHillSwellScaleG = 1.0f;
    float LandHillDetailTexelsPerCell = 0.0f;  // 0: no detail
    float LandHillDetailScaleHard = 0.0f;
    float LandHillDetailScaleSoft = 0.0f;
    float LandHillDetailFade = 0.0f;
    float LandHillFoothill = 0.0f;         // T4b: the hills' growth in the foothills
    float LandStrataTexelsPerCell = 0.0f;      // T4c: the strata set's placement (0: none)
    float LandStrataScaleR = 1.0f;
    float LandStrataScaleG = 1.0f;
    float LandStrataScaleB = 1.0f;
    float LandStrataMeanR = 0.0f;
    float LandStrataMeanG = 0.0f;
    float LandStrataSpacingM = 0.0f;
    float LandStrataRelief = 0.0f;             // m: the set's relief times the strength, capped
    float LandStrataFlat = 0.0f;               // its share on the flats
    float LandStrataThreshold = 2.0f;          // the province switch (above 1: none)
    float LandStrataWidth = 0.05f;
    uint32 LandStrataSeed = 0;
    float LandStrataDetailTexelsPerCell = 0.0f;   // 0: no detail
    float LandStrataDetailScaleHard = 0.0f;
    float LandStrataDetailScaleSoft = 0.0f;
    float LandStrataDetailFade = 0.0f;
    TArray<uint32> LandReliefPacked;    // the bake's local relief, four cells per word (empty: none)
    TArray<uint32> LandProvincePacked;  // T4c: the bake's province, four cells per word (as the relief's)
    TSharedPtr<const FVoxelLandRiverSet, ESPMode::ThreadSafe> LandRivers;   // R1: the rivers (null: none)
    uint32 LandRiverTilesPerFace = 0;          // R1: the rivers (0: none)
    float LandRiverRadiusM = 1.0f;
    float LandRiverWidthScale = 1.0f;
    float LandRiverValleyScale = 1.0f;
    // R2: the rivers' and lakes' carve (VoxelLandCarve.ush).
    float LandRiverCarve = 0.0f;               // 0: not carved
    float LandRiverDepthScale = 1.0f;
    float LandRiverFloodplainScale = 1.0f;
    float LandRiverMinLog2Area = 0.0f;
    float LandLakeShoreBand = 0.0f;
    float LandRiverMeander = 0.0f;      // R3: RiverMeander (the meanders' swing)
    float LandRiverFalls = 0.0f;        // R3: RiverFalls (the knickpoints' falls)
    uint32 LandHasWater = 0;                   // 1: the bake's lake levels (its Water) are read
    // N1: the detail layer from the pack's detail set (VoxelLandDetail.ush): its A channel at
    // LandDetailScales scales, 16 times apart.
    float LandDetailTexelsPerCell = 0.0f;      // the first scale's (0: no detail)
    uint32 LandDetailScales = 0;
    float LandDetailMeanA = 0.0f;
    float LandDetailScaleA = 0.0f;
    float LandDetailScaleGain = 0.0f;
    float LandDetailFade = 0.0f;
    uint32 LandDetailSeed = 0;
    // T5c: the dunes (VoxelLandDunes.ush; their chains: LandNoise's Dune* and Sand*), where the
    // climate (LandClimate: the land climate's cells and the sand's drift) makes desert.
    TSharedPtr<const FVoxelLandClimateField, ESPMode::ThreadSafe> LandClimate;   // null: none (no dunes)
    float LandClimateLapse = 6.5f;             // C per km
    float LandDuneTexelsPerCell = 0.0f;        // 0: no dunes
    float LandDuneScaleR = 1.0f;
    float LandDuneScaleG = 1.0f;
    float LandDuneMeanR = 0.0f;
    float LandDuneMeanG = 0.0f;
    float LandDuneRelief = 0.0f;               // m
    uint32 LandDuneSeed = 0;
    float LandDuneErgFrequency = 1.0f;
    float LandDuneErgThreshold = 2.0f;         // above 1: no ergs
    float LandDuneErgWidth = 0.06f;
    float LandDuneReliefMax = 400.0f;          // m
    float LandDuneDetailTexelsPerCell = 0.0f;  // 0: no sand detail
    float LandDuneDetailScale = 0.0f;
    float LandDuneDetailMean = 0.0f;
    float LandDuneDetailFade = 0.0f;
    // T5d: the glacial layer (VoxelLandPeaks.ush; its chain: LandNoise's Glacial*), in the peaks'
    // place where the climate (LandClimate) has had glaciers.
    float LandGlacialTexelsPerCell = 0.0f;     // 0: none
    float LandGlacialScaleR = 1.0f;
    float LandGlacialScaleG = 1.0f;
    float LandGlacialScaleB = 1.0f;
    float LandGlacialMeanR = 0.0f;
    float LandGlacialMeanG = 0.0f;
    float LandGlacialReliefM = 0.0f;
    float LandGlacialStrength = 0.0f;
    uint32 LandGlacialSeed = 0;
    float LandGlacialSpacingM = 0.0f;
    float LandGlacialCooling = 0.0f;
    float LandGlacialDetailScaleHard = 0.0f;
    float LandGlacialDetailScaleSoft = 0.0f;
    // T5d2: the ice sheets (VoxelLandIce.ush; their swells' chain: LandNoise's Ice*).
    float LandIceMaxThickness = 0.0f;          // m (0: none)
    bool bLandIceShelf = false;                // T5e: the planet has an ice shelf (the climate kept for its share)
    float LandIceBlend = 0.0f;                 // m
    float LandIceSwellTexelsPerCell = 0.0f;    // 0: no swells
    float LandIceMeanG = 0.0f;
    float LandIceSwellM = 0.0f;
    uint32 LandIceSeed = 0;
    float LandIceSwellSpacingM = 0.0f;
    // T5d3: the polar surfaces (VoxelLandPolar.ush; their chain: LandNoise's Polar*).
    uint32 LandPolarSeed = 0;
    float LandPolarCrevasseTexelsPerCell = 0.0f;                 // 0: none
    float LandPolarCrevasseM = 0.0f;
    float LandPolarCrevasseMean = 0.0f;
    float LandPolarCrevasseFade = 0.0f;
    float LandPolarSastrugiTexelsPerCell = 0.0f;                 // 0: none
    float LandPolarSastrugiM = 0.0f;
    float LandPolarSastrugiMean = 0.0f;
    float LandPolarSastrugiFade = 0.0f;
    float LandPolarPolygonTexelsPerCell = 0.0f;                 // 0: none
    float LandPolarPolygonM = 0.0f;
    float LandPolarPolygonMean = 0.0f;
    float LandPolarPolygonFade = 0.0f;
    float LandPolarPermafrostT = -6.0f;    // C
    float LandPolarCrevasseTurn = 0.0f;    // radians: the set's grain
    float LandPolarSastrugiTurn = 0.0f;
    // The snow dunes: the dune set's G (its chain: LandNoise's Dune*).
    float LandPolarDuneTexelsPerCell = 0.0f;                 // 0: none
    float LandPolarDuneM = 0.0f;
    float LandPolarDuneMean = 0.0f;
    float LandPolarDuneFade = 0.0f;

    TArray<uint32> LandFlowPacked;      // the bake's drainage, two f16 cells per word (empty: none)
    TArray<uint32> LandFormPacked;      // the bake's landform, two cells per word (empty: none)

    /** The land bake's sampler (VoxelLandSample.ush and VoxelLandGully.ush with the constants
     *  and buffers above as its members), built once by the constructor (MAT-3: it was built
     *  per sample point) and read by every sample. The pointee is not const because the shared
     *  land functions aren't const-qualified (VOXEL_FUNC, HLSL); they only read it. Defined in
     *  VoxelTerrestrialDensity.cpp, so the destructor is too. */
    TUniquePtr<VoxelShader::FVoxelLandSamplerCPU> LandSampler;
};
