// VoxelLandDebugViews.cpp - The land debug views (voxel.Debug.LandView): which view the terrain shows,
// and where (the material's land node or the vertex colours), and its legend on screen. The planet's
// terrain actor (APlanetTerrainActor) applies them; they came out of the brick terrain in S1b
// (Design/PlanetPluginSplit.md).

#include "VoxelLandDebugViews.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "VoxelShaderCompat.h"

namespace VoxelShader
{
#include "VoxelLandDebugPalette.ush"
}

static TAutoConsoleVariable<int32> CVarVoxelDebugLandViewSource(
    TEXT("voxel.Debug.LandViewSource"), 0,
    TEXT("Where voxel.Debug.LandView colours terrestrial terrain. 0: per pixel, in the terrain material's land node (VoxelLandMaterial.ush; STAGE_M1_NOTES.md): a view change is instant (where the material has no land node, or it isn't bound, the vertex colours show it). 1: per vertex, in the vertex colour with alpha 0 (STAGE_L1b_NOTES.md): a view change meshes the terrain again. The noise surface always uses the vertex colour."),
    ECVF_Default);

static TAutoConsoleVariable<int32> CVarVoxelDebugLandView(
    TEXT("voxel.Debug.LandView"), 0,
    TEXT("Colours the terrain by a component of the generator: per pixel in the terrain material's land node, or per vertex in the vertex colour (voxel.Debug.LandViewSource). A legend is on screen while a view is on.\n")
    TEXT(" 0: off\n")
    TEXT(" 1: elevation about sea level (hypsometric; 500 m bands on land, 1 km at sea, close up)\n")
    TEXT(" 2: the bake's height alone (no gullies or detail)\n")
    TEXT(" 3: the detail layer (blue down, red up, over its amplitude)\n")
    TEXT(" 4: crust (oceanic blue to continental tan; the threshold yellow)\n")
    TEXT(" 5: boundary class (red collision, orange arc range, purple trench, yellow island arc, cyan rift, green ridge)\n")
    TEXT(" 6: plates (a hue each, brighter on continental crust)\n")
    TEXT(" 7: slope (green flat, yellow 20 deg, red 45 deg, purple 60+)\n")
    TEXT(" 8: the bake's cells (a checker in each cube face's colour)\n")
    TEXT(" 9: drainage (rivers: blue from 1,000 km^2)\n")
    TEXT(" 10: lakes by depth\n")
    TEXT(" 11: the gullies (blue down, red up, over their local depth)\n")
    TEXT(" 12: lakes and rivers (blue over neutral ground)\n")
    TEXT(" 13: elevation with the lakes and rivers over it\n")
    TEXT(" 14: the landform (flat plains straw, rolling ground green, hill country dark green to brown by roughness)\n")
    TEXT(" 15: the hills layer (white none, green to brown over HillRelief)\n")
    TEXT(" 16: the peaks layer (blue valleys, red summits, over their local relief)\n")
    TEXT(" 17: the peaks' relief (white none, green to brown, dark at 3 km: mountain crests)\n")
    TEXT(" 18: the sedimentary province (grey, lighter sedimentary) and where the strata replace the hills (orange)\n")
    TEXT(" 19: the rivers (blue channels, darker for larger rivers, a pale wash over their valleys) over the elevation\n")
    TEXT("2 to 6 and 8 to 19 need a terrestrial planet, 9, 10, 12, 13 and 19 a GPU bake (magenta otherwise)."),
    ECVF_Default);

namespace VoxelLandDebugViewsDetail
{
    /** The legend of a land view: a title, then one line per colour, each in its colour. */
    struct FLegendLine
    {
        FString Text;
        FColor Color;
    };

    FColor ToColor(const VoxelShader::float3& C)
    {
        return FColor((uint8)FMath::RoundToInt(FMath::Clamp(C.x, 0.0f, 1.0f) * 255.0f), (uint8)FMath::RoundToInt(FMath::Clamp(C.y, 0.0f, 1.0f) * 255.0f),
            (uint8)FMath::RoundToInt(FMath::Clamp(C.z, 0.0f, 1.0f) * 255.0f));
    }

    TArray<FLegendLine> LandViewLegend(int32 View, bool bTerrestrial, bool bPerPixel)
    {
        using namespace VoxelShader;
        static const TCHAR* Names[VOXEL_LAND_VIEW_COUNT] = { TEXT("off"), TEXT("elevation"), TEXT("bake height"), TEXT("detail layer"),
            TEXT("crust"), TEXT("boundary class"), TEXT("plates"), TEXT("slope"), TEXT("bake cells"), TEXT("drainage"), TEXT("lakes"), TEXT("gullies"),
            TEXT("lakes and rivers"), TEXT("elevation, lakes and rivers"), TEXT("landform"), TEXT("hills"), TEXT("peaks"), TEXT("peak amplitude"),
            TEXT("strata province"), TEXT("rivers") };
        TArray<FLegendLine> Lines;
        Lines.Add({ FString::Printf(TEXT("voxel.Debug.LandView %d: %s (0 hides it; %s)"), View, Names[View],
            bPerPixel
                ? TEXT("per pixel, in the terrain material's land node")
                : bTerrestrial && CVarVoxelDebugLandViewSource.GetValueOnGameThread() == 0
                ? TEXT("per vertex: the terrain material has no land node bound (STAGE_M1_NOTES.md), so it must show vertex colour")
                : TEXT("per vertex: the terrain material must show vertex colour")), FColor::White });
        const bool bBakeView = View != (int32)VOXEL_LAND_VIEW_ELEVATION && View != (int32)VOXEL_LAND_VIEW_SLOPE;
        if (bBakeView && !bTerrestrial)
        {
            Lines.Add({ TEXT("  needs a terrestrial planet (Shape > Generator): magenta here"), ToColor(VoxelLandViewNone()) });
            return Lines;
        }
        auto Add = [&Lines](const TCHAR* Text, const float3& C) { Lines.Add({ FString(TEXT("  ")) + Text, ToColor(C) }); };
        switch ((uint32)View)
        {
        case VOXEL_LAND_VIEW_ELEVATION:
        case VOXEL_LAND_VIEW_BAKE:
        {
            static const float Heights[] = { -7000.0f, -3000.0f, -800.0f, -100.0f, 0.0f, 300.0f, 800.0f, 1500.0f, 2500.0f, 4000.0f, 6000.0f };
            static const TCHAR* Labels[] = { TEXT("-7 km"), TEXT("-3 km"), TEXT("-800 m"), TEXT("-100 m (shelf)"), TEXT("0 m"), TEXT("300 m"),
                TEXT("800 m"), TEXT("1.5 km"), TEXT("2.5 km"), TEXT("4 km"), TEXT("6 km and up") };
            for (int32 i = 0; i < (int32)UE_ARRAY_COUNT(Heights); i++)
                Add(Labels[i], VoxelLandViewHeight(Heights[i], 1.0e9f));
            break;
        }
        case VOXEL_LAND_VIEW_DETAIL:
            Add(TEXT("-1 (down by the full amplitude)"), VoxelLandViewSigned(-1.0f));
            Add(TEXT("-0.5"), VoxelLandViewSigned(-0.5f));
            Add(TEXT("0"), VoxelLandViewSigned(0.0f));
            Add(TEXT("+0.5"), VoxelLandViewSigned(0.5f));
            Add(TEXT("+1 (up; a quarter as strong under the sea, scaled by GullyDetailScale on land)"), VoxelLandViewSigned(1.0f));
            break;
        case VOXEL_LAND_VIEW_GULLY:
            Add(TEXT("-1 (a gully floor: down by the coarsest octave's local half-depth)"), VoxelLandViewSigned(-1.0f));
            Add(TEXT("-0.5"), VoxelLandViewSigned(-0.5f));
            Add(TEXT("0 (none: flats, the sea, valley floors, or too coarse a level)"), VoxelLandViewSigned(0.0f));
            Add(TEXT("+0.5"), VoxelLandViewSigned(0.5f));
            Add(TEXT("+1 (a spur between gullies)"), VoxelLandViewSigned(1.0f));
            Add(TEXT("magenta: no gullies (GullyStrength 0, or GullyFinestWavelength above two bake cells)"), VoxelLandViewNone());
            break;
        case VOXEL_LAND_VIEW_CRUST:
            Add(TEXT("0 oceanic"), VoxelLandViewCrust(0.0f));
            Add(TEXT("0.25"), VoxelLandViewCrust(0.25f));
            Add(TEXT("0.5 the threshold (coasts lie near it)"), VoxelLandViewCrust(0.5f));
            Add(TEXT("0.75"), VoxelLandViewCrust(0.75f));
            Add(TEXT("1 continental"), VoxelLandViewCrust(1.0f));
            break;
        case VOXEL_LAND_VIEW_CLASS:
            Add(TEXT("no boundary (oceanic crust)"), VoxelLandViewClass(0u, 0.0f));
            Add(TEXT("no boundary (continental crust)"), VoxelLandViewClass(0u, 1.0f));
            Add(TEXT("collision range"), VoxelLandViewClass(1u, 0.0f));
            Add(TEXT("arc range (ocean under a continent)"), VoxelLandViewClass(2u, 0.0f));
            Add(TEXT("trench"), VoxelLandViewClass(3u, 0.0f));
            Add(TEXT("island arc"), VoxelLandViewClass(4u, 0.0f));
            Add(TEXT("rift"), VoxelLandViewClass(5u, 0.0f));
            Add(TEXT("mid-ocean ridge"), VoxelLandViewClass(6u, 0.0f));
            break;
        case VOXEL_LAND_VIEW_PLATES:
            for (uint32 p = 0; p < 4u; p++)
                Add(*FString::Printf(TEXT("plate %u (continental crust)"), p), VoxelLandViewPlate(p, 1.0f));
            Add(TEXT("plate 0 (oceanic crust): darker"), VoxelLandViewPlate(0u, 0.0f));
            break;
        case VOXEL_LAND_VIEW_SLOPE:
            Add(TEXT("0 deg"), VoxelLandViewSlope(0.0f));
            Add(TEXT("10 deg"), VoxelLandViewSlope(10.0f));
            Add(TEXT("20 deg"), VoxelLandViewSlope(20.0f));
            Add(TEXT("30 deg"), VoxelLandViewSlope(30.0f));
            Add(TEXT("45 deg"), VoxelLandViewSlope(45.0f));
            Add(TEXT("60 deg and up"), VoxelLandViewSlope(60.0f));
            break;
        case VOXEL_LAND_VIEW_CELLS:
        {
            static const TCHAR* Faces[] = { TEXT("face +X"), TEXT("face -X"), TEXT("face +Y"), TEXT("face -Y"), TEXT("face +Z"), TEXT("face -Z") };
            for (uint32 f = 0; f < 6u; f++)
                Add(Faces[f], VoxelLandViewFace(f));
            Add(TEXT("alternate cells darker (close up)"), VoxelLandViewCell(0u, 1, 0, 1.0f));
            break;
        }
        case VOXEL_LAND_VIEW_FLOW:
            Add(TEXT("hillslope"), VoxelLandViewFlow(0.0f, false));
            Add(TEXT("10 km^2"), VoxelLandViewFlow(1.0f, false));
            Add(TEXT("100 km^2"), VoxelLandViewFlow(2.0f, false));
            Add(TEXT("1,000 km^2 (river)"), VoxelLandViewFlow(3.0f, false));
            Add(TEXT("100,000 km^2"), VoxelLandViewFlow(5.0f, false));
            Add(TEXT("sea"), VoxelLandViewFlow(0.0f, true));
            Add(TEXT("magenta: the bake has no drainage (a CPU bake)"), VoxelLandViewNone());
            break;
        case VOXEL_LAND_VIEW_LAKES:
            Add(TEXT("lake, shallow"), VoxelLandViewLake(0.0f, true, false));
            Add(TEXT("lake, 50 m"), VoxelLandViewLake(50.0f, true, false));
            Add(TEXT("lake, 100 m and deeper"), VoxelLandViewLake(100.0f, true, false));
            Add(TEXT("dry land"), VoxelLandViewLake(0.0f, false, false));
            Add(TEXT("sea"), VoxelLandViewLake(0.0f, false, true));
            Add(TEXT("magenta: the bake has no drainage (a CPU bake)"), VoxelLandViewNone());
            break;
        case VOXEL_LAND_VIEW_WATER:
        case VOXEL_LAND_VIEW_TERRAIN_WATER:
        {
            const bool bTerrain = View == (int32)VOXEL_LAND_VIEW_TERRAIN_WATER;
            const float3 Land = bTerrain ? VoxelLandViewHeight(300.0f, 1.0e9f) : VoxelLandViewDry(false);
            Add(TEXT("lake, shallow"), VoxelLandViewWater(Land, 0.0f, 0.0f, true, false));
            Add(TEXT("lake, 100 m and deeper"), VoxelLandViewWater(Land, 0.0f, 100.0f, true, false));
            Add(TEXT("stream, 10 km^2"), VoxelLandViewWater(Land, 1.0f, 0.0f, false, false));
            Add(TEXT("river, 1,000 km^2"), VoxelLandViewWater(Land, 3.0f, 0.0f, false, false));
            Add(TEXT("river, 100,000 km^2"), VoxelLandViewWater(Land, 5.0f, 0.0f, false, false));
            Add(bTerrain ? TEXT("land (the elevation's colours; 300 m here)") : TEXT("land"), Land);
            Add(TEXT("sea"), bTerrain ? VoxelLandViewHeight(-800.0f, 1.0e9f) : VoxelLandViewDry(true));
            Add(TEXT("magenta: the bake has no drainage (a CPU bake)"), VoxelLandViewNone());
            break;
        }
        case VOXEL_LAND_VIEW_LANDFORM:
            Add(TEXT("flat plains"), VoxelLandViewLandform(0.0f, 0.0f, false));
            Add(TEXT("rolling ground"), VoxelLandViewLandform(0.4f, 0.0f, false));
            Add(TEXT("hill country, smooth"), VoxelLandViewLandform(1.0f, 0.0f, false));
            Add(TEXT("hill country, rough-topped"), VoxelLandViewLandform(1.0f, 1.0f, false));
            Add(TEXT("sea"), VoxelLandViewLandform(0.0f, 0.0f, true));
            Add(TEXT("magenta: the bake has no landform"), VoxelLandViewNone());
            break;
        case VOXEL_LAND_VIEW_HILLS:
            Add(TEXT("none (flat plains, river corridors, coasts)"), VoxelLandViewHills(0.0f));
            Add(TEXT("half HillRelief"), VoxelLandViewHills(0.5f));
            Add(TEXT("HillRelief"), VoxelLandViewHills(1.0f));
            Add(TEXT("1.5 x HillRelief"), VoxelLandViewHills(1.5f));
            Add(TEXT("magenta: no hills (HillRelief 0, or no landform)"), VoxelLandViewNone());
            break;
        case VOXEL_LAND_VIEW_PEAKS:
            Add(TEXT("-1 (down by the peaks' local relief)"), VoxelLandViewSigned(-1.0f));
            Add(TEXT("-0.5"), VoxelLandViewSigned(-0.5f));
            Add(TEXT("0 (the set's mean; none on plains, in the bake's valleys, at sea)"), VoxelLandViewSigned(0.0f));
            Add(TEXT("+0.5"), VoxelLandViewSigned(0.5f));
            Add(TEXT("+1 (up by the peaks' local relief)"), VoxelLandViewSigned(1.0f));
            Add(TEXT("magenta: no peaks (PeakHeightScale 0, or no landform)"), VoxelLandViewNone());
            break;
        case VOXEL_LAND_VIEW_PEAK_AREA:
            Add(TEXT("none (plains, valleys, coasts)"), VoxelLandViewPeakArea(0.0f));
            Add(TEXT("250 m"), VoxelLandViewPeakArea(250.0f));
            Add(TEXT("500 m"), VoxelLandViewPeakArea(500.0f));
            Add(TEXT("1 km"), VoxelLandViewPeakArea(1000.0f));
            Add(TEXT("2 km"), VoxelLandViewPeakArea(2000.0f));
            Add(TEXT("3 km"), VoxelLandViewPeakArea(3000.0f));
            Add(TEXT("magenta: no peaks (PeakHeightScale 0, or no landform)"), VoxelLandViewNone());
            break;
        case VOXEL_LAND_VIEW_STRATA:
            Add(TEXT("crystalline (province 0)"), VoxelLandViewStrata(0.0f, 0.0f, false));
            Add(TEXT("province 0.5"), VoxelLandViewStrata(0.5f, 0.0f, false));
            Add(TEXT("sedimentary (province 1), hills"), VoxelLandViewStrata(1.0f, 0.0f, false));
            Add(TEXT("strata half in (the switch)"), VoxelLandViewStrata(0.5f, 0.5f, false));
            Add(TEXT("strata (StrataCoverage of the land)"), VoxelLandViewStrata(0.7f, 1.0f, false));
            Add(TEXT("sea"), VoxelLandViewStrata(0.0f, 0.0f, true));
            Add(TEXT("magenta: the bake has no landform"), VoxelLandViewNone());
            break;
        case VOXEL_LAND_VIEW_RIVERS:
        {
            const float3 Ground = VoxelLandViewHeight(300.0f, 1.0e9f);
            Add(TEXT("a river of 200 km^2 (the smallest at the default RiverMinArea)"), VoxelLandViewRiver(Ground, 1.0f, 1.0f, 27.6f));
            Add(TEXT("10,000 km^2"), VoxelLandViewRiver(Ground, 1.0f, 1.0f, 33.2f));
            Add(TEXT("100,000 km^2 and more"), VoxelLandViewRiver(Ground, 1.0f, 1.0f, 36.6f));
            Add(TEXT("its valley (ValleyWidthScale)"), VoxelLandViewRiver(Ground, 0.0f, 1.0f, 27.6f));
            Add(TEXT("a kept lake, shallow to 100 m (the lakes view's colours)"), VoxelLandViewLake(20.0f, true, false));
            Add(TEXT("land (the elevation's colours)"), Ground);
            Add(TEXT("magenta: no rivers (a CPU bake, or RiverMinArea 0)"), VoxelLandViewNone());
            break;
        }
        default:
            break;
        }
        return Lines;
    }
}


namespace VLDV = VoxelLandDebugViewsDetail;

int32 VoxelLandDebugViews::GetView()
{
    const int32 View = CVarVoxelDebugLandView.GetValueOnGameThread();
    return View > 0 && View < (int32)VOXEL_LAND_VIEW_COUNT ? View : 0;
}

int32 VoxelLandDebugViews::GetVertexView(bool bMaterialViews)
{
    return !bMaterialViews || CVarVoxelDebugLandViewSource.GetValueOnGameThread() != 0 ? GetView() : 0;
}

int32 VoxelLandDebugViews::GetMaterialView()
{
    return CVarVoxelDebugLandViewSource.GetValueOnGameThread() == 0 ? GetView() : 0;
}

void VoxelLandDebugViews::ShowLegend(bool bTerrestrial, bool bMaterialViews)
{
    const int32 View = GetView();
    if (View == 0 || !GEngine) return;
    const TArray<VLDV::FLegendLine> Lines = VLDV::LandViewLegend(View, bTerrestrial, bMaterialViews && GetMaterialView() != 0);
    // One key per line, shared by every terrain (they show the same legend).
    constexpr uint64 KeyBase = 0x564f584c56000000ull;   // "VOXLV"
    for (int32 i = 0; i < Lines.Num(); i++)
        GEngine->AddOnScreenDebugMessage(KeyBase + (uint64)i, 0.25f, Lines[i].Color, Lines[i].Text, false);
}
