// PlanetTerrainActor.h - The planet's terrain: the voxel engine's terrain actor (AAdaptiveVoxelActor)
// with what the planet adds to it (Design/PlanetPluginSplit.md, S1b): the terrain material's land
// node bound to the land textures (VoxelLandMaterial.h), the land surface cache it reads
// (Design/LandSurfaceCache.md), the climate node's map (stage T5a), and the land debug views
// (voxel.Debug.LandView: VoxelLandDebugViews.h). APlanetActor spawns and drives it.

#pragma once

#include "CoreMinimal.h"
#include "AdaptiveVoxelActor.h"
#include "VoxelDensity.h"
#include "VoxelLandCache.h"
#include "PlanetTerrainActor.generated.h"

class UMaterialInstanceDynamic;
class UTexture2DDynamic;
class UTextureRenderTarget2D;

UCLASS(NotPlaceable)
class VOXELPLANETS_API APlanetTerrainActor : public AAdaptiveVoxelActor
{
    GENERATED_BODY()

public:
    /** Terrestrial planets, materials with the land node (VoxelLandMaterial.ush): how far the
     *  rendered surface may lie off the analytic one (m) before the node's normal fades to the
     *  mesh's (edits, overhangs). It also grows by 16 pixel footprints, for the coarse mesh far
     *  off. A mesh surface facing well away from the land's takes the mesh's normal whatever
     *  this is (VoxelLandFacingToMesh). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Materials", meta = (ClampMin = "0.0", UIMax = "50"))
    double LandSurfaceTolerance = 0.33;

    /** Terrestrial planets, the land node: close to the camera, where the finest mesh cells
     *  (ActualPrecision) carry all the land's detail, the node uses the mesh's normal. Below a
     *  pixel footprint of this fraction of the finest cell it is the mesh's, above 2.5 times it
     *  the node's (0 never). At the default 0.05 and 1 m cells, looking down at 1080p and 90
     *  degrees: the mesh's within about 50 m, the node's beyond about 120 m (nearer at grazing
     *  angles, where pixels stretch). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terrain|Materials", meta = (ClampMin = "0.0", UIMax = "0.5"))
    double LandNearMeshNormal = 0.05;

    virtual void BeginDestroy() override;
    virtual void Tick(float DeltaTime) override;

    /** The land surface cache (Design/LandSurfaceCache.md) once it's made (the ocean's depths read
     *  it while it's written for the camera, FVoxelLandCache::IsLive; stage O9m); null otherwise.
     *  Game thread. */
    const FVoxelLandCache* GetLandCache() const { return LandCache.IsValid() && LandCache->IsInitialized() ? LandCache.Get() : nullptr; }

    /** The sea radius the elevation debug view (voxel.Debug.LandView 1) measures heights from
     *  (cm, actor space; 0: the density's own). The planet sets its ocean radius. */
    void SetDebugSeaRadius(double InSeaRadius);

    /** Stage T5a: the land's climate for the terrain material's climate node
     *  (VoxelLandClimateMaterial.ush): the map's texture (null turns the node off) and its
     *  parameters (VoxelLandMaterial::SetClimateParameters: Params y the sea's radius in m, z the
     *  lapse rate, w the season; Shape; Tint). The planet sets them; they stay across rebinds of
     *  the material. */
    void SetLandClimate(UTexture2DDynamic* Texture, const FLinearColor& Params, const FLinearColor& Shape, const FLinearColor& Tint);

protected:
    // --- The engine's hooks (AAdaptiveVoxelActor) ---

    /** Game thread. A dynamic instance of SurfaceMaterial, its land node (VoxelLandMaterial.ush)
     *  bound to the land textures on a terrestrial planet (bLandMaterialBound) and off
     *  otherwise. */
    virtual UMaterialInterface* ResolveBrickMaterial() override;

    /** The land debug view where the vertex colours show it (VoxelLandDebugViews::GetVertexView),
     *  with heights about DebugSeaRadius. */
    virtual FVoxelBrickMesherGPUDebugView GetVertexColourView() const override;

    /** The land surface cache rewrites the columns through the edit's box (its frame is the
     *  actor's). */
    virtual void OnEditNotice(uint32 Sequence, const FBox& Bounds) override;

    // --- The land material ---

    /** SetDebugSeaRadius's value, for the vertex-colour view. */
    double DebugSeaRadius = 0.0;

    /** Drops the land textures (and the instance's references to them). */
    void ReleaseLandTextures();

    /** Game thread, per frame after every actor's tick: the planet's frame for the land node;
     *  bTickCache (from the end of the actors' tick only) moves the land surface cache too. */
    void UpdateLandMaterial(bool bTickCache = false);
    void OnWorldPostActorTick(UWorld* World, ELevelTick TickType, float DeltaSeconds);

    /** The land material (VoxelLandMaterial.h): the instance, the textures, and what they were
     *  made from. */
    UPROPERTY(Transient)
    TObjectPtr<UMaterialInstanceDynamic> LandMaterialInstance;

    UPROPERTY(Transient)
    TObjectPtr<UTexture2DDynamic> LandHeightTexture;

    UPROPERTY(Transient)
    TObjectPtr<UTexture2DDynamic> LandFieldTexture;

    UPROPERTY(Transient)
    TObjectPtr<UTexture2DDynamic> LandConstantTexture;

    UPROPERTY(Transient)
    TObjectPtr<UTexture2DDynamic> LandNoiseTexture;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialInterface> LandMaterialParent;

    TWeakPtr<const FVoxelLandBake, ESPMode::ThreadSafe> LandTexturesBake;   // the height and field textures'
    TWeakPtr<const FVoxelDensityCPU> LandTexturesDensity;                   // the constants'
    TWeakPtr<const FVoxelDensityCPU> LandTexturesFailed;                    // the textures couldn't be made for it
    /** The instance's land node is bound to the textures. */
    bool bLandMaterialBound = false;
    FDelegateHandle LandMaterialTickHandle;

    /** Stage T5a: the climate node's map and parameters (SetLandClimate). */
    UPROPERTY(Transient)
    TObjectPtr<UTexture2DDynamic> LandClimateTexture;

    FLinearColor LandClimateParams = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
    FLinearColor LandClimateShape = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
    FLinearColor LandClimateTint = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);

    /** The land surface cache (Design/LandSurfaceCache.md): its textures and the levels' plan,
     *  made with the land textures, ticked with the frame. */
    UPROPERTY(Transient)
    TObjectPtr<UTextureRenderTarget2D> LandCacheHeight;

    UPROPERTY(Transient)
    TObjectPtr<UTextureRenderTarget2D> LandCacheSlope;

    UPROPERTY(Transient)
    TObjectPtr<UTextureRenderTarget2D> LandCacheFields;

    UPROPERTY(Transient)
    TObjectPtr<UTexture2DDynamic> LandCacheState;

    TUniquePtr<FVoxelLandCache> LandCache;
    TWeakPtr<const FVoxelDensityCPU> LandCacheDensity;   // the density the cache was made for
    TWeakPtr<const FVoxelDensityCPU> LandCacheFailed;    // the cache couldn't be made for it
    double LandCacheUnwritableSince = -1.0;              // when the made cache was first found unwritable (-1: it is writable)
    bool bLandCacheUnwritableLogged = false;             // the warning given for that cache

    /** Makes the cache for Density (or keeps it), and binds it; false when it can't be made. */
    bool ResolveLandCache(const TSharedPtr<const FVoxelDensityCPU>& Density);
    void ReleaseLandCache();
    /** The camera the land surface cache follows, in the planet's frame (cm, no scale). */
    bool GetLandCacheCamera(const FTransform& PlanetFrame, FVector& OutCameraPlanet) const;
};
