#include "VoxelDensityGPU.h"
#include "VoxelGPUCommon.h"
#include "VoxelLandGPU.h"
#include "VoxelMesherShaders.h"
#include "VoxelBrickMesherGPU.h"
#include "VoxelPlanets.h"
#include "FSparseEditStore.h"
#include "VoxelEditCore.h"
#include "GlobalShader.h"
#include "DataDrivenShaderPlatformInfo.h"
#include "ShaderParameterStruct.h"
#include "RHIGPUReadback.h"
#include "Async/Async.h"
#include "Misc/App.h"
#include <atomic>

/** Samples the density on a box of lattice points (VoxelDensityGPU.usf). Global shaders must
 *  register before the engine compiles them, hence the module's PostConfigInit loading phase. */
class FVoxelDensityCS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FVoxelDensityCS);
    SHADER_USE_PARAMETER_STRUCT(FVoxelDensityCS, FGlobalShader);

    static constexpr int32 GroupSize = 4;

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVoxelOctave>, Octaves)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWStructuredBuffer<float>, OutDensity)
        SHADER_PARAMETER(FIntVector, BlockMin)
        SHADER_PARAMETER(FIntVector, BlockDim)
        SHADER_PARAMETER(int32, BlockStep)
        SHADER_PARAMETER(uint32, OutOffset)
        SHADER_PARAMETER(float, SampleFootprint)
        SHADER_PARAMETER(uint32, OutputMode)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, EditCells)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, EditOpList)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<uint>, EditOps)
        SHADER_PARAMETER(uint32, EditTableMask)
        SHADER_PARAMETER(uint32, EditCellShift)
        SHADER_PARAMETER(uint32, EditOpCount)
        SHADER_PARAMETER(FVector2f, HalfFinest)
        SHADER_PARAMETER(uint32, ContinentOctaves)
        SHADER_PARAMETER(uint32, MountainOctaves)
        SHADER_PARAMETER(uint32, DetailOctaves)
        SHADER_PARAMETER(FVector2f, InvNormContinent)
        SHADER_PARAMETER(FVector2f, InvNormMountain)
        SHADER_PARAMETER(FVector2f, InvNormDetail)
        SHADER_PARAMETER(FVector2f, PlanetRadius)
        SHADER_PARAMETER(FVector2f, NoiseAmplitude)
        SHADER_PARAMETER(FVector2f, ContinentWeight)
        SHADER_PARAMETER(FVector2f, MountainWeight)
        SHADER_PARAMETER(FVector2f, MountainStart)
        SHADER_PARAMETER(FVector2f, MountainBlend)
        SHADER_PARAMETER(FVector2f, DetailWeight)
        SHADER_PARAMETER(float, RidgeGain)
        SHADER_PARAMETER(float, SlopeDamping)
        SHADER_PARAMETER_STRUCT_INCLUDE(FVoxelLandShaderParameters, Land)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
    }

    static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
    {
        FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
        OutEnvironment.SetDefine(TEXT("GROUP_SIZE"), GroupSize);
        // The double-float code needs exact float rounding (precise alone isn't honoured
        // everywhere, e.g. Metal's fast math).
        OutEnvironment.CompilerFlags.Add(CFLAG_NoFastMath);
    }
};

IMPLEMENT_GLOBAL_SHADER(FVoxelDensityCS, "/Plugin/VoxelPlanets/Private/VoxelDensityGPU.usf", "MainCS", SF_Compute);

namespace VoxelDensityGPUDetail
{
    struct FReadbackState
    {
        TUniquePtr<FRHIGPUBufferReadback> Buffer;
        int32 Count = 0;
        std::atomic<bool> bDone{ false };
        TFunction<void(TArray<float>&&)> OnDone;
    };
}

void VoxelDensityGPU::SampleBlocksAsync(TSharedRef<const FVoxelDensityGPUData> Data, double HalfFinest,
    TArray<FVoxelDensityGPUBlock> Blocks, TFunction<void(TArray<float>&&)> OnDone,
    EVoxelDensityGPUOutput Output, TSharedPtr<const FVoxelEditGPUData> Edits)
{
    using namespace VoxelDensityGPUDetail;
    check(!IsInRenderingThread());

    const TSharedRef<FReadbackState, ESPMode::ThreadSafe> State = MakeShared<FReadbackState, ESPMode::ThreadSafe>();
    State->OnDone = MoveTemp(OnDone);
    for (const FVoxelDensityGPUBlock& Block : Blocks)
        State->Count += FMath::Max(Block.NumPoints(), 0);
    if (State->Count == 0 || !FApp::CanEverRender())
    {
        AsyncTask(ENamedThreads::GameThread, [State]() { State->OnDone(TArray<float>()); });
        return;
    }

    ENQUEUE_RENDER_COMMAND(VoxelDensityGPUSample)([Data, HalfFinest, Blocks = MoveTemp(Blocks), State, Output, Edits](FRHICommandListImmediate& RHICmdList)
        {
            // GetShader by type returns an empty reference when the shader isn't compiled for
            // this platform (TShaderMapRef would assert).
            if (!GetGlobalShaderMap(GMaxRHIFeatureLevel)->GetShader(&FVoxelDensityCS::GetStaticType(), 0).IsValid())
            {
                State->bDone.store(true, std::memory_order_release);
                AsyncTask(ENamedThreads::GameThread, [State]() { State->OnDone(TArray<float>()); });
                return;
            }
            const uint32 NumBytes = (uint32)State->Count * (uint32)sizeof(float);
            FRDGBuilder GraphBuilder(RHICmdList);

            FRDGBufferRef OutBuffer = GraphBuilder.CreateBuffer(FRDGBufferDesc::CreateStructuredDesc(sizeof(float), State->Count), TEXT("Voxel.DensityOut"));
            FRDGBufferSRVRef OctaveSRV = GraphBuilder.CreateSRV(VoxelGPUCommon::UploadStructuredBuffer(GraphBuilder, TEXT("Voxel.DensityOctaves"), MakeArrayView(Data->Octaves)));
            const FVoxelEditGPUData NoEdits;
            const FVoxelEditGPUData& EditData = Edits.IsValid() ? *Edits : NoEdits;
            FRDGBufferSRVRef EditCellsSRV = GraphBuilder.CreateSRV(VoxelGPUCommon::UploadStructuredBuffer(GraphBuilder, TEXT("Voxel.DensityEditCells"), MakeArrayView(EditData.Cells)));
            FRDGBufferSRVRef EditOpListSRV = GraphBuilder.CreateSRV(VoxelGPUCommon::UploadStructuredBuffer(GraphBuilder, TEXT("Voxel.DensityEditOpList"), MakeArrayView(EditData.OpList)));
            FRDGBufferSRVRef EditOpsSRV = GraphBuilder.CreateSRV(VoxelGPUCommon::UploadStructuredBuffer(GraphBuilder, TEXT("Voxel.DensityEditOps"), MakeArrayView(EditData.Ops)));
            // Blocks write disjoint ranges, so their passes need no barrier between them.
            FRDGBufferUAVRef OutUAV = GraphBuilder.CreateUAV(OutBuffer, ERDGUnorderedAccessViewFlags::SkipBarrier);
            TShaderMapRef<FVoxelDensityCS> Shader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
            const FVoxelLandShaderParameters Land = VoxelLandGPU::GetParameters(GraphBuilder, *Data);

            uint32 Offset = 0;
            for (const FVoxelDensityGPUBlock& Block : Blocks)
            {
                if (Block.NumPoints() <= 0) continue;
                FVoxelDensityCS::FParameters* Parameters = GraphBuilder.AllocParameters<FVoxelDensityCS::FParameters>();
                Parameters->Octaves = OctaveSRV;
                Parameters->OutDensity = OutUAV;
                Parameters->BlockMin = Block.Min;
                Parameters->BlockDim = Block.Dim;
                Parameters->BlockStep = Block.Step;
                Parameters->OutOffset = Offset;
                Parameters->SampleFootprint = (float)FMath::Max(Block.Footprint, Data->MinFootprint);
                Parameters->OutputMode = (uint32)Output;
                Parameters->EditCells = EditCellsSRV;
                Parameters->EditOpList = EditOpListSRV;
                Parameters->EditOps = EditOpsSRV;
                Parameters->EditTableMask = EditData.TableMask;
                Parameters->EditCellShift = EditData.CellShift;
                Parameters->EditOpCount = EditData.OpCount;
                Parameters->HalfFinest = VoxelDoubleFloat(HalfFinest);
                Parameters->ContinentOctaves = Data->ContinentOctaves;
                Parameters->MountainOctaves = Data->MountainOctaves;
                Parameters->DetailOctaves = Data->DetailOctaves;
                Parameters->InvNormContinent = Data->InvNormContinent;
                Parameters->InvNormMountain = Data->InvNormMountain;
                Parameters->InvNormDetail = Data->InvNormDetail;
                Parameters->PlanetRadius = Data->PlanetRadius;
                Parameters->NoiseAmplitude = Data->NoiseAmplitude;
                Parameters->ContinentWeight = Data->ContinentWeight;
                Parameters->MountainWeight = Data->MountainWeight;
                Parameters->MountainStart = Data->MountainStart;
                Parameters->MountainBlend = Data->MountainBlend;
                Parameters->DetailWeight = Data->DetailWeight;
                Parameters->RidgeGain = Data->RidgeGain;
                Parameters->SlopeDamping = Data->SlopeDamping;
                Parameters->Land = Land;
                FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("VoxelDensity %dx%dx%d", Block.Dim.X, Block.Dim.Y, Block.Dim.Z),
                    Shader, Parameters, FComputeShaderUtils::GetGroupCount(Block.Dim, FVoxelDensityCS::GroupSize));
                Offset += (uint32)Block.NumPoints();
            }

            State->Buffer = MakeUnique<FRHIGPUBufferReadback>(TEXT("Voxel.DensityReadback"));
            AddEnqueueCopyPass(GraphBuilder, State->Buffer.Get(), OutBuffer, NumBytes);
            GraphBuilder.Execute();
        });

    // Poll the readback once per frame on the render thread; hand the result to the game thread.
    VoxelGPUCommon::PollRenderThread([State]() -> bool
        {
            if (State->bDone.load(std::memory_order_acquire)) return true;
            if (!State->Buffer.IsValid() || !State->Buffer->IsReady()) return false;
            const uint32 NumBytes = (uint32)State->Count * (uint32)sizeof(float);
            TArray<float> Result;
            if (const void* Source = State->Buffer->Lock(NumBytes))
            {
                Result.SetNumUninitialized(State->Count);
                FMemory::Memcpy(Result.GetData(), Source, NumBytes);
                State->Buffer->Unlock();
            }
            State->Buffer.Reset();
            State->bDone.store(true, std::memory_order_release);
            AsyncTask(ENamedThreads::GameThread, [State, Result = MoveTemp(Result)]() mutable { State->OnDone(MoveTemp(Result)); });
            return true;
        });
}

// ---------------------------------------------------------------------------
// The planet's mesher passes (S1a, Design/PlanetPluginSplit.md): the voxel engine's mesher
// records them through FVoxelDensityGPUSource.
// ---------------------------------------------------------------------------

/** The planet density's parameters (VoxelTerrestrialMesher.usf): the noise surface's octaves and
 *  constants, the land chain's, and the land debug views'. */
BEGIN_SHADER_PARAMETER_STRUCT(FVoxelTerrestrialMesherParameters, )
    SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FVoxelOctave>, Octaves)
    SHADER_PARAMETER(uint32, ContinentOctaves)
    SHADER_PARAMETER(uint32, MountainOctaves)
    SHADER_PARAMETER(uint32, DetailOctaves)
    SHADER_PARAMETER(FVector2f, InvNormContinent)
    SHADER_PARAMETER(FVector2f, InvNormMountain)
    SHADER_PARAMETER(FVector2f, InvNormDetail)
    SHADER_PARAMETER(FVector2f, PlanetRadius)
    SHADER_PARAMETER(FVector2f, NoiseAmplitude)
    SHADER_PARAMETER(FVector2f, ContinentWeight)
    SHADER_PARAMETER(FVector2f, MountainWeight)
    SHADER_PARAMETER(FVector2f, MountainStart)
    SHADER_PARAMETER(FVector2f, MountainBlend)
    SHADER_PARAMETER(FVector2f, DetailWeight)
    SHADER_PARAMETER(float, RidgeGain)
    SHADER_PARAMETER(float, SlopeDamping)
    SHADER_PARAMETER_STRUCT_INCLUDE(FVoxelLandShaderParameters, Land)
    SHADER_PARAMETER_STRUCT_INCLUDE(FVoxelLandDebugParameters, LandDebug)
END_SHADER_PARAMETER_STRUCT()

VOXEL_MESHER_SOURCE_SHADER(FVoxelTerrestrialSampleCS, FVoxelTerrestrialMesherParameters)
VOXEL_MESHER_SOURCE_SHADER(FVoxelTerrestrialColourCS, FVoxelTerrestrialMesherParameters)
IMPLEMENT_GLOBAL_SHADER(FVoxelTerrestrialSampleCS, "/Plugin/VoxelPlanets/Private/VoxelTerrestrialMesher.usf", "SampleCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FVoxelTerrestrialColourCS, "/Plugin/VoxelPlanets/Private/VoxelTerrestrialMesher.usf", "LandDebugCS", SF_Compute);

bool FVoxelDensityGPUSource::AreShadersReady() const
{
    // GetShader by type returns an empty reference when the shader isn't compiled for this
    // platform (TShaderMapRef would assert).
    FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
    return ShaderMap && ShaderMap->GetShader(&FVoxelTerrestrialSampleCS::GetStaticType(), 0).IsValid();
}

FVoxelMesherSampler FVoxelDensityGPUSource::MakeSampler(FRDGBuilder& GraphBuilder, const FVoxelBrickMesherGPUDebugView& DebugView) const
{
    const FVoxelDensityGPUData& D = *Data;
    // The octaves, uploaded with the first batch and kept.
    FRDGBufferRef OctaveBuffer = nullptr;
    if (!Octaves.IsValid())
    {
        OctaveBuffer = VoxelGPUCommon::UploadStructuredBuffer(GraphBuilder, TEXT("Voxel.MesherOctaves"), MakeArrayView(D.Octaves));
        Octaves = GraphBuilder.ConvertToExternalBuffer(OctaveBuffer);
    }
    else
    {
        OctaveBuffer = GraphBuilder.RegisterExternalBuffer(Octaves);
    }
    FVoxelTerrestrialMesherParameters Source;
    Source.Octaves = GraphBuilder.CreateSRV(OctaveBuffer);
    Source.ContinentOctaves = D.ContinentOctaves;
    Source.MountainOctaves = D.MountainOctaves;
    Source.DetailOctaves = D.DetailOctaves;
    Source.InvNormContinent = D.InvNormContinent;
    Source.InvNormMountain = D.InvNormMountain;
    Source.InvNormDetail = D.InvNormDetail;
    Source.PlanetRadius = D.PlanetRadius;
    Source.NoiseAmplitude = D.NoiseAmplitude;
    Source.ContinentWeight = D.ContinentWeight;
    Source.MountainWeight = D.MountainWeight;
    Source.MountainStart = D.MountainStart;
    Source.MountainBlend = D.MountainBlend;
    Source.DetailWeight = D.DetailWeight;
    Source.RidgeGain = D.RidgeGain;
    Source.SlopeDamping = D.SlopeDamping;
    Source.Land = VoxelLandGPU::GetParameters(GraphBuilder, D);
    // The land debug view's colour pass, when its shader is ready (it isn't part of
    // AreShadersReady, so the terrain never waits for it).
    FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
    const bool bColour = DebugView.Mode > 0 && ShaderMap && ShaderMap->GetShader(&FVoxelTerrestrialColourCS::GetStaticType(), 0).IsValid();
    Source.LandDebug = VoxelLandGPU::GetDebugParameters(GraphBuilder, D, bColour ? DebugView.Mode : 0, DebugView.SeaRadius);

    FVoxelMesherSampler Sampler;
    Sampler.AddSamplePass = [Source](FRDGBuilder& InGraphBuilder, ERDGPassFlags PassFlags, const FVoxelMesherCommonParameters& Common, uint32 Mode,
        uint32 NumThreads, const TCHAR* Name)
        {
            VoxelBrickMesherGPU::AddSourcePass<FVoxelTerrestrialSampleCS>(InGraphBuilder, PassFlags, Common, Source, Mode, NumThreads, Name);
        };
    if (bColour)
    {
        Sampler.AddColourPass = [Source](FRDGBuilder& InGraphBuilder, ERDGPassFlags PassFlags, const FVoxelMesherCommonParameters& Common, uint32 NumThreads)
            {
                VoxelBrickMesherGPU::AddSourcePass<FVoxelTerrestrialColourCS>(InGraphBuilder, PassFlags, Common, Source, 0u, NumThreads, TEXT("Land debug colours"));
            };
    }
    return Sampler;
}
