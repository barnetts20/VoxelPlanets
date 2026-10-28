#include "PlanetSpawnTimeline.h"
#include "VoxelPlanets.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/MiscTrace.h"

namespace PlanetSpawnTimelineDetail
{
    const TCHAR* MarkName(FPlanetSpawnTimeline::EMark What)
    {
        switch (What)
        {
        case FPlanetSpawnTimeline::EMark::Bake: return TEXT("bake");
        case FPlanetSpawnTimeline::EMark::Rivers: return TEXT("rivers");
        case FPlanetSpawnTimeline::EMark::Noise: return TEXT("noise pack");
        case FPlanetSpawnTimeline::EMark::ClimateStart: return TEXT("climate capture started");
        case FPlanetSpawnTimeline::EMark::Atmosphere: return TEXT("atmosphere on");
        case FPlanetSpawnTimeline::EMark::Climate: return TEXT("climate");
        case FPlanetSpawnTimeline::EMark::Initialize: return TEXT("initialized");
        case FPlanetSpawnTimeline::EMark::TerrainFirst: return TEXT("first terrain");
        case FPlanetSpawnTimeline::EMark::TerrainSettled: return TEXT("terrain settled");
        case FPlanetSpawnTimeline::EMark::Shore: return TEXT("shore field");
        case FPlanetSpawnTimeline::EMark::Coast: return TEXT("coast field");
        default: return TEXT("?");
        }
    }

    const TCHAR* SpentName(FPlanetSpawnTimeline::ESpent What)
    {
        switch (What)
        {
        case FPlanetSpawnTimeline::ESpent::BakeGate: return TEXT("bake gate");
        case FPlanetSpawnTimeline::ESpent::ClimateBase: return TEXT("climate base");
        case FPlanetSpawnTimeline::ESpent::AtmosphereConfig: return TEXT("atmosphere config");
        case FPlanetSpawnTimeline::ESpent::Field: return TEXT("climate field");
        case FPlanetSpawnTimeline::ESpent::Shelf: return TEXT("ice shelf");
        case FPlanetSpawnTimeline::ESpent::Compositor: return TEXT("compositor");
        case FPlanetSpawnTimeline::ESpent::Terrain: return TEXT("terrain init");
        case FPlanetSpawnTimeline::ESpent::Ocean: return TEXT("ocean init");
        case FPlanetSpawnTimeline::ESpent::Initialize: return TEXT("Initialize in all");
        default: return TEXT("?");
        }
    }
}

void FPlanetSpawnTimeline::Clear()
{
    StartSeconds = 0.0;
    for (double& V : At) V = -1.0;
    for (double& V : Spent) V = 0.0;
    Ticks = 0;
    LongestTick = 0.0;
    TickSum = 0.0;
    BakeSource = TEXT("");
    ClimateOutcome.Reset();
}

void FPlanetSpawnTimeline::Begin(const FString& InPlanet, const TCHAR* InReason)
{
    if (IsRunning())
        End(TEXT("interrupted by another build"));
    Clear();
    Planet = InPlanet;
    Reason = InReason;
    StartSeconds = FPlatformTime::Seconds();
    TRACE_BOOKMARK(TEXT("Planet %s: build starts (%s)"), *Planet, Reason);
}

double FPlanetSpawnTimeline::Elapsed() const
{
    return IsRunning() ? FPlatformTime::Seconds() - StartSeconds : 0.0;
}

void FPlanetSpawnTimeline::Mark(EMark What)
{
    if (!IsRunning() || Has(What)) return;
    At[(int32)What] = FPlatformTime::Seconds() - StartSeconds;
    TRACE_BOOKMARK(TEXT("Planet %s: %s"), *Planet, PlanetSpawnTimelineDetail::MarkName(What));
}

void FPlanetSpawnTimeline::AddSpent(ESpent What, double Seconds)
{
    if (!IsRunning()) return;
    Spent[(int32)What] += Seconds;
}

void FPlanetSpawnTimeline::NoteTick(double Seconds)
{
    if (!IsRunning()) return;
    Ticks++;
    TickSum += Seconds;
    LongestTick = FMath::Max(LongestTick, Seconds);
}

void FPlanetSpawnTimeline::End(const TCHAR* Why)
{
    using namespace PlanetSpawnTimelineDetail;
    if (!IsRunning()) return;
    FString Marks;
    for (int32 i = 0; i < (int32)EMark::Num; i++)
    {
        if (At[i] < 0.0) continue;
        Marks += FString::Printf(TEXT("%s%s %.2f"), Marks.IsEmpty() ? TEXT("") : TEXT(", "), MarkName((EMark)i), At[i]);
        if ((EMark)i == EMark::Bake && *BakeSource)
            Marks += FString::Printf(TEXT(" (%s)"), BakeSource);
        if ((EMark)i == EMark::Climate && !ClimateOutcome.IsEmpty())
            Marks += FString::Printf(TEXT(" (%s)"), *ClimateOutcome);
    }
    FString Times;
    for (int32 i = 0; i < (int32)ESpent::Num; i++)
    {
        if (Spent[i] <= 0.0) continue;
        Times += FString::Printf(TEXT("%s%s %.0f"), Times.IsEmpty() ? TEXT("") : TEXT(", "), SpentName((ESpent)i), Spent[i] * 1000.0);
    }
    UE_LOG(LogVoxelPlanets, Log, TEXT("[Planet] %s spawn timeline (%s%s%s), seconds from the start: %s. Game thread ms: %s; the planet's ticks %d, %.0f ms in all, the longest %.0f ms."),
        *Planet, Reason, Why ? TEXT("; ") : TEXT(""), Why ? Why : TEXT(""), Marks.IsEmpty() ? TEXT("none") : *Marks,
        Times.IsEmpty() ? TEXT("none") : *Times, Ticks, TickSum * 1000.0, LongestTick * 1000.0);
    TRACE_BOOKMARK(TEXT("Planet %s: timeline ends"), *Planet);
    Clear();
}
