#include "VoxelLandCaves.h"
#include "VoxelLandBake.h"
#include "VoxelLandSettings.h"
#include "VoxelLandGPU.h"
#include "VoxelDensityGPU.h"
#include "VoxelGPUCommon.h"
#include "VoxelPlanets.h"
#include "RenderingThread.h"
#include "Misc/ScopeLock.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

FVoxelLandCaveBake::FVoxelLandCaveBake()
    : GPU(MakeShared<FVoxelLandCaveBakeGPU, ESPMode::ThreadSafe>())
{
}

FVoxelLandCaveBake::~FVoxelLandCaveBake()
{
    // The GPU copy is released on the render thread, which made it.
    if (GPU.IsValid())
    {
        TSharedPtr<FVoxelLandCaveBakeGPU, ESPMode::ThreadSafe> Resource = MoveTemp(GPU);
        ENQUEUE_RENDER_COMMAND(VoxelLandReleaseCaveBake)([Resource](FRHICommandListImmediate&) mutable
            {
                Resource.Reset();
            });
    }
}

uint32 FVoxelCaveSettings::GetHash() const
{
    uint32 H = ::GetTypeHash(bEnabled);
    H = HashCombine(H, ::GetTypeHash(Levels));
    for (const double V : { CellSize, LevelSpacing, WaterTableShare, KarstCoverage, PassageRadius, PassageRadiusMax, TrunkGain,
        ChamberShare, ChamberRadius, ShaftShare, MouthShare, DolineShare, SeaCaveShare, SeaCaveRadius, MeshedDepth, ShallowMeshedDepth,
        DeepFootprint, CoverDepth, CoverSlope, WallRoughness, Bedding, Blend })
    {
        H = HashCombine(H, ::GetTypeHash(V));
    }
    return H;
}

FVoxelCaveConstants VoxelLand::CaveConstantsFor(const FVoxelTerrestrialSettings& Settings, const FVoxelLandBake& Bake)
{
    const FVoxelCaveSettings& S = Settings.Caves;
    FVoxelCaveConstants C;
    C.CaveOn = S.bEnabled && Bake.N > 0 ? 1u : 0u;
    C.CaveSeed = HashCombine(::GetTypeHash(Settings.Seed), 0x63617665u);
    // The cave cell: a whole fraction of the bake cell, at least six a cell (the gather's 4 x 4
    // texel patch then covers a region of up to VOXEL_CAVE_REGION_MAX cells and the two around it).
    const double BakeCellM = VoxelLand::CellArc(Bake.SeaRadius, Bake.N) * 0.01;
    const int32 M = FMath::Max(6, FMath::RoundToInt32(BakeCellM / FMath::Max(S.CellSize, 1.0)));
    C.CaveCellsPerBake = (uint32)M;
    C.CaveCellM = (float)(BakeCellM / (double)M);
    // The wall detail's coarsest octave at about 8 m whatever the cell.
    C.CaveRoughCells = (uint32)FMath::Max(1, FMath::RoundToInt32((double)C.CaveCellM / 8.0));
    C.CaveLevels = (uint32)FMath::Clamp(S.Levels, 1, 4);
    C.CaveLevelM = (float)FMath::Max(S.LevelSpacing, 1.0);
    C.CaveTableShare = (float)FMath::Clamp(S.WaterTableShare, 0.0, 1.0);
    // Karst where the province is above the threshold for KarstCoverage of the land (as the
    // strata's switch), over a band of +-0.02.
    const float Threshold = Bake.ProvinceThreshold(FMath::Clamp(S.KarstCoverage, 0.0, 1.0));
    if (Threshold <= 1.0f)
    {
        C.CaveKarstLo = Threshold - 0.02f;
        C.CaveKarstHi = Threshold + 0.02f;
    }
    else
    {
        C.CaveKarstLo = 2.0f;
        C.CaveKarstHi = 2.1f;
    }
    C.CaveRadiusM = (float)FMath::Max(S.PassageRadius, 0.1);
    C.CaveRadiusMaxM = (float)FMath::Max(S.PassageRadiusMax, (double)C.CaveRadiusM);
    C.CaveTrunkGain = (float)FMath::Max(S.TrunkGain, 0.0);
    C.CaveChamberShare = (float)FMath::Clamp(S.ChamberShare, 0.0, 1.0);
    C.CaveChamberRadiusM = (float)FMath::Max(S.ChamberRadius, 1.0);
    C.CaveShaftShare = (float)FMath::Clamp(S.ShaftShare, 0.0, 1.0);
    C.CaveMouthShare = (float)FMath::Clamp(S.MouthShare, 0.0, 1.0);
    C.CaveDolineShare = (float)FMath::Clamp(S.DolineShare, 0.0, 1.0);
    C.CaveSeaShare = (float)FMath::Clamp(S.SeaCaveShare, 0.0, 1.0);
    C.CaveSeaRadiusM = (float)FMath::Max(S.SeaCaveRadius, 0.5);
    C.CaveDepthM = (float)FMath::Max(S.MeshedDepth, 0.0);
    C.CaveShallowDepthM = (float)FMath::Max(S.ShallowMeshedDepth, 0.0);
    C.CaveDeepCellM = (float)FMath::Max(S.DeepFootprint, 0.0);
    C.CaveCoverM = (float)FMath::Max(S.CoverDepth, 0.0);
    C.CaveCoverSlope = (float)FMath::Clamp(S.CoverSlope, 0.05, 1.0);
    C.CaveRoughM = (float)FMath::Max(S.WallRoughness, 0.0);
    C.CaveBeddingM = (float)FMath::Max(S.Bedding, 0.0);
    C.CaveBlendM = (float)FMath::Max(S.Blend, 0.0);
    // The largest radius any primitive can take (VoxelLandCaves.ush: passages, chambers' vertical
    // radii, shafts, dolines, sea caves): at footprints at or above it every cave is closed.
    C.CaveMaxRadiusM = FMath::Max3(C.CaveRadiusMaxM, C.CaveChamberRadiusM * 1.5f * C.CaveChamberSquash,
        FMath::Max3(C.CaveShaftRadiusM * 1.4f, C.CaveDolineRadiusM * 1.4f, C.CaveSeaRadiusM * 1.5f));
    return C;
}

uint32 VoxelLand::CaveBakeKey(const FVoxelTerrestrialSettings& Settings, const FVoxelLandBake& Bake)
{
    // 1: K1's packing.
    uint32 H = HashCombine(0x43415645u, 1u);
    H = HashCombine(H, Bake.Key);
    H = HashCombine(H, ::GetTypeHash(Bake.N));
    H = HashCombine(H, ::GetTypeHash(Bake.SeaRadius));
    H = HashCombine(H, ::GetTypeHash(Settings.Seed));
    H = HashCombine(H, ::GetTypeHash(Settings.BakeWarp));
    return H;
}

TSharedPtr<const FVoxelLandCaveBake, ESPMode::ThreadSafe> VoxelLand::GetCaveBake(const FVoxelLandBake& Bake, uint32 Key,
    TFunctionRef<TSharedPtr<FVoxelLandCaveBake, ESPMode::ThreadSafe>()> Build)
{
    FScopeLock Lock(&Bake.CaveLock);
    if (Bake.Caves.IsValid() && Bake.Caves->Key == Key) return Bake.Caves;
    TSharedPtr<FVoxelLandCaveBake, ESPMode::ThreadSafe> Made = Build();
    if (!Made.IsValid()) return nullptr;
    Made->Key = Key;
    Bake.Caves = Made;
    return Made;
}

FVoxelCaveShaderParameters VoxelLandGPU::GetCaveParameters(FRDGBuilder& GraphBuilder, const FVoxelDensityGPUData& Density)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(VoxelLandGPU::GetCaveParameters);
    FVoxelCaveShaderParameters Parameters;
    const FVoxelCaveConstants& C = Density.Caves;
    Parameters.CaveOn = C.CaveOn;
    Parameters.CaveSeed = C.CaveSeed;
    Parameters.CaveCellsPerBake = C.CaveCellsPerBake;
    Parameters.CaveLevels = C.CaveLevels;
    Parameters.CaveRoughCells = C.CaveRoughCells;
    Parameters.CaveRoughOctaves = C.CaveRoughOctaves;
    Parameters.CaveSeaCells = C.CaveSeaCells;
    Parameters.CaveCellM = C.CaveCellM;
    Parameters.CaveLevelM = C.CaveLevelM;
    Parameters.CaveTableShare = C.CaveTableShare;
    Parameters.CaveFloorClearM = C.CaveFloorClearM;
    Parameters.CavePhiWaveCells = C.CavePhiWaveCells;
    Parameters.CavePhiNoiseM = C.CavePhiNoiseM;
    Parameters.CavePhiJitterM = C.CavePhiJitterM;
    Parameters.CavePhiNoiseSlope = C.CavePhiNoiseSlope;
    Parameters.CaveTrunkFlowLo = C.CaveTrunkFlowLo;
    Parameters.CaveTrunkFlowHi = C.CaveTrunkFlowHi;
    Parameters.CaveRadiusM = C.CaveRadiusM;
    Parameters.CaveRadiusSpread = C.CaveRadiusSpread;
    Parameters.CaveTrunkGain = C.CaveTrunkGain;
    Parameters.CaveRadiusMaxM = C.CaveRadiusMaxM;
    Parameters.CaveKarstLo = C.CaveKarstLo;
    Parameters.CaveKarstHi = C.CaveKarstHi;
    Parameters.CaveKarstSlack = C.CaveKarstSlack;
    Parameters.CaveActiveShare = C.CaveActiveShare;
    Parameters.CaveTrunkActive = C.CaveTrunkActive;
    Parameters.CaveChamberShare = C.CaveChamberShare;
    Parameters.CaveChamberRadiusM = C.CaveChamberRadiusM;
    Parameters.CaveChamberSquash = C.CaveChamberSquash;
    Parameters.CaveShaftShare = C.CaveShaftShare;
    Parameters.CaveShaftRadiusM = C.CaveShaftRadiusM;
    Parameters.CaveMouthShare = C.CaveMouthShare;
    Parameters.CaveDolineShare = C.CaveDolineShare;
    Parameters.CaveDolineRadiusM = C.CaveDolineRadiusM;
    Parameters.CaveDolineHeadM = C.CaveDolineHeadM;
    Parameters.CaveDolineFunnel = C.CaveDolineFunnel;
    Parameters.CaveDolineBowlM = C.CaveDolineBowlM;
    Parameters.CaveDeepCellM = C.CaveDeepCellM;
    Parameters.CaveDepthM = C.CaveDepthM;
    Parameters.CaveShallowDepthM = C.CaveShallowDepthM;
    Parameters.CaveRoughM = C.CaveRoughM;
    Parameters.CaveRoughGain = C.CaveRoughGain;
    Parameters.CaveBeddingM = C.CaveBeddingM;
    Parameters.CaveBeddingWaveM = C.CaveBeddingWaveM;
    Parameters.CaveBlendM = C.CaveBlendM;
    Parameters.CaveBandSlackM = C.CaveBandSlackM;
    Parameters.CaveSeaShare = C.CaveSeaShare;
    Parameters.CaveSeaCoastM = C.CaveSeaCoastM;
    Parameters.CaveSeaZM = C.CaveSeaZM;
    Parameters.CaveSeaRadiusM = C.CaveSeaRadiusM;
    Parameters.CaveSeaReliefM = C.CaveSeaReliefM;
    Parameters.CaveSeaTall = C.CaveSeaTall;
    Parameters.CaveSeaFloorM = C.CaveSeaFloorM;
    Parameters.CaveSeaRoofM = C.CaveSeaRoofM;
    Parameters.CaveSeaCoverM = C.CaveSeaCoverM;
    Parameters.CaveFloorFill = C.CaveFloorFill;
    Parameters.CaveCanyonDepth = C.CaveCanyonDepth;
    Parameters.CaveCanyonWidth = C.CaveCanyonWidth;
    Parameters.CaveMeander = C.CaveMeander;
    Parameters.CaveMeanderZM = C.CaveMeanderZM;
    Parameters.CaveCoverM = C.CaveCoverM;
    Parameters.CaveCoverSlope = C.CaveCoverSlope;
    Parameters.CaveMaxRadiusM = C.CaveMaxRadiusM;
    const FVoxelLandCaveBake* Caves = Density.LandMode != 0 && C.CaveOn != 0u ? Density.LandCaves.Get() : nullptr;
    if (Caves && Caves->GPU.IsValid() && Caves->Words.Num() > 0)
    {
        // Uploaded once per cave bake (shared by the planet's densities).
        FVoxelLandCaveBakeGPU& Resource = *Caves->GPU;
        if (!Resource.Words.IsValid())
        {
            Resource.Words = GraphBuilder.ConvertToExternalBuffer(
                VoxelGPUCommon::UploadStructuredBuffer(GraphBuilder, TEXT("Voxel.LandCaveBake"), MakeConstArrayView(Caves->Words)));
        }
        Parameters.CaveBake = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(Resource.Words));
    }
    else
    {
        Parameters.CaveOn = 0u;
        Parameters.CaveBake = GraphBuilder.CreateSRV(VoxelGPUCommon::UploadStructuredBuffer<uint32>(GraphBuilder, TEXT("Voxel.LandCaveBakeNone"), TConstArrayView<uint32>()));
    }
    return Parameters;
}
