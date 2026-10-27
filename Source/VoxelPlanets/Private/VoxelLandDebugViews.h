// VoxelLandDebugViews.h - The land debug views (voxel.Debug.LandView, voxel.Debug.LandViewSource):
// the planet's policy for the terrain's debug colours. The views are VOXEL_LAND_VIEW_*
// (VoxelLandDebugPalette.ush): the terrain material's land node shows them per pixel
// (VoxelLandMaterial.ush), or the terrestrial density source's colour pass per vertex
// (VoxelLandDebug.ush) through the brick terrain's vertex-colour view.

#pragma once

#include "CoreMinimal.h"

namespace VoxelLandDebugViews
{
    /** Game thread. voxel.Debug.LandView, 0 when out of range. */
    int32 GetView();

    /** Game thread. The view the vertex colours show: the selected one where they show it
     *  (voxel.Debug.LandViewSource 1, or a material without a bound land node: bMaterialViews
     *  false), else 0. */
    int32 GetVertexView(bool bMaterialViews);

    /** Game thread. The view the terrain material's land node shows (voxel.Debug.LandViewSource
     *  0), when it is bound. */
    int32 GetMaterialView();

    /** Game thread. The selected view's legend on screen (nothing when it's off): bTerrestrial,
     *  the terrain is a terrestrial planet's (else the views of the bake show magenta);
     *  bMaterialViews, its material's land node is bound. */
    void ShowLegend(bool bTerrestrial, bool bMaterialViews);
}
