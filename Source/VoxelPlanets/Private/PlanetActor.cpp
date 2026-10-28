#include "PlanetActor.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "VoxelDensity.h"
#include "VoxelPlanetDensity.h"
#include "VoxelTerrestrialDensity.h"
#include "PlanetGravityZone.h"
#include "Components/DirectionalLightComponent.h"
#include "VoxelLandBake.h"
#include "VoxelLandClimate.h"
#include "VoxelLandShelf.h"
#include "VoxelLandMaterial.h"
#include "VoxelCubeSphere.h"
#include "Engine/Texture2DDynamic.h"
#include "FlowSimSubsystem.h"
#include "AtmosphereWeather.h"
#include "VoxelOceanProfile.h"
#include "Async/ParallelFor.h"
#include "Async/Async.h"
#include "VoxelPlanets.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "VoxelBrickTerrain.h"

// Stage P1h (S6): the atmosphere from a terrestrial planet's first tick.
static TAutoConsoleVariable<int32> CVarVoxelPlanetEarlyAtmosphere(
    TEXT("voxel.Planet.EarlyAtmosphere"), 1,
    TEXT("Stage P1h. 1 (default): a terrestrial planet's first build shows its atmosphere from its first tick, over the sea's sphere and with no ")
    TEXT("ground under its weather yet, while the land bakes; the climate capture then gives it the bake's ground and restarts its sim, as before. ")
    TEXT("0: the atmosphere appears with the capture, once the bake is in (R1a)."),
    ECVF_Default);

namespace PlanetActorTimelineDetail
{
    /** Stage P1a: adds a scope's game-thread time to the build's timeline. */
    struct FSpentScope
    {
        FPlanetSpawnTimeline& Timeline;
        FPlanetSpawnTimeline::ESpent What;
        double Start;
        FSpentScope(FPlanetSpawnTimeline& InTimeline, FPlanetSpawnTimeline::ESpent InWhat)
            : Timeline(InTimeline), What(InWhat), Start(FPlatformTime::Seconds()) {}
        ~FSpentScope() { Timeline.AddSpent(What, FPlatformTime::Seconds() - Start); }
    };

    /** Seconds after Initialize the timeline waits for the terrain to settle and the shore field. */
    constexpr double SettleWaitSeconds = 90.0;

    using ESpawnMark = FPlanetSpawnTimeline::EMark;
    using ESpawnSpent = FPlanetSpawnTimeline::ESpent;
}
using namespace PlanetActorTimelineDetail;

APlanetActor::APlanetActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;

    PlanetRoot = CreateDefaultSubobject<USceneComponent>(TEXT("PlanetRoot"));
    SetRootComponent(PlanetRoot);

    // Default planet radius: 1000 km = 100,000,000 cm
    SetActorScale3D(FVector(100000000.0));

#if WITH_EDITORONLY_DATA
    // Dragging the planet with the gizmo reconstructs it once, on release (a scale drag
    // would otherwise rebuild the terrain and ocean every frame).
    bRunConstructionScriptOnDrag = false;
#endif
}

void APlanetActor::BeginPlay()
{
    Super::BeginPlay();
    // bPendingInitialize defaults to true - first Tick calls Initialize.
}

void APlanetActor::Destroyed()
{
    // Destroyed() fires when the actor is explicitly deleted - both in-editor
    // and at runtime. The world is still valid here, so Destroy() on children works.
    DestroyChildActors();
    Super::Destroyed();
}

void APlanetActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    DestroyChildActors();
    Super::EndPlay(EndPlayReason);
}

void APlanetActor::BeginDestroy()
{
    // Fallback - catch anything EndPlay missed (shouldn't happen, but safe).
    DestroyChildActors();
    Super::BeginDestroy();
}

void APlanetActor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    if (!GetWorld() || GetWorld()->IsPreviewWorld()) return;

    // A slider or a gizmo drag still moving: the release flags the work once.
    if (bInteractiveEdit) return;
    FlagPendingWork();
}

void APlanetActor::FlagPendingWork()
{
    // Flag for deferred work in Tick - never spawn actors during OnConstruction.
    const bool bTerrestrial = Generator == EVoxelSurfaceGenerator::Terrestrial;
    if (!bInitialized
        || !GetActorScale3D().Equals(LastInitScale, 0.01)
        || NoiseAmplitudeRatio != LastNoiseAmplitudeRatio
        || Generator != LastGenerator
        || (!bTerrestrial && SurfaceSettings.GetSettingsHash() != LastSurfaceSettingsHash)
        || (bTerrestrial && TerrestrialSettings.GetSettingsHash() != LastTerrestrialHash))
    {
        bPendingInitialize = true;
    }
    else if ((!bTerrestrial && SeaLevel != LastSeaLevel)
        || bEnableOcean != bLastEnableOcean
        || bEnableGravity != bLastEnableGravity
        || bEnableAtmosphere != bLastEnableAtmosphere)
    {
        bPendingOceanUpdate = true;
    }
}

#if WITH_EDITOR
void APlanetActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    // The construction rerun inside Super sees whether a slider is still moving.
    const bool bInteractive = PropertyChangedEvent.ChangeType == EPropertyChangeType::Interactive;
    bInteractiveEdit = bInteractive;
    Super::PostEditChangeProperty(PropertyChangedEvent);
    bInteractiveEdit = false;
    if (!bInitialized) return;
    // A PIE world doesn't rerun construction after an edit; flagging twice is harmless.
    if (!bInteractive)
        FlagPendingWork();

    const FName PropName = PropertyChangedEvent.GetPropertyName();
    const FName MemberName = PropertyChangedEvent.MemberProperty ? PropertyChangedEvent.MemberProperty->GetFName() : PropName;

    // The terrain material changes in place (no rebuild).
    if (PropName == GET_MEMBER_NAME_CHECKED(APlanetActor, TerrainMaterial))
    {
        if (TerrainActor)
            TerrainActor->SetSurfaceMaterial(TerrainMaterial);
        return;
    }

    // So does the ocean's.
    if (PropName == GET_MEMBER_NAME_CHECKED(APlanetActor, OceanMaterial))
    {
        if (OceanActor)
            OceanActor->SetOceanMaterial(OceanMaterial);
        return;
    }

    // Stage A1: the atmosphere's generation (its seed, model and set) applies in place: the
    // atmosphere regenerates, the planet isn't rebuilt.
    if (MemberName == GET_MEMBER_NAME_CHECKED(APlanetActor, AtmosphereSeed)
        || MemberName == GET_MEMBER_NAME_CHECKED(APlanetActor, AtmosphereType)
        || MemberName == GET_MEMBER_NAME_CHECKED(APlanetActor, AtmosphereSet)
        || MemberName == GET_MEMBER_NAME_CHECKED(APlanetActor, AtmosphereTerrainHeight)
        || MemberName == GET_MEMBER_NAME_CHECKED(APlanetActor, AtmosphereRiverWater))
    {
        if (!bInteractive)
        {
            // Stage T5b: a regenerated set restarts the sim without changing the capture's key.
            if (MemberName == GET_MEMBER_NAME_CHECKED(APlanetActor, AtmosphereSet))
                RecaptureClimate();
            double PlanetRadius = 0.0, NoiseAmplitude = 0.0, OceanRadius = 0.0;
            ComputeShape(PlanetRadius, NoiseAmplitude, OceanRadius);
            ConfigureAtmosphere(PlanetRadius, OceanRadius, MemberName == GET_MEMBER_NAME_CHECKED(APlanetActor, AtmosphereSet));
        }
        return;
    }

    // The ocean's settings apply in place, as far as they can (a change inside the struct
    // reports the struct as the member property); slider drags apply on release.
    // Stage A12: the ocean's seeding applies in place (the sweep's slider live).
    if (MemberName == GET_MEMBER_NAME_CHECKED(APlanetActor, OceanProfile)
        || MemberName == GET_MEMBER_NAME_CHECKED(APlanetActor, OceanSeed)
        || MemberName == GET_MEMBER_NAME_CHECKED(APlanetActor, OceanLocks)
        || MemberName == GET_MEMBER_NAME_CHECKED(APlanetActor, OceanSweepPath)
        || MemberName == GET_MEMBER_NAME_CHECKED(APlanetActor, OceanSweepPosition))
    {
        if (OceanActor)
            OceanActor->SetOceanSettings(GenerateOceanSettings(), bInteractive);
        return;
    }

    if (MemberName == GET_MEMBER_NAME_CHECKED(APlanetActor, OceanSettings))
    {
        // The planet shows what the ocean builds (TileQuads snapped, limits) once released.
        if (!bInteractive)
            OceanSettings.Sanitize();
        if (OceanActor)
            OceanActor->SetOceanSettings(GenerateOceanSettings(), bInteractive);
        return;
    }

    // Scale, NoiseAmplitudeRatio, SurfaceSettings, SeaLevel, and toggle changes are handled
    // by FlagPendingWork via the bPendingInitialize / bPendingOceanUpdate flags. An undo or redo
    // (it reports no property) forwards the children's settings again; any other property
    // (a climate, terrain or gravity one) has nothing of the children's to forward (R1, PLN-6:
    // it used to regenerate and re-apply the ocean's seeded settings on every such edit).
    if (!bInteractive && PropName.IsNone())
        ForwardChildSettings();
}

void APlanetActor::PostEditMove(bool bFinished)
{
    // A gizmo drag (also on a Blueprint planet, whose asset may rerun construction on drag)
    // flags the work once, when it ends.
    bInteractiveEdit = !bFinished;
    Super::PostEditMove(bFinished);
    bInteractiveEdit = false;
}
#endif

void APlanetActor::ForwardChildSettings()
{
    if (TerrainActor && TerrainMaterial && TerrainActor->SurfaceMaterial != TerrainMaterial)
        TerrainActor->SetSurfaceMaterial(TerrainMaterial);
    if (OceanActor)
    {
        if (OceanMaterial && OceanActor->OceanMaterial != OceanMaterial)
            OceanActor->SetOceanMaterial(OceanMaterial);
        OceanActor->SetOceanSettings(GenerateOceanSettings());
    }
}

void APlanetActor::SetOceanSettings(const FVoxelOceanSettings& InSettings)
{
    OceanSettings = InSettings;
    OceanSettings.Sanitize();
    if (OceanActor && bInitialized)
        OceanActor->SetOceanSettings(GenerateOceanSettings());
}

void APlanetActor::OceanPresetRealistic()
{
    Modify();
    FVoxelOceanSettings S = OceanSettings;
    S.ApplyRealisticPreset();
    SetOceanSettings(S);
}

void APlanetActor::OceanPresetGodot()
{
    Modify();
    FVoxelOceanSettings S = OceanSettings;
    S.ApplyGodotPreset();
    SetOceanSettings(S);
}

void APlanetActor::Tick(float DeltaTime)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(APlanetActor::Tick);
    Super::Tick(DeltaTime);

    // Stage P1a: the build's timeline (measurement only). A build starts with the pending flag;
    // one flagged again after Initialize (a climate landing late, a settings change) starts anew.
    const double TickStart = FPlatformTime::Seconds();
    if (bPendingInitialize && (!SpawnTimeline.IsRunning() || SpawnTimeline.Has(ESpawnMark::Initialize)))
        SpawnTimeline.Begin(GetName(), bInitialized ? TEXT("rebuild") : TEXT("first build"));
    ON_SCOPE_EXIT
    {
        SpawnTimeline.NoteTick(FPlatformTime::Seconds() - TickStart);
        TickSpawnTimeline();
    };

    // Deferred init - safe to spawn actors here.
    if (bPendingInitialize)
    {
        // Stage P1h (S6): on the first build, the atmosphere at once (the kickoff's "atmosphere
        // first, as the placeholder"): nothing it draws needs the bake but its surface map, which
        // it goes without until the capture below gives it one and restarts its sim. Its floor is
        // the sea's sphere, the actor's radius, as the capture's configuration has it then
        // (ConfigureAtmosphere: the terrestrial generator's floor is the sea), so it doesn't move.
        if (Generator == EVoxelSurfaceGenerator::Terrestrial && !bInitialized && bEnableAtmosphere
            && LastAtmosphereSeed == INDEX_NONE && CVarVoxelPlanetEarlyAtmosphere.GetValueOnGameThread() > 0)
        {
            SpawnChildActors();
            const double SeaRadius = FMath::Max(1.0, GetActorScale3D().GetMax());
            ConfigureAtmosphere(SeaRadius, SeaRadius);
        }

        // Stage P1i: a climate field made on a worker since the last tick is taken first.
        TickClimateFieldJob();

        // A terrestrial planet is built once its bake is ready; until then it shows what it
        // showed (nothing on the first build).
        if (Generator == EVoxelSurfaceGenerator::Terrestrial && !TickLandBake())
            return;
        // Stage T5b: then the climate's snapshot for it (the atmosphere spins up on the new ground;
        // the biomes it makes shape the terrain, which is built once, after it).
        if (Generator == EVoxelSurfaceGenerator::Terrestrial && LandClimate.bWaitForClimate && !UpdateClimateCapture())
            return;
        if (!SpawnTimeline.Has(ESpawnMark::Climate))
        {
            SpawnTimeline.SetClimateOutcome(Generator != EVoxelSurfaceGenerator::Terrestrial || !LandClimate.bEnabled ? TEXT("off")
                : !LandClimate.bWaitForClimate ? TEXT("not waited for")
                : ClimateSim.IsValid() ? TEXT("snapshot") : TEXT("provisional model"));
            SpawnTimeline.Mark(ESpawnMark::Climate);
        }
        // Stage T5d2: the land chain's climate for the settings the build has (the lapse rate, the
        // ice sheets on or off), so a settings change rebuilds once, not again when the climate
        // catches up.
        // T5e: and the map's own settings (the ice shelf's share is in it), so a change to them
        // rebuilds once.
        // (Only on the current bake's base: after a new bake the capture makes the map, else a map
        // of the old planet's climate would place the shelf for one build.)
        if (Generator == EVoxelSurfaceGenerator::Terrestrial && (IsLandClimateFieldStale()
            || (LandClimateField.IsValid() && ClimateMapHash != LandClimateMapHash() && ClimateBaseKey == CurrentClimateBaseKey())))
            RebuildLandClimateTarget();
        // Stage P1i (S2): the field is made on a worker; the build waits for it (the shelf is
        // placed by it, the terrain's dunes too).
        if (Generator == EVoxelSurfaceGenerator::Terrestrial && TickClimateFieldJob())
            return;
        // T5e: the ice shelf in the terrain's bake, by the climate captured. Stage P1d: made on a
        // worker; the build waits for it (a few frames) as it waits for the bake, so the field's
        // tick and Initialize's are no longer one tick with the shelf in it.
        if (Generator == EVoxelSurfaceGenerator::Terrestrial && !UpdateIceShelf())
            return;
        // Stage P1i (S2): and the compositor on a worker too, for Initialize to take.
        if (Generator == EVoxelSurfaceGenerator::Terrestrial && !PrebuildCompositor())
            return;
        bPendingInitialize = false;
        bPendingOceanUpdate = false;
        Initialize();
    }
    else if (bPendingOceanUpdate)
    {
        // A terrestrial planet not built yet has nothing to update: its build applies the toggles.
        if (Generator == EVoxelSurfaceGenerator::Terrestrial && !LandBake.IsValid())
        {
            bPendingOceanUpdate = false;
            return;
        }
        bPendingOceanUpdate = false;
        double PlanetRadius = 0.0;
        double NoiseAmplitude = 0.0;
        double OceanRadius = 0.0;
        ComputeShape(PlanetRadius, NoiseAmplitude, OceanRadius);
        // The same configuration as the first initialization: the ocean keeps its material and
        // settings, and the gravity zone follows the new sea level. A gravity toggle alone
        // leaves the ocean as it is.
        const bool bSeaLevelChanged = Generator != EVoxelSurfaceGenerator::Terrestrial && SeaLevel != LastSeaLevel;
        if (bSeaLevelChanged || bEnableOcean != bLastEnableOcean)
            ConfigureOcean(PlanetRadius, NoiseAmplitude, OceanRadius);
        // (Stage A1: the atmosphere at the new surface, or on or off; and the ocean's sun again.
        // Not for a gravity toggle alone: R1, PLN-7.)
        if (bSeaLevelChanged || bEnableOcean != bLastEnableOcean || bEnableAtmosphere != bLastEnableAtmosphere)
            ConfigureAtmosphere(PlanetRadius, OceanRadius);
        // The elevation debug view measures from the new sea level.
        if (bSeaLevelChanged && TerrainActor)
            TerrainActor->SetDebugSeaRadius(OceanRadius);
        ConfigureGravity(PlanetRadius, OceanRadius);
        RecordAppliedState();
    }

    // Stage T5a: the land's climate (its map from the sim as it qualifies, the fade, the terrain's
    // climate node).
    TickLandClimate(DeltaTime);

    // Stage T5c: the terrain's dunes follow the climate captured for the seed. A capture that
    // lands after the build (bWaitForClimate off, or a recapture: a new atmosphere seed, new map
    // settings), or the climate turned off, rebuilds the terrain once; the shown map's fade and
    // the seasons don't.
    // Not while a capture is under way: the provisional map shown meanwhile would rebuild the
    // terrain once for nothing (turning the climate off resets the capture, so that still rebuilds).
    if (bInitialized && !bPendingInitialize && TerrainActor && Generator == EVoxelSurfaceGenerator::Terrestrial && LandBake.IsValid()
        && ClimateCaptureState != EVoxelPlanetClimateCapture::Waiting && DuneClimateHash() != LandClimateFieldBuilt)
    {
        UE_LOG(LogVoxelPlanets, Log, TEXT("[Planet] %s: the climate the dunes are placed by changed; rebuilding the terrain."), *GetName());
        bPendingInitialize = true;
    }
}

// ---------------------------------------------------------------------------
// Initialize
// ---------------------------------------------------------------------------

bool APlanetActor::TickLandBake()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(APlanetActor::TickLandBake);
    FSpentScope Spent(SpawnTimeline, ESpawnSpent::BakeGate);
    const double SeaRadius = GetActorScale3D().GetMax();
    const uint32 Key = VoxelLand::BakeKey(TerrestrialSettings, SeaRadius);
    // The shared noise pack (the peaks', detail, hills' and strata's sets): the density takes it when it is
    // built, so it is read or generated in the background first (the game thread never waits
    // for it), and held here from then on (it can't go before the density takes it).
    auto NoiseReady = [this]()
    {
        const FString Name = VoxelLand::LandNoiseNameFor(TerrestrialSettings);
        if (VoxelLand::TryGetLandNoise(Name, LandNoise))
        {
            SpawnTimeline.Mark(ESpawnMark::Noise);
            return true;
        }
        VoxelLand::PrewarmLandNoise(Name);
        return false;
    };
    // The rivers for the settings (R1b): a fresh bake comes with them; a loaded one, or new river
    // settings, build them on the GPU first (a few frames).
    auto RiversReady = [this]()
    {
        const bool bReady = VoxelLand::TickRivers(LandBake, TerrestrialSettings);
        if (bReady)
            SpawnTimeline.Mark(ESpawnMark::Rivers);
        return bReady;
    };
    if (LandBake.IsValid() && LandBake->Key == Key && LandBake->N == VoxelLand::BakeResolutionFor(TerrestrialSettings))
    {
        // Only the per-sample settings changed: the bake is reused.
        LandBaker.Reset();
        if (!SpawnTimeline.Has(ESpawnMark::Bake))
        {
            SpawnTimeline.SetBakeSource(TEXT("held"));
            SpawnTimeline.Mark(ESpawnMark::Bake);
        }
        const bool bRivers = RiversReady();
        return NoiseReady() && bRivers;
    }
    if (!LandBaker.IsValid() || LandBaker->GetKey() != Key)
        LandBaker = MakeShared<FVoxelLandBaker>(TerrestrialSettings, SeaRadius);
    LandBaker->Tick();
    if (!LandBaker->IsDone())
        return false;
    LandBake = LandBaker->GetResult();
    SpawnTimeline.SetBakeSource(LandBaker->GetSource());
    LandBaker.Reset();
    if (!LandBake.IsValid()) return false;
    SpawnTimeline.Mark(ESpawnMark::Bake);
    const bool bRivers = RiversReady();
    return NoiseReady() && bRivers;
}

float APlanetActor::GetBakeProgress() const
{
    if (Generator != EVoxelSurfaceGenerator::Terrestrial) return 1.0f;
    if (LandBaker.IsValid()) return LandBaker->GetProgress();
    return LandBake.IsValid() && LandBake->Key == VoxelLand::BakeKey(TerrestrialSettings, GetActorScale3D().GetMax()) ? 1.0f : 0.0f;
}

void APlanetActor::ComputeShape(double& OutPlanetRadius, double& OutNoiseAmplitude, double& OutOceanRadius) const
{
    const double Radius = GetActorScale3D().GetMax();
    if (Generator == EVoxelSurfaceGenerator::Terrestrial && LandBake.IsValid())
    {
        // Heights about sea level, the actor's radius; the lattice spans the bake's range,
        // within the NoiseAmplitudeRatio ceiling (T5e: the terrain's, with the ice shelf).
        VoxelLand::ShapeFor(TerrestrialSettings, *TerrainBake(), LandNoise.Get(), OutPlanetRadius, OutNoiseAmplitude);
        const double Ceiling = Radius * NoiseAmplitudeRatio;
        if (OutNoiseAmplitude > Ceiling)
        {
            UE_LOG(LogVoxelPlanets, Warning, TEXT("[Planet] %s: the terrestrial heights span %.0f m, more than NoiseAmplitudeRatio allows (%.0f m); they are clamped."),
                *GetName(), OutNoiseAmplitude * 0.01, Ceiling * 0.01);
            OutNoiseAmplitude = Ceiling;
        }
        OutOceanRadius = FMath::Max(1.0, Radius);
        return;
    }
    OutPlanetRadius = Radius;
    OutNoiseAmplitude = Radius * NoiseAmplitudeRatio;
    OutOceanRadius = ComputeOceanRadius(OutPlanetRadius, OutNoiseAmplitude);
}

double APlanetActor::ChildNoiseAmplitudeRatio(double PlanetRadius, double NoiseAmplitude) const
{
    if (Generator == EVoxelSurfaceGenerator::Terrestrial && LandBake.IsValid())
        return NoiseAmplitude / FMath::Max(PlanetRadius, 1.0);
    return NoiseAmplitudeRatio;
}

void APlanetActor::Initialize()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(APlanetActor::Initialize);
    FSpentScope Spent(SpawnTimeline, ESpawnSpent::Initialize);
    if (Generator != EVoxelSurfaceGenerator::Terrestrial)
    {
        // The noise surface needs no bake: release one a terrestrial build left.
        LandBake.Reset();
        ShelfBake.Reset();
        ShelfBakeHash = 0;
        ShelfFuture = TFuture<TSharedPtr<const FVoxelLandBake, ESPMode::ThreadSafe>>();
        ShelfFutureHash = 0;
        LandBaker.Reset();
        LandNoise.Reset();
    }
    double PlanetRadius = 0.0;
    double NoiseAmplitude = 0.0;
    double OceanRadius = 0.0;
    ComputeShape(PlanetRadius, NoiseAmplitude, OceanRadius);
    const double RootExtent = VoxelDensity::RootExtentFor(PlanetRadius, NoiseAmplitude);

    SpawnChildActors();
    if (!TerrainActor) return;

    // The terrain lattice depth the terrain actor derives in Rebuild: the edit store and the
    // compositor's finest footprint must match it.
    const int32 TempMaxDepth = APlanetTerrainActor::MaxDepthFor(RootExtent, TerrainActor->TargetPrecision);

    TSharedPtr<FDensitySampleCompositor> NewCompositor =
        BuildCompositor(PlanetRadius, NoiseAmplitude, RootExtent, TempMaxDepth);
    SharedCompositor = NewCompositor;

    // --- Terrain actor ---
    // The material is resolved by the rebuild below (InitializeFromOwner), against the new
    // density; SetSurfaceMaterial would resolve it now against the old one (R1, ENG-7). It is set
    // through the setter only before the first terrain exists (the default when none is given).
    if (TerrainActor->GetBrickTerrain().IsValid())
    {
        if (TerrainMaterial)
            TerrainActor->SurfaceMaterial = TerrainMaterial;
    }
    else
        TerrainActor->SetSurfaceMaterial(TerrainMaterial);
    TerrainActor->NoiseAmplitudeRatio = ChildNoiseAmplitudeRatio(PlanetRadius, NoiseAmplitude);
    TerrainActor->SetDebugSeaRadius(OceanRadius);
    {
        FSpentScope TerrainSpent(SpawnTimeline, ESpawnSpent::Terrain);
        TerrainActor->InitializeFromOwner(SharedCompositor, PlanetRoot, FVector(PlanetRadius));
    }
    // Re-activate after a pool park: shown, ticking, and with collision (dormancy dropped it).
    TerrainActor->SetActorHiddenInGame(false);
    TerrainActor->SetActorTickEnabled(true);
    TerrainActor->SetActorEnableCollision(true);

    ConfigureOcean(PlanetRadius, NoiseAmplitude, OceanRadius);
    ConfigureGravity(PlanetRadius, OceanRadius);
    // Stage A1: after the ocean, which takes the atmosphere's light as its sun.
    ConfigureAtmosphere(PlanetRadius, OceanRadius);

    RecordAppliedState();
    bInitialized = true;
    SpawnTimeline.Mark(ESpawnMark::Initialize);
}

void APlanetActor::ConfigureOcean(double PlanetRadius, double NoiseAmplitude, double OceanRadius)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(APlanetActor::ConfigureOcean);
    FSpentScope Spent(SpawnTimeline, ESpawnSpent::Ocean);
    if (!OceanActor) return;
    if (!bEnableOcean)
    {
        OceanActor->SetActorHiddenInGame(true);
        OceanActor->SetActorTickEnabled(false);
#if WITH_EDITOR
        // Hidden in game doesn't hide it in the editor's viewports.
        OceanActor->SetIsTemporarilyHiddenInEditor(true);
#endif
        return;
    }
    OceanActor->OceanMaterial = OceanMaterial;
    OceanActor->TerrainPlanetRadius = PlanetRadius;
    OceanActor->NoiseAmplitudeRatio = ChildNoiseAmplitudeRatio(PlanetRadius, NoiseAmplitude);
    OceanActor->SurfaceSettings = SurfaceSettings;
    // Before the initialization, which builds the surface with them.
    OceanActor->OceanSettings = GenerateOceanSettings();
    OceanActor->InitializeFromPlanet(SharedCompositor, PlanetRoot, FVector(OceanRadius));
    OceanActor->SetActorHiddenInGame(false);
    OceanActor->SetActorTickEnabled(true);
#if WITH_EDITOR
    OceanActor->SetIsTemporarilyHiddenInEditor(false);
#endif
}

void APlanetActor::ConfigureGravity(double PlanetRadius, double OceanRadius)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(APlanetActor::ConfigureGravity);
    if (!GravityZone) return;
    if (!bEnableGravity)
    {
        GravityZone->SetActorHiddenInGame(true);
        GravityZone->SetActorEnableCollision(false);
        return;
    }
    // Inner radius = max of ocean/planet radius, so full gravity is reached at the visible
    // surface; set on every update, so a sea level change or a late enable sizes it too.
    GravityZone->SetActorScale3D(FVector(FMath::Max(OceanRadius, PlanetRadius)));
    GravityZone->InitializeFromPlanet();
    GravityZone->SetActorHiddenInGame(false);
    GravityZone->SetActorEnableCollision(true);
}

int32 APlanetActor::GetOceanSeed() const
{
    return OceanSeed >= 0 ? OceanSeed : TerrestrialSettings.Seed;
}

FVoxelOceanSettings APlanetActor::GenerateOceanSettings()
{
    return VoxelOceanSeeding::Generate(GetOceanSeed(), OceanSettings, OceanProfile, OceanLocks, OceanSweepPath, OceanSweepPosition, OceanReport);
}

int32 APlanetActor::GetAtmosphereSeed() const
{
    return AtmosphereSeed >= 0 ? AtmosphereSeed : TerrestrialSettings.Seed;
}

bool APlanetActor::HasAtmosphereSet() const
{
    const FAtmosphereGenerationSet& S = AtmosphereSet;
    return S.AirOnly != nullptr || S.Terrestrial.Look != nullptr || S.GasGiant.Look != nullptr
        || S.Terrestrial.Archetypes.Num() > 0 || S.GasGiant.Archetypes.Num() > 0;
}

namespace VoxelPlanetClimate
{
    /** The land bake's cell holding a planet-local direction (index into its fields). */
    int32 BakeCellOf(const FVoxelLandBake& Bake, const FVector& Dir)
    {
        int32 Face = 0;
        double S = 0.0, T = 0.0;
        VoxelCubeSphere::Locate(Dir, Face, S, T);
        const int32 N = Bake.N;
        const int32 Side = Bake.Side();
        const int32 CI = FMath::Clamp((int32)FMath::FloorToInt((S + 1.0) * 0.5 * N), 0, N - 1);
        const int32 CJ = FMath::Clamp((int32)FMath::FloorToInt((T + 1.0) * 0.5 * N), 0, N - 1);
        return Face * Side * Side + (CJ + FVoxelLandBake::Pad) * Side + CI + FVoxelLandBake::Pad;
    }
}

void APlanetActor::ConfigureAtmosphereSurface()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(APlanetActor::ConfigureAtmosphereSurface);
    if (!AtmosphereActor) return;
    const FVoxelLandBake* Bake = Generator == EVoxelSurfaceGenerator::Terrestrial && LandBake.IsValid() && AtmosphereTerrainHeight > 0.0f
        ? LandBake.Get() : nullptr;
    const uint32 Key = Bake ? (HashCombine(HashCombine(Bake->Key, GetTypeHash(AtmosphereTerrainHeight)), AtmosphereSurfaceWaterHash()) | 1u) : 0u;
    if (Key == LastSurfaceMapKey && (Key == 0u) == (AtmosphereActor->GetSurfaceMap() == nullptr)) return;
    LastSurfaceMapKey = Key;
    if (!Bake)
    {
        AtmosphereActor->SetSurfaceMap(0, 0, TConstArrayView<FVector2f>());
        return;
    }
    // THE WEATHER SIM'S LAYOUT (CloudAtmosphere, APlanetAtmosphereActor::SetSurfaceMap): longitude x
    // sin(latitude) in the planet's frame (Z the spin axis, longitude 0 on +X), row 0 the south.
    // 512 x 256 (the sim's grid at resolution 128; coarser grids average it), each texel the
    // mean of 4 x 4 points over its span: the elevation above sea level over the terrain height
    // (the sea's floor counts as the sea's level), and the land's share (not sea-connected), less
    // the land's surface water: a kept lake whole, a river by its drainage (AtmosphereRiverWater).
    // A river's channel is a cell wide and the points are a few cells apart, so a point takes the
    // largest drainage of its cell and the four beside it.
    const double StartSeconds = FPlatformTime::Seconds();
    constexpr int32 Width = 512;
    constexpr int32 Height = 256;
    constexpr int32 Sub = 4;
    TArray<FVector2f> Cells;
    Cells.SetNumZeroed(Width * Height);
    const bool bAttr = Bake->Attr.Num() == Bake->NumCells();
    const bool bFlow = Bake->Flow.Num() == Bake->NumCells() && AtmosphereRiverWater > 0.0f;
    const float InvHeight = 1.0f / AtmosphereTerrainHeight;
    const int32 Side = Bake->Side();
    // The river ramp, log2 of the drainage (m^2): RiverMinArea (km^2) to 50 times it.
    const float RiverStart = (float)FMath::Log2(FMath::Max(TerrestrialSettings.RiverMinArea, 1.0) * 1.0e6);
    const float RiverFull = RiverStart + (float)FMath::Log2(50.0);
    const float RiverWater = FMath::Clamp(AtmosphereRiverWater, 0.0f, 1.0f);
    ParallelFor(Height, [&](int32 J)
        {
            for (int32 I = 0; I < Width; I++)
            {
                float Elevation = 0.0f;
                float Land = 0.0f;
                for (int32 SJ = 0; SJ < Sub; SJ++)
                {
                    const double Mu = -1.0 + 2.0 * ((double)J + ((double)SJ + 0.5) / Sub) / Height;
                    const double Cos = FMath::Sqrt(FMath::Max(1.0 - Mu * Mu, 0.0));
                    for (int32 SI = 0; SI < Sub; SI++)
                    {
                        const double Lon = 2.0 * UE_DOUBLE_PI * ((double)I + ((double)SI + 0.5) / Sub) / Width;
                        const FVector Dir(Cos * FMath::Cos(Lon), Cos * FMath::Sin(Lon), Mu);
                        const int32 Index = VoxelPlanetClimate::BakeCellOf(*Bake, Dir);
                        const float H = Bake->Height[Index];
                        const bool bSea = bAttr ? (Bake->Attr[Index] & FVoxelLandBake::SeaBit) != 0 : H <= 0.0f;
                        Elevation += FMath::Clamp(H * InvHeight, 0.0f, 1.0f);
                        if (bSea) continue;
                        float Water = 0.0f;
                        if (bAttr && (Bake->Attr[Index] & FVoxelLandBake::LakeBit) != 0)
                        {
                            Water = 1.0f;
                        }
                        else if (bFlow)
                        {
                            // The bake's cells keep a pad of at least one around each face's interior.
                            const float Flow = FMath::Max(FMath::Max((float)Bake->Flow[Index], FMath::Max((float)Bake->Flow[Index - 1], (float)Bake->Flow[Index + 1])),
                                FMath::Max((float)Bake->Flow[Index - Side], (float)Bake->Flow[Index + Side]));
                            const float T = FMath::Clamp((Flow - RiverStart) / (RiverFull - RiverStart), 0.0f, 1.0f);
                            Water = RiverWater * T * T * (3.0f - 2.0f * T);
                        }
                        Land += 1.0f - Water;
                    }
                }
                Cells[J * Width + I] = FVector2f(Elevation / (Sub * Sub), Land / (Sub * Sub));
            }
        });
    AtmosphereActor->SetSurfaceMap(Width, Height, Cells);
    UE_LOG(LogVoxelPlanets, Log, TEXT("[Planet] %s: the atmosphere's surface map (%dx%d from the land bake, terrain height %.0f m) in %.0f ms."),
        *GetName(), Width, Height, AtmosphereTerrainHeight, (FPlatformTime::Seconds() - StartSeconds) * 1000.0);
}

TSharedPtr<const FAtmosphereClimateMap, ESPMode::ThreadSafe> APlanetActor::GetClimateMap() const
{
    return AtmosphereActor ? AtmosphereActor->GetClimateFeed()->Latest() : nullptr;
}

FVoxelPlanetClimate APlanetActor::GetClimateAt(const FVector& WorldLocation) const
{
    // The planet-local direction (the frame the bake and the atmosphere's sim share).
    const FTransform Frame = PlanetRoot ? PlanetRoot->GetComponentTransform() : GetActorTransform();
    const FVector Dir = Frame.InverseTransformVectorNoScale(WorldLocation - Frame.GetLocation()).GetSafeNormal();
    if (Dir.IsNearlyZero()) return FVoxelPlanetClimate();
    return ClimateAtLocal(Dir, nullptr, FVector::ZeroVector);
}

FVoxelPlanetClimate APlanetActor::GetSurfaceClimateAt(const FVector& WorldLocation, const FVector& SurfaceNormal) const
{
    const FTransform Frame = PlanetRoot ? PlanetRoot->GetComponentTransform() : GetActorTransform();
    const FVector LocalM = Frame.InverseTransformVectorNoScale(WorldLocation - Frame.GetLocation()) * 0.01;
    const FVector Dir = LocalM.GetSafeNormal();
    if (Dir.IsNearlyZero()) return FVoxelPlanetClimate();
    const FVector Normal = Frame.InverseTransformVectorNoScale(SurfaceNormal).GetSafeNormal();
    return ClimateAtLocal(Dir, &LocalM, Normal);
}

FVoxelPlanetClimate APlanetActor::ClimateAtLocal(const FVector& Dir, const FVector* SurfaceLocalM, const FVector& NormalLocal) const
{
    FVoxelPlanetClimate Out;
    const TSharedPtr<const FVoxelLandBake, ESPMode::ThreadSafe> Ground = TerrainBake();
    if (Generator == EVoxelSurfaceGenerator::Terrestrial && Ground.IsValid() && Ground->Height.Num() == Ground->NumCells())
        Out.Elevation = FMath::Max(Ground->Height[VoxelPlanetClimate::BakeCellOf(*Ground, Dir)], 0.0f);
    // The latitude's profile (stage T5a: |sin(latitude)|^ClimateLatitudeExponent; A11's sin^2 at 2).
    const double Profile = FMath::Pow(FMath::Abs(FMath::Clamp(Dir.Z, -1.0, 1.0)), (double)FMath::Max(ClimateLatitudeExponent, 0.1f));
    float Temperature = (float)(ClimateEquatorTemperature + (ClimatePoleTemperature - ClimateEquatorTemperature) * Profile)
        - ClimateLapseRate * Out.Elevation * 0.001f;
    if (const TSharedPtr<const FAtmosphereClimateMap, ESPMode::ThreadSafe> Map = GetClimateMap(); Map.IsValid())
    {
        const FVector4f C = Map->Sample(Dir);
        Out.Precipitation = FMath::Max(C.X, 0.0f);
        Out.Humidity = FMath::Clamp(C.Y, 0.0f, 1.0f);
        Out.CloudCover = FMath::Clamp(C.Z, 0.0f, 1.0f);
        Temperature += ClimateWarmthScale * C.W;
        Out.bFromSim = true;
    }
    Out.Temperature = Temperature;
    // Stage T5a: the land's climate there, as the terrain material has it: on flat ground at the
    // bake's height, or at the surface point given.
    if (ClimateShown.IsValid())
    {
        const double SeaRadiusM = GetActorScale3D().GetMax() * 0.01;
        const FVector LocalM = SurfaceLocalM ? *SurfaceLocalM : Dir * (SeaRadiusM + Out.Elevation);
        const FVoxelLandClimatePointResult P = LandClimatePointAt(LocalM, SurfaceLocalM ? NormalLocal : Dir);
        Out.bFromLandClimate = true;
        Out.Temperature = P.Temperature;
        Out.SummerTemperature = P.Summer;
        Out.PrecipitationMm = P.Precipitation;
        Out.Aridity = P.Aridity;
        Out.Snow = P.Snow;
        Out.Glacier = P.Glacier;
        Out.Desert = P.Desert;
        Out.Vegetation = P.Vegetation;
    }
    return Out;
}

FVoxelLandClimatePointResult APlanetActor::LandClimatePointAt(const FVector& LocalM, const FVector& NormalLocal) const
{
    // The drainage there for the riparian wetting (the bake's cell; the material's is the land
    // node's, interpolated).
    float Flow = 0.0f;
    const TSharedPtr<const FVoxelLandBake, ESPMode::ThreadSafe> Ground = TerrainBake();
    if (Generator == EVoxelSurfaceGenerator::Terrestrial && Ground.IsValid() && Ground->Flow.Num() == Ground->NumCells() && !LocalM.IsNearlyZero())
        Flow = (float)Ground->Flow[VoxelPlanetClimate::BakeCellOf(*Ground, LocalM.GetSafeNormal())];
    return VoxelLandClimate::PointAt(ClimateShown, LandClimate, ClimateLapseRate, LocalM, NormalLocal, GetActorScale3D().GetMax() * 0.01, Flow);
}

uint32 APlanetActor::AtmosphereSurfaceWaterHash() const
{
    return HashCombine(GetTypeHash(AtmosphereRiverWater), GetTypeHash(TerrestrialSettings.RiverMinArea));
}

void APlanetActor::LogClimate()
{
    const TSharedPtr<const FAtmosphereClimateMap, ESPMode::ThreadSafe> Map = GetClimateMap();
    if (!LandClimateStatus.IsEmpty())
        UE_LOG(LogVoxelPlanets, Log, TEXT("[Planet] %s land climate: %s"), *GetName(), *LandClimateStatus);
    if (!Map.IsValid() && !ClimateShown.IsValid())
    {
        UE_LOG(LogVoxelPlanets, Log, TEXT("[Planet] %s: no climate yet (the atmosphere's sim gives one every ClimateReadbackSeconds while this planet drives it)."), *GetName());
        return;
    }
    // A grid even in area (longitude x sin(latitude)), land and sea apart by the bake's sea.
    struct FStats
    {
        double N = 0, T = 0, P = 0, H = 0, C = 0;
        float TMin = 1e9f, TMax = -1e9f, PMin = 1e9f, PMax = -1e9f;
        // Stage T5a: the land climate's (mm a year; the cover's shares above a half).
        double Mm = 0, Snow = 0, Glacier = 0, Desert = 0, Plants = 0;
        TArray<float> MmAll;
    };
    FStats Land, Sea;
    const FTransform Frame = PlanetRoot ? PlanetRoot->GetComponentTransform() : GetActorTransform();
    const bool bBake = Generator == EVoxelSurfaceGenerator::Terrestrial && LandBake.IsValid() && LandBake->Attr.Num() == LandBake->NumCells();
    constexpr int32 W = 128, H = 64;
    for (int32 J = 0; J < H; J++)
    {
        const double Mu = -1.0 + 2.0 * (J + 0.5) / H;
        const double Cos = FMath::Sqrt(FMath::Max(1.0 - Mu * Mu, 0.0));
        for (int32 I = 0; I < W; I++)
        {
            const double Lon = 2.0 * UE_DOUBLE_PI * (I + 0.5) / W;
            const FVector Dir(Cos * FMath::Cos(Lon), Cos * FMath::Sin(Lon), Mu);
            const FVoxelPlanetClimate C = GetClimateAt(Frame.GetLocation() + Frame.TransformVectorNoScale(Dir) * 100.0);
            const bool bSea = bBake ? (LandBake->Attr[VoxelPlanetClimate::BakeCellOf(*LandBake, Dir)] & FVoxelLandBake::SeaBit) != 0 : true;
            FStats& S = bSea ? Sea : Land;
            S.N += 1.0; S.T += C.Temperature; S.P += C.Precipitation; S.H += C.Humidity; S.C += C.CloudCover;
            S.TMin = FMath::Min(S.TMin, C.Temperature); S.TMax = FMath::Max(S.TMax, C.Temperature);
            S.PMin = FMath::Min(S.PMin, C.Precipitation); S.PMax = FMath::Max(S.PMax, C.Precipitation);
            if (C.bFromLandClimate)
            {
                S.Mm += C.PrecipitationMm;
                S.MmAll.Add(C.PrecipitationMm);
                S.Snow += C.Snow > 0.5f ? 1.0 : 0.0;
                S.Glacier += C.Glacier > 0.5f ? 1.0 : 0.0;
                S.Desert += C.Desert > 0.5f ? 1.0 : 0.0;
                S.Plants += C.Vegetation;
            }
        }
    }
    if (Land.MmAll.Num() > 0)
    {
        Land.MmAll.Sort();
        const auto Pct = [&Land](double Q) { return Land.MmAll[FMath::Clamp((int32)(Q * Land.MmAll.Num()), 0, Land.MmAll.Num() - 1)]; };
        const double N = (double)Land.MmAll.Num();
        UE_LOG(LogVoxelPlanets, Log, TEXT("[Planet] %s land climate, land: precipitation %.0f mm (10%% %.0f, median %.0f, 90%% %.0f); snow %.0f%%, glacier %.0f%%, desert %.0f%%, plant cover %.2f (flat ground at the bake's height)."),
            *GetName(), Land.Mm / N, Pct(0.1), Pct(0.5), Pct(0.9), 100.0 * Land.Snow / N, 100.0 * Land.Glacier / N, 100.0 * Land.Desert / N, Land.Plants / N);
    }
    if (!Map.IsValid()) return;
    const auto Report = [this](const TCHAR* Name, const FStats& S)
        {
            if (S.N <= 0.0) return;
            UE_LOG(LogVoxelPlanets, Log, TEXT("[Planet] %s climate, %s (%.0f%%): temperature %.1f C (%.1f to %.1f), precipitation %.3f (%.3f to %.3f), humidity %.2f, cloud %.2f."),
                *GetName(), Name, 100.0 * S.N / (128.0 * 64.0), S.T / S.N, S.TMin, S.TMax, S.P / S.N, S.PMin, S.PMax, S.H / S.N, S.C / S.N);
        };
    Report(TEXT("land"), Land);
    Report(TEXT("sea"), Sea);
    UE_LOG(LogVoxelPlanets, Log, TEXT("[Planet] %s climate: map %u (%dx%d)."), *GetName(), (uint32)Map->Serial, Map->Width, Map->Height);
}

void APlanetActor::RecaptureClimate()
{
    // A new capture on the next tick (the sim restarted for it); the shown climate stays until it lands.
    ClimateCaptureState = EVoxelPlanetClimateCapture::None;
}

uint32 APlanetActor::CurrentClimateBaseKey() const
{
    if (!LandBake.IsValid()) return 0u;
    const double SeaRadiusM = GetActorScale3D().GetMax() * 0.01;
    return HashCombine(LandBake->Key, GetTypeHash(SeaRadiusM)) | 1u;
}

bool APlanetActor::EnsureLandClimateBase()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(APlanetActor::EnsureLandClimateBase);
    if (Generator != EVoxelSurfaceGenerator::Terrestrial || !LandBake.IsValid() || LandBake->Height.Num() != LandBake->NumCells())
        return false;
    // The base for the bake (the land's share and the distance to the sea).
    const double SeaRadiusM = GetActorScale3D().GetMax() * 0.01;
    const uint32 BaseKey = CurrentClimateBaseKey();
    if (BaseKey != ClimateBaseKey || ClimateLand.Num() != VoxelLandClimate::Width() * VoxelLandClimate::Height())
    {
        const double StartSeconds = FPlatformTime::Seconds();
        FSpentScope Spent(SpawnTimeline, ESpawnSpent::ClimateBase);
        VoxelLandClimate::BuildBase(*LandBake, SeaRadiusM, ClimateLand, ClimateCoast, &ClimateHeight);
        UE_LOG(LogVoxelPlanets, Log, TEXT("[Planet] %s: the land climate's base (%dx%d: the land's share, the distance to the sea) in %.0f ms."),
            *GetName(), VoxelLandClimate::Width(), VoxelLandClimate::Height(), (FPlatformTime::Seconds() - StartSeconds) * 1000.0);
        ClimateBaseKey = BaseKey;
    }
    return true;
}

bool APlanetActor::UpdateClimateCapture()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(APlanetActor::UpdateClimateCapture);
    if (!LandClimate.bEnabled || !EnsureLandClimateBase()) return true;

    // ONE SNAPSHOT PER SEED (Design/LandClimate.md): what the climate is captured for, the bake and
    // the atmosphere (seed, model, on), and how (the source). A change captures again, once.
    // (An air-only atmosphere never drives the sim: the provisional model at once, not after
    // ClimateWaitSeconds: R1, PLN-3.)
    const bool bSnapshot = LandClimate.Source == EVoxelLandClimateSource::Snapshot && bEnableAtmosphere
        && AtmosphereType != EPlanetAtmosphereType::AirOnly;
    uint32 Key = HashCombine(ClimateBaseKey, GetTypeHash((uint8)LandClimate.Source));
    if (bSnapshot)
        Key = HashCombine(Key, HashCombine(HashCombine(GetTypeHash(GetAtmosphereSeed()), GetTypeHash((uint8)AtmosphereType)),
            HashCombine(GetTypeHash(AtmosphereTerrainHeight), AtmosphereSurfaceWaterHash())));
    if (Key == ClimateCaptureKey && ClimateCaptureState == EVoxelPlanetClimateCapture::Captured) return true;

    const double Now = FPlatformTime::Seconds();
    if (Key != ClimateCaptureKey || ClimateCaptureState == EVoxelPlanetClimateCapture::None)
    {
        ClimateCaptureKey = Key;
        ClimateSim.Reset();
        if (!bSnapshot)
        {
            ClimateCaptureState = EVoxelPlanetClimateCapture::Captured;
            RebuildLandClimateTarget();
            LandClimateStatus = !bEnableAtmosphere ? TEXT("Provisional model (no atmosphere).")
                : AtmosphereType == EPlanetAtmosphereType::AirOnly && LandClimate.Source == EVoxelLandClimateSource::Snapshot
                    ? TEXT("Provisional model (an air-only atmosphere runs no sim).") : TEXT("Provisional model (the source asks for it).");
            return true;
        }
        // The atmosphere at the planet with this bake's ground (its surface map), and its sim
        // restarted: a spin-up, the climate window, then the snapshot (CloudAtmosphere T5b). Before
        // the first build too: the children are spawned (the terrain isn't built until the capture).
        ClimateCaptureState = EVoxelPlanetClimateCapture::Waiting;
        ClimateCaptureStart = Now;
        SpawnTimeline.Mark(ESpawnMark::ClimateStart);
        ClimateProgressTime = Now;
        ClimateProgressSteps = -1;
        ClimateOtherOwnerSince = 0.0;
        SpawnChildActors();
        if (AtmosphereActor)
        {
            double PlanetRadius = 0.0, NoiseAmplitude = 0.0, OceanRadius = 0.0;
            ComputeShape(PlanetRadius, NoiseAmplitude, OceanRadius);
            ConfigureAtmosphere(PlanetRadius, OceanRadius);
            AtmosphereActor->RestartSimulation();
        }
        // The provisional model shows until the snapshot (nothing shown yet: at once).
        if (!ClimateShown.IsValid())
            RebuildLandClimateTarget();
        LandClimateStatus = TEXT("Waiting for the climate snapshot (the atmosphere spins up, then averages its climate window).");
        UE_LOG(LogVoxelPlanets, Log, TEXT("[Planet] %s: capturing the climate (the atmosphere's spin-up and climate window)."), *GetName());
    }

    // Waiting: the first snapshot requested since the capture began (one requested before is an
    // earlier sim's: the old ground or atmosphere).
    // A build waiting on the capture shows its climate at once (no fade from the provisional
    // model, or from the old seed's, over the new terrain).
    const bool bForBuild = bPendingInitialize && LandClimate.bWaitForClimate;
    auto ShowAtOnce = [this, bForBuild]()
        {
            if (!bForBuild) return;
            ClimateShown.Cells.Empty();
            ClimateFrom.Empty();
            ClimateTarget.Cells.Empty();
        };
    const TSharedPtr<const FAtmosphereClimateMap, ESPMode::ThreadSafe> Latest = GetClimateMap();
    if (Latest.IsValid() && Latest->bSnapshot && Latest->RequestSeconds > ClimateCaptureStart)
    {
        ClimateSim = Latest;
        ClimateCaptureState = EVoxelPlanetClimateCapture::Captured;
        ShowAtOnce();
        RebuildLandClimateTarget();
        // Stage P1i: the status says whether the sim's climate was used, which only the map knows:
        // written (and logged) when the field job lands (TickClimateFieldJob).
        ClimateSnapshotStatusDelay = Latest->RequestSeconds - ClimateCaptureStart;
        LandClimateStatus = TEXT("Snapshot captured; making the land's climate from it.");
        return true;
    }
    // No snapshot coming. The provisional model, kept for this seed (the terrain mustn't change
    // later); RecaptureClimate tries again:
    //   - this planet drives the sim but it can't give one (paused, its config's snapshot off, the
    //     climate's read-back off): at once;
    //   - another planet drives it (nearer the camera) for 3 s on end (not counted while no planet
    //     does: the atmospheres bid once a view has rendered);
    //   - the sim stopped stepping for ClimateWaitSeconds (wall time between steps, so a slow
    //     editor frame rate doesn't run it out).
    UWorld* World = GetWorld();
    const UFlowSimSubsystem* Sim = World ? World->GetSubsystem<UFlowSimSubsystem>() : nullptr;
    const bool bDriving = AtmosphereActor && AtmosphereActor->DrivesSimulation();
    if (Sim && Sim->GetStepsCompleted() != ClimateProgressSteps)
    {
        ClimateProgressSteps = Sim->GetStepsCompleted();
        ClimateProgressTime = Now;
    }
    const bool bOtherOwner = Sim && Sim->HasOwner() && !bDriving;
    if (!bOtherOwner)
        ClimateOtherOwnerSince = 0.0;
    else if (ClimateOtherOwnerSince <= 0.0)
        ClimateOtherOwnerSince = Now;
    FString Reason;
    if (bDriving && !AtmosphereActor->CanProvideClimateSnapshot())
        Reason = TEXT("the sim can't give one (paused, its config's bClimateSnapshot off, or the atmosphere's ClimateReadbackSeconds 0)");
    else if (!bDriving && AtmosphereActor && !AtmosphereActor->BidsForSimulation())
        Reason = TEXT("the atmosphere doesn't drive a sim (air only, its claiming off, or no sim config)");
    else if (bOtherOwner && Now - ClimateOtherOwnerSince > 3.0)
        Reason = TEXT("this planet's atmosphere doesn't drive the sim (another planet is nearer the camera)");
    else if (Now - ClimateProgressTime > (double)LandClimate.ClimateWaitSeconds)
        Reason = FString::Printf(TEXT("the sim didn't step for %.0f s (ClimateWaitSeconds)"), LandClimate.ClimateWaitSeconds);
    if (!Reason.IsEmpty())
    {
        ClimateCaptureState = EVoxelPlanetClimateCapture::Captured;
        ClimateSim.Reset();
        ShowAtOnce();
        RebuildLandClimateTarget();
        LandClimateStatus = FString::Printf(TEXT("Provisional model: no snapshot, %s; kept for this seed."), *Reason);
        UE_LOG(LogVoxelPlanets, Warning, TEXT("[Planet] %s: %s"), *GetName(), *LandClimateStatus);
        return true;
    }
    return false;
}

void APlanetActor::RebuildLandClimateTarget()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(APlanetActor::RebuildLandClimateTarget);
    FSpentScope Spent(SpawnTimeline, ESpawnSpent::Field);
    if (ClimateLand.Num() != VoxelLandClimate::Width() * VoxelLandClimate::Height()) return;
    // T5e: the ice shelf's share in the map (the material's whitening, the chain's ice and rivers).
    const bool bShelf = IceShelfOn() && LandBake.IsValid();
    const FVoxelLandShelfParams Shelf = VoxelLandClimate::ShelfParamsFor(TerrestrialSettings, LandBake.IsValid() ? LandBake->PlateSeed : 0u);
    ClimateMapHash = LandClimateMapHash();

    // Stage P1i (S2): what the map and the field are made from (the base by its key, the sim's
    // climate by its pointer: each map is new); a job for the same is waited for, not repeated.
    uint32 Key = HashCombine(ClimateBaseKey, ClimateMapHash);
    Key = HashCombine(Key, PointerHash(ClimateSim.Get()));
    Key = HashCombine(Key, GetTypeHash(ClimateLapseRate));
    Key = HashCombine(Key, GetTypeHash(TerrestrialSettings.IceSheetMaxThickness));
    Key = HashCombine(Key, GetTypeHash(bShelf));
    Key |= 1u;
    if (ClimateFieldFuture.IsValid())
    {
        if (ClimateFieldFutureKey == Key) return;
        ClimateFieldDiscarded.Add(MoveTemp(ClimateFieldFuture));
        ClimateFieldFuture = TFuture<TSharedPtr<FClimateFieldResult, ESPMode::ThreadSafe>>();
    }

    // The same two calls with the same inputs as before P1i, on the thread pool: the base copied
    // (a few hundred KB), the sim's climate held.
    ClimateFieldFutureKey = Key;
    ClimateFieldFutureBaseKey = ClimateBaseKey;
    ClimateFieldFuture = Async(EAsyncExecution::ThreadPool,
        [Land = ClimateLand, Coast = ClimateCoast, Height = ClimateHeight, Sim = ClimateSim, Settings = LandClimate,
            SeaRadiusM = GetActorScale3D().GetMax() * 0.01, Equator = ClimateEquatorTemperature, Pole = ClimatePoleTemperature,
            Exponent = ClimateLatitudeExponent, Warmth = ClimateWarmthScale, Lapse = ClimateLapseRate,
            IceMax = (float)TerrestrialSettings.IceSheetMaxThickness, bShelf, Shelf]() -> TSharedPtr<FClimateFieldResult, ESPMode::ThreadSafe>
        {
            TSharedPtr<FClimateFieldResult, ESPMode::ThreadSafe> Out = MakeShared<FClimateFieldResult, ESPMode::ThreadSafe>();
            Out->bFromSim = VoxelLandClimate::BuildMap(Land, Coast, Sim.Get(), Settings, SeaRadiusM,
                Equator, Pole, Exponent, Warmth, Out->Map, &Height, bShelf ? &Shelf : nullptr);
            // Stage T5c: the land chain's climate, from the target (the sim's drift with it, or the
            // provisional winds).
            Out->Field = VoxelLandClimate::MakeField(Out->Map, Out->bFromSim ? Sim.Get() : nullptr, Lapse, Land, Height, SeaRadiusM, IceMax);
            return Out;
        });
}

bool APlanetActor::TickClimateFieldJob()
{
    // Superseded jobs, let go once done (here, on the game thread).
    for (int32 i = ClimateFieldDiscarded.Num() - 1; i >= 0; i--)
        if (!ClimateFieldDiscarded[i].IsValid() || ClimateFieldDiscarded[i].IsReady())
            ClimateFieldDiscarded.RemoveAtSwap(i);
    if (!ClimateFieldFuture.IsValid()) return false;
    if (!ClimateFieldFuture.IsReady()) return true;
    TRACE_CPUPROFILER_EVENT_SCOPE(APlanetActor::TickClimateFieldJob);
    const TSharedPtr<FClimateFieldResult, ESPMode::ThreadSafe> Result = ClimateFieldFuture.Get();
    ClimateFieldFuture = TFuture<TSharedPtr<FClimateFieldResult, ESPMode::ThreadSafe>>();
    if (!Result.IsValid()) return false;

    bClimateFromSim = Result->bFromSim;
    LandClimateField = Result->Field;
    LandClimateFieldBaseKey = ClimateFieldFutureBaseKey;
    if (ClimateSnapshotStatusDelay >= 0.0)
    {
        LandClimateStatus = bClimateFromSim
            ? FString::Printf(TEXT("Snapshot: the sim's climate, captured %.1f s after the request; kept for this seed."), ClimateSnapshotStatusDelay)
            : FString(TEXT("Provisional model: the sim's snapshot had no precipitation."));
        UE_LOG(LogVoxelPlanets, Log, TEXT("[Planet] %s: %s"), *GetName(), *LandClimateStatus);
        ClimateSnapshotStatusDelay = -1.0;
    }
    if (!ClimateShown.IsValid() || LandClimate.FadeSeconds <= 0.0f)
    {
        // The first map (or no fade): at once.
        ClimateShown = MoveTemp(Result->Map);
        ClimateFrom.Empty();
        ClimateTarget.Cells.Empty();
        ClimateFade = 1.0f;
        UploadLandClimate();
        return false;
    }
    // Fades in from what is shown now (mid-fade, from the blend).
    ClimateFrom = ClimateShown.Cells;
    ClimateTarget = MoveTemp(Result->Map);
    ClimateFade = 0.0f;
    return false;
}

bool APlanetActor::IceShelfOn() const
{
    return Generator == EVoxelSurfaceGenerator::Terrestrial && LandClimate.bEnabled && TerrestrialSettings.IceShelfHeight > 0.0;
}

bool APlanetActor::UpdateIceShelf()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(APlanetActor::UpdateIceShelf);
    FSpentScope Spent(SpawnTimeline, ESpawnSpent::Shelf);
    // (A field made for the current bake only: see LandClimateFieldBaseKey.)
    if (!IceShelfOn() || !LandBake.IsValid() || !LandClimateField.IsValid() || !LandClimateField->IsValid()
        || LandClimateFieldBaseKey != CurrentClimateBaseKey())
    {
        ShelfBake.Reset();
        ShelfBakeHash = 0;
        // (One in flight finishes unread: it holds its inputs.)
        ShelfFuture = TFuture<TSharedPtr<const FVoxelLandBake, ESPMode::ThreadSafe>>();
        ShelfFutureHash = 0;
        return true;
    }
    // (The field's sea-level temperatures, not its whole hash: R1, BAKE-3.)
    uint32 Hash = HashCombine(LandBake->Key, LandClimateField->SeaTemperatureHash);
    for (const double V : { TerrestrialSettings.IceShelfHeight, TerrestrialSettings.IceShelfTemperature, TerrestrialSettings.IceShelfFrontNoise,
        TerrestrialSettings.IceShelfRise, TerrestrialSettings.IceShelfRoll, TerrestrialSettings.IceShelfDraft })
        Hash = HashCombine(Hash, ::GetTypeHash(V));
    Hash |= 1u;
    if (ShelfBake.IsValid() && ShelfBakeHash == Hash)
    {
        // The same shelf: only the rivers may be new (other river settings: LandBake's, remade by
        // TickLandBake).
        TSharedPtr<const FVoxelLandRiverSet, ESPMode::ThreadSafe> Rivers;
        {
            FScopeLock RiverLock(&LandBake->RiverLock);
            Rivers = LandBake->Rivers;
        }
        {
            FScopeLock ShelfLock(&ShelfBake->RiverLock);
            ShelfBake->Rivers = Rivers;
        }
        // (A river rebuild uploaded the planet's bake's fields again: let go, as below.)
        VoxelLand::ReleaseBakeGPU(*LandBake);
        return true;
    }
    // Stage P1d: on the thread pool (MakeIceShelfBake runs on any thread: it reads its inputs,
    // holds them by pointer and value, and takes Base's rivers under their lock). The same
    // function with the same inputs: the same shelf, a few frames later and off the game thread.
    if (!ShelfFuture.IsValid() || ShelfFutureHash != Hash)
    {
        ShelfFutureHash = Hash;
        ShelfFuture = Async(EAsyncExecution::ThreadPool,
            [Base = LandBake, Field = LandClimateField, Settings = TerrestrialSettings]() -> TSharedPtr<const FVoxelLandBake, ESPMode::ThreadSafe>
            {
                return VoxelLandClimate::MakeIceShelfBake(*Base, *Field, Settings);
            });
        return false;
    }
    if (!ShelfFuture.IsReady())
        return false;
    ShelfBake = ShelfFuture.Get();
    ShelfFuture = TFuture<TSharedPtr<const FVoxelLandBake, ESPMode::ThreadSafe>>();
    ShelfFutureHash = 0;
    ShelfBakeHash = ShelfBake.IsValid() ? Hash : 0u;
    // The terrain samples the shelf's bake from now on (its own GPU copy, uploaded on first use):
    // the planet's bake's GPU fields (the solve's, 64 MB at N 1024) would stay resident unread
    // for the planet's life (R1, BAKE-1). Not while its rivers are being built (TickRivers reads
    // them): TickLandBake finished them before this.
    if (ShelfBake.IsValid())
        VoxelLand::ReleaseBakeGPU(*LandBake);
    return true;
}

bool APlanetActor::IsLandClimateFieldStale() const
{
    // The ice block holds no cap (the density's): only whether the ice sheets are on.
    return LandClimateField.IsValid() && (LandClimateField->LapseRate != ClimateLapseRate
        || (LandClimateField->IceMaxThickness > 0.0f) != (TerrestrialSettings.IceSheetMaxThickness > 0.0));
}

TSharedPtr<const FVoxelLandClimateField, ESPMode::ThreadSafe> APlanetActor::DuneClimate() const
{
    // The layers placed by the climate: the dunes (T5c) and the glacial layer (T5d).
    // T5d2: and the ice sheets (no set needed: their swells come from one if the pack has it).
    // T5d3: and the polar surfaces (with a polar set).
    const bool bPolar = LandNoise.IsValid() && LandNoise->PolarN > 0
        && (TerrestrialSettings.CrevasseHeightScale > 0.0 || TerrestrialSettings.SastrugiHeightScale > 0.0 || TerrestrialSettings.PolygonHeightScale > 0.0
            || TerrestrialSettings.SnowDuneHeight > 0.0);
    // T5e: and the ice shelf (the field's map places it, and the chain reads its share).
    const bool bLayers = LandClimate.bEnabled && (TerrestrialSettings.IceSheetMaxThickness > 0.0 || bPolar || IceShelfOn() || (LandNoise.IsValid()
        && ((LandNoise->DuneN > 0 && TerrestrialSettings.DuneHeightScale > 0.0) || (LandNoise->GlacialN > 0 && TerrestrialSettings.GlacialHeightScale > 0.0))));
    return bLayers && LandClimateField.IsValid() && LandClimateField->IsValid() ? LandClimateField : nullptr;
}

uint32 APlanetActor::DuneClimateHash() const
{
    const TSharedPtr<const FVoxelLandClimateField, ESPMode::ThreadSafe> Field = DuneClimate();
    return Field.IsValid() ? (Field->Hash | 1u) : 0u;
}

uint32 APlanetActor::LandClimateMapHash() const
{
    uint32 Hash = HashCombine(LandClimate.GetMapHash(), GetTypeHash(ClimateEquatorTemperature));
    Hash = HashCombine(Hash, GetTypeHash(ClimatePoleTemperature));
    Hash = HashCombine(Hash, GetTypeHash(ClimateLatitudeExponent));
    Hash = HashCombine(Hash, GetTypeHash(ClimateWarmthScale));
    // T5e: the ice shelf's share in the cells.
    if (IceShelfOn())
    {
        Hash = HashCombine(Hash, GetTypeHash(TerrestrialSettings.IceShelfTemperature));
        Hash = HashCombine(Hash, GetTypeHash(TerrestrialSettings.IceShelfFrontNoise));
        Hash = HashCombine(Hash, LandBake.IsValid() ? LandBake->PlateSeed : 0u);
    }
    return Hash;
}

void APlanetActor::UploadLandClimate()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(APlanetActor::UploadLandClimate);
    if (!ClimateShown.IsValid()) return;
    if (!LandClimateTexture)
        LandClimateTexture = VoxelLandMaterial::MakeClimateTexture();
    VoxelLandMaterial::UploadClimateTexture(LandClimateTexture, ClimateShown.Cells);
    ClimateUploadTime = FPlatformTime::Seconds();
}

void APlanetActor::TickLandClimate(float DeltaTime)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(APlanetActor::TickLandClimate);
    const bool bOn = LandClimate.bEnabled && bInitialized && TerrainActor && Generator == EVoxelSurfaceGenerator::Terrestrial
        && LandBake.IsValid() && LandBake->Height.Num() == LandBake->NumCells();
    if (!bOn)
    {
        // Off: the terrain's node off, the map gone (it's made again when on).
        if (TerrainActor && (ClimateTextureSent.IsValid() || ClimateTerrainSent.Get() != TerrainActor))
            TerrainActor->SetLandClimate(nullptr, FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f), FLinearColor(0.0f, 0.0f, 0.0f, 0.0f));
        ClimateTextureSent.Reset();
        ClimateTerrainSent = TerrainActor;
        ClimateParamsSent = FLinearColor(-1.0f, 0.0f, 0.0f, 0.0f);
        if (ClimateShown.IsValid() || ClimateBaseKey != 0u)
        {
            ClimateShown.Cells.Empty();
            ClimateFrom.Empty();
            ClimateTarget.Cells.Empty();
            ClimateLand.Empty();
            ClimateCoast.Empty();
            ClimateHeight.Empty();
            ClimateSim.Reset();
            LandClimateField.Reset();
            // (Stage P1i: a field under way finishes unread.)
            if (ClimateFieldFuture.IsValid())
                ClimateFieldDiscarded.Add(MoveTemp(ClimateFieldFuture));
            ClimateFieldFuture = TFuture<TSharedPtr<FClimateFieldResult, ESPMode::ThreadSafe>>();
            ClimateSnapshotStatusDelay = -1.0;
            ClimateBaseKey = 0u;
            ClimateCaptureKey = 0u;
            ClimateCaptureState = EVoxelPlanetClimateCapture::None;
            bClimateFromSim = false;
            LandClimateStatus = LandClimate.bEnabled ? TEXT("Off (terrestrial planets only, once built).") : TEXT("Off.");
        }
        return;
    }

    // Stage P1i: a field made since the last tick.
    TickClimateFieldJob();

    // The capture (the build waited for it unless bWaitForClimate is off; a change after the build,
    // such as a new atmosphere seed, captures again here). Then the map's own settings.
    UpdateClimateCapture();
    if (ClimateMapHash != LandClimateMapHash() || IsLandClimateFieldStale())
        RebuildLandClimateTarget();

    // The fade: the shown map toward the new one, uploaded a few times a second.
    if (ClimateFade < 1.0f && ClimateTarget.IsValid() && ClimateFrom.Num() == ClimateTarget.Cells.Num())
    {
        ClimateFade = LandClimate.FadeSeconds > 0.0f ? FMath::Min(ClimateFade + DeltaTime / LandClimate.FadeSeconds, 1.0f) : 1.0f;
        if (ClimateFade >= 1.0f || FPlatformTime::Seconds() - ClimateUploadTime >= 0.2)
        {
            const float A = FMath::SmoothStep(0.0f, 1.0f, ClimateFade);
            ClimateShown.Cells.SetNumUninitialized(ClimateTarget.Cells.Num());
            for (int32 i = 0; i < ClimateTarget.Cells.Num(); i++)
                ClimateShown.Cells[i] = ClimateFrom[i] + (ClimateTarget.Cells[i] - ClimateFrom[i]) * A;
            UploadLandClimate();
            if (ClimateFade >= 1.0f)
            {
                ClimateShown = MoveTemp(ClimateTarget);
                ClimateTarget.Cells.Empty();
                ClimateFrom.Empty();
            }
        }
    }

    // The terrain's climate node: the map and the look (sent when they change).
    const double SeaRadiusM = GetActorScale3D().GetMax() * 0.01;
    const FLinearColor Params(1.0f, (float)SeaRadiusM, ClimateLapseRate, LandClimate.Season);
    const FLinearColor Shape(LandClimate.AspectWarming, LandClimate.BreakupAmplitude, LandClimate.BreakupScale, LandClimate.SnowWidth);
    const FLinearColor Tint(LandClimate.SnowTint, LandClimate.GlacierTint, LandClimate.DesertTint, LandClimate.VegetationTint);
    if (LandClimateTexture && (ClimateTextureSent.Get() != LandClimateTexture || ClimateTerrainSent.Get() != TerrainActor
        || ClimateParamsSent != Params || ClimateShapeSent != Shape || ClimateTintSent != Tint))
    {
        TerrainActor->SetLandClimate(LandClimateTexture, Params, Shape, Tint);
        ClimateTextureSent = LandClimateTexture;
        ClimateTerrainSent = TerrainActor;
        ClimateParamsSent = Params;
        ClimateShapeSent = Shape;
        ClimateTintSent = Tint;
    }
}

void APlanetActor::ConfigureAtmosphere(double PlanetRadius, double OceanRadius, bool bRegenerate)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(APlanetActor::ConfigureAtmosphere);
    FSpentScope Spent(SpawnTimeline, ESpawnSpent::AtmosphereConfig);
    if (!AtmosphereActor) return;
    if (!bEnableAtmosphere)
    {
        // The only off-switch for its march (hiding it leaves it drawing): parked, the sim given
        // up, its light hidden.
        AtmosphereActor->SetAtmosphereActive(false);
        AtmosphereActor->SetActorHiddenInGame(true);
        AtmosphereActor->SetActorTickEnabled(false);
#if WITH_EDITOR
        AtmosphereActor->SetIsTemporarilyHiddenInEditor(true);
#endif
        if (OceanActor) OceanActor->SetAtmosphere(nullptr);
        return;
    }
    // The generation: the seed, the model and the planet's set (if it names any; else the
    // atmosphere's own). Its first tick generates; later, only a change does (each restarts the
    // sim from the rolled start).
    const int32 Seed = GetAtmosphereSeed();
    const bool bConfigured = LastAtmosphereSeed != INDEX_NONE;
    const bool bChanged = !bConfigured || Seed != LastAtmosphereSeed || AtmosphereType != LastAtmosphereType || bRegenerate;
    AtmosphereActor->Seed = Seed;
    if (HasAtmosphereSet())
        AtmosphereActor->Set = AtmosphereSet;
    if (AtmosphereActor->PlanetType != AtmosphereType)
        AtmosphereActor->SetPlanetType(AtmosphereType);
    // The atmosphere's floor. Stage A7: THE SEA'S LEVEL where there is one (and on the terrestrial
    // generator, whose heights are about sea level, the actor's radius): the floor is where the air
    // starts, where the sun sets (its planet shadow) and where the sky tables are made. Above the
    // water (the terrestrial lattice's centre can sit over the sea on some seeds) the sun set behind
    // an invisible floor over the horizon, and the air under it was missing. Without either, the
    // larger of the two, as before.
    const bool bSeaFloor = bEnableOcean || Generator == EVoxelSurfaceGenerator::Terrestrial;
    AtmosphereActor->InitializeFromPlanet(PlanetRoot, FVector(bSeaFloor ? OceanRadius : FMath::Max(OceanRadius, PlanetRadius)));
    AtmosphereActor->SetAtmosphereActive(true);
    AtmosphereActor->SetActorHiddenInGame(false);
    AtmosphereActor->SetActorTickEnabled(true);
    SpawnTimeline.Mark(ESpawnMark::Atmosphere);
#if WITH_EDITOR
    AtmosphereActor->SetIsTemporarilyHiddenInEditor(false);
#endif
    if (bConfigured && bChanged)
        AtmosphereActor->Generate();
    LastAtmosphereSeed = Seed;
    LastAtmosphereType = AtmosphereType;
    // Stage A9: the ground under the weather (the map lives on the atmosphere, which puts it on
    // each config it generates).
    ConfigureAtmosphereSurface();
    if (bHasStarWorldPosition)
        AtmosphereActor->OrientToStar(StarWorldPosition);
    // The ocean's sun: the atmosphere's light (the star's direction, the light at the top of the
    // air), and stage A2: the sun at the water from its march (the clouds' shadow and the air),
    // the sea's pixels left to the ocean.
    if (OceanActor)
        OceanActor->SetAtmosphere(AtmosphereActor);
}

void APlanetActor::TickSpawnTimeline()
{
    // Stage P1a: after Initialize, the terrain's first bricks and its first fill settling (and
    // the ocean's fields on a terrestrial planet, made from its bake); then the line is logged.
    if (!SpawnTimeline.IsRunning() || !SpawnTimeline.Has(ESpawnMark::Initialize)) return;
    const TSharedPtr<FVoxelBrickTerrain> Bricks = TerrainActor ? TerrainActor->GetBrickTerrain() : nullptr;
    if (Bricks.IsValid())
    {
        if (Bricks->NumOnScreen() > 0)
            SpawnTimeline.Mark(ESpawnMark::TerrainFirst);
        if (SpawnTimeline.Has(ESpawnMark::TerrainFirst) && Bricks->IsSettled())
            SpawnTimeline.Mark(ESpawnMark::TerrainSettled);
    }
    const bool bShoreExpected = OceanActor && bEnableOcean && Generator == EVoxelSurfaceGenerator::Terrestrial;
    // (Stage P1d: this build's field, by its bake's key: a reseed holds the old seed's until then.)
    const TSharedPtr<const FVoxelLandBake, ESPMode::ThreadSafe> ShoreBake = TerrainBake();
    if (bShoreExpected && ShoreBake.IsValid() && OceanActor->HasShoreFieldFor(ShoreBake->Key))
        SpawnTimeline.Mark(ESpawnMark::Shore);
    if (bShoreExpected && OceanActor->HasCoastField())
        SpawnTimeline.Mark(ESpawnMark::Coast);
    if (SpawnTimeline.Has(ESpawnMark::TerrainSettled) && (!bShoreExpected || SpawnTimeline.Has(ESpawnMark::Shore)))
        SpawnTimeline.End();
    else if (SpawnTimeline.Elapsed() - SpawnTimeline.When(ESpawnMark::Initialize) > PlanetActorTimelineDetail::SettleWaitSeconds)
        SpawnTimeline.End(TEXT("stopped waiting 90 s after Initialize: the camera kept the terrain busy, or the shore field didn't come"));
}

void APlanetActor::RecordAppliedState()
{
    LastInitScale = GetActorScale3D();
    LastSeaLevel = SeaLevel;
    LastNoiseAmplitudeRatio = NoiseAmplitudeRatio;
    LastSurfaceSettingsHash = SurfaceSettings.GetSettingsHash();
    LastGenerator = Generator;
    LastTerrestrialHash = TerrestrialSettings.GetSettingsHash();
    bLastEnableOcean = bEnableOcean;
    bLastEnableGravity = bEnableGravity;
    bLastEnableAtmosphere = bEnableAtmosphere;
}

// ---------------------------------------------------------------------------
// Compositor
// ---------------------------------------------------------------------------

TSharedPtr<FSparseEditStore> APlanetActor::HeldEditStore(double RootExtent, int32 MaxDepth) const
{
    // The edit store: kept across a rebuild of the same planet that leaves the lattice as it is
    // (the same generator and surface settings, root box and depth: a climate landing after the
    // build, a recapture, the climate's settings), so the edits dug meanwhile survive it (R1,
    // PLN-4); a new one for another planet (a seed or shape change) or a new lattice.
    const bool bSamePlanet = Generator == LastGenerator && (Generator == EVoxelSurfaceGenerator::Terrestrial
        ? TerrestrialSettings.GetSettingsHash() == LastTerrestrialHash : SurfaceSettings.GetSettingsHash() == LastSurfaceSettingsHash);
    TSharedPtr<FSparseEditStore> EditStore = bSamePlanet && SharedCompositor.IsValid() ? SharedCompositor->GetEditStore() : nullptr;
    const double HalfFinest = RootExtent / FMath::Pow(2.0, (double)FMath::Max(MaxDepth, 0));
    if (!EditStore.IsValid() || !EditStore->GetCenter().IsZero() || EditStore->GetExtent() != RootExtent || EditStore->GetHalfFinest() != HalfFinest)
        return nullptr;
    return EditStore;
}

uint32 APlanetActor::CompositorKey(double PlanetRadius, double NoiseAmplitude, double RootExtent, int32 MaxDepth) const
{
    uint32 Key = HashCombine(PointerHash(TerrainBake().Get()), PointerHash(DuneClimate().Get()));
    Key = HashCombine(Key, TerrestrialSettings.GetSettingsHash());
    Key = HashCombine(Key, GetTypeHash(PlanetRadius));
    Key = HashCombine(Key, GetTypeHash(NoiseAmplitude));
    Key = HashCombine(Key, GetTypeHash(RootExtent));
    Key = HashCombine(Key, GetTypeHash(MaxDepth));
    Key = HashCombine(Key, PointerHash(HeldEditStore(RootExtent, MaxDepth).Get()));
    return Key | 1u;
}

bool APlanetActor::PrebuildCompositor()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(APlanetActor::PrebuildCompositor);
    // Only where Initialize would build a terrestrial compositor; else it builds as before.
    if (Generator != EVoxelSurfaceGenerator::Terrestrial || !LandBake.IsValid()) return true;
    SpawnChildActors();
    if (!TerrainActor) return true;
    double PlanetRadius = 0.0, NoiseAmplitude = 0.0, OceanRadius = 0.0;
    ComputeShape(PlanetRadius, NoiseAmplitude, OceanRadius);
    const double RootExtent = VoxelDensity::RootExtentFor(PlanetRadius, NoiseAmplitude);
    const int32 MaxDepth = APlanetTerrainActor::MaxDepthFor(RootExtent, TerrainActor->TargetPrecision);
    const uint32 Key = CompositorKey(PlanetRadius, NoiseAmplitude, RootExtent, MaxDepth);
    if (CompositorFuture.IsValid() && CompositorFutureKey == Key)
        return CompositorFuture.IsReady();

    // BuildCompositor's call, with BuildCompositor's inputs, on the thread pool: the density reads
    // the bake, the climate and the noise pack (shared, immutable; the pack's registry locks), and
    // the compositor holds the edit store without writing it.
    TSharedPtr<FSparseEditStore> EditStore = HeldEditStore(RootExtent, MaxDepth);
    if (!EditStore.IsValid())
        EditStore = MakeShared<FSparseEditStore>(FVector::ZeroVector, RootExtent, MaxDepth);
    const double Finest = VoxelDensity::TerrainFinestCell(PlanetRadius, NoiseAmplitude, MaxDepth);
    CompositorFutureKey = Key;
    CompositorFuture = Async(EAsyncExecution::ThreadPool,
        [Settings = TerrestrialSettings, Bake = TerrainBake(), PlanetRadius, NoiseAmplitude, EditStore, Finest, Climate = DuneClimate()]()
        {
            return VoxelDensity::MakeTerrestrialCompositor(Settings, Bake, PlanetRadius, NoiseAmplitude, EditStore, Finest, Climate);
        });
    return false;
}

TSharedPtr<FDensitySampleCompositor> APlanetActor::BuildCompositor(
    double PlanetRadius, double NoiseAmplitude, double RootExtent, int32 MaxDepth)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(APlanetActor::BuildCompositor);
    FSpentScope Spent(SpawnTimeline, ESpawnSpent::Compositor);

    if (Generator == EVoxelSurfaceGenerator::Terrestrial && LandBake.IsValid())
    {
        // Stage T5c: the dunes by the climate captured for this seed (the build waited for it,
        // unless bWaitForClimate is off: TickLandClimate rebuilds once it lands).
        LandClimateFieldBuilt = DuneClimateHash();
        // Stage P1i (S2): the one made on a worker, when it is for these inputs.
        if (CompositorFuture.IsValid())
        {
            const bool bMatch = CompositorFutureKey == CompositorKey(PlanetRadius, NoiseAmplitude, RootExtent, MaxDepth)
                && CompositorFuture.IsReady();
            TSharedPtr<FDensitySampleCompositor> Made = bMatch ? CompositorFuture.Get() : nullptr;
            CompositorFuture = TFuture<TSharedPtr<FDensitySampleCompositor>>();
            CompositorFutureKey = 0;
            if (Made.IsValid())
                return Made;
        }
    }

    TSharedPtr<FSparseEditStore> EditStore = HeldEditStore(RootExtent, MaxDepth);
    if (!EditStore.IsValid())
        EditStore = MakeShared<FSparseEditStore>(FVector::ZeroVector, RootExtent, MaxDepth);

    const double Finest = VoxelDensity::TerrainFinestCell(PlanetRadius, NoiseAmplitude, MaxDepth);
    if (Generator == EVoxelSurfaceGenerator::Terrestrial && LandBake.IsValid())
    {
        // T5e: on the terrain's bake (with the ice shelf when there is one).
        return VoxelDensity::MakeTerrestrialCompositor(TerrestrialSettings, TerrainBake(), PlanetRadius, NoiseAmplitude, EditStore, Finest, DuneClimate());
    }
    LandClimateFieldBuilt = 0;
    return VoxelDensity::MakeCompositor(SurfaceSettings, PlanetRadius, NoiseAmplitude, EditStore, Finest);
}

// ---------------------------------------------------------------------------
// Child actor management
// ---------------------------------------------------------------------------

void APlanetActor::SpawnChildActors()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(APlanetActor::SpawnChildActors);
    UWorld* World = GetWorld();
    if (!World) return;

    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = this;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    // A child another planet owns (this planet is a copy of it) is not ours to configure.
    if (TerrainActor && TerrainActor->GetOwner() != this) TerrainActor = nullptr;
    if (OceanActor && OceanActor->GetOwner() != this) OceanActor = nullptr;
    if (GravityZone && GravityZone->GetOwner() != this) GravityZone = nullptr;
    if (AtmosphereActor && AtmosphereActor->GetOwner() != this) AtmosphereActor = nullptr;

    // A terrain saved before stage S1b is the engine's base actor: replaced by the planet's.
    DestroyLegacyTerrainActors();

    if (!TerrainActor)
    {
        TerrainActor = World->SpawnActor<APlanetTerrainActor>(
            APlanetTerrainActor::StaticClass(),
            FTransform(GetActorRotation(), GetActorLocation()),
            SpawnParams);
        if (USceneComponent* ChildRoot = TerrainActor->GetRootComponent())
        {
            ChildRoot->SetAbsolute(false, false, true);
            ChildRoot->AttachToComponent(PlanetRoot,
                FAttachmentTransformRules::KeepWorldTransform);
        }
    }

    if (!OceanActor)
    {
        OceanActor = World->SpawnActor<AOceanSphereActor>(
            AOceanSphereActor::StaticClass(),
            FTransform(GetActorRotation(), GetActorLocation()),
            SpawnParams);
        if (USceneComponent* ChildRoot = OceanActor->GetRootComponent())
        {
            ChildRoot->SetAbsolute(false, false, true);
            ChildRoot->AttachToComponent(PlanetRoot,
                FAttachmentTransformRules::KeepWorldTransform);
        }
    }

    if (!GravityZone)
    {
        GravityZone = World->SpawnActor<APlanetGravityZone>(
            APlanetGravityZone::StaticClass(),
            FTransform(GetActorRotation(), GetActorLocation()),
            SpawnParams);
        if (USceneComponent* ChildRoot = GravityZone->GetRootComponent())
        {
            ChildRoot->SetAbsolute(false, false, true);
            ChildRoot->AttachToComponent(PlanetRoot,
                FAttachmentTransformRules::KeepWorldTransform);
        }
        // Stage T5b: off until ConfigureGravity sizes it (the climate capture spawns the children
        // before the build). (It never ticks: the manager's tick applies the gravity.)
        GravityZone->SetActorEnableCollision(false);
    }

    // Stage A1: no second atmosphere. A level saved before the atmosphere was detached (or by
    // an older build) can still hold one this planet spawned then, attached under it or owned by
    // it: a seeded one is adopted (when this planet has none), any other is the old plumbing's
    // and goes, so one march draws the planet.
    {
        TArray<AActor*> Candidates;
        GetAttachedActors(Candidates, true, true);
        for (TActorIterator<APlanetAtmosphereActor> It(World); It; ++It)
            if (It->GetOwner() == this) Candidates.AddUnique(*It);
        for (AActor* Actor : Candidates)
        {
            APlanetAtmosphereActor* Atmosphere = Cast<APlanetAtmosphereActor>(Actor);
            if (!Atmosphere || Atmosphere == AtmosphereActor) continue;
            ASeededAtmosphereActor* Seeded = Cast<ASeededAtmosphereActor>(Atmosphere);
            if (!AtmosphereActor && Seeded && Seeded->GetOwner() == this)
            {
                AtmosphereActor = Seeded;
                LastAtmosphereSeed = INDEX_NONE;
                continue;
            }
            UE_LOG(LogVoxelPlanets, Log, TEXT("[Planet] %s: removed %s, an atmosphere left from the old planet plumbing (the planet's seeded atmosphere replaces it)."),
                *GetName(), *Atmosphere->GetName());
            Atmosphere->Destroy();
        }
    }

    // Stage A1: the atmosphere (seeded), attached as the others with absolute scale (its scale is
    // the surface's radius, set by ConfigureAtmosphere through InitializeFromPlanet).
    if (!AtmosphereActor)
    {
        AtmosphereActor = World->SpawnActor<ASeededAtmosphereActor>(
            ASeededAtmosphereActor::StaticClass(),
            FTransform(GetActorRotation(), GetActorLocation()),
            SpawnParams);
        if (AtmosphereActor)
        {
            if (USceneComponent* ChildRoot = AtmosphereActor->GetRootComponent())
            {
                ChildRoot->SetAbsolute(false, false, true);
                ChildRoot->AttachToComponent(PlanetRoot,
                    FAttachmentTransformRules::KeepWorldTransform);
            }
            LastAtmosphereSeed = INDEX_NONE;
        }
    }

    // The plugin's materials when none is set (VoxelPlanets' content).
    if (!TerrainMaterial)
        TerrainMaterial = VoxelPlanets::LoadPluginAsset<UMaterialInterface>(TEXT("MT_TriPlanar"));
    if (!OceanMaterial)
        OceanMaterial = VoxelPlanets::LoadPluginAsset<UMaterialInterface>(TEXT("MT_Ocean"));
}

void APlanetActor::DestroyLegacyTerrainActors()
{
    // The property no longer takes such a child (it loads unreferenced, still owned by the planet
    // and usually attached to it). Not while the world or the planet is being torn down: the
    // world takes it then.
    UWorld* World = GetWorld();
    if (!World || World->bIsTearingDown || HasAnyFlags(RF_BeginDestroyed)) return;
    TArray<AActor*> Candidates;
    GetAttachedActors(Candidates);
    for (TActorIterator<AAdaptiveVoxelActor> It(World); It; ++It)
        if (It->GetOwner() == this) Candidates.AddUnique(*It);
    for (AActor* Candidate : Candidates)
    {
        if (!IsValid(Candidate) || Candidate->GetOwner() != this || !Candidate->IsA<AAdaptiveVoxelActor>() || Candidate->IsA<APlanetTerrainActor>())
            continue;
        UE_LOG(LogVoxelPlanets, Log, TEXT("[VoxelPlanet] %s: removing the terrain actor %s saved before the planet terrain class (save the level to keep the change)."),
            *GetName(), *Candidate->GetName());
        Candidate->Destroy();
    }
}

void APlanetActor::DestroyChildActors()
{
    // A terrain saved before stage S1b, if the planet goes before its first build.
    DestroyLegacyTerrainActors();
    // Only children this planet spawned (see SpawnChildActors).
    if (TerrainActor && TerrainActor->GetOwner() == this)
        TerrainActor->Destroy();
    TerrainActor = nullptr;
    if (OceanActor && OceanActor->GetOwner() == this)
        OceanActor->Destroy();
    OceanActor = nullptr;
    if (GravityZone && GravityZone->GetOwner() == this)
        GravityZone->Destroy();
    GravityZone = nullptr;
    if (AtmosphereActor && AtmosphereActor->GetOwner() == this)
        AtmosphereActor->Destroy();
    AtmosphereActor = nullptr;
    LastAtmosphereSeed = INDEX_NONE;
}

double APlanetActor::ComputeOceanRadius(double PlanetRadius, double NoiseAmplitude) const
{
    return FMath::Max(1.0, PlanetRadius + SeaLevel * NoiseAmplitude);
}

FVoxelWaterSample APlanetActor::GetWaterAt(const FVector& WorldLocation) const
{
    FVoxelWaterSample Out;
    if (!SharedCompositor.IsValid()) return Out;
    const TSharedPtr<const FVoxelPlanetDensity> Density = FVoxelPlanetDensity::Of(*SharedCompositor);
    if (!Density.IsValid()) return Out;
    // The planet's frame (no scale: the density's positions are cm from the centre).
    const FTransform Frame(GetActorRotation(), GetActorLocation());
    const FVector Local = Frame.InverseTransformPositionNoScale(WorldLocation);
    const double Radius = Local.Size();
    if (Radius < 1.0) return Out;
    Out = Density->WaterAt(Local);
    double SeaRadius = 0.0;
    const TSharedPtr<const FVoxelTerrestrialDensity> Land = FVoxelTerrestrialDensity::Of(*SharedCompositor);
    if (Land.IsValid())
    {
        SeaRadius = Land->GetLandBake()->SeaRadius;
    }
    else
    {
        // The noise surface: its ground came above the planet radius; the sea is the planet's.
        SeaRadius = ComputeOceanRadius(Density->GetPlanetRadius(), Density->GetNoiseAmplitude());
        const float Ground = (float)((Density->GetPlanetRadius() + (double)Out.GroundHeight * 100.0 - SeaRadius) * 0.01);
        Out.GroundHeight = Ground;
        Out.SurfaceHeight = Ground;
        if (Ground < 0.0f)
        {
            Out.Kind = EVoxelWaterKind::Sea;
            Out.SurfaceHeight = 0.0f;
            Out.Depth = -Ground;
        }
    }
    if (Out.Kind == EVoxelWaterKind::Sea && !bEnableOcean)
    {
        // No ocean drawn: no sea to be in.
        Out.Kind = EVoxelWaterKind::None;
        Out.SurfaceHeight = Out.GroundHeight;
        Out.Depth = 0.0f;
    }
    Out.PointDepth = (float)((SeaRadius + (double)Out.SurfaceHeight * 100.0 - Radius) * 0.01);
    Out.FlowVelocity = Frame.TransformVectorNoScale(Out.FlowVelocity);
    return Out;
}

// ---------------------------------------------------------------------------
// Pooling: dormancy + reconfigure (IPooledActor, proxy-carried)
// ---------------------------------------------------------------------------

void APlanetActor::SetDormant(bool bDormant)
{
    const bool bActive = !bDormant;
    // Stage T5b: a capture parked mid-wait starts again on waking (its clocks would have run out).
    if (bDormant && ClimateCaptureState == EVoxelPlanetClimateCapture::Waiting)
        ClimateCaptureState = EVoxelPlanetClimateCapture::None;

    SetActorHiddenInGame(bDormant);
    SetActorTickEnabled(bActive);

    if (TerrainActor)
    {
        TerrainActor->SetActorHiddenInGame(bDormant);
        TerrainActor->SetActorTickEnabled(bActive);   // stops the brick LOD updates and edits
        TerrainActor->SetActorEnableCollision(bActive); // its collision bricks stop updating while parked
    }
    if (OceanActor)
    {
        // Hidden and not ticking: no selection, no commits, no underwater pass. Its pages stay
        // allocated, so waking draws at once.
        const bool bOn = bActive && bEnableOcean;
        OceanActor->SetActorHiddenInGame(!bOn);
        OceanActor->SetActorTickEnabled(bOn);
    }
    if (GravityZone)
    {
        const bool bOn = bActive && bEnableGravity;
        GravityZone->SetActorHiddenInGame(!bOn);
        GravityZone->SetActorEnableCollision(bOn);
    }
    if (AtmosphereActor)
    {
        // Parked: its march, its sim bid and its light off (waking rebakes its shadow levels).
        const bool bOn = bActive && bEnableAtmosphere;
        AtmosphereActor->SetAtmosphereActive(bOn);
        AtmosphereActor->SetActorHiddenInGame(!bOn);
        AtmosphereActor->SetActorTickEnabled(bOn);
    }
}

void APlanetActor::SetStarWorldPosition(const FVector& StarWorldPos)
{
    StarWorldPosition = StarWorldPos;
    bHasStarWorldPosition = true;
    // Stage A1: the atmosphere's light and march toward the star: from the capture on, when the
    // atmosphere already draws, not only once the planet is built (R1, PLN-5).
    if (AtmosphereActor && bEnableAtmosphere)
        AtmosphereActor->OrientToStar(StarWorldPos);
}

void APlanetActor::OnReturnToPool()
{
    SetDormant(true);
}

void APlanetActor::OnAcquired(double WorldRadius)
{
    SetActorScale3D(FVector(WorldRadius));

    // Terrain geometry is a function of radius, so only rebuild when it actually
    // changed; a re-acquired planet at the same radius (warm revisit) skips the
    // rebuild entirely and just un-parks.
    const double BuiltRadius = LastInitScale.GetMax();
    const bool bNeedsRebuild = !bInitialized ||
        !FMath::IsNearlyEqual(BuiltRadius, WorldRadius, FMath::Max(1.0, BuiltRadius * 1e-4));

    if (bNeedsRebuild)
    {
        // Wake this actor's tick so its next Tick runs Initialize(), which reconfigures
        // and re-activates every child at the new scale (children stay parked until then).
        SetActorHiddenInGame(false);
        SetActorTickEnabled(true);
        bPendingInitialize = true;
    }
    else
    {
        SetDormant(false);   // warm revisit: geometry matches, just un-park
    }
}

// ---------------------------------------------------------------------------
// voxel.Land.ExportBake
// ---------------------------------------------------------------------------

namespace PlanetActorLandExport
{
    void Run(const TArray<FString>& Args, UWorld* World)
    {
        int32 Count = 0;
        for (TActorIterator<APlanetActor> It(World); It; ++It)
        {
            const TSharedPtr<const FVoxelLandBake, ESPMode::ThreadSafe> Bake = It->GetLandBake();
            if (!Bake.IsValid()) continue;
            const FString Base = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("VoxelLand"), FString::Printf(TEXT("Export_%s_%08x"), *It->GetName(), Bake->Key));
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(Base), true);
            TArray<uint8> Bytes;
            Bytes.SetNumUninitialized(Bake->Height.Num() * sizeof(float));
            FMemory::Memcpy(Bytes.GetData(), Bake->Height.GetData(), Bytes.Num());
            FFileHelper::SaveArrayToFile(Bytes, *(Base + TEXT("_height.f32")));
            Bytes.SetNumUninitialized(Bake->Attr.Num() * sizeof(uint16));
            FMemory::Memcpy(Bytes.GetData(), Bake->Attr.GetData(), Bytes.Num());
            FFileHelper::SaveArrayToFile(Bytes, *(Base + TEXT("_attr.u16")));
            // The landform (VoxelLandPackForm: hilliness low byte, character high) and the
            // province from bit 16 (T4c), a u32 per cell as the harnesses read it (Tag_form.u32).
            if (Bake->Form.Num() == Bake->Height.Num())
            {
                TArray<uint32> Form;
                Form.SetNumUninitialized(Bake->Form.Num());
                const bool bProvince = Bake->Province.Num() == Bake->Form.Num();
                for (int32 i = 0; i < Form.Num(); i++)
                    Form[i] = (uint32)Bake->Form[i] | (bProvince ? (uint32)Bake->Province[i] << 16 : 0u);
                Bytes.SetNumUninitialized(Form.Num() * sizeof(uint32));
                FMemory::Memcpy(Bytes.GetData(), Form.GetData(), Bytes.Num());
                FFileHelper::SaveArrayToFile(Bytes, *(Base + TEXT("_form.u32")));
            }
            // The final routing's receivers (R1: the rivers), a u32 per cell (~0 none), when eroded.
            if (Bake->Receiver.Num() == Bake->Height.Num())
            {
                Bytes.SetNumUninitialized(Bake->Receiver.Num() * sizeof(uint32));
                FMemory::Memcpy(Bytes.GetData(), Bake->Receiver.GetData(), Bytes.Num());
                FFileHelper::SaveArrayToFile(Bytes, *(Base + TEXT("_rec.u32")));
            }
            // The drainage (log2 m^2) and the lake levels, as f32, when the bake is eroded.
            if (Bake->Flow.Num() == Bake->Height.Num() && Bake->Water.Num() == Bake->Height.Num())
            {
                TArray<float> Flow;
                Flow.SetNumUninitialized(Bake->Flow.Num());
                for (int32 i = 0; i < Flow.Num(); i++)
                    Flow[i] = Bake->Flow[i].GetFloat();
                Bytes.SetNumUninitialized(Flow.Num() * sizeof(float));
                FMemory::Memcpy(Bytes.GetData(), Flow.GetData(), Bytes.Num());
                FFileHelper::SaveArrayToFile(Bytes, *(Base + TEXT("_flow.f32")));
                Bytes.SetNumUninitialized(Bake->Water.Num() * sizeof(float));
                FMemory::Memcpy(Bytes.GetData(), Bake->Water.GetData(), Bytes.Num());
                FFileHelper::SaveArrayToFile(Bytes, *(Base + TEXT("_water.f32")));
            }
            FString LakesJson;
            for (const FVoxelLandLake& Lake : Bake->Lakes)
            {
                LakesJson += FString::Printf(TEXT("%s{\"cell\": %u, \"level_m\": %.3f, \"outlet\": %u, \"area_km2\": %.3f}"),
                    LakesJson.IsEmpty() ? TEXT("") : TEXT(", "), Lake.Cell, Lake.Level, Lake.Outlet, Lake.AreaKm2);
            }
            const FString Json = FString::Printf(TEXT("{\"n\": %d, \"pad\": %d, \"sea_radius_cm\": %.17g, \"key\": \"%08x\", \"min_m\": %.3f, \"max_m\": %.3f, \"crust_threshold\": %.6f, \"eroded\": %s, \"solve_steps\": %d, \"solve_residual\": %.5f, \"relief_factor\": %.4f, \"lakes\": [%s]}\n"),
                Bake->N, FVoxelLandBake::Pad, Bake->SeaRadius, Bake->Key, Bake->MinHeight, Bake->MaxHeight, Bake->CrustThreshold,
                Bake->bEroded ? TEXT("true") : TEXT("false"), Bake->SolveSteps, Bake->SolveResidual, Bake->ReliefFactor, *LakesJson);
            FFileHelper::SaveStringToFile(Json, *(Base + TEXT(".json")));
            UE_LOG(LogVoxelPlanets, Log, TEXT("[VoxelLand] Exported %s's bake to %s_*."), *It->GetName(), *Base);
            Count++;
        }
        if (Count == 0)
            UE_LOG(LogVoxelPlanets, Warning, TEXT("[VoxelLand] No terrestrial planet with a bake in this world."));
    }

    FAutoConsoleCommandWithWorldAndArgs Command(
        TEXT("voxel.Land.ExportBake"),
        TEXT("Writes every terrestrial planet's bake (heights f32, attributes u16, the landform and province u32, the receivers u32, drainage and lake levels f32 when eroded, a json header with the lakes) to Saved/VoxelLand/Export_* (Tools/LandView/view_bake.py renders them)."),
        FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}
