// VoxelDensity.h - The planet's density function as its consumers see it: the surface settings,
// the water query's types, and the factories every consumer (terrain, ocean, planet) builds its
// compositor from. The densities themselves are FVoxelNoiseSurfaceDensity and
// FVoxelTerrestrialDensity (FVoxelPlanetDensity, their base).

#pragma once

#include "CoreMinimal.h"
#include "FDensitySampleCompositor.h"
#include "VoxelTerrainLattice.h"
#include "VoxelDensity.generated.h"

struct FVoxelLandBake;
struct FVoxelLandClimateField;
struct FVoxelTerrestrialSettings;

/** Shape of the planet's surface heightfield. Wavelengths are fractions of the planet radius,
 *  so a setting keeps its look at any planet size; weights are fractions of the noise
 *  amplitude (the height range above the planet radius). */
USTRUCT(BlueprintType)
struct VOXELPLANETS_API FVoxelSurfaceSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface")
    int32 Seed = 1337;

    /** Frequency ratio between successive octaves in every layer. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface", meta = (ClampMin = "1.01", UIMin = "1.5", UIMax = "3.0"))
    double Lacunarity = 2.0;

    // --- Continents: large, smooth landmasses and basins ---

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface|Continents", meta = (ClampMin = "0.000001", UIMin = "0.05", UIMax = "2.0"))
    double ContinentWavelength = 0.6;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface|Continents", meta = (ClampMin = "0", UIMin = "1", UIMax = "10"))
    int32 ContinentOctaves = 5;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface|Continents", meta = (UIMin = "0.2", UIMax = "0.8"))
    double ContinentGain = 0.5;

    /** Height swing of the continents around half the amplitude (1 = the full range). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface|Continents", meta = (UIMin = "0.0", UIMax = "1.0"))
    double ContinentWeight = 0.6;

    // --- Mountains: ridged ranges on the higher continental ground ---

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface|Mountains", meta = (ClampMin = "0.000001", UIMin = "0.005", UIMax = "0.5"))
    double MountainWavelength = 0.08;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface|Mountains", meta = (ClampMin = "0", UIMin = "1", UIMax = "12"))
    int32 MountainOctaves = 8;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface|Mountains", meta = (UIMin = "0.2", UIMax = "0.8"))
    double MountainGain = 0.45;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface|Mountains", meta = (UIMin = "0.0", UIMax = "1.0"))
    double MountainWeight = 0.25;

    /** Continent value (about -1..1) where mountains begin, and the range they fade in over. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface|Mountains", meta = (UIMin = "-1.0", UIMax = "1.0"))
    double MountainStart = 0.1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface|Mountains", meta = (ClampMin = "0.0001", UIMin = "0.05", UIMax = "1.0"))
    double MountainBlend = 0.4;

    /** Ridge sharpness feedback: higher concentrates detail on the crests. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface|Mountains", meta = (UIMin = "0.5", UIMax = "4.0"))
    double RidgeGain = 2.0;

    // --- Detail: fine relief, damped on steep slopes ---

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface|Detail", meta = (ClampMin = "0.0000001", UIMin = "0.0005", UIMax = "0.05"))
    double DetailWavelength = 0.01;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface|Detail", meta = (ClampMin = "0", UIMin = "1", UIMax = "20"))
    int32 DetailOctaves = 14;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface|Detail", meta = (UIMin = "0.2", UIMax = "0.8"))
    double DetailGain = 0.45;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface|Detail", meta = (UIMin = "0.0", UIMax = "0.5"))
    double DetailWeight = 0.02;

    /** How strongly accumulated slope suppresses finer octaves (0 = plain fBm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface|Detail", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "4.0"))
    double DetailSlopeDamping = 3.0;

    /** Hash of every field, for change detection. */
    uint32 GetSettingsHash() const;
};

/** What GetWaterAt finds (R2, Design/RiversLakes.md 3.6). */
UENUM(BlueprintType)
enum class EVoxelWaterKind : uint8
{
    None,
    Sea,
    Lake,
    River
};

/** The water at a point of a planet (APlanetActor::GetWaterAt). Heights are metres above the
 *  planet's sea level, along the planet's radius through the point. */
USTRUCT(BlueprintType)
struct VOXELPLANETS_API FVoxelWaterSample
{
    GENERATED_BODY()

    /** The water over the ground there (None: dry ground). */
    UPROPERTY(BlueprintReadOnly, Category = "Water")
    EVoxelWaterKind Kind = EVoxelWaterKind::None;

    /** The water's surface (m above sea level); the ground's where there is none. */
    UPROPERTY(BlueprintReadOnly, Category = "Water")
    float SurfaceHeight = 0.0f;

    /** The ground's height (m above sea level), without edits. */
    UPROPERTY(BlueprintReadOnly, Category = "Water")
    float GroundHeight = 0.0f;

    /** The water's depth there (m: surface less ground; 0 without water). */
    UPROPERTY(BlueprintReadOnly, Category = "Water")
    float Depth = 0.0f;

    /** How far the queried point lies below the water's surface (m; negative above it). */
    UPROPERTY(BlueprintReadOnly, Category = "Water")
    float PointDepth = 0.0f;

    /** A river's current (cm/s, world): downstream, faster on steeper and deeper reaches. Zero
     *  in lakes and the sea (tides and currents are later work). */
    UPROPERTY(BlueprintReadOnly, Category = "Water")
    FVector FlowVelocity = FVector::ZeroVector;
};

namespace VoxelDensity
{
    /** The compositor every terrain, ocean and planet consumer samples: the noise surface
     *  (FVoxelNoiseSurfaceDensity) as its base density and only layer, plus EditStore (may be
     *  null) applied on top. MinFootprint is the terrain's finest cell size (TerrainFinestCell),
     *  so every consumer of one compositor sees the same field; 0 keeps every octave. */
    VOXELPLANETS_API TSharedPtr<FDensitySampleCompositor> MakeCompositor(const FVoxelSurfaceSettings& Settings,
        double PlanetRadius, double NoiseAmplitude, TSharedPtr<FSparseEditStore> EditStore, double MinFootprint = 0.0);

    /** The same for the terrestrial generator (FVoxelTerrestrialDensity): PlanetRadius and
     *  NoiseAmplitude from VoxelLand::ShapeFor(Land, *Bake, the noise pack). Climate (T5c): the
     *  land climate the dunes are placed by (null: no dunes). */
    VOXELPLANETS_API TSharedPtr<FDensitySampleCompositor> MakeTerrestrialCompositor(const FVoxelTerrestrialSettings& Land,
        TSharedPtr<const FVoxelLandBake, ESPMode::ThreadSafe> Bake, double PlanetRadius, double NoiseAmplitude,
        TSharedPtr<FSparseEditStore> EditStore, double MinFootprint = 0.0,
        TSharedPtr<const FVoxelLandClimateField, ESPMode::ThreadSafe> Climate = nullptr);

    /** Root half-extent of a planet's terrain lattice: holds every possible surface point
     *  (the engine's lattice: VoxelTerrainLattice.h). */
    inline double RootExtentFor(double PlanetRadius, double NoiseAmplitude) { return VoxelTerrainLattice::RootExtentFor(PlanetRadius, NoiseAmplitude); }

    /** Cell size (cm) of the finest terrain lattice level (MaxDepth halvings of the root). */
    inline double TerrainFinestCell(double PlanetRadius, double NoiseAmplitude, int32 MaxDepth)
    {
        return VoxelTerrainLattice::FinestCell(PlanetRadius, NoiseAmplitude, MaxDepth);
    }
}
