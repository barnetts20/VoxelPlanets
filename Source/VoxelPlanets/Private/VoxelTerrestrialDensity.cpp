#include "VoxelTerrestrialDensity.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "VoxelDensityGPU.h"
#include "VoxelLandBake.h"
#include "VoxelLandClimate.h"
#include "VoxelLandCaves.h"
#include "VoxelPlanets.h"
#include "Async/ParallelFor.h"
#include "VoxelShaderCompat.h"
#include "FDensitySampleCompositor.h"

namespace VoxelShader
{
#include "VoxelDensityCore.ush"
#include "VoxelDF64.ush"
#include "VoxelCubeSphere.ush"
// T5c: at namespace scope first (VoxelLandDunes.ush includes it again inside the sampler, a no-op
// then): in a unity build VoxelLandClimate.cpp and VoxelLandMaterial.cpp include it here too, and
// a first include inside the struct would leave them without it.
#include "VoxelLandClimate.ush"
// MAT-8: VoxelLandAccess.ush's free functions (VoxelLandUnpackForm, VoxelLandDecodeRelief,
// VoxelLandUnpackProvince) at namespace scope too, for the same reason: the land code includes
// it first, and a first include inside a host struct (here, or VoxelLandBake.cpp's plate model)
// would make them that struct's members and leave the other host without them in a unity build.
#include "VoxelLandAccess.ush"

    /** The land bake's sampler on the CPU: VoxelLandSample.ush and VoxelLandGully.ush (with
     *  VoxelLandHills.ush, VoxelLandNoise.ush and VoxelLandPeaks.ush) with the shader's buffers
     *  and constants as members (the GPU runs the same code); and the caves (VoxelLandCaves.ush,
     *  K1), their constants the base's members (FVoxelCaveConstants, the shaders' names). */
    struct FVoxelLandSamplerCPU : public FVoxelCaveConstants
    {
        const float* LandHeight = nullptr;
        const float2* LandTanTable = nullptr;
        const uint* LandFlow = nullptr;
        const uint* LandForm = nullptr;
        const uint* LandRelief = nullptr;
        const uint* LandProvince = nullptr;
        const uint* LandRiverTiles = nullptr;
        const float* LandRiverData = nullptr;
        const uint* LandNoise = nullptr;
        uint LandN = 1;
        uint LandPad = 0;
        uint LandHasFlow = 0;
        uint LandHasForm = 0;
        float LandCellM = 1.0f;
        float LandGullyStrength = 0.0f;
        uint LandGullyOctaves = 0;
        float LandGullyFlowGain = 0.0f;
        float LandGullyDetail = 1.0f;
        uint LandGullySeed = 0;
        float LandHillRelief = 0.0f;
        float2 LandHillFrequency = float2(0.0f, 0.0f);
        uint LandHillOctaves = 0;
        uint LandHillSeed = 0;
        float LandPeakStrength = 0.0f;
        float LandPeakTexelsPerCell = 0.0f;
        uint LandPeakSeed = 0;
        float LandPeakSpacingM = 0.0f;
        float LandPeakReliefM = 0.0f;
        float LandPeakReliefStart = 500.0f;   // (R2, D5: the GPU side's defaults; set whenever read)
        float LandPeakReliefEnd = 1500.0f;
        float LandPeakMeanR = 0.0f;
        float LandPeakMeanG = 0.0f;
        float LandPeakScaleR = 1.0f;
        float LandPeakScaleG = 1.0f;
        float LandPeakScaleB = 1.0f;
        uint LandNoiseN = 0;
        uint LandNoiseMips = 0;
        uint LandDetailBase = 0;
        uint LandDetailN = 0;
        uint LandDetailMips = 0;
        float LandPeakDetailTexelsPerCell = 0.0f;
        float LandDetailMeanHard = 0.0f;
        float LandDetailMeanSoft = 0.0f;
        float LandPeakDetailScaleHard = 0.0f;
        float LandPeakDetailScaleSoft = 0.0f;
        float LandPeakDetailFade = 0.0f;
        float LandHillTexelsPerCell = 0.0f;
        uint LandHillBase = 0;
        uint LandHillN = 0;
        uint LandHillMips = 0;
        float LandHillScaleR = 1.0f;
        float LandHillScaleG = 1.0f;
        float LandHillScaleB = 1.0f;
        float LandHillMeanR = 0.0f;
        float LandHillMeanG = 0.0f;
        float LandHillSpacingM = 0.0f;
        float LandHillSwellTexelsPerCell = 0.0f;
        float LandHillSwellM = 0.0f;
        float LandHillSwellScaleG = 1.0f;
        float LandHillDetailTexelsPerCell = 0.0f;
        float LandHillDetailScaleHard = 0.0f;
        float LandHillDetailScaleSoft = 0.0f;
        float LandHillDetailFade = 0.0f;
        float LandHillFoothill = 0.0f;
        float LandStrataTexelsPerCell = 0.0f;
        uint LandStrataBase = 0;
        uint LandStrataN = 0;
        uint LandStrataMips = 0;
        float LandStrataScaleR = 1.0f;
        float LandStrataScaleG = 1.0f;
        float LandStrataScaleB = 1.0f;
        float LandStrataMeanR = 0.0f;
        float LandStrataMeanG = 0.0f;
        float LandStrataSpacingM = 0.0f;
        float LandStrataRelief = 0.0f;
        float LandStrataFlat = 0.0f;
        float LandStrataThreshold = 2.0f;
        float LandStrataWidth = 0.05f;
        uint LandStrataSeed = 0;
        float LandStrataDetailTexelsPerCell = 0.0f;
        float LandStrataDetailScaleHard = 0.0f;
        float LandStrataDetailScaleSoft = 0.0f;
        float LandStrataDetailFade = 0.0f;
        uint LandRiverTilesPerFace = 0;
        float LandRiverRadiusM = 1.0f;
        float LandRiverWidthScale = 1.0f;
        float LandRiverValleyScale = 1.0f;
        const float* LandWater = nullptr;
        uint LandHasWater = 0;
        float LandRiverCarve = 0.0f;
        float LandRiverDepthScale = 1.0f;
        float LandRiverFloodplainScale = 1.0f;
        float LandRiverMinLog2Area = 0.0f;
        float LandLakeShoreBand = 0.0f;
        float LandRiverMeander = 0.0f;
        float LandRiverFalls = 0.0f;
        float LandDetailTexelsPerCell = 0.0f;
        uint LandDetailScales = 0;
        float LandDetailMeanA = 0.0f;
        float LandDetailScaleA = 0.0f;
        float LandDetailScaleGain = 0.0f;
        float LandDetailFade = 0.0f;
        uint LandDetailSeed = 0;
        // T5c: the dunes (VoxelLandDunes.ush).
        const float4* LandClimate = nullptr;
        uint LandHasClimate = 0;
        float LandClimateLapse = 6.5f;
        float LandDuneTexelsPerCell = 0.0f;
        uint LandDuneBase = 0;
        uint LandDuneN = 0;
        uint LandDuneMips = 0;
        float LandDuneScaleR = 1.0f;
        float LandDuneScaleG = 1.0f;
        float LandDuneMeanR = 0.0f;
        float LandDuneMeanG = 0.0f;
        float LandDuneRelief = 0.0f;
        uint LandDuneSeed = 0;
        float LandDuneErgFrequency = 1.0f;
        float LandDuneErgThreshold = 2.0f;
        float LandDuneErgWidth = 0.06f;
        float LandDuneReliefMax = 400.0f;
        float LandDuneDetailTexelsPerCell = 0.0f;
        uint LandDuneDetailBase = 0;
        uint LandDuneDetailN = 0;
        uint LandDuneDetailMips = 0;
        float LandDuneDetailScale = 0.0f;
        float LandDuneDetailMean = 0.0f;
        float LandDuneDetailFade = 0.0f;
        // T5d: the glacial layer (VoxelLandPeaks.ush).
        float LandGlacialTexelsPerCell = 0.0f;
        uint LandGlacialBase = 0;
        uint LandGlacialN = 0;
        uint LandGlacialMips = 0;
        float LandGlacialScaleR = 1.0f;
        float LandGlacialScaleG = 1.0f;
        float LandGlacialScaleB = 1.0f;
        float LandGlacialMeanR = 0.0f;
        float LandGlacialMeanG = 0.0f;
        float LandGlacialReliefM = 0.0f;
        float LandGlacialStrength = 0.0f;
        uint LandGlacialSeed = 0;
        float LandGlacialSpacingM = 0.0f;
        float LandGlacialCooling = 0.0f;
        float LandGlacialDetailScaleHard = 0.0f;
        float LandGlacialDetailScaleSoft = 0.0f;
        // T5d2: the ice sheets (VoxelLandIce.ush).
        float LandIceMaxThickness = 0.0f;
        float LandIceBlend = 0.0f;
        float LandIceSwellTexelsPerCell = 0.0f;
        uint LandIceBase = 0;
        uint LandIceN = 0;
        uint LandIceMips = 0;
        float LandIceMeanG = 0.0f;
        float LandIceSwellM = 0.0f;
        uint LandIceSeed = 0;
        float LandIceSwellSpacingM = 0.0f;
        // T5d3: the polar surfaces (VoxelLandPolar.ush).
        uint LandPolarBase = 0;
        uint LandPolarN = 0;
        uint LandPolarMips = 0;
        uint LandPolarSeed = 0;
        float LandPolarCrevasseTexelsPerCell = 0.0f;
        float LandPolarCrevasseM = 0.0f;
        float LandPolarCrevasseMean = 0.0f;
        float LandPolarCrevasseFade = 0.0f;
        float LandPolarSastrugiTexelsPerCell = 0.0f;
        float LandPolarSastrugiM = 0.0f;
        float LandPolarSastrugiMean = 0.0f;
        float LandPolarSastrugiFade = 0.0f;
        float LandPolarPolygonTexelsPerCell = 0.0f;
        float LandPolarPolygonM = 0.0f;
        float LandPolarPolygonMean = 0.0f;
        float LandPolarPolygonFade = 0.0f;
        float LandPolarPermafrostT = -6.0f;
        float LandPolarCrevasseTurn = 0.0f;
        float LandPolarSastrugiTurn = 0.0f;
        uint LandPolarDuneBase = 0;
        uint LandPolarDuneN = 0;
        uint LandPolarDuneMips = 0;
        float LandPolarDuneTexelsPerCell = 0.0f;
        float LandPolarDuneM = 0.0f;
        float LandPolarDuneMean = 0.0f;
        float LandPolarDuneFade = 0.0f;
        // K1: the cave bake (VoxelLandCaves.ush VOXEL_CAVE_WORD; read when CaveOn).
        const uint* CaveBake = nullptr;

#include "VoxelLandSample.ush"
#include "VoxelLandGully.ush"
#include "VoxelLandCaves.ush"
    };
}

namespace VoxelTerrestrialDensityDetail
{
    constexpr uint32 SaltLandWarp = 0x11u;   // + component
    constexpr uint32 SaltLandGully = 0x21u;
    constexpr uint32 SaltLandHill = 0x31u;
    constexpr uint32 SaltLandPeak = 0x41u;
    constexpr uint32 SaltLandStrata = 0x51u;
    constexpr uint32 SaltLandDetail = 0x61u;
    constexpr uint32 SaltLandDune = 0x71u;
    constexpr uint32 SaltLandGlacial = 0x81u;
    constexpr uint32 SaltLandIce = 0x91u;
    constexpr uint32 SaltLandPolar = 0xA1u;

    /** The ergs' noise's edge (VoxelLandDunes.ush: their share's smoothstep half-width, in the
     *  noise's units; its spread is about 0.25). */
    constexpr float LandDuneErgEdge = 0.06f;

    /** The sampling warp of the land bake: three octaves of fBm per component, its longest
     *  wavelength six bake cells. */
    constexpr int32 LandWarpOctaves = 3;
    constexpr double LandWarpWavelengthCells = 6.0;

    VoxelShader::float2 ToDoubleFloat(double V)
    {
        const float Hi = (float)V;
        return VoxelShader::float2(Hi, (float)(V - (double)Hi));
    }

    /** The warp's layers are plain, so VoxelLayerOctave ignores these; they are the noise
     *  surface's defaults (FVoxelSurfaceSettings), which the warp was evaluated with before the
     *  generators were split (FVoxelNoiseSurfaceDensity). */
    constexpr float LandWarpRidgeGain = 2.0f;
    constexpr float LandWarpSlopeDamping = 3.0f;
}

FVoxelTerrestrialDensity::FVoxelTerrestrialDensity(const FVoxelTerrestrialSettings& InLand, TSharedPtr<const FVoxelLandBake, ESPMode::ThreadSafe> InBake,
    double InPlanetRadius, double InNoiseAmplitude, double InMinFootprint, TSharedPtr<const FVoxelLandClimateField, ESPMode::ThreadSafe> InClimate)
    : FVoxelPlanetDensity(InPlanetRadius, InNoiseAmplitude, InMinFootprint)
    , LandBake(MoveTemp(InBake))
{
    TRACE_CPUPROFILER_EVENT_SCOPE(FVoxelTerrestrialDensity::FVoxelTerrestrialDensity);
    using namespace VoxelTerrestrialDensityDetail;
    using VoxelDensityLayers::MakeLayer;

    check(LandBake.IsValid());
    const uint32 Seed = (uint32)InLand.Seed;
    // The per-sample layers (warp, detail) are sampled on the sea-level sphere, not on
    // PlanetRadius: that is the bottom of the height range, which moves with the layers'
    // amplitudes, and would move every pattern with it.
    LandSampleRadius = FMath::Max(LandBake->SeaRadius, 1.0);
    const double SampleRadius = LandSampleRadius;

    // The noise surface's layers are absent (N1: the detail layer is the detail set's,
    // VoxelLandDetail.ush, below); the octave table holds only the warp's.
    const double Cell = VoxelLand::CellArc(LandBake->SeaRadius, LandBake->N);
    const double WarpFraction = LandWarpWavelengthCells * Cell / SampleRadius;
    for (int32 Axis = 0; Axis < 3; Axis++)
        LandWarp[Axis] = MakeLayer(VoxelShader::VoxelLayerPlain, WarpFraction, SampleRadius, 2.0, 0.5, LandWarpOctaves, VoxelShader::VoxelHashSeed(Seed, SaltLandWarp + (uint32)Axis));
    LandWarpAmplitude = FMath::Max(InLand.BakeWarp, 0.0) * Cell;
    LandDetailAmplitude = FMath::Max(InLand.DetailAmplitude, 0.0) * 100.0;
    LandHeightOffset = LandBake->SeaRadius - PlanetRadius;

    // The gullies (VoxelLandGully.ush): octaves from two bake cells down to the finest wavelength.
    LandCellM = (float)(Cell * 0.01);
    LandGullyStrength = (float)FMath::Max(InLand.GullyStrength, 0.0);
    LandGullyOctaves = (uint32)VoxelLand::GullyOctavesFor(InLand, *LandBake);
    LandGullyFlowGain = (float)FMath::Clamp(InLand.GullyFlowGain, 0.0, 1.0);
    LandGullyDetail = (float)FMath::Clamp(InLand.GullyDetailScale, 0.0, 1.0);
    LandGullySeed = VoxelShader::VoxelHashSeed(Seed, SaltLandGully);
    if (LandBake->Flow.Num() == LandBake->NumCells() && LandBake->NumCells() % 2 == 0)
    {
        // Two cells per word, the lower index in the low half: the GPU's packing (and the
        // little-endian layout of the f16 array).
        LandFlowPacked.SetNumUninitialized(LandBake->NumCells() / 2);
        for (int32 i = 0; i < LandFlowPacked.Num(); i++)
            LandFlowPacked[i] = (uint32)LandBake->Flow[2 * i].Encoded | ((uint32)LandBake->Flow[2 * i + 1].Encoded << 16);
    }

    // The hills (VoxelLandHills.ush), on the landform field.
    LandHillRelief = (float)FMath::Max(InLand.HillRelief, 0.0);
    LandHillOctaves = (uint32)VoxelLand::HillOctavesFor(InLand);
    LandHillFrequency = VoxelLand::HillFinestFrequency(InLand);
    LandHillSeed = VoxelShader::VoxelHashSeed(Seed, SaltLandHill);
    if (LandBake->Form.Num() == LandBake->NumCells() && LandBake->NumCells() % 2 == 0)
    {
        LandFormPacked.SetNumUninitialized(LandBake->NumCells() / 2);
        for (int32 i = 0; i < LandFormPacked.Num(); i++)
            LandFormPacked[i] = (uint32)LandBake->Form[2 * i] | ((uint32)LandBake->Form[2 * i + 1] << 16);
    }

    // The peaks (VoxelLandPeaks.ush): one lookup of the shared noise set, placed at the massif
    // spacing, on the local relief.
    LandPeakStrength = (float)FMath::Max(InLand.PeakHeightScale, 0.0);
    LandPeakSeed = VoxelShader::VoxelHashSeed(Seed, SaltLandPeak);
    // The peaks' fade-in over the local relief (T4b), ordered with a metre between.
    LandPeakReliefStart = (float)FMath::Max(InLand.PeakReliefFadeStart, 0.0);
    LandPeakReliefEnd = (float)FMath::Max(InLand.PeakReliefFadeEnd, (double)LandPeakReliefStart + 1.0);
    if (InLand.PeakReliefFadeEnd < InLand.PeakReliefFadeStart + 1.0)
    {
        UE_LOG(LogVoxelPlanets, Warning, TEXT("[VoxelDensity] PeakReliefFadeEnd (%.0f m) is not above PeakReliefFadeStart (%.0f m): the peaks will switch on in a 1 m step."),
            InLand.PeakReliefFadeEnd, InLand.PeakReliefFadeStart);
    }
    if (LandBake->Relief.Num() == LandBake->NumCells())
    {
        LandReliefPacked.SetNumZeroed((LandBake->Relief.Num() + 3) / 4);
        for (int32 i = 0; i < LandBake->Relief.Num(); i++)
            LandReliefPacked[i >> 2] |= (uint32)LandBake->Relief[i] << ((i & 3) * 8);
        // The province (T4c), packed as the relief: zeros if the bake has none.
        LandProvincePacked.SetNumZeroed(LandReliefPacked.Num());
        if (LandBake->Province.Num() == LandBake->NumCells())
            for (int32 i = 0; i < LandBake->Province.Num(); i++)
                LandProvincePacked[i >> 2] |= (uint32)FMath::Min(LandBake->Province[i], (uint8)63) << ((i & 3) * 8);
    }
    // The noise pack: the peaks' set, the detail set, the hills' set (T4a), the strata's (T4c).
    // The layers on the landform need the landform; the detail layer (N1) doesn't.
    const bool bHasForm = LandFormPacked.Num() > 0 && LandReliefPacked.Num() > 0;
    LandNoise = VoxelLand::GetLandNoise(VoxelLand::LandNoiseNameFor(InLand));
    if (LandNoise.IsValid() && bHasForm && LandPeakStrength > 0.0f && LandNoise->N > 0)
        LandPeakTexelsPerCell = VoxelLand::PeakTexelsPerCell(InLand, *LandBake, *LandNoise);
    if (LandNoise.IsValid() && LandNoise->DetailN > 0)
    {
        // The detail set's means, whoever reads it (the peaks, the hills' and the strata's sets).
        LandDetailMeanHard = LandNoise->DetailMeanHard;
        LandDetailMeanSoft = LandNoise->DetailMeanSoft;
    }
    // N1: the detail layer (VoxelLandDetail.ush): scales of the set's A channel, four octaves
    // each, the first's coarsest at DetailWavelength, from there down to the finest cell (at most
    // DetailMaxScales), and no finer than the lattice's range holds (VoxelLandPeakRotated: the
    // turned texel coordinates, at most sqrt 2 times the face's half-width and blend band in
    // texels, under 2^30).
    LandDetailSeed = VoxelShader::VoxelHashSeed(Seed, SaltLandDetail);
    if (LandNoise.IsValid() && LandNoise->DetailN > 0 && LandNoise->DetailSpacingTexels > 0 && LandDetailAmplitude > 0.0 && LandNoise->DetailStdA > 0.0f)
    {
        const FVoxelLandNoiseSet& Set = *LandNoise;
        const double WavelengthM = VoxelLand::DetailLayerWavelength(InLand);
        const double TexelM = WavelengthM / (double)Set.DetailSpacingTexels;
        LandDetailTexelsPerCell = (float)((double)LandCellM / TexelM);
        // Octaves from the wavelength to the finest cell, four a scale.
        const double Finest = FMath::Max(MinFootprint, 1.0) * 0.01;
        const int32 Octaves = FMath::Max(FMath::CeilToInt(FMath::Log2(WavelengthM / Finest)) + 1, 1);
        int32 Scales = FMath::Clamp((Octaves + 3) / 4, 1, VoxelLand::DetailMaxScales);
        const double FaceTexels = UE_SQRT_2 * (0.5 * (double)LandBake->N + 4.0);
        while (Scales > 1 && (double)LandDetailTexelsPerCell * FMath::Pow(16.0, (double)(Scales - 1)) * FaceTexels >= 1073741824.0)
            Scales--;
        LandDetailScales = (uint32)Scales;
        LandDetailMeanA = Set.DetailMeanA;
        // A's unit over its standard deviation times 0.4 (the old fBm's normalisation).
        LandDetailScaleA = 0.4f / Set.DetailStdA;
        // One scale to the next: A's gain to the fourth (its octaves' spectrum continued).
        LandDetailScaleGain = Set.DetailScaleGain;
        LandDetailFade = (float)FMath::FloorLog2(Set.DetailSpacingTexels) - 1.0f;
        UE_LOG(LogVoxelPlanets, Log, TEXT("[VoxelLand] Detail %s: %d scales from %.0f m (finest octave %.2f m), %.0f m amplitude."),
            *Set.DetailName, Scales, WavelengthM, WavelengthM / FMath::Pow(16.0, (double)(Scales - 1)) / 8.0, LandDetailAmplitude * 0.01);
    }
    if (LandPeakTexelsPerCell > 0.0f)
    {
        const FVoxelLandNoiseSet& Set = *LandNoise;
        const float TexelM = LandCellM / LandPeakTexelsPerCell;
        LandPeakSpacingM = (float)FMath::Max(InLand.PeakSpacing, VoxelLand::PeakMinSpacing);
        // The shape over the larger of hard and soft rock's ranges: within -1 .. 1.
        const float Range = FMath::Max3(Set.RangeR, Set.RangeG, 1e-6f);
        LandPeakReliefM = Range * TexelM;
        LandPeakMeanR = Set.MeanR;
        LandPeakMeanG = Set.MeanG;
        LandPeakScaleR = Set.RangeR / Range;
        LandPeakScaleG = Set.RangeG / Range;
        LandPeakScaleB = Set.RangeB / Range;
        // The detail set (T3e, N1): its own texel; its heights (texels) over the peaks' relief, so
        // strength 1 keeps its slopes; faded out by the mip level where its coarsest octave
        // (DetailSpacingTexels) has two texels.
        LandPeakDetailTexelsPerCell = VoxelLand::PeakDetailTexelsPerCell(InLand, *LandBake, Set);
        if (LandPeakDetailTexelsPerCell > 0.0f)
        {
            const float DetailTexelM = LandCellM / LandPeakDetailTexelsPerCell;
            const float Strength = (float)InLand.PeakDetailStrength;
            LandPeakDetailScaleHard = Strength * Set.DetailRangeHard * DetailTexelM / LandPeakReliefM;
            LandPeakDetailScaleSoft = Strength * Set.DetailRangeSoft * DetailTexelM / LandPeakReliefM;
            LandPeakDetailFade = (float)FMath::FloorLog2(Set.DetailSpacingTexels) - 1.0f;
        }
    }
    else
    {
        LandPeakStrength = 0.0f;
        LandPeakTexelsPerCell = 0.0f;
    }

    // The hills from the pack's set (VoxelLandHillSet.ush, T4a): placed at the hill spacing, from
    // its base network up; LandHillRelief becomes the set's relief (m) times the height scale,
    // capped (HillSetReliefCap). Else the procedural hills above.
    if (LandNoise.IsValid() && bHasForm && LandHillRelief > 0.0f && LandNoise->HillN > 0)
        LandHillTexelsPerCell = VoxelLand::HillTexelsPerCell(InLand, *LandBake, *LandNoise);
    if (LandHillTexelsPerCell > 0.0f)
    {
        const FVoxelLandNoiseSet& Set = *LandNoise;
        const float TexelM = LandCellM / LandHillTexelsPerCell;
        const float Range = FMath::Max3(Set.HillRangeR, Set.HillRangeG, 1e-6f);
        const float ReliefM = Range * TexelM;
        const float Wanted = (float)InLand.HillHeightScale * ReliefM;
        const float Cap = (float)VoxelLand::HillSetReliefCap(InLand);
        if (Wanted > Cap)
            UE_LOG(LogVoxelPlanets, Warning, TEXT("[VoxelLand] The hill set %s's relief (%.0f m at HillHeightScale %.2f) is over half HillSpacing; capped at %.0f m."),
                *Set.HillName, Wanted, InLand.HillHeightScale, Cap);
        LandHillRelief = FMath::Min(Wanted, Cap);
        LandHillSpacingM = (float)FMath::Max(InLand.HillSpacing, VoxelLand::HillMinSpacing);
        LandHillFoothill = (float)FMath::Clamp(InLand.HillFoothillGrowth, 0.0, 2.0);
        LandHillScaleR = Set.HillRangeR / Range;
        LandHillScaleG = Set.HillRangeG / Range;
        LandHillScaleB = Set.HillRangeB / Range;
        LandHillMeanR = Set.HillMeanR;
        LandHillMeanG = Set.HillMeanG;
        if (InLand.HillSwellHeight > 0.0)
        {
            LandHillSwellTexelsPerCell = LandHillTexelsPerCell / (float)FMath::Clamp(InLand.HillSwellScale, 1.5, 8.0);
            LandHillSwellM = (float)InLand.HillSwellHeight;
            // G's whole range is the swells' unit.
            LandHillSwellScaleG = 1.0f;
        }
        // The detail set, as the peaks': its heights (texels) over the set's relief, so strength 1
        // keeps its slopes where the hills keep theirs.
        if (InLand.HillDetailStrength > 0.0 && Set.DetailN > 0 && Set.DetailSpacingTexels > 0)
        {
            const double DetailTexelM = VoxelLand::HillDetailWavelength(InLand) / (double)Set.DetailSpacingTexels;
            LandHillDetailTexelsPerCell = (float)(LandCellM / DetailTexelM);
            const float DetailTexelMF = LandCellM / LandHillDetailTexelsPerCell;
            const float Strength = (float)InLand.HillDetailStrength;
            LandHillDetailScaleHard = Strength * Set.DetailRangeHard * DetailTexelMF / ReliefM;
            LandHillDetailScaleSoft = Strength * Set.DetailRangeSoft * DetailTexelMF / ReliefM;
            LandHillDetailFade = (float)FMath::FloorLog2(Set.DetailSpacingTexels) - 1.0f;
        }
    }
    // The strata (VoxelLandHillSet.ush, T4c): the pack's strata set, placed at the strata spacing,
    // where the province is above the threshold for StrataCoverage of the land; in the hill
    // set's layer (none without it).
    LandStrataSeed = VoxelShader::VoxelHashSeed(Seed, SaltLandStrata);
    if (LandHillTexelsPerCell > 0.0f && LandNoise->StrataN > 0)
        LandStrataTexelsPerCell = VoxelLand::StrataTexelsPerCell(InLand, *LandBake, *LandNoise);
    if (LandStrataTexelsPerCell > 0.0f)
    {
        const FVoxelLandNoiseSet& Set = *LandNoise;
        const float TexelM = LandCellM / LandStrataTexelsPerCell;
        const float Range = FMath::Max3(Set.StrataRangeR, Set.StrataRangeG, 1e-6f);
        const float ReliefM = Range * TexelM;
        const float Wanted = (float)InLand.StrataHeightScale * ReliefM;
        const float Cap = (float)VoxelLand::StrataSetReliefCap(InLand);
        if (Wanted > Cap)
            UE_LOG(LogVoxelPlanets, Warning, TEXT("[VoxelLand] The strata set %s's relief (%.0f m at StrataHeightScale %.2f) is over half StrataSpacing; capped at %.0f m."),
                *Set.StrataName, Wanted, InLand.StrataHeightScale, Cap);
        LandStrataRelief = FMath::Min(Wanted, Cap);
        LandStrataSpacingM = (float)FMath::Max(InLand.StrataSpacing, VoxelLand::HillMinSpacing);
        LandStrataScaleR = Set.StrataRangeR / Range;
        LandStrataScaleG = Set.StrataRangeG / Range;
        LandStrataScaleB = Set.StrataRangeB / Range;
        LandStrataMeanR = Set.StrataMeanR;
        LandStrataMeanG = Set.StrataMeanG;
        LandStrataFlat = (float)FMath::Clamp(InLand.StrataFlatShare, 0.0, 1.0);
        LandStrataWidth = (float)FMath::Clamp(InLand.StrataSwitchWidth, 0.005, 0.5);
        // At least the switch's half-width above 0: the shields and belt cores (province 0)
        // keep the hills whatever the coverage.
        LandStrataThreshold = FMath::Max(LandBake->ProvinceThreshold(InLand.StrataCoverage), LandStrataWidth);
        if (InLand.StrataDetailStrength > 0.0 && Set.DetailN > 0 && Set.DetailSpacingTexels > 0)
        {
            const double DetailTexelM = VoxelLand::StrataDetailWavelength(InLand) / (double)Set.DetailSpacingTexels;
            LandStrataDetailTexelsPerCell = (float)(LandCellM / DetailTexelM);
            const float DetailTexelMF = LandCellM / LandStrataDetailTexelsPerCell;
            const float Strength = (float)InLand.StrataDetailStrength;
            LandStrataDetailScaleHard = Strength * Set.DetailRangeHard * DetailTexelMF / ReliefM;
            LandStrataDetailScaleSoft = Strength * Set.DetailRangeSoft * DetailTexelMF / ReliefM;
            LandStrataDetailFade = (float)FMath::FloorLog2(Set.DetailSpacingTexels) - 1.0f;
        }
        UE_LOG(LogVoxelPlanets, Log, TEXT("[VoxelLand] Strata %s: %.0f m relief at %.0f m spacing over %.0f%% of the land (province above %.3f)."),
            *Set.StrataName, LandStrataRelief, LandStrataSpacingM, 100.0 * FMath::Clamp(InLand.StrataCoverage, 0.0, 1.0), LandStrataThreshold);
    }
    // T5c: the dunes (VoxelLandDunes.ush): the pack's dune set placed at the dune spacing, in the
    // hill set's layer, where the climate (the planet's snapshot) makes desert, in sand seas over
    // DuneErgCoverage of it (the ergs' noise's threshold for that share of the sphere).
    LandDuneSeed = VoxelShader::VoxelHashSeed(Seed, SaltLandDune);
    const bool bClimate = InClimate.IsValid() && InClimate->IsValid();
    if (bClimate && LandHillTexelsPerCell > 0.0f && LandNoise->DuneN > 0)
        LandDuneTexelsPerCell = VoxelLand::DuneTexelsPerCell(InLand, *LandBake, *LandNoise);
    // R2 (D3): the peaks', hills' and strata's detail placements held to the lattice's range too,
    // as every other placement (past 2^30 turned texels the float-to-int split is undefined, and
    // the CPU and the GPU disagree): on very large planets at the finest detail wavelengths.
    {
        const double LatticeFaceTexels = UE_SQRT_2 * (0.5 * (double)LandBake->N + 4.0);
        for (float* PerCell : { &LandPeakDetailTexelsPerCell, &LandHillDetailTexelsPerCell, &LandStrataDetailTexelsPerCell })
        {
            if (*PerCell > 0.0f && (double)*PerCell * LatticeFaceTexels >= 1073741824.0)
            {
                UE_LOG(LogVoxelPlanets, Warning, TEXT("[VoxelLand] A detail wavelength (the peaks', hills' or strata's) is too fine for this planet's lattice; that detail is left out."));
                *PerCell = 0.0f;
            }
        }
    }
    // The lattice's range (as the detail layer's): the turned texel coordinates under 2^30.
    const double FaceTexels = UE_SQRT_2 * (0.5 * (double)LandBake->N + 4.0);
    if (LandDuneTexelsPerCell > 0.0f && (double)LandDuneTexelsPerCell * FaceTexels >= 1073741824.0)
    {
        UE_LOG(LogVoxelPlanets, Warning, TEXT("[VoxelLand] DuneSpacing %.0f m is too fine for this planet's lattice; no dunes."), InLand.DuneSpacing);
        LandDuneTexelsPerCell = 0.0f;
    }
    // T5d: the glacial layer (VoxelLandPeaks.ush): the pack's glacial set at the glacial spacing, in
    // the peaks' place where the climate has had glaciers.
    LandGlacialSeed = VoxelShader::VoxelHashSeed(Seed, SaltLandGlacial);
    if (bClimate && LandPeakTexelsPerCell > 0.0f && LandNoise->GlacialN > 0)
        LandGlacialTexelsPerCell = VoxelLand::GlacialTexelsPerCell(InLand, *LandBake, *LandNoise);
    if (LandGlacialTexelsPerCell > 0.0f && (double)LandGlacialTexelsPerCell * FaceTexels >= 1073741824.0)
    {
        UE_LOG(LogVoxelPlanets, Warning, TEXT("[VoxelLand] GlacialSpacing %.0f m is too fine for this planet's lattice; the peaks everywhere."), InLand.GlacialSpacing);
        LandGlacialTexelsPerCell = 0.0f;
    }
    // T5d2: the ice sheets (VoxelLandIce.ush): the climate's ice, the pack's ice sheet set's swells.
    LandIceSeed = VoxelShader::VoxelHashSeed(Seed, SaltLandIce);
    if (bClimate && InLand.IceSheetMaxThickness > 0.0)
    {
        LandIceMaxThickness = (float)InLand.IceSheetMaxThickness;
        LandIceBlend = (float)VoxelLand::IceSheetBlend;
        if (LandNoise.IsValid() && LandNoise->IceN > 0 && LandNoise->IceSpacingTexels > 0 && InLand.IceSheetSwellHeight > 0.0)
        {
            const double TexelM = FMath::Max(InLand.IceSheetSwellSpacing, 1000.0) / (double)LandNoise->IceSpacingTexels;
            LandIceSwellTexelsPerCell = (float)((double)LandCellM / TexelM);
            LandIceSwellM = (float)InLand.IceSheetSwellHeight;
            LandIceMeanG = LandNoise->IceMeanG;
            LandIceSwellSpacingM = (float)FMath::Max(InLand.IceSheetSwellSpacing, 1000.0);
        }
        UE_LOG(LogVoxelPlanets, Log, TEXT("[VoxelLand] Ice sheets: up to %.0f m thick where the climate ices the land; swells %s."), LandIceMaxThickness,
            LandIceSwellTexelsPerCell > 0.0f ? *FString::Printf(TEXT("%s, %.0f m at %.0f km"), *LandNoise->IceName, LandIceSwellM, LandIceSwellSpacingM * 0.001f) : TEXT("none"));
    }
    // T5d3: the polar surfaces (VoxelLandPolar.ush): the pack's polar set's crevasses (R), sastrugi
    // (G) and ice-wedge polygons (B), each at its spacing (one of the set's cells) and height scale
    // (1: the simulated slopes), where the climate makes ice or frozen ground.
    LandPolarSeed = VoxelShader::VoxelHashSeed(Seed, SaltLandPolar);
    if (bClimate && LandNoise.IsValid() && LandNoise->PolarN > 0)
    {
        const FVoxelLandNoiseSet& Set = *LandNoise;
        const double Spacings[3] = { VoxelLand::PolarSpacing(InLand, 0), VoxelLand::PolarSpacing(InLand, 1), VoxelLand::PolarSpacing(InLand, 2) };
        const double Scales[3] = { InLand.CrevasseHeightScale, InLand.SastrugiHeightScale, InLand.PolygonHeightScale };
        const TCHAR* Names[3] = { TEXT("CrevasseSpacing"), TEXT("SastrugiSpacing"), TEXT("PolygonSpacing") };
        float* TexelsPerCell[3] = { &LandPolarCrevasseTexelsPerCell, &LandPolarSastrugiTexelsPerCell, &LandPolarPolygonTexelsPerCell };
        float* Heights[3] = { &LandPolarCrevasseM, &LandPolarSastrugiM, &LandPolarPolygonM };
        float* Means[3] = { &LandPolarCrevasseMean, &LandPolarSastrugiMean, &LandPolarPolygonMean };
        float* Fades[3] = { &LandPolarCrevasseFade, &LandPolarSastrugiFade, &LandPolarPolygonFade };
        for (int32 c = 0; c < 3; c++)
        {
            if (!(Scales[c] > 0.0) || !(Set.PolarSpacingTexels[c] > 0.0f)) continue;
            const double TexelM = Spacings[c] / (double)Set.PolarSpacingTexels[c];
            const double PerCell = (double)LandCellM / TexelM;
            if (PerCell * FaceTexels >= 1073741824.0)
            {
                UE_LOG(LogVoxelPlanets, Warning, TEXT("[VoxelLand] %s %.1f m is too fine for this planet's lattice; left out."), Names[c], Spacings[c]);
                continue;
            }
            *TexelsPerCell[c] = (float)PerCell;
            // m per unit value: the channel's range in texels, at the texel, times the height scale.
            *Heights[c] = (float)(Scales[c] * (double)Set.PolarRange[c] * ((double)LandCellM / (double)(float)PerCell));
            *Means[c] = Set.PolarMean[c];
            // Faded out by the level at which one of the set's cells fits a texel.
            *Fades[c] = (float)FMath::Log2(FMath::Max((double)Set.PolarSpacingTexels[c], 2.0));
        }
        LandPolarPermafrostT = (float)InLand.PermafrostTemperature;
        LandPolarCrevasseTurn = Set.PolarTurn[0];
        LandPolarSastrugiTurn = Set.PolarTurn[1];
        // The snow dunes: the pack's dune set's G (rounded crests) at SnowDuneSpacing, SnowDuneHeight
        // from its lowest to its highest.
        if (InLand.SnowDuneHeight > 0.0 && Set.DuneN > 0 && Set.DuneSpacingTexels > 0)
        {
            const double TexelM = VoxelLand::SnowDuneSpacing(InLand) / (double)Set.DuneSpacingTexels;
            const double PerCell = (double)LandCellM / TexelM;
            if (PerCell * FaceTexels < 1073741824.0)
            {
                LandPolarDuneTexelsPerCell = (float)PerCell;
                LandPolarDuneM = (float)InLand.SnowDuneHeight;
                LandPolarDuneMean = Set.DuneMeanG;
                LandPolarDuneFade = (float)FMath::Log2((double)FMath::Max(Set.DuneSpacingTexels, 2u));
            }
        }
        if (LandPolarCrevasseTexelsPerCell > 0.0f || LandPolarSastrugiTexelsPerCell > 0.0f || LandPolarPolygonTexelsPerCell > 0.0f || LandPolarDuneTexelsPerCell > 0.0f)
            UE_LOG(LogVoxelPlanets, Log, TEXT("[VoxelLand] Polar surfaces %s: crevasses %s; sastrugi %s; ice-wedge polygons %s (permafrost below %.1f C); snow dunes %s."), *Set.PolarName,
                LandPolarCrevasseM > 0.0f ? *FString::Printf(TEXT("%.1f m deep at %.0f m"), LandPolarCrevasseM * Set.PolarMean[0], Spacings[0]) : TEXT("none"),
                LandPolarSastrugiM > 0.0f ? *FString::Printf(TEXT("%.2f m at %.1f m"), LandPolarSastrugiM * (1.0f - Set.PolarMean[1]), Spacings[1]) : TEXT("none"),
                LandPolarPolygonM > 0.0f ? *FString::Printf(TEXT("%.2f m at %.0f m"), LandPolarPolygonM, Spacings[2]) : TEXT("none"),
                LandPolarPermafrostT,
                LandPolarDuneTexelsPerCell > 0.0f ? *FString::Printf(TEXT("%s, %.1f m at %.0f m"), *Set.DuneName, LandPolarDuneM, VoxelLand::SnowDuneSpacing(InLand))
                    : TEXT("none (needs SnowDuneHeight and the pack's dune set)"));
    }
    const bool bPolar = LandPolarCrevasseTexelsPerCell > 0.0f || LandPolarSastrugiTexelsPerCell > 0.0f || LandPolarPolygonTexelsPerCell > 0.0f
        || LandPolarDuneTexelsPerCell > 0.0f;
    // The climate, kept for whichever layer reads it (T5e: and the ice shelf's share: the rivers fade
    // on it).
    // (R2, D1: the shelf only with a climate, as every climate layer: IceShelfHeight defaults to 40,
    // so a planet with the climate off dereferenced a null field here, on a pool worker since P1i.)
    bLandIceShelf = bClimate && InLand.IceShelfHeight > 0.0;
    if (LandDuneTexelsPerCell > 0.0f || LandGlacialTexelsPerCell > 0.0f || LandIceMaxThickness > 0.0f || bPolar || bLandIceShelf)
    {
        LandClimate = MoveTemp(InClimate);
        if (LandClimate.IsValid())
            LandClimateLapse = LandClimate->LapseRate;
    }
    if (LandGlacialTexelsPerCell > 0.0f)
    {
        const FVoxelLandNoiseSet& Set = *LandNoise;
        const float TexelM = LandCellM / LandGlacialTexelsPerCell;
        const float Range = FMath::Max3(Set.GlacialRangeR, Set.GlacialRangeG, 1e-6f);
        LandGlacialReliefM = Range * TexelM;
        LandGlacialStrength = (float)FMath::Max(InLand.GlacialHeightScale, 0.0);
        LandGlacialScaleR = Set.GlacialRangeR / Range;
        LandGlacialScaleG = Set.GlacialRangeG / Range;
        LandGlacialScaleB = Set.GlacialRangeB / Range;
        LandGlacialMeanR = Set.GlacialMeanR;
        LandGlacialMeanG = Set.GlacialMeanG;
        LandGlacialSpacingM = (float)FMath::Max(InLand.GlacialSpacing, VoxelLand::PeakMinSpacing);
        LandGlacialCooling = (float)FMath::Max(InLand.GlacialEraCooling, 0.0);
        // The peaks' detail (its placement and fade), over the glacial set's relief.
        if (LandPeakDetailTexelsPerCell > 0.0f)
        {
            const float DetailTexelM = LandCellM / LandPeakDetailTexelsPerCell;
            const float Strength = (float)InLand.PeakDetailStrength;
            LandGlacialDetailScaleHard = Strength * Set.DetailRangeHard * DetailTexelM / LandGlacialReliefM;
            LandGlacialDetailScaleSoft = Strength * Set.DetailRangeSoft * DetailTexelM / LandGlacialReliefM;
        }
        UE_LOG(LogVoxelPlanets, Log, TEXT("[VoxelLand] Glacial %s: %.0f m relief at %.0f m spacing, where glaciers are or were (%.1f C colder)."),
            *Set.GlacialName, LandGlacialReliefM, LandGlacialSpacingM, LandGlacialCooling);
    }
    if (LandDuneTexelsPerCell > 0.0f)
    {
        const FVoxelLandNoiseSet& Set = *LandNoise;
        const float Range = FMath::Max3(Set.DuneRangeR, Set.DuneRangeG, 1e-6f);
        LandDuneRelief = (float)VoxelLand::DuneRelief(InLand, Set);
        LandDuneScaleR = Set.DuneRangeR / Range;
        LandDuneScaleG = Set.DuneRangeG / Range;
        LandDuneMeanR = Set.DuneMeanR;
        LandDuneMeanG = Set.DuneMeanG;
        LandDuneReliefMax = (float)FMath::Max(InLand.DuneMaxRelief, 10.0);
        // The ergs: the sphere's radius over their size, at least one cell a radian.
        LandDuneErgFrequency = (float)FMath::Max(LandBake->SeaRadius * 1.0e-5 / FMath::Max(InLand.DuneErgSize, 10.0), 1.0);
        LandDuneErgWidth = LandDuneErgEdge;
        LandDuneErgThreshold = VoxelLandClimate::ErgThreshold(LandDuneErgFrequency, (float)InLand.DuneErgCoverage);
        // The sand detail (its G, the megaripples): its own placement; its height (m) over the
        // dunes' relief, in their unit; faded as the peaks' detail.
        const double SandRelief = VoxelLand::SandDetailRelief(InLand, Set);
        if (SandRelief > 0.0 && LandDuneRelief > 0.0f)
        {
            const double DetailTexelM = VoxelLand::SandDetailWavelength(InLand) / (double)Set.SandSpacingTexels;
            LandDuneDetailTexelsPerCell = (float)((double)LandCellM / DetailTexelM);
            if ((double)LandDuneDetailTexelsPerCell * FaceTexels >= 1073741824.0)
            {
                UE_LOG(LogVoxelPlanets, Warning, TEXT("[VoxelLand] SandDetailWavelength %.1f m is too fine for this planet's lattice; the dunes go without it."),
                    InLand.SandDetailWavelength);
                LandDuneDetailTexelsPerCell = 0.0f;
            }
            else
            {
                LandDuneDetailScale = (float)(SandRelief / (double)LandDuneRelief);
                LandDuneDetailMean = Set.SandMeanG;
                LandDuneDetailFade = (float)FMath::FloorLog2(FMath::Max((uint32)Set.SandSpacingTexels, 2u)) - 1.0f;
            }
        }
        UE_LOG(LogVoxelPlanets, Log, TEXT("[VoxelLand] Dunes %s: %.0f m relief at %.0f m spacing, sand seas %.0f km over %.0f%% of the desert (threshold %.3f); sand detail %s."),
            *Set.DuneName, LandDuneRelief, FMath::Max(InLand.DuneSpacing, VoxelLand::DuneMinSpacing), FMath::Max(InLand.DuneErgSize, 10.0),
            100.0 * FMath::Clamp(InLand.DuneErgCoverage, 0.0, 1.0), LandDuneErgThreshold,
            LandDuneDetailTexelsPerCell > 0.0f ? *FString::Printf(TEXT("%s, %.1f m"), *Set.SandName, SandRelief) : TEXT("none"));
    }
    if (LandPeakTexelsPerCell <= 0.0f && LandHillTexelsPerCell <= 0.0f && LandDetailTexelsPerCell <= 0.0f)
    {
        LandNoise.Reset();
        LandIceSwellTexelsPerCell = 0.0f;   // its chain went with the pack
        LandPolarCrevasseTexelsPerCell = 0.0f;   // T5d3: likewise
        LandPolarSastrugiTexelsPerCell = 0.0f;
        LandPolarPolygonTexelsPerCell = 0.0f;
        LandPolarDuneTexelsPerCell = 0.0f;
    }

    // The rivers (VoxelLandRivers.ush, R1): made on the GPU with the bake, or by VoxelLand::TickRivers
    // before the planet builds (null until then).
    LandRivers = VoxelLand::GetRivers(*LandBake, InLand);
    if (LandRivers.IsValid() && LandRivers->TilesPerFace > 0)
    {
        LandRiverTilesPerFace = (uint32)LandRivers->TilesPerFace;
        LandRiverRadiusM = (float)(LandBake->SeaRadius * 0.01);
        LandRiverWidthScale = (float)FMath::Max(InLand.RiverWidthScale, 0.1);
        LandRiverValleyScale = (float)FMath::Max(InLand.ValleyWidthScale, 0.1);
    }
    else
        LandRivers.Reset();

    // R2: the rivers' and lakes' carve (VoxelLandCarve.ush). The lakes need the bake's levels.
    LandRiverCarve = InLand.bCarveRivers ? 1.0f : 0.0f;
    LandRiverDepthScale = (float)FMath::Max(InLand.RiverDepthScale, 0.0);
    LandRiverFloodplainScale = (float)FMath::Max(InLand.FloodplainScale, 0.0);
    LandRiverMinLog2Area = InLand.RiverMinArea > 0.0 ? (float)FMath::Log2(InLand.RiverMinArea * 1.0e6) : 0.0f;
    LandLakeShoreBand = InLand.bCarveRivers ? (float)FMath::Max(InLand.LakeShoreBand, 0.0) : 0.0f;
    // R3: the meanders and the knickpoints' falls.
    LandRiverMeander = (float)FMath::Max(InLand.RiverMeander, 0.0);
    LandRiverFalls = (float)FMath::Clamp(InLand.RiverFalls, 0.0, 1.4);
    LandHasWater = InLand.bCarveRivers && LandBake->Water.Num() == LandBake->NumCells() ? 1u : 0u;

    BuildLandSampler();
    BuildCaves(InLand);
}

FVoxelTerrestrialDensity::~FVoxelTerrestrialDensity() = default;

double FVoxelTerrestrialDensity::HeightAt(const FVector& Dir, double Footprint) const
{
    const double Len = Dir.Size();
    if (Len <= 1e-12 || NoiseAmplitude <= 0.0) return 0.0;

    // Footprints below the finest cell are raised to it, as Sample does (and the GPU).
    const double Band = FMath::Max(Footprint, MinFootprint);
    // The terrestrial layers on the sea-level sphere (see the constructor).
    return LandHeightAt(Dir * (LandSampleRadius / Len), Band);
}

double FVoxelTerrestrialDensity::LandHeightAt(const FVector& SurfacePoint, double Footprint) const
{
    return LandSurfaceAt(SurfacePoint, Footprint, nullptr);
}

void FVoxelTerrestrialDensity::BuildCaves(const FVoxelTerrestrialSettings& InLand)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(FVoxelTerrestrialDensity::BuildCaves);
    CaveConstants = VoxelLand::CaveConstantsFor(InLand, *LandBake);
    if (CaveConstants.CaveOn != 0u)
    {
        const uint32 Key = VoxelLand::CaveBakeKey(InLand, *LandBake);
        LandCaves = VoxelLand::GetCaveBake(*LandBake, Key, [this]() { return MakeCaveBake(); });
        if (!LandCaves.IsValid() || LandCaves->Words.Num() != 2 * LandBake->NumCells())
        {
            LandCaves.Reset();
            CaveConstants.CaveOn = 0u;
        }
    }
    if (CaveConstants.CaveOn != 0u)
    {
        UE_LOG(LogVoxelPlanets, Log, TEXT("[VoxelLand] Caves: %u cells of %.0f m a bake cell, %u levels %.0f m apart; karst above province %.3f (%.0f%% of the land)%s; meshed %.0f m deep to %.1f m cells; closed from %.1f m cells."),
            CaveConstants.CaveCellsPerBake, CaveConstants.CaveCellM, CaveConstants.CaveLevels, CaveConstants.CaveLevelM,
            0.5f * (CaveConstants.CaveKarstLo + CaveConstants.CaveKarstHi), 100.0 * FMath::Clamp(InLand.Caves.KarstCoverage, 0.0, 1.0),
            CaveConstants.CaveKarstLo > 1.0f ? TEXT(" (no province: no karst)") : TEXT(""), CaveConstants.CaveDepthM, CaveConstants.CaveDeepCellM,
            CaveConstants.CaveMaxRadiusM);
    }
    VoxelShader::FVoxelLandSamplerCPU& Sampler = *LandSampler;
    static_cast<FVoxelCaveConstants&>(Sampler) = CaveConstants;
    Sampler.CaveBake = LandCaves.IsValid() ? LandCaves->Words.GetData() : nullptr;
}

TSharedPtr<FVoxelLandCaveBake, ESPMode::ThreadSafe> FVoxelTerrestrialDensity::MakeCaveBake() const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(FVoxelTerrestrialDensity::MakeCaveBake);
    using namespace VoxelShader;
    using namespace VoxelTerrestrialDensityDetail;
    const double StartSeconds = FPlatformTime::Seconds();
    const FVoxelLandBake& Bake = *LandBake;
    const int32 BakeN = Bake.N;
    const int32 Side = Bake.Side();
    const int32 Pad = FVoxelLandBake::Pad;
    const int32 NumCells = Bake.NumCells();
    const bool bProvince = Bake.Province.Num() == NumCells;
    const TSharedPtr<FVoxelLandCaveBake, ESPMode::ThreadSafe> Made = MakeShared<FVoxelLandCaveBake, ESPMode::ThreadSafe>();
    Made->N = BakeN;
    Made->Words.SetNumZeroed(2 * NumCells);
    uint32* Words = Made->Words.GetData();
    FVoxelLandSamplerCPU& Sampler = *LandSampler;

    // The province bilinear between cell centres at a direction (the bake's own, 0..1).
    auto ProvinceAt = [&Bake, BakeN, Side, Pad](const FVector& Dir) -> float
        {
            int32 Axis = 0;
            for (int32 a = 1; a < 3; a++) if (FMath::Abs(Dir[a]) > FMath::Abs(Dir[Axis])) Axis = a;
            const uint32 Face = (uint32)(Axis * 2 + (Dir[Axis] < 0.0 ? 1 : 0));
            const double Den = FMath::Abs(Dir[Axis]);
            const double S = FMath::Atan(Dir[(int32)VoxelCubeUAxis(Face)] / Den) * 4.0 / UE_DOUBLE_PI;
            const double T = FMath::Atan(Dir[(int32)VoxelCubeVAxis(Face)] / Den) * 4.0 / UE_DOUBLE_PI;
            const double J = (S + 1.0) * 0.5 * BakeN - 0.5;
            const double K = (T + 1.0) * 0.5 * BakeN - 0.5;
            const int32 J0 = FMath::FloorToInt32(J);
            const int32 K0 = FMath::FloorToInt32(K);
            const double FX = J - J0;
            const double FY = K - K0;
            auto At = [&Bake, BakeN, Side, Pad, Face](int32 CJ, int32 CK) -> double
                {
                    CJ = FMath::Clamp(CJ, -Pad, BakeN + Pad - 1);
                    CK = FMath::Clamp(CK, -Pad, BakeN + Pad - 1);
                    return (double)Bake.Province[(int32)Face * Side * Side + (CK + Pad) * Side + (CJ + Pad)] / 63.0;
                };
            return (float)((At(J0, K0) * (1.0 - FX) + At(J0 + 1, K0) * FX) * (1.0 - FY) + (At(J0, K0 + 1) * (1.0 - FX) + At(J0 + 1, K0 + 1) * FX) * FY);
        };

    // Each cell's centre through the warp (as LandSurfaceAt warps): the height, drainage, relief
    // and province there.
    ParallelFor(6 * Side, [&](int32 Row)
        {
            const int32 Face = Row / Side;
            const int32 k = Row % Side - Pad;
            const int32 NormalAxis = (int32)VoxelCubeNormalAxis((uint32)Face);
            const int32 UAxis = (int32)VoxelCubeUAxis((uint32)Face);
            const int32 VAxis = (int32)VoxelCubeVAxis((uint32)Face);
            const double TanV = FMath::Tan(((k + 0.5) * 2.0 / BakeN - 1.0) * UE_DOUBLE_PI * 0.25);
            for (int32 i = 0; i < Side; i++)
            {
                const int32 j = i - Pad;
                FVector Dir;
                Dir[NormalAxis] = (Face & 1) ? -1.0 : 1.0;
                Dir[UAxis] = FMath::Tan(((j + 0.5) * 2.0 / BakeN - 1.0) * UE_DOUBLE_PI * 0.25);
                Dir[VAxis] = TanV;
                Dir.Normalize();
                const FVector SurfacePoint = Dir * LandSampleRadius;
                FVector Q = SurfacePoint;
                if (LandWarpAmplitude > 0.0)
                {
                    for (int32 Axis = 0; Axis < 3; Axis++)
                        Q[Axis] += VoxelDensityLayers::EvaluateLayer(LandWarp[Axis], SurfacePoint, 0.0, LandWarpRidgeGain, LandWarpSlopeDamping) * LandWarpAmplitude;
                }
                const FVoxelLandSamplerCPU::FVoxelLandBakeSample Sample = Sampler.VoxelLandBakeSampleDF(ToDoubleFloat(Q.X), ToDoubleFloat(Q.Y), ToDoubleFloat(Q.Z), true);
                const float Province = bProvince ? ProvinceAt(Q.GetSafeNormal()) : 0.0f;
                const int32 Cell = Face * Side * Side + (k + Pad) * Side + i;
                Words[2 * Cell] = asuint(Sample.Height);
                Words[2 * Cell + 1] = Sampler.VoxelCavePackWord(0.0f, Sample.Flow, Sample.Relief, Province);
            }
        });

    // The local base: the lowest of those heights (sea level for the sea) within VoxelLandRelief's
    // window (about two cells), within the face's padded cells.
    ParallelFor(6 * Side, [&](int32 Row)
        {
            const int32 Face = Row / Side;
            const int32 Row0 = Row % Side;
            for (int32 i = 0; i < Side; i++)
            {
                float Lo = 3.0e38f;
                for (int32 dj = -2; dj <= 2; dj++)
                {
                    for (int32 di = -2; di <= 2; di++)
                    {
                        if (di * di + dj * dj > 5) continue;
                        const int32 r = Row0 + dj;
                        const int32 c = i + di;
                        if (r < 0 || r >= Side || c < 0 || c >= Side) continue;
                        Lo = FMath::Min(Lo, FMath::Max(asfloat(Words[2 * (Face * Side * Side + r * Side + c)]), 0.0f));
                    }
                }
                const int32 Cell = Face * Side * Side + Row0 * Side + i;
                Words[2 * Cell + 1] = (Words[2 * Cell + 1] & ~0x3FFFu) | (Sampler.VoxelCavePackWord(Lo, VOXEL_CAVE_FLOW_FROM, 0.0f, 0.0f) & 0x3FFFu);
            }
        });

    Made->BuildSeconds = FPlatformTime::Seconds() - StartSeconds;
    UE_LOG(LogVoxelPlanets, Log, TEXT("[VoxelLand] Cave bake for bake %08x: %d cells in %.2f s (%.0f MB)."), Bake.Key, NumCells, Made->BuildSeconds,
        (double)Made->Words.Num() * 4.0 / (1024.0 * 1024.0));
    return Made;
}

bool FVoxelTerrestrialDensity::MayHoldSurfaceInSolid(const FBox& Region, double Footprint) const
{
    using namespace VoxelShader;
    using VoxelTerrestrialDensityDetail::ToDoubleFloat;
    FVoxelLandSamplerCPU& Sampler = *LandSampler;
    const float FootprintM = (float)(FMath::Max(Footprint, MinFootprint) * 0.01);
    if (Sampler.CaveOn == 0u || FootprintM >= Sampler.CaveMaxRadiusM || !Region.IsValid) return false;
    return Sampler.VoxelCaveBoxMayHold(ToDoubleFloat(Region.Min.X), ToDoubleFloat(Region.Min.Y), ToDoubleFloat(Region.Min.Z),
        ToDoubleFloat(Region.Max.X), ToDoubleFloat(Region.Max.Y), ToDoubleFloat(Region.Max.Z), ToDoubleFloat(LandSampleRadius), FootprintM);
}

void FVoxelTerrestrialDensity::SampleRange(const FSampleInput& Input, int32 Begin, int32 End, double Footprint, float* DensityOut) const
{
    using namespace VoxelShader;
    using VoxelTerrestrialDensityDetail::ToDoubleFloat;
    FVoxelLandSamplerCPU& Sampler = *LandSampler;
    // Footprints below the finest cell are raised to it, as HeightAt does (and the GPU).
    const double Band = FMath::Max(Footprint, MinFootprint);
    const float FootprintM = (float)(Band * 0.01);
    const bool bCaves = Sampler.CaveOn != 0u && FootprintM < Sampler.CaveMaxRadiusM;
    // The last region gathered (a cave cell and 32 m of altitude) and its list.
    constexpr float BandM = 32.0f;
    FVoxelLandSamplerCPU::FVoxelCaveRegion Region{};
    FVoxelLandSamplerCPU::FVoxelCaveList List{};
    bool bList = false;
    for (int32 i = Begin; i < End; i++)
    {
        const FVector Position = Input.GetPosition(i);
        const double Len = Position.Size();
        if (Len <= 1e-12 || NoiseAmplitude <= 0.0)
        {
            DensityOut[i] = (float)(Len - PlanetRadius);
            continue;
        }
        float BakeM = 0.0f;
        const double Height = LandSurfaceAt(Position * (LandSampleRadius / Len), Band, nullptr, &BakeM);
        float Density = (float)(Len - (PlanetRadius + Height));
        if (bCaves)
        {
            // The caves' cheap gates first (as VoxelLandCaveCarve.ush VoxelCaveDistanceDF).
            const float LandM = (float)((Height - LandHeightOffset) * 0.01);
            const float Z = (float)((Len - LandSampleRadius) * 0.01);
            if (Z - LandM <= 2.0f * FootprintM + Sampler.CaveBlendM + 1.0f && BakeM - Z <= FMath::Max(Sampler.CaveDepthM, Sampler.CaveShallowDepthM) + Sampler.CaveBandSlackM)
            {
                const FVoxelLandSamplerCPU::FVoxelCaveSample S = Sampler.VoxelCaveSampleDF(ToDoubleFloat(Position.X), ToDoubleFloat(Position.Y), ToDoubleFloat(Position.Z),
                    ToDoubleFloat(Len), ToDoubleFloat(LandSampleRadius), LandM, BakeM, FootprintM);
                if (!Sampler.VoxelCaveSkip(S))
                {
                    if (!bList || S.Face != List.Face || S.IU != List.OU || S.IV != List.OV || S.Z < Region.ZMin || S.Z > Region.ZMax)
                    {
                        const float Z0 = FMath::FloorToFloat(S.Z / BandM) * BandM;
                        Region = Sampler.VoxelCaveCellRegion(S, Z0, Z0 + BandM);
                        List = Sampler.VoxelCaveGather(Region);
                        bList = true;
                    }
                    Density = Sampler.VoxelCaveCarve(Density, Sampler.VoxelCaveEvaluate(List, S).Distance, 100.0f);
                }
            }
        }
        DensityOut[i] = Density;
    }
}


void FVoxelTerrestrialDensity::BuildLandSampler()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(FVoxelTerrestrialDensity::BuildLandSampler);
    using namespace VoxelShader;
    using VoxelTerrestrialDensityDetail::ToDoubleFloat;

    // Once (MAT-3): the constants and buffers are final here, and the object never moves (its
    // arrays' data stays where the sampler points), so every sample reads this one.
    LandSampler = MakeUnique<FVoxelLandSamplerCPU>();
    FVoxelLandSamplerCPU& Sampler = *LandSampler;
    Sampler.LandHeight = LandBake->Height.GetData();
    Sampler.LandTanTable = reinterpret_cast<const float2*>(LandBake->TanTable.GetData());
    Sampler.LandFlow = LandFlowPacked.GetData();
    Sampler.LandForm = LandFormPacked.GetData();
    // The landform with the local relief (the GPU's LandHasForm the same).
    Sampler.LandHasForm = LandFormPacked.Num() > 0 && LandReliefPacked.Num() > 0 ? 1u : 0u;
    Sampler.LandHillRelief = LandHillRelief;
    Sampler.LandHillFrequency = ToDoubleFloat(LandHillFrequency);
    Sampler.LandHillOctaves = LandHillOctaves;
    Sampler.LandHillSeed = LandHillSeed;
    Sampler.LandRelief = LandReliefPacked.GetData();
    Sampler.LandProvince = LandProvincePacked.GetData();
    if (LandRivers.IsValid())
    {
        Sampler.LandRiverTiles = LandRivers->Tiles.GetData();
        Sampler.LandRiverData = LandRivers->Data.GetData();
        Sampler.LandRiverTilesPerFace = LandRiverTilesPerFace;
        Sampler.LandRiverRadiusM = LandRiverRadiusM;
        Sampler.LandRiverWidthScale = LandRiverWidthScale;
        Sampler.LandRiverValleyScale = LandRiverValleyScale;
    }
    // R2: the carve (the lakes with the bake's levels).
    Sampler.LandRiverCarve = LandRiverCarve;
    Sampler.LandRiverDepthScale = LandRiverDepthScale;
    Sampler.LandRiverFloodplainScale = LandRiverFloodplainScale;
    Sampler.LandRiverMinLog2Area = LandRiverMinLog2Area;
    Sampler.LandLakeShoreBand = LandLakeShoreBand;
    Sampler.LandRiverMeander = LandRiverMeander;
    Sampler.LandRiverFalls = LandRiverFalls;
    Sampler.LandHasWater = LandHasWater;
    Sampler.LandDetailSeed = LandDetailSeed;
    Sampler.LandWater = LandHasWater != 0u ? LandBake->Water.GetData() : nullptr;
    if (LandNoise.IsValid())
    {
        Sampler.LandNoise = LandNoise->Words.GetData();
        // The detail set's chain and means, whoever reads it.
        Sampler.LandDetailBase = LandNoise->DetailBase;
        Sampler.LandDetailN = LandNoise->DetailN;
        Sampler.LandDetailMips = LandNoise->DetailMips;
        Sampler.LandDetailMeanHard = LandDetailMeanHard;
        Sampler.LandDetailMeanSoft = LandDetailMeanSoft;
        Sampler.LandDetailTexelsPerCell = LandDetailTexelsPerCell;
        Sampler.LandDetailScales = LandDetailScales;
        Sampler.LandDetailMeanA = LandDetailMeanA;
        Sampler.LandDetailScaleA = LandDetailScaleA;
        Sampler.LandDetailScaleGain = LandDetailScaleGain;
        Sampler.LandDetailFade = LandDetailFade;
        Sampler.LandHillTexelsPerCell = LandHillTexelsPerCell;
        Sampler.LandHillBase = LandNoise->HillBase;
        Sampler.LandHillN = LandNoise->HillN;
        Sampler.LandHillMips = LandNoise->HillMips;
        Sampler.LandHillScaleR = LandHillScaleR;
        Sampler.LandHillScaleG = LandHillScaleG;
        Sampler.LandHillScaleB = LandHillScaleB;
        Sampler.LandHillMeanR = LandHillMeanR;
        Sampler.LandHillMeanG = LandHillMeanG;
        Sampler.LandHillSpacingM = LandHillSpacingM;
        Sampler.LandHillSwellTexelsPerCell = LandHillSwellTexelsPerCell;
        Sampler.LandHillSwellM = LandHillSwellM;
        Sampler.LandHillSwellScaleG = LandHillSwellScaleG;
        Sampler.LandHillDetailTexelsPerCell = LandHillDetailTexelsPerCell;
        Sampler.LandHillDetailScaleHard = LandHillDetailScaleHard;
        Sampler.LandHillDetailScaleSoft = LandHillDetailScaleSoft;
        Sampler.LandHillDetailFade = LandHillDetailFade;
        Sampler.LandHillFoothill = LandHillFoothill;
        Sampler.LandStrataTexelsPerCell = LandStrataTexelsPerCell;
        Sampler.LandStrataBase = LandNoise->StrataBase;
        Sampler.LandStrataN = LandNoise->StrataN;
        Sampler.LandStrataMips = LandNoise->StrataMips;
        Sampler.LandStrataScaleR = LandStrataScaleR;
        Sampler.LandStrataScaleG = LandStrataScaleG;
        Sampler.LandStrataScaleB = LandStrataScaleB;
        Sampler.LandStrataMeanR = LandStrataMeanR;
        Sampler.LandStrataMeanG = LandStrataMeanG;
        Sampler.LandStrataSpacingM = LandStrataSpacingM;
        Sampler.LandStrataRelief = LandStrataRelief;
        Sampler.LandStrataFlat = LandStrataFlat;
        Sampler.LandStrataThreshold = LandStrataThreshold;
        Sampler.LandStrataWidth = LandStrataWidth;
        Sampler.LandStrataSeed = LandStrataSeed;
        Sampler.LandStrataDetailTexelsPerCell = LandStrataDetailTexelsPerCell;
        Sampler.LandStrataDetailScaleHard = LandStrataDetailScaleHard;
        Sampler.LandStrataDetailScaleSoft = LandStrataDetailScaleSoft;
        Sampler.LandStrataDetailFade = LandStrataDetailFade;
        if (LandIceSwellTexelsPerCell > 0.0f && LandClimate.IsValid())
        {
            // T5d2: the ice sheets' swells' chain.
            Sampler.LandIceBase = LandNoise->IceBase;
            Sampler.LandIceN = LandNoise->IceN;
            Sampler.LandIceMips = LandNoise->IceMips;
        }
        if (LandClimate.IsValid() && LandNoise->PolarN > 0
            && (LandPolarCrevasseTexelsPerCell > 0.0f || LandPolarSastrugiTexelsPerCell > 0.0f || LandPolarPolygonTexelsPerCell > 0.0f
                || LandPolarDuneTexelsPerCell > 0.0f))
        {
            // T5d3: the polar surfaces.
            Sampler.LandPolarBase = LandNoise->PolarBase;
            Sampler.LandPolarN = LandNoise->PolarN;
            Sampler.LandPolarMips = LandNoise->PolarMips;
            Sampler.LandPolarSeed = LandPolarSeed;
            Sampler.LandPolarCrevasseTexelsPerCell = LandPolarCrevasseTexelsPerCell;
            Sampler.LandPolarCrevasseM = LandPolarCrevasseM;
            Sampler.LandPolarCrevasseMean = LandPolarCrevasseMean;
            Sampler.LandPolarCrevasseFade = LandPolarCrevasseFade;
            Sampler.LandPolarSastrugiTexelsPerCell = LandPolarSastrugiTexelsPerCell;
            Sampler.LandPolarSastrugiM = LandPolarSastrugiM;
            Sampler.LandPolarSastrugiMean = LandPolarSastrugiMean;
            Sampler.LandPolarSastrugiFade = LandPolarSastrugiFade;
            Sampler.LandPolarPolygonTexelsPerCell = LandPolarPolygonTexelsPerCell;
            Sampler.LandPolarPolygonM = LandPolarPolygonM;
            Sampler.LandPolarPolygonMean = LandPolarPolygonMean;
            Sampler.LandPolarPolygonFade = LandPolarPolygonFade;
            Sampler.LandPolarPermafrostT = LandPolarPermafrostT;
            Sampler.LandPolarCrevasseTurn = LandPolarCrevasseTurn;
            Sampler.LandPolarSastrugiTurn = LandPolarSastrugiTurn;
            if (LandPolarDuneTexelsPerCell > 0.0f)
            {
                Sampler.LandPolarDuneBase = LandNoise->DuneBase;
                Sampler.LandPolarDuneN = LandNoise->DuneN;
                Sampler.LandPolarDuneMips = LandNoise->DuneMips;
                Sampler.LandPolarDuneTexelsPerCell = LandPolarDuneTexelsPerCell;
                Sampler.LandPolarDuneM = LandPolarDuneM;
                Sampler.LandPolarDuneMean = LandPolarDuneMean;
                Sampler.LandPolarDuneFade = LandPolarDuneFade;
            }
        }
        if (LandGlacialTexelsPerCell > 0.0f && LandClimate.IsValid())
        {
            Sampler.LandGlacialBase = LandNoise->GlacialBase;
            Sampler.LandGlacialN = LandNoise->GlacialN;
            Sampler.LandGlacialMips = LandNoise->GlacialMips;
            Sampler.LandGlacialTexelsPerCell = LandGlacialTexelsPerCell;
            Sampler.LandGlacialScaleR = LandGlacialScaleR;
            Sampler.LandGlacialScaleG = LandGlacialScaleG;
            Sampler.LandGlacialScaleB = LandGlacialScaleB;
            Sampler.LandGlacialMeanR = LandGlacialMeanR;
            Sampler.LandGlacialMeanG = LandGlacialMeanG;
            Sampler.LandGlacialReliefM = LandGlacialReliefM;
            Sampler.LandGlacialStrength = LandGlacialStrength;
            Sampler.LandGlacialSeed = LandGlacialSeed;
            Sampler.LandGlacialSpacingM = LandGlacialSpacingM;
            Sampler.LandGlacialCooling = LandGlacialCooling;
            Sampler.LandGlacialDetailScaleHard = LandGlacialDetailScaleHard;
            Sampler.LandGlacialDetailScaleSoft = LandGlacialDetailScaleSoft;
        }
        if (LandDuneTexelsPerCell > 0.0f && LandClimate.IsValid())
        {
            // T5c: the dunes.
            Sampler.LandDuneBase = LandNoise->DuneBase;
            Sampler.LandDuneN = LandNoise->DuneN;
            Sampler.LandDuneMips = LandNoise->DuneMips;
            Sampler.LandDuneDetailBase = LandNoise->SandBase;
            Sampler.LandDuneDetailN = LandNoise->SandN;
            Sampler.LandDuneDetailMips = LandNoise->SandMips;
            Sampler.LandDuneTexelsPerCell = LandDuneTexelsPerCell;
            Sampler.LandDuneScaleR = LandDuneScaleR;
            Sampler.LandDuneScaleG = LandDuneScaleG;
            Sampler.LandDuneMeanR = LandDuneMeanR;
            Sampler.LandDuneMeanG = LandDuneMeanG;
            Sampler.LandDuneRelief = LandDuneRelief;
            Sampler.LandDuneSeed = LandDuneSeed;
            Sampler.LandDuneErgFrequency = LandDuneErgFrequency;
            Sampler.LandDuneErgThreshold = LandDuneErgThreshold;
            Sampler.LandDuneErgWidth = LandDuneErgWidth;
            Sampler.LandDuneReliefMax = LandDuneReliefMax;
            Sampler.LandDuneDetailTexelsPerCell = LandDuneDetailTexelsPerCell;
            Sampler.LandDuneDetailScale = LandDuneDetailScale;
            Sampler.LandDuneDetailMean = LandDuneDetailMean;
            Sampler.LandDuneDetailFade = LandDuneDetailFade;
        }
        Sampler.LandNoiseN = LandNoise->N;
        Sampler.LandNoiseMips = LandNoise->Mips;
        Sampler.LandPeakStrength = LandPeakStrength;
        Sampler.LandPeakTexelsPerCell = LandPeakTexelsPerCell;
        Sampler.LandPeakSeed = LandPeakSeed;
        Sampler.LandPeakSpacingM = LandPeakSpacingM;
        Sampler.LandPeakReliefM = LandPeakReliefM;
        Sampler.LandPeakReliefStart = LandPeakReliefStart;
        Sampler.LandPeakReliefEnd = LandPeakReliefEnd;
        Sampler.LandPeakMeanR = LandPeakMeanR;
        Sampler.LandPeakMeanG = LandPeakMeanG;
        Sampler.LandPeakScaleR = LandPeakScaleR;
        Sampler.LandPeakScaleG = LandPeakScaleG;
        Sampler.LandPeakScaleB = LandPeakScaleB;
        if (LandPeakDetailTexelsPerCell > 0.0f)
        {
            Sampler.LandPeakDetailTexelsPerCell = LandPeakDetailTexelsPerCell;
            Sampler.LandPeakDetailScaleHard = LandPeakDetailScaleHard;
            Sampler.LandPeakDetailScaleSoft = LandPeakDetailScaleSoft;
            Sampler.LandPeakDetailFade = LandPeakDetailFade;
        }
    }
    if (LandClimate.IsValid())
    {
        // T5c, T5d, T5d2: the climate the dunes, the glacial layer and the ice sheets are placed by
        // (the ice sheets need no pack: outside its block).
        Sampler.LandClimate = reinterpret_cast<const float4*>(LandClimate->Entries.GetData());
        Sampler.LandHasClimate = 1u;
        Sampler.LandClimateLapse = LandClimateLapse;
        Sampler.LandIceMaxThickness = LandIceMaxThickness;
        Sampler.LandIceBlend = LandIceBlend;
        Sampler.LandIceSwellTexelsPerCell = LandIceSwellTexelsPerCell;
        Sampler.LandIceMeanG = LandIceMeanG;
        Sampler.LandIceSwellM = LandIceSwellM;
        Sampler.LandIceSeed = LandIceSeed;
        Sampler.LandIceSwellSpacingM = LandIceSwellSpacingM;
    }
    Sampler.LandN = (uint32)LandBake->N;
    Sampler.LandPad = (uint32)FVoxelLandBake::Pad;
    Sampler.LandHasFlow = LandFlowPacked.Num() > 0 ? 1u : 0u;
    Sampler.LandCellM = LandCellM;
    Sampler.LandGullyStrength = LandGullyStrength;
    Sampler.LandGullyOctaves = LandGullyOctaves;
    Sampler.LandGullyFlowGain = LandGullyFlowGain;
    Sampler.LandGullyDetail = LandGullyDetail;
    Sampler.LandGullySeed = LandGullySeed;
}

double FVoxelTerrestrialDensity::LandSurfaceAt(const FVector& SurfacePoint, double Footprint, FVoxelWaterSample* OutWater, float* OutBake) const
{
    using namespace VoxelShader;
    using namespace VoxelTerrestrialDensityDetail;

    // The bake through the sampling warp (every octave: the warp doesn't change with the
    // footprint, so the bake looks the same at every level), and the warp's Jacobian for the
    // gullies' steering.
    FVector Q = SurfacePoint;
    FVector DW[3] = { FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector };
    if (LandWarpAmplitude > 0.0)
    {
        FVector W;
        for (int32 Axis = 0; Axis < 3; Axis++)
        {
            W[Axis] = VoxelDensityLayers::EvaluateLayerGradient(LandWarp[Axis], SurfacePoint, 0.0, LandWarpRidgeGain, LandWarpSlopeDamping, &DW[Axis]) * LandWarpAmplitude;
            DW[Axis] *= LandWarpAmplitude;
        }
        Q += W;
    }
    // The sampler the constructor built (BuildLandSampler); its shared land functions are not
    // const-qualified (VOXEL_FUNC), but only read it.
    FVoxelLandSamplerCPU& Sampler = *LandSampler;
    auto ToFloat3 = [](const FVector& V) { return float3((float)V.X, (float)V.Y, (float)V.Z); };
    const FVoxelLandSamplerCPU::FVoxelLandSurface Land = Sampler.VoxelLandSurfaceDF(
        ToDoubleFloat(SurfacePoint.X), ToDoubleFloat(SurfacePoint.Y), ToDoubleFloat(SurfacePoint.Z),
        ToDoubleFloat(Q.X), ToDoubleFloat(Q.Y), ToDoubleFloat(Q.Z), ToFloat3(DW[0]), ToFloat3(DW[1]), ToFloat3(DW[2]), (float)Footprint, false);

    // Detail (VoxelLandDetail.ush), a quarter as strong under the sea and scaled by
    // GullyDetailScale on land while there are gullies (VoxelDensitySample.ush the same way).
    const float DetailWeight = lerp(0.25f, 1.0f, smoothstep(-50.0f, 50.0f, Land.Bake));
    const double D = (double)Sampler.VoxelLandDetailDF(ToDoubleFloat(SurfacePoint.X), ToDoubleFloat(SurfacePoint.Y), ToDoubleFloat(SurfacePoint.Z),
        (float)Footprint, false).Unit;
    const double Height = (double)Land.Bake * 100.0 + LandHeightOffset + D * (double)((float)LandDetailAmplitude * DetailWeight * Land.DetailScale)
        + (double)Land.Hills * 100.0 + (double)Land.Peaks * 100.0 + (double)Land.Gully * 100.0 + (double)Land.Carve * 100.0;
    const double Clamped = FMath::Clamp(Height, 0.0, NoiseAmplitude);
    if (OutBake) *OutBake = Land.Bake;
    if (OutWater)
    {
        // R2 (Design/RiversLakes.md 3.6): a kept lake over the ground, else a river's water line
        // in its channel, else the sea below sea level.
        FVoxelWaterSample& W = *OutWater;
        W = FVoxelWaterSample();
        const float Ground = (float)((Clamped - LandHeightOffset) * 0.01);
        W.GroundHeight = Ground;
        W.SurfaceHeight = Ground;
        if (Land.Lake.Weight >= 0.5f && Land.Lake.Level > Ground)
        {
            W.Kind = EVoxelWaterKind::Lake;
            W.SurfaceHeight = Land.Lake.Level;
        }
        else if (Land.River.WaterKind == VOXEL_LAND_WATER_RIVER && Land.River.WaterLevel > Ground)
        {
            W.Kind = EVoxelWaterKind::River;
            W.SurfaceHeight = Land.River.WaterLevel;
            // Manning's mean speed, R^(2/3) S^(1/2) / n: n 0.035 (a natural channel), more on steep
            // reaches by Jarrett's 0.39 S^0.38 R^-0.16 (boulders and steps); the hydraulic radius
            // about the water's depth in a wide channel. Within 0.05 .. 5 m/s.
            const double Radius = FMath::Max((double)VOXEL_LAND_RIVER_WATER_SHARE * Land.River.ChannelDepth, 0.1);
            const double Slope = FMath::Max((double)Land.River.Grade, 1.0e-4);
            const double Roughness = FMath::Max(0.035, 0.39 * FMath::Pow(Slope, 0.38) * FMath::Pow(Radius, -0.16));
            const double Speed = FMath::Clamp(FMath::Pow(Radius, 2.0 / 3.0) * FMath::Sqrt(Slope) / Roughness, 0.05, 5.0);
            W.FlowVelocity = FVector(Land.River.FlowDirection.x, Land.River.FlowDirection.y, Land.River.FlowDirection.z) * (Speed * 100.0);
        }
        else if (Ground < 0.0f)
        {
            W.Kind = EVoxelWaterKind::Sea;
            W.SurfaceHeight = 0.0f;
        }
        W.Depth = FMath::Max(W.SurfaceHeight - W.GroundHeight, 0.0f);
    }
    return Clamped;
}

FVoxelWaterSample FVoxelTerrestrialDensity::WaterAt(const FVector& Direction) const
{
    FVoxelWaterSample Out;
    const double Len = Direction.Size();
    if (Len < 1.0e-6) return Out;
    LandSurfaceAt(Direction * (LandSampleRadius / Len), MinFootprint, &Out);
    return Out;
}

FName FVoxelTerrestrialDensity::SourceTypeName()
{
    static const FName Name(TEXT("VoxelTerrestrialDensity"));
    return Name;
}

TSharedPtr<const FVoxelTerrestrialDensity> FVoxelTerrestrialDensity::Of(const FDensitySampleCompositor& Compositor)
{
    const TSharedPtr<const IVoxelDensitySource> Base = Compositor.GetBaseDensity();
    return Base.IsValid() && Base->GetSourceType() == SourceTypeName() ? StaticCastSharedPtr<const FVoxelTerrestrialDensity>(Base) : nullptr;
}

void FVoxelTerrestrialDensity::BuildGPUData(FVoxelDensityGPUData& Out) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(FVoxelTerrestrialDensity::BuildGPUData);
    FVoxelPlanetDensity::BuildGPUData(Out);
    // The noise surface's layers are absent: no continent, mountain or detail octaves (their
    // counts and InvNorm* stay 0), and the octave table holds the warp's three layers, where
    // VoxelDensitySample.ush expects them (after the detail octaves). The surface weights are
    // the noise surface's defaults, as the table carried before the generators were split
    // (the land chain reads none of them).
    for (int32 Axis = 0; Axis < 3; Axis++)
        VoxelDensityLayers::AppendGPUOctaves(LandWarp[Axis], Out.Octaves);
    const FVoxelSurfaceSettings NoiseDefaults;
    Out.ContinentWeight = VoxelDoubleFloat(NoiseDefaults.ContinentWeight);
    Out.MountainWeight = VoxelDoubleFloat(NoiseDefaults.MountainWeight);
    Out.MountainStart = VoxelDoubleFloat(NoiseDefaults.MountainStart);
    Out.MountainBlend = VoxelDoubleFloat(FMath::Max(NoiseDefaults.MountainBlend, 1e-6));
    Out.DetailWeight = VoxelDoubleFloat(NoiseDefaults.DetailWeight);
    Out.RidgeGain = (float)NoiseDefaults.RidgeGain;
    Out.SlopeDamping = (float)NoiseDefaults.DetailSlopeDamping;
    // The land chain (LandMode 1).
    Out.LandMode = 1;
    Out.Land = LandBake;
    Out.LandWarpOctaves = (uint32)LandWarp[0].Octaves.Num();
    Out.LandWarpInvNorm = (float)LandWarp[0].InvNorm;
    Out.LandWarpAmplitude = (float)LandWarpAmplitude;
    Out.LandDetailAmplitude = (float)LandDetailAmplitude;
    Out.LandHeightOffset = VoxelDoubleFloat(LandHeightOffset);
    Out.LandCellM = LandCellM;
    Out.LandGullyStrength = LandGullyStrength;
    Out.LandGullyOctaves = LandGullyOctaves;
    Out.LandGullyFlowGain = LandGullyFlowGain;
    Out.LandGullyDetail = LandGullyDetail;
    Out.LandGullySeed = LandGullySeed;
    Out.LandHillRelief = LandHillRelief;
    Out.LandHillFrequency = VoxelDoubleFloat(LandHillFrequency);
    Out.LandHillOctaves = LandHillOctaves;
    Out.LandHillSeed = LandHillSeed;
    Out.LandNoise = LandNoise;
    Out.LandPeakStrength = LandPeakStrength;
    Out.LandPeakTexelsPerCell = LandPeakTexelsPerCell;
    Out.LandPeakSeed = LandPeakSeed;
    Out.LandPeakSpacingM = LandPeakSpacingM;
    Out.LandPeakReliefM = LandPeakReliefM;
    Out.LandPeakReliefStart = LandPeakReliefStart;
    Out.LandPeakReliefEnd = LandPeakReliefEnd;
    Out.LandPeakMeanR = LandPeakMeanR;
    Out.LandPeakMeanG = LandPeakMeanG;
    Out.LandPeakScaleR = LandPeakScaleR;
    Out.LandPeakScaleG = LandPeakScaleG;
    Out.LandPeakScaleB = LandPeakScaleB;
    Out.LandPeakDetailTexelsPerCell = LandPeakDetailTexelsPerCell;
    Out.LandDetailMeanHard = LandDetailMeanHard;
    Out.LandDetailMeanSoft = LandDetailMeanSoft;
    Out.LandPeakDetailScaleHard = LandPeakDetailScaleHard;
    Out.LandPeakDetailScaleSoft = LandPeakDetailScaleSoft;
    Out.LandPeakDetailFade = LandPeakDetailFade;
    Out.LandHillTexelsPerCell = LandHillTexelsPerCell;
    Out.LandHillScaleR = LandHillScaleR;
    Out.LandHillScaleG = LandHillScaleG;
    Out.LandHillScaleB = LandHillScaleB;
    Out.LandHillMeanR = LandHillMeanR;
    Out.LandHillMeanG = LandHillMeanG;
    Out.LandHillSpacingM = LandHillSpacingM;
    Out.LandHillSwellTexelsPerCell = LandHillSwellTexelsPerCell;
    Out.LandHillSwellM = LandHillSwellM;
    Out.LandHillSwellScaleG = LandHillSwellScaleG;
    Out.LandHillDetailTexelsPerCell = LandHillDetailTexelsPerCell;
    Out.LandHillDetailScaleHard = LandHillDetailScaleHard;
    Out.LandHillDetailScaleSoft = LandHillDetailScaleSoft;
    Out.LandHillDetailFade = LandHillDetailFade;
    Out.LandHillFoothill = LandHillFoothill;
    Out.LandStrataTexelsPerCell = LandStrataTexelsPerCell;
    Out.LandStrataScaleR = LandStrataScaleR;
    Out.LandStrataScaleG = LandStrataScaleG;
    Out.LandStrataScaleB = LandStrataScaleB;
    Out.LandStrataMeanR = LandStrataMeanR;
    Out.LandStrataMeanG = LandStrataMeanG;
    Out.LandStrataSpacingM = LandStrataSpacingM;
    Out.LandStrataRelief = LandStrataRelief;
    Out.LandStrataFlat = LandStrataFlat;
    Out.LandStrataThreshold = LandStrataThreshold;
    Out.LandStrataWidth = LandStrataWidth;
    Out.LandStrataSeed = LandStrataSeed;
    Out.LandStrataDetailTexelsPerCell = LandStrataDetailTexelsPerCell;
    Out.LandStrataDetailScaleHard = LandStrataDetailScaleHard;
    Out.LandStrataDetailScaleSoft = LandStrataDetailScaleSoft;
    Out.LandStrataDetailFade = LandStrataDetailFade;
    Out.LandRivers = LandRivers;
    Out.LandRiverTilesPerFace = LandRiverTilesPerFace;
    Out.LandRiverRadiusM = LandRiverRadiusM;
    Out.LandRiverWidthScale = LandRiverWidthScale;
    Out.LandRiverValleyScale = LandRiverValleyScale;
    Out.LandRiverCarve = LandRiverCarve;
    Out.LandRiverDepthScale = LandRiverDepthScale;
    Out.LandRiverFloodplainScale = LandRiverFloodplainScale;
    Out.LandRiverMinLog2Area = LandRiverMinLog2Area;
    Out.LandLakeShoreBand = LandLakeShoreBand;
    Out.LandRiverMeander = LandRiverMeander;
    Out.LandRiverFalls = LandRiverFalls;
    Out.LandHasWater = LandHasWater;
    Out.LandDetailTexelsPerCell = LandDetailTexelsPerCell;
    Out.LandDetailScales = LandDetailScales;
    Out.LandDetailMeanA = LandDetailMeanA;
    Out.LandDetailScaleA = LandDetailScaleA;
    Out.LandDetailScaleGain = LandDetailScaleGain;
    Out.LandDetailFade = LandDetailFade;
    Out.LandDetailSeed = LandDetailSeed;
    // T5c: the dunes.
    Out.LandClimate = LandClimate;
    Out.LandClimateLapse = LandClimateLapse;
    Out.LandDuneTexelsPerCell = LandDuneTexelsPerCell;
    Out.LandDuneScaleR = LandDuneScaleR;
    Out.LandDuneScaleG = LandDuneScaleG;
    Out.LandDuneMeanR = LandDuneMeanR;
    Out.LandDuneMeanG = LandDuneMeanG;
    Out.LandDuneRelief = LandDuneRelief;
    Out.LandDuneSeed = LandDuneSeed;
    Out.LandDuneErgFrequency = LandDuneErgFrequency;
    Out.LandDuneErgThreshold = LandDuneErgThreshold;
    Out.LandDuneErgWidth = LandDuneErgWidth;
    Out.LandDuneReliefMax = LandDuneReliefMax;
    Out.LandDuneDetailTexelsPerCell = LandDuneDetailTexelsPerCell;
    Out.LandDuneDetailScale = LandDuneDetailScale;
    Out.LandDuneDetailMean = LandDuneDetailMean;
    Out.LandDuneDetailFade = LandDuneDetailFade;
    // T5d2: the ice sheets.
    Out.LandIceMaxThickness = LandIceMaxThickness;
    Out.bLandIceShelf = bLandIceShelf;
    Out.LandIceBlend = LandIceBlend;
    Out.LandIceSwellTexelsPerCell = LandIceSwellTexelsPerCell;
    Out.LandIceMeanG = LandIceMeanG;
    Out.LandIceSwellM = LandIceSwellM;
    Out.LandIceSeed = LandIceSeed;
    Out.LandIceSwellSpacingM = LandIceSwellSpacingM;
    // T5d3: the polar surfaces.
    Out.LandPolarSeed = LandPolarSeed;
    Out.LandPolarCrevasseTexelsPerCell = LandPolarCrevasseTexelsPerCell;
    Out.LandPolarCrevasseM = LandPolarCrevasseM;
    Out.LandPolarCrevasseMean = LandPolarCrevasseMean;
    Out.LandPolarCrevasseFade = LandPolarCrevasseFade;
    Out.LandPolarSastrugiTexelsPerCell = LandPolarSastrugiTexelsPerCell;
    Out.LandPolarSastrugiM = LandPolarSastrugiM;
    Out.LandPolarSastrugiMean = LandPolarSastrugiMean;
    Out.LandPolarSastrugiFade = LandPolarSastrugiFade;
    Out.LandPolarPolygonTexelsPerCell = LandPolarPolygonTexelsPerCell;
    Out.LandPolarPolygonM = LandPolarPolygonM;
    Out.LandPolarPolygonMean = LandPolarPolygonMean;
    Out.LandPolarPolygonFade = LandPolarPolygonFade;
    Out.LandPolarPermafrostT = LandPolarPermafrostT;
    Out.LandPolarCrevasseTurn = LandPolarCrevasseTurn;
    Out.LandPolarSastrugiTurn = LandPolarSastrugiTurn;
    Out.LandPolarDuneTexelsPerCell = LandPolarDuneTexelsPerCell;
    Out.LandPolarDuneM = LandPolarDuneM;
    Out.LandPolarDuneMean = LandPolarDuneMean;
    Out.LandPolarDuneFade = LandPolarDuneFade;
    // T5d: the glacial layer.
    Out.LandGlacialTexelsPerCell = LandGlacialTexelsPerCell;
    Out.LandGlacialScaleR = LandGlacialScaleR;
    Out.LandGlacialScaleG = LandGlacialScaleG;
    Out.LandGlacialScaleB = LandGlacialScaleB;
    Out.LandGlacialMeanR = LandGlacialMeanR;
    Out.LandGlacialMeanG = LandGlacialMeanG;
    Out.LandGlacialReliefM = LandGlacialReliefM;
    Out.LandGlacialStrength = LandGlacialStrength;
    Out.LandGlacialSeed = LandGlacialSeed;
    Out.LandGlacialSpacingM = LandGlacialSpacingM;
    Out.LandGlacialCooling = LandGlacialCooling;
    Out.LandGlacialDetailScaleHard = LandGlacialDetailScaleHard;
    Out.LandGlacialDetailScaleSoft = LandGlacialDetailScaleSoft;
    // K1: the caves.
    Out.Caves = CaveConstants;
    Out.LandCaves = LandCaves;

    // The warp is sampled on the sea-level sphere (the finest octaves are its).
    double MaxFrequency = 0.0;
    for (int32 Axis = 0; Axis < 3; Axis++)
        MaxFrequency = FMath::Max(MaxFrequency, VoxelDensityLayers::FinestFrequency(LandWarp[Axis]));
    CheckLatticeRange(FMath::Max(PlanetRadius, LandSampleRadius), MaxFrequency);
}
