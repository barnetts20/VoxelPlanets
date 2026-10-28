#include "PlanetTerrainActor.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "VoxelPlanets.h"
#include "VoxelTerrestrialDensity.h"
#include "VoxelLandMaterial.h"
#include "VoxelLandBake.h"
#include "VoxelLandCacheLayout.ush"
#include "VoxelLandDebugViews.h"
#include "FDensitySampleCompositor.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Texture2DDynamic.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/App.h"

void APlanetTerrainActor::BeginDestroy()
{
    if (LandMaterialTickHandle.IsValid())
    {
        FWorldDelegates::OnWorldPostActorTick.Remove(LandMaterialTickHandle);
        LandMaterialTickHandle.Reset();
    }
    Super::BeginDestroy();
}

void APlanetTerrainActor::Tick(float DeltaTime)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(APlanetTerrainActor::Tick);
    Super::Tick(DeltaTime);
    // The land view's legend while one is on (VoxelLandDebugViews.h), once the terrain runs.
    if (!BrickTerrain.IsValid() || !BrickTerrain->IsRunning() || !DensityCompositor.IsValid()) return;
    const TSharedPtr<const FVoxelTerrestrialDensity> Density = FVoxelTerrestrialDensity::Of(*DensityCompositor);
    VoxelLandDebugViews::ShowLegend(Density.IsValid() && Density->GetLandBake().IsValid(), bLandMaterialBound);
}

bool APlanetTerrainActor::ResolveLandCache(const TSharedPtr<const FVoxelTerrestrialDensity>& Density)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(APlanetTerrainActor::ResolveLandCache);
    if (!Density.IsValid()) return false;
    if (LandCache.IsValid() && LandCache->IsInitialized() && LandCacheDensity.Pin() == Density)
        return true;
    // A failure is remembered (as the land textures'), so a later call doesn't try again.
    if (LandCacheFailed.Pin() == Density) return false;
    // The textures fit the levels of the bake's radius (kept across densities with the same).
    const TSharedPtr<const FVoxelLandBake, ESPMode::ThreadSafe> Bake = Density->GetLandBake();
    if (!Bake.IsValid()) return false;
    const int32 Levels = FVoxelLandCache::LevelsFor(Bake->SeaRadius);
    const int32 Capacity = LandCacheHeight ? (LandCacheHeight->SizeY / VOXEL_LAND_CACHE_N) * VOXEL_LAND_CACHE_COLUMNS : 0;
    if (!LandCacheHeight || !LandCacheSlope || !LandCacheFields || !LandCacheState || Capacity < FVoxelLandCache::TilesFor(Levels))
    {
        FVoxelLandCacheTextures Made;
        if (!FVoxelLandCache::MakeTextures(Levels, this, Made))
        {
            ReleaseLandCache();
            LandCacheFailed = Density;
            return false;
        }
        LandCacheHeight = Made.Height;
        LandCacheSlope = Made.Slope;
        LandCacheFields = Made.Fields;
        LandCacheState = Made.State;
    }
    FVoxelLandCacheTextures Textures;
    Textures.Height = LandCacheHeight;
    Textures.Slope = LandCacheSlope;
    Textures.Fields = LandCacheFields;
    Textures.State = LandCacheState;
    if (!LandCache.IsValid())
        LandCache = MakeUnique<FVoxelLandCache>();
    bLandCacheUnwritableLogged = false;
    LandCacheUnwritableSince = -1.0;
    if (!LandCache->Initialize(*Density, Textures))
    {
        UE_LOG(LogVoxelPlanets, Warning, TEXT("[VoxelLand] %s: the land surface cache couldn't be initialized; the terrain material evaluates the land per pixel instead, without the climate layers."),
            *GetName());
        ReleaseLandCache();
        LandCacheFailed = Density;
        return false;
    }
    LandCacheDensity = Density;
    return true;
}

void APlanetTerrainActor::ReleaseLandCache()
{
    if (LandCache.IsValid())
        LandCache->Reset();
    // The instance lets go of the atlases too (so they can be collected).
    if (LandMaterialInstance)
    {
        LandMaterialInstance->SetTextureParameterValue(FVoxelLandCache::HeightTextureName, nullptr);
        LandMaterialInstance->SetTextureParameterValue(FVoxelLandCache::SlopeTextureName, nullptr);
        LandMaterialInstance->SetTextureParameterValue(FVoxelLandCache::FieldsTextureName, nullptr);
        LandMaterialInstance->SetTextureParameterValue(FVoxelLandCache::StateTextureName, nullptr);
    }
    LandCacheDensity.Reset();
    LandCacheHeight = nullptr;
    LandCacheSlope = nullptr;
    LandCacheFields = nullptr;
    LandCacheState = nullptr;
}

void APlanetTerrainActor::ReleaseLandTextures()
{
    ReleaseLandCache();
    LandHeightTexture = nullptr;
    LandFieldTexture = nullptr;
    LandConstantTexture = nullptr;
    LandNoiseTexture = nullptr;
    LandTexturesBake.Reset();
    LandTexturesDensity.Reset();
    // The instance lets go of them too (the frame parameters are set again every frame).
    if (LandMaterialInstance)
        LandMaterialInstance->ClearParameterValues();
}

UMaterialInterface* APlanetTerrainActor::ResolveBrickMaterial()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(APlanetTerrainActor::ResolveBrickMaterial);
    bLandMaterialBound = false;
    if (!SurfaceMaterial)
    {
        ReleaseLandTextures();
        LandMaterialInstance = nullptr;
        LandMaterialParent = nullptr;
        return SurfaceMaterial;
    }
    // The bricks always draw an instance: with the land node off (VoxelLandParams.z 0) unless the
    // textures are bound, whatever the material's own default.
    if (!LandMaterialInstance || LandMaterialParent != SurfaceMaterial)
    {
        LandMaterialInstance = UMaterialInstanceDynamic::Create(SurfaceMaterial, this);
        LandMaterialInstance->SetFlags(RF_Transient | RF_DuplicateTransient);
        LandMaterialParent = SurfaceMaterial;
    }

    // The textures, on a terrestrial planet whose material has the land node: the bake's (kept
    // while the bake is, across densities rebuilt with other sample settings) and the density's
    // constants. A failure is remembered, so a later call doesn't build them again.
    const TSharedPtr<const FVoxelTerrestrialDensity> Density = DensityCompositor.IsValid() ? FVoxelTerrestrialDensity::Of(*DensityCompositor) : nullptr;
    const TSharedPtr<const FVoxelLandBake, ESPMode::ThreadSafe> Bake = Density.IsValid() ? Density->GetLandBake() : nullptr;
    if (!FApp::CanEverRender() || !Bake.IsValid() || !VoxelLandMaterial::HasLandNode(SurfaceMaterial) || LandTexturesFailed.Pin() == Density)
    {
        ReleaseLandTextures();
    }
    else
    {
        bool bFailed = false;
        if (LandTexturesBake.Pin() != Bake || !LandHeightTexture || !LandFieldTexture)
        {
            UTexture2DDynamic* Height = nullptr;
            UTexture2DDynamic* Fields = nullptr;
            bFailed = !VoxelLandMaterial::MakeBakeTextures(Bake, Height, Fields);
            LandHeightTexture = Height;
            LandFieldTexture = Fields;
            LandTexturesBake = Bake;
            LandTexturesDensity.Reset();
        }
        if (!bFailed && (LandTexturesDensity.Pin() != Density || !LandConstantTexture || !LandNoiseTexture))
        {
            LandConstantTexture = VoxelLandMaterial::MakeConstantTexture(*Density);
            LandNoiseTexture = VoxelLandMaterial::MakeNoiseTexture(*Density);
            LandTexturesDensity = Density;
            bFailed = !LandConstantTexture || !LandNoiseTexture;
        }
        if (bFailed)
        {
            ReleaseLandTextures();
            LandTexturesFailed = Density;
        }
        else
        {
            VoxelLandMaterial::SetTextureParameters(LandMaterialInstance, LandHeightTexture, LandFieldTexture, LandConstantTexture, LandNoiseTexture);
            bLandMaterialBound = true;
            // The land surface cache the node reads by default (without it, the analytic node).
            if (ResolveLandCache(Density))
                LandCache->SetTextureParameters(LandMaterialInstance);
        }
    }

    // Stage T5a: the climate node's map (the textures' release above clears the instance's
    // parameters).
    VoxelLandMaterial::SetClimateParameters(LandMaterialInstance, LandClimateTexture, LandClimateParams, LandClimateShape, LandClimateTint);

    // The frame every frame, once all actors (the planet among them) have moved.
    if (!LandMaterialTickHandle.IsValid())
        LandMaterialTickHandle = FWorldDelegates::OnWorldPostActorTick.AddUObject(this, &APlanetTerrainActor::OnWorldPostActorTick);
    UpdateLandMaterial();
    return LandMaterialInstance;
}

void APlanetTerrainActor::OnWorldPostActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
    // A world delegate, not the actor's tick: a parked planet (SetDormant: hidden, tick off) must
    // not keep writing its land cache and pushing material parameters every frame (R1, PLN-1).
    if (World == GetWorld() && !IsTerrainDestroyed() && IsActorTickEnabled() && !IsHidden())
        UpdateLandMaterial(true);
}

void APlanetTerrainActor::UpdateLandMaterial(bool bTickCache)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(APlanetTerrainActor::UpdateLandMaterial);
    if (!LandMaterialInstance) return;
    const FTransform PlanetFrame(GetActorRotation(), GetActorLocation());
    // The land surface cache follows the primary camera, kept warm whenever it's bound (so
    // voxel.Land.MaterialSource switches back to it at once): from the end of the actors' tick
    // only, when the cameras have moved. Without the write shader or the textures' resources,
    // the material evaluates the land itself.
    const bool bCache = bLandMaterialBound && LandCache.IsValid() && LandCache->IsWritable();
    // A made cache that stays unwritable falls the material back to the analytic node (which has
    // no climate layers: dunes, glacial forms, ice sheets, polar surfaces): say so, once a cache,
    // after a few seconds (the write shader may still be compiling).
    if (bLandMaterialBound && LandCache.IsValid() && LandCache->IsInitialized() && !bCache)
    {
        const double Now = FPlatformTime::Seconds();
        if (LandCacheUnwritableSince < 0.0) LandCacheUnwritableSince = Now;
        if (!bLandCacheUnwritableLogged && Now - LandCacheUnwritableSince > 5.0)
        {
            UE_LOG(LogVoxelPlanets, Warning, TEXT("[VoxelLand] %s: the land surface cache can't be written (%s); the terrain material evaluates the land per pixel instead, without the climate layers (dunes, glacial forms, ice sheets, polar surfaces)."),
                *GetName(), *LandCache->WhyNotWritable());
            bLandCacheUnwritableLogged = true;
        }
    }
    else
    {
        LandCacheUnwritableSince = -1.0;
    }
    if (bCache && bTickCache)
    {
        // The edits the cache's writes apply: the mesh's (the cache rewrites an edit's texels
        // once these carry it, so both change together).
        if (BrickTerrain.IsValid())
            LandCache->SetEdits(BrickTerrain->GetGPUEdits(), BrickTerrain->GetGPUEditsSequence());
        FVector CameraPlanet;
        if (GetLandCacheCamera(PlanetFrame, CameraPlanet))
            LandCache->Tick(CameraPlanet);
        if (VoxelLandMaterial::ShowsCacheLevels() && GEngine)
            GEngine->AddOnScreenDebugMessage((uint64)(UPTRINT)this + 0x1a7dcac4eull, 0.0f, FColor::Cyan, LandCache->Describe());
    }
    // The planet's frame as it renders this frame (every actor has moved); the material places
    // each pixel from its own view.
    VoxelLandMaterial::SetFrameParameters(LandMaterialInstance, PlanetFrame, VoxelLandDebugViews::GetMaterialView(), LandSurfaceTolerance,
        LandNearMeshNormal * ActualPrecision, bLandMaterialBound, bCache);
    // Stage T5a: the climate node (its debug view follows voxel.Debug.LandClimate).
    VoxelLandMaterial::SetClimateParameters(LandMaterialInstance, LandClimateTexture, LandClimateParams, LandClimateShape, LandClimateTint, false);
}

void APlanetTerrainActor::SetLandClimate(UTexture2DDynamic* Texture, const FLinearColor& Params, const FLinearColor& Shape, const FLinearColor& Tint)
{
    const bool bNewTexture = Texture != LandClimateTexture;
    LandClimateTexture = Texture;
    LandClimateParams = Params;
    LandClimateShape = Shape;
    LandClimateTint = Tint;
    if (LandMaterialInstance)
        VoxelLandMaterial::SetClimateParameters(LandMaterialInstance, LandClimateTexture, LandClimateParams, LandClimateShape, LandClimateTint, bNewTexture);
}

bool APlanetTerrainActor::GetLandCacheCamera(const FTransform& PlanetFrame, FVector& OutCameraPlanet) const
{
    const UWorld* World = GetWorld();
    if (!World) return false;
    // A game (and play in editor): the first local player's camera, this frame's (the cameras
    // have updated by the end of the actors' tick).
    if (World->IsGameWorld())
    {
        if (const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(World, 0))
        {
            OutCameraPlanet = PlanetFrame.InverseTransformPositionNoScale(Camera->GetCameraLocation());
            return true;
        }
    }
    // The editor (or no player): of the views the last frame rendered, the one nearest the
    // planet's surface (the viewport looking at it, not a far one framing the planet).
    const double Sea = LandCache.IsValid() ? LandCache->GetSeaRadius() : 0.0;
    double Best = TNumericLimits<double>::Max();
    bool bFound = false;
    for (const FVector& View : World->ViewLocationsRenderedLastFrame)
    {
        const FVector Local = PlanetFrame.InverseTransformPositionNoScale(View);
        const double Distance = FMath::Abs(Local.Size() - Sea);
        if (Distance < Best)
        {
            Best = Distance;
            OutCameraPlanet = Local;
            bFound = true;
        }
    }
    return bFound;
}

void APlanetTerrainActor::SetDebugSeaRadius(double InSeaRadius)
{
    DebugSeaRadius = FMath::Max(InSeaRadius, 0.0);
    // The vertex colours of the elevation view are meshed with it (at once, not at the next tick).
    if (BrickTerrain.IsValid())
    {
        const FVoxelBrickMesherGPUDebugView View = GetVertexColourView();
        BrickTerrain->SetVertexColourView(View.Mode, View.SeaRadius);
    }
}

FVoxelBrickMesherGPUDebugView APlanetTerrainActor::GetVertexColourView() const
{
    // Where the material's land node is bound it shows the views itself, unless the vertex
    // colours are asked for (voxel.Debug.LandViewSource 1).
    FVoxelBrickMesherGPUDebugView View;
    View.Mode = VoxelLandDebugViews::GetVertexView(bLandMaterialBound);
    View.SeaRadius = DebugSeaRadius;
    return View;
}

void APlanetTerrainActor::OnEditNotice(uint32 Sequence, const FBox& Bounds)
{
    // The land surface cache's columns through the box (its frame is the actor's).
    if (LandCache.IsValid())
        LandCache->NotifyEdit(Sequence, Bounds);
}
