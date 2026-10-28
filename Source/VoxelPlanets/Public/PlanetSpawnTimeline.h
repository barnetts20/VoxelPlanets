// PlanetSpawnTimeline.h - Stage P1a: one planet build's timeline, for measuring the spawn.
//
// From the tick that starts a build (a spawn, a pool acquire at a new radius, a rebuild) until the
// terrain's first fill has settled (and the ocean's shore field is in): when each stage became
// ready, in seconds from the start, and the game thread's time in the synchronous steps. Logged
// once per build as one line ("[Planet] <name> spawn timeline ..."), each milestone marked in
// Unreal Insights as a bookmark. Measurement only: nothing reads it back.

#pragma once

#include "CoreMinimal.h"

struct VOXELPLANETS_API FPlanetSpawnTimeline
{
    /** Milestones, each recorded the first time it is reached. */
    enum class EMark : uint8
    {
        Bake,            // the land bake held for the key (loaded, solved or kept)
        Rivers,          // its rivers built
        Noise,           // the shared noise pack read
        ClimateStart,    // the climate capture began (the atmosphere configured, the sim restarted)
        Atmosphere,      // the atmosphere first configured and active (it draws from here)
        Climate,         // the climate gate passed (a snapshot, or the provisional model)
        Initialize,      // the planet's Initialize returned (terrain, ocean and atmosphere set up)
        TerrainFirst,    // the first terrain bricks on screen
        TerrainSettled,  // the first fill done: nothing left to mesh for the camera
        Shore,           // the ocean's shore field in
        Coast,           // the ocean's coastal field in
        Num
    };

    /** Game-thread time in the build's synchronous steps (summed over the build). */
    enum class ESpent : uint8
    {
        BakeGate,          // TickLandBake (the baker's ticks, the rivers' and the noise's checks)
        ClimateBase,       // VoxelLandClimate::BuildBase
        AtmosphereConfig,  // ConfigureAtmosphere (its surface map included)
        Field,             // RebuildLandClimateTarget (BuildMap + MakeField)
        Shelf,             // UpdateIceShelf (MakeIceShelfBake)
        Compositor,        // BuildCompositor (the density)
        Terrain,           // the terrain's InitializeFromOwner (its rebuild, the material)
        Ocean,             // ConfigureOcean (the ocean's initialization)
        Initialize,        // the whole Initialize
        Num
    };

    FPlanetSpawnTimeline() { Clear(); }

    bool IsRunning() const { return StartSeconds > 0.0; }
    bool Has(EMark What) const { return At[(int32)What] >= 0.0; }
    /** Seconds from the start to a milestone (negative before it). */
    double When(EMark What) const { return At[(int32)What]; }

    /** Starts a timeline (a running one is logged first, as interrupted). */
    void Begin(const FString& InPlanet, const TCHAR* InReason);
    /** Records a milestone the first time it is reached (and bookmarks it). */
    void Mark(EMark What);
    void AddSpent(ESpent What, double Seconds);
    /** The planet's tick time while the build runs (the longest is the spawn's worst hitch on
     *  the game thread from the planet itself). */
    void NoteTick(double Seconds);
    void SetBakeSource(const TCHAR* InSource) { BakeSource = InSource; }
    void SetClimateOutcome(const FString& InOutcome) { ClimateOutcome = InOutcome; }
    /** Seconds since the start (0 when not running). */
    double Elapsed() const;
    /** Logs the line and stops. */
    void End(const TCHAR* Why = nullptr);

private:
    FString Planet;
    const TCHAR* Reason = TEXT("");
    const TCHAR* BakeSource = TEXT("");
    FString ClimateOutcome;
    double StartSeconds = 0.0;
    double At[(int32)EMark::Num] = {};
    double Spent[(int32)ESpent::Num] = {};
    int32 Ticks = 0;
    double LongestTick = 0.0;
    double TickSum = 0.0;

    void Clear();
};
