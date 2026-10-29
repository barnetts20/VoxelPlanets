// VoxelLandCaves.h - The cave network's CPU side (Design/EditsAndCaves.md, "K1 as built"): the
// constants the shared cave code reads (VoxelLandCaves.ush; the shaders declare the same names in
// VoxelCaveParameters.ush), made from the planet's settings and bake, and the cave bake (the bake
// as the land chain sees it through its warp, packed two words a cell), made once per bake and warp
// and kept for the planet's densities.

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

struct FVoxelLandBake;
struct FVoxelTerrestrialSettings;
struct FVoxelLandCaveBakeGPU;

/** The cave code's constants, named as VoxelLandCaves.ush reads them (FVoxelTerrestrialDensity's
 *  CPU sampler derives from this; FVoxelCaveShaderParameters copies it to the shaders). The
 *  defaults are K0's look-test values (FVoxelCaveSettings exposes the ones worth tuning;
 *  VoxelLand::CaveConstantsFor fills them). */
struct FVoxelCaveConstants
{
    uint32 CaveOn = 0;                  // 0: no caves
    uint32 CaveSeed = 0x63617665u;
    uint32 CaveCellsPerBake = 8;        // cave cells along a bake cell (at least 6)
    uint32 CaveLevels = 3;              // 1..4
    uint32 CaveRoughCells = 24;         // the wall detail's coarsest octave: this many a cave cell
    uint32 CaveRoughOctaves = 3;
    uint32 CaveSeaCells = 4;            // the sea caves' lattice, in cave cells
    float CaveCellM = 192.0f;           // a cave cell at the face centre (m)
    float CaveLevelM = 40.0f;
    float CaveTableShare = 0.2f;
    float CaveFloorClearM = 3.0f;
    float CavePhiWaveCells = 4.0f;
    float CavePhiNoiseM = 6.0f;
    float CavePhiJitterM = 2.0f;
    float CavePhiNoiseSlope = 1.0f;
    float CaveTrunkFlowLo = 26.0f;      // log2 m^2
    float CaveTrunkFlowHi = 32.0f;
    float CaveRadiusM = 3.0f;
    float CaveRadiusSpread = 0.35f;
    float CaveTrunkGain = 1.5f;
    float CaveRadiusMaxM = 24.0f;
    float CaveKarstLo = 2.0f;           // the province's karst switch (above 1: none)
    float CaveKarstHi = 2.1f;
    float CaveKarstSlack = 0.05f;
    float CaveActiveShare = 0.7f;
    float CaveTrunkActive = 0.3f;
    float CaveChamberShare = 0.05f;
    float CaveChamberRadiusM = 22.0f;
    float CaveChamberSquash = 0.5f;
    float CaveShaftShare = 0.12f;
    float CaveShaftRadiusM = 2.5f;
    float CaveMouthShare = 0.004f;
    float CaveDolineShare = 0.0008f;
    float CaveDolineRadiusM = 4.0f;
    float CaveDolineHeadM = 300.0f;
    float CaveDolineFunnel = 0.4f;
    float CaveDolineBowlM = 25.0f;
    float CaveDeepCellM = 4.0f;
    float CaveDepthM = 500.0f;
    float CaveShallowDepthM = 150.0f;
    float CaveRoughM = 0.7f;
    float CaveRoughGain = 0.45f;
    float CaveBeddingM = 0.25f;
    float CaveBeddingWaveM = 1.6f;
    float CaveBlendM = 1.5f;
    float CaveBandSlackM = 300.0f;
    float CaveSeaShare = 0.35f;
    float CaveSeaCoastM = 60.0f;
    float CaveSeaZM = 1.5f;
    float CaveSeaRadiusM = 6.0f;
    float CaveSeaReliefM = 150.0f;
    float CaveSeaTall = 1.5f;
    float CaveSeaFloorM = 1.0f;
    float CaveSeaRoofM = 5.0f;
    float CaveSeaCoverM = 45.0f;
    float CaveFloorFill = 0.45f;
    float CaveCanyonDepth = 2.0f;
    float CaveCanyonWidth = 0.4f;
    float CaveMeander = 0.12f;
    float CaveMeanderZM = 6.0f;
    float CaveCoverM = 24.0f;
    float CaveCoverSlope = 0.4f;
    float CaveMaxRadiusM = 24.0f;       // the largest radius any primitive can have: every cave is closed at footprints at or above it
};

/** A planet's cave bake (VoxelLandCaves.ush VOXEL_CAVE_WORD): per padded bake cell, the bake's
 *  height, local base, relief, drainage and province as the land chain sees them through its
 *  sampling warp, two words a cell. Immutable once made; shared by the planet's densities and
 *  kept with the bake while its key holds (VoxelLand::GetCaveBake). */
struct VOXELPLANETS_API FVoxelLandCaveBake
{
    FVoxelLandCaveBake();
    ~FVoxelLandCaveBake();
    FVoxelLandCaveBake(const FVoxelLandCaveBake&) = delete;
    FVoxelLandCaveBake& operator=(const FVoxelLandCaveBake&) = delete;

    uint32 Key = 0;               // VoxelLand::CaveBakeKey
    int32 N = 0;                  // the bake's
    TArray<uint32> Words;         // 2 x FVoxelLandBake::NumCells()
    double BuildSeconds = 0.0;

    /** The GPU copy (render thread; uploaded on first use, released on the render thread). */
    TSharedPtr<FVoxelLandCaveBakeGPU, ESPMode::ThreadSafe> GPU;
};

namespace VoxelLand
{
    /** The cave constants for the planet's settings (FVoxelTerrestrialSettings::Caves) and bake:
     *  the cave cell a whole fraction of the bake cell (at least 6 a cell), the karst switch at the
     *  province above which KarstCoverage of the land lies (FVoxelLandBake::ProvinceThreshold),
     *  the wall detail's coarsest octave at about 8 m. CaveOn 0 when the caves are off. */
    VOXELPLANETS_API FVoxelCaveConstants CaveConstantsFor(const FVoxelTerrestrialSettings& Settings, const FVoxelLandBake& Bake);

    /** The key of the cave bake for these settings and bake: the bake's key and size, the seed and
     *  the sampling warp (what the cave bake is made from). */
    VOXELPLANETS_API uint32 CaveBakeKey(const FVoxelTerrestrialSettings& Settings, const FVoxelLandBake& Bake);

    /** The cave bake for Key on Bake: the one it keeps (FVoxelLandBake::Caves) if its key
     *  matches, else Build's (made under the bake's CaveLock, so densities built together make it
     *  once), then kept there. Any thread. */
    VOXELPLANETS_API TSharedPtr<const FVoxelLandCaveBake, ESPMode::ThreadSafe> GetCaveBake(const FVoxelLandBake& Bake, uint32 Key,
        TFunctionRef<TSharedPtr<FVoxelLandCaveBake, ESPMode::ThreadSafe>()> Build);
}
