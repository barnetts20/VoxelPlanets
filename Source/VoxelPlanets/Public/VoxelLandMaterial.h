// VoxelLandMaterial.h - The terrestrial generator's data in the terrain material
// (Design/TerrainMaterial.md): the bake and the density's land constants as three textures the
// material's land node reads (Shaders/Private/VoxelLandMaterial.ush, layout
// VoxelLandMaterialLayout.ush), and the per-frame parameters that place the pixel in the planet's
// frame. The terrain actor owns the textures and the dynamic material instance.

#pragma once

#include "CoreMinimal.h"
#include "Math/Float16Color.h"

class FVoxelTerrestrialDensity;
struct FVoxelDensityGPUData;
struct FVoxelLandBake;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTexture2DDynamic;

namespace VoxelLandMaterial
{
    /** The material's parameter names (the land node's inputs; STAGE_M1_NOTES.md). */
    VOXELPLANETS_API extern const FName HeightTextureName;      // VoxelLandHeight (texture)
    VOXELPLANETS_API extern const FName FieldTextureName;       // VoxelLandFields (texture)
    VOXELPLANETS_API extern const FName ConstantTextureName;    // VoxelLandConstants (texture)
    VOXELPLANETS_API extern const FName NoiseTextureName;       // VoxelLandNoise (texture: the peaks' noise set, T3)
    VOXELPLANETS_API extern const FName OriginHighName;         // VoxelLandOriginHigh (vector: the planet's origin in world, cm)
    VOXELPLANETS_API extern const FName OriginLowName;          // VoxelLandOriginLow (its double-float remainder)
    VOXELPLANETS_API extern const FName AxisXName;              // VoxelLandAxisX (vector: the planet frame's X in world)
    VOXELPLANETS_API extern const FName AxisYName;              // VoxelLandAxisY
    VOXELPLANETS_API extern const FName AxisZName;              // VoxelLandAxisZ
    VOXELPLANETS_API extern const FName AxisXLowName;           // VoxelLandAxisXLow (the axes' double-float remainders)
    VOXELPLANETS_API extern const FName AxisYLowName;           // VoxelLandAxisYLow
    VOXELPLANETS_API extern const FName AxisZLowName;           // VoxelLandAxisZLow
    VOXELPLANETS_API extern const FName ParamsName;             // VoxelLandParams (x view, y tolerance m, z enabled, w near footprint cm)

    /** Stage T5a: the climate node's parameters (VoxelLandClimateMaterial.ush). */
    VOXELPLANETS_API extern const FName ClimateTextureName;     // VoxelLandClimate (texture: the climate map)
    VOXELPLANETS_API extern const FName ClimateParamsName;      // VoxelLandClimateParams (x on + 2 view, y sea radius m, z lapse C/km, w season)
    VOXELPLANETS_API extern const FName ClimateShapeName;       // VoxelLandClimateShape (x aspect C, y breakup C, z breakup m, w snow width C)
    VOXELPLANETS_API extern const FName ClimateTintName;        // VoxelLandClimateTint (x snow, y glacier, z desert, w plants)

    /** The field texels of a bake (Side x 6 Side, the bake's cell order). Any thread. */
    VOXELPLANETS_API void BuildFieldTexels(const FVoxelLandBake& Bake, TArray<FFloat16Color>& Out);

    /** The height texture's texels (RG32F, VoxelLandMaterialLayout.ush): the height, and the
     *  landform's 16 bits plus the local relief's 8 above them, as a float (exact: 24 bits;
     *  0 without them). */
    VOXELPLANETS_API void BuildHeightTexels(const FVoxelLandBake& Bake, TArray<FVector2f>& Out);

    /** The constant texels (VOXEL_LAND_MATERIAL_CONST_WIDTH x OutRows) for a density's land
     *  bake (Density.LandMode must be 1). Any thread. */
    VOXELPLANETS_API void BuildConstantTexels(const FVoxelDensityGPUData& Density, const FVoxelLandBake& Bake, TArray<FVector4f>& Out, int32& OutRows);

    /** voxel.Land.MaterialSource 0: the node reads the land surface cache (VoxelLandCache.h). */
    VOXELPLANETS_API bool UsesCache();

    /** voxel.Debug.LandCache: the node shows the cache's levels. */
    VOXELPLANETS_API bool ShowsCacheLevels();

    /** Stage T5a: voxel.Debug.LandClimate, the climate node's debug view (0 none). */
    VOXELPLANETS_API int32 ClimateView();

    /** Stage T5a, game thread: the climate map's texture (RGBA16F, VoxelLandClimate.ush's layout),
     *  empty until UploadClimateTexture fills it. Null when it can't be made. */
    VOXELPLANETS_API UTexture2DDynamic* MakeClimateTexture();

    /** Stage T5a, game thread: the cells (x the year's mean at sea level, y the precipitation, z the
     *  swing, w the continentality; VOXEL_LAND_CLIMATE_WIDTH x HEIGHT) into the texture. */
    VOXELPLANETS_API void UploadClimateTexture(UTexture2DDynamic* Texture, const TArray<FVector4f>& Cells);

    /** Stage T5a, game thread: the climate node's parameters on a material instance (Params.x is
     *  set here: 1 when Texture is given, plus 2 x the debug view; 0 turns the node off), and the
     *  texture when bBindTexture (the per-frame update leaves it bound). */
    VOXELPLANETS_API void SetClimateParameters(UMaterialInstanceDynamic* Material, UTexture2DDynamic* Texture, const FLinearColor& Params,
        const FLinearColor& Shape, const FLinearColor& Tint, bool bBindTexture = true);

    /** Whether a material has the land node: its VoxelLandParams vector parameter. */
    VOXELPLANETS_API bool HasLandNode(const UMaterialInterface* Material);

    /** Game thread. The height and field textures of a bake (they depend on nothing else, so a
     *  rebuilt density with the same bake keeps them): dynamic textures filled on the render
     *  thread from texel copies made here (about 76 MB at N 1024), held until the upload ran,
     *  then freed: no CPU copy stays.
     *  False (and null) when the RHI can't hold them. */
    VOXELPLANETS_API bool MakeBakeTextures(const TSharedPtr<const FVoxelLandBake, ESPMode::ThreadSafe>& Bake,
        UTexture2DDynamic*& OutHeight, UTexture2DDynamic*& OutFields);

    /** Game thread. The constants texture of a terrestrial density (null when the RHI can't hold it). */
    VOXELPLANETS_API UTexture2DDynamic* MakeConstantTexture(const FVoxelTerrestrialDensity& Density);

    /** Game thread. The peaks' noise set as a texture (RGBA16 unsigned normalised: its packed mip
     *  chain in rows of N texels, VoxelLandNoise.ush), one for every planet sharing the set, or a
     *  1 x 1 stand-in when the density has none (the constants then turn the peaks off). */
    VOXELPLANETS_API UTexture2DDynamic* MakeNoiseTexture(const FVoxelTerrestrialDensity& Density);

    /** Game thread. The textures on a material instance. */
    VOXELPLANETS_API void SetTextureParameters(UMaterialInstanceDynamic* Material, UTexture2DDynamic* Height, UTexture2DDynamic* Fields,
        UTexture2DDynamic* Constants, UTexture2DDynamic* Noise);

    /** Game thread, every frame: the planet's frame (PlanetFrame: its location and rotation, no
     *  scale) as its origin and axes (double-float), the debug view, the surface tolerance
     *  (m) and the footprint (cm) below which the mesh's normal is used (NearFootprint). The material places each pixel from its own view, so no camera is needed here.
     *  bEnabled false turns the node off (it then reads no texture and leaves the mesh's
     *  normal); bCacheAvailable false makes it evaluate the land itself (the analytic node)
     *  whatever voxel.Land.MaterialSource asks. */
    VOXELPLANETS_API void SetFrameParameters(UMaterialInstanceDynamic* Material, const FTransform& PlanetFrame,
        int32 View, double Tolerance, double NearFootprint, bool bEnabled, bool bCacheAvailable);
}
