// K0 cave look test (Design/EditsAndCaves.md, "K0 as built"; K1: the engine's paths, mode k1): the
// shared cave SDF (Shaders/Private/VoxelLandCaves.ush since K1, in the shared HLSL/C++ subset) carved
// into the gully harness's planets
// (its Load: a dumped bake with the full land chain, FVoxelTerrestrialDensity::LandHeightAt's
// terms with the rivers' and lakes' carve).
//
// g++ -O2 -std=c++20 -ffp-contract=off -pthread $(<VoxelPlanets>/Tools/include_flags.sh) -I. cave_harness.cpp -o cave_harness
//
// cave_harness Tag N RadiusM Mode   (Tag_*.f32 from `solve_harness N Tag dump RadiusM`; K0 used
// `solve_harness 512 n512 dump 1e6`: the 1,000 km planet at 3 km bake cells). Modes:
//   info       the lattice, the karst threshold and coverage, the sites the other modes use
//   probe      what the gates leave around the sites (stages, how much of the band is cave)
//   slope      the slope bound: |grad| of the cave distance and of the carved density near walls
//              and near mouths (central differences), and the detail noise's bound
//   cost       CPU time per sample, one sample at a time: the land chain, and the cave term in
//              and out of its band (with counts of levels, near levels, primitives, detail)
//   brickcost  the brick path: one VoxelCaveGather per brick, VoxelCaveEvaluate per sample
//              (BRICK_LEVEL: the brick's level), checked against the per-sample path
//   micro      the pieces' cost (patch, lattice, node placement and dressing, passage, detail)
//   mouths     entrances per 1,000 km^2 of karst (connected openings of the surface on a 4 m
//              grid, tested 0.7 m into the rock along the surface's normal), and the land surface
//              cache's column (E2: 49 samples over +-24 m) with the caves: flagged texels at a
//              mouth (an opening within 12 m, or a mouth's own passage in the column) and
//              elsewhere (a gated passage within 24 m of the surface: should be none)
//   seaprobe   the sea caves at the cliff sites: what each passage's axis meets
//   bricks     the brick tree's wanted and surface bricks per level, land alone and with caves,
//              camera above a karst valley and inside a cave (FVoxelBrickTerrain's descent: the
//              centre test with SlopeBound 3.5, split within 2.5 brick sizes; a brick to be split
//              is tested with the caves at the finest footprint, BRICK_NAIVE=1 with its own)
//   views      cross-sections, plan views and ray-marched views (Tag_cave_*.ppm); VIEWS picks
//              from sec, plan, passage, chamber, mouth (with upland), coastview
//   k1         K1's engine paths against the per-sample path: the sample from a double-float
//              position (VoxelCaveSampleDF) against SampleAt; the mesher's brick lists
//              (VoxelCaveBoxRegion's face slots gathered, VoxelLandCaveCarve.ush's stored path)
//              at levels 0..4 (K1_LEVELS); the CPU density's cell cache
//              (FVoxelTerrestrialDensity::SampleRange); the cave bake's packing
// Every Cave* parameter can be set from the environment by its name (CaveRadiusM=4 ...).
#include <map>
#include <mutex>
#include <cstdlib>
// The cave term's counters (cost mode): levels evaluated, near a level's nodes, edges, detail.
struct FCaveCounters { uint64_t Levels = 0, Near = 0, Edges = 0, Detail = 0; };
static thread_local FCaveCounters GCount;
#define VOXEL_CAVE_COUNT(Name) (GCount.Name++)
#define main gully_main
#include "../LandGullyHarness/gully_harness.cpp"
#undef main

namespace VoxelShader
{
    // The caves' inputs (VoxelLandCaves.ush), with K0's look-test values.
    const uint* CaveBake = nullptr;      // the cave bake (VoxelLandCaves.ush VOXEL_CAVE_WORD: two words a padded cell)
    uint CaveOn = 1u, CaveSeed = 0x63617665u, CaveCellsPerBake = 16u, CaveLevels = 3u;
    uint CaveRoughCells = 24u, CaveRoughOctaves = 3u;
    float CaveCellM = 192.0f, CaveLevelM = 40.0f, CaveTableShare = 0.2f, CaveFloorClearM = 3.0f;
    float CavePhiWaveCells = 4.0f, CavePhiNoiseM = 6.0f, CavePhiJitterM = 2.0f, CavePhiNoiseSlope = 1.0f;
    float CaveTrunkFlowLo = 26.0f, CaveTrunkFlowHi = 32.0f;
    float CaveRadiusM = 3.0f, CaveRadiusSpread = 0.35f, CaveTrunkGain = 1.5f, CaveRadiusMaxM = 24.0f;
    float CaveKarstLo = 2.0f, CaveKarstHi = 2.1f, CaveKarstSlack = 0.05f;
    float CaveActiveShare = 0.7f, CaveTrunkActive = 0.3f;
    float CaveChamberShare = 0.05f, CaveChamberRadiusM = 22.0f, CaveChamberSquash = 0.5f;
    float CaveShaftShare = 0.12f, CaveShaftRadiusM = 2.5f;
    float CaveMouthShare = 0.004f;
    float CaveDolineShare = 0.0008f, CaveDolineRadiusM = 4.0f, CaveDolineHeadM = 300.0f, CaveDolineFunnel = 0.4f, CaveDolineBowlM = 25.0f;
    float CaveDeepCellM = 4.0f, CaveDepthM = 500.0f, CaveShallowDepthM = 150.0f;
    float CaveRoughM = 0.7f, CaveRoughGain = 0.45f, CaveBeddingM = 0.25f, CaveBeddingWaveM = 1.6f;
    float CaveBlendM = 1.5f, CaveBandSlackM = 300.0f;
    float CaveSeaShare = 0.35f, CaveSeaCoastM = 60.0f, CaveSeaZM = 1.5f, CaveSeaRadiusM = 6.0f, CaveSeaReliefM = 150.0f;
    float CaveSeaTall = 1.5f, CaveSeaFloorM = 1.0f, CaveSeaRoofM = 5.0f, CaveSeaCoverM = 45.0f;
    uint CaveSeaCells = 4u;
    float CaveFloorFill = 0.45f, CaveCanyonDepth = 2.0f, CaveCanyonWidth = 0.4f;
    float CaveMeander = 0.12f, CaveMeanderZM = 6.0f;
    float CaveCoverM = 24.0f, CaveCoverSlope = 0.4f;
    float CaveMaxRadiusM = 24.0f;        // set by SetupCaves (FVoxelCaveConstants' rule)
#include "VoxelLandCaves.ush"
    // K1: the mesher's stored path (VoxelLandCaveCarve.ush with VOXEL_CAVE_STORED) on lists the
    // harness gathers as the gather pass does (mode k1): one brick, VOXEL_CAVE_SLOTS heads.
    std::vector<FVoxelCaveListHead> GHeads;
    std::vector<FVoxelCavePrim> GPrims;
#define VOXEL_CAVE_STORED 1
#define VOXEL_CAVE_HEAD(I) GHeads[I]
#define VOXEL_CAVE_PRIM(I) GPrims[I]
#include "VoxelLandCaveCarve.ush"
}
using VoxelShader::FVoxelCaveSample;
using VoxelShader::FVoxelCaveResult;

// ---------------------------------------------------------------------------------------------
// Set-up
// ---------------------------------------------------------------------------------------------

static void EnvF(const char* Name, float& V) { if (const char* E = getenv(Name)) V = (float)atof(E); }
static void EnvU(const char* Name, uint& V) { if (const char* E = getenv(Name)) V = (uint)atoi(E); }
static double EnvD(const char* Name, double V) { const char* E = getenv(Name); return E ? atof(E) : V; }

static std::vector<float> GBase, GCaveH, GCaveR, GCaveF, GCaveP;   // the cave bake's fields, unpacked (the sites, the threshold)
static std::vector<uint32_t> GCaveWords;                             // and packed (VoxelLandCaves.ush reads these)
static void TexelDir(const FPlanet& P, int Face, int J, int K, double O[3]);
struct FFaceAt { uint Face; double J, K, A, B; };
static FFaceAt FaceAt(const FPlanet& P, const double D[3]);
template <typename FGet> static double BakeLerp(const FPlanet& P, const FFaceAt& F, FGet Get);
static double GKarstThreshold = 0, GKarstCoverage = 0.25;
static int GM = 16;

#define CAVE_PARAM_F(X) EnvF(#X, VoxelShader::X)
#define CAVE_PARAM_U(X) EnvU(#X, VoxelShader::X)

static void SetupCaves(FPlanet& P)
{
    using namespace VoxelShader;
    // The cave bake: per padded cell, the bake through the land's warp (Terms' Q), then the
    // base level: the lowest such height (sea level for the sea) over VoxelLandRelief's window.
    const int Side = P.Side();
    const size_t Cells = (size_t)6 * Side * Side;
    GCaveH.assign(Cells, 0.0f); GCaveR.assign(Cells, 0.0f); GCaveF.assign(Cells, 0.0f); GCaveP.assign(Cells, 0.0f);
    ParallelFor(6 * Side, [&](int Row) {
        const int F = Row / Side, k = Row % Side - P.Pad;
        for (int i = 0; i < Side; i++)
        {
            const int j = i - P.Pad;
            double D[3]; TexelDir(P, F, j, k, D);
            double SP[3] = { D[0] * P.SeaRadius, D[1] * P.SeaRadius, D[2] * P.SeaRadius }, Q[3];
            for (int a = 0; a < 3; a++) Q[a] = SP[a] + Evaluate(P.W[a], SP, 0.0) * P.WarpAmp;
            const auto Bk = P.S.VoxelLandBakeSampleDF(DF(Q[0]), DF(Q[1]), DF(Q[2]), true);
            const size_t I = (size_t)F * Side * Side + (size_t)(k + P.Pad) * Side + i;
            double QN[3] = { Q[0], Q[1], Q[2] }; Normalize(QN);
            const FFaceAt FQ = FaceAt(P, QN);
            GCaveH[I] = Bk.Height; GCaveR[I] = Bk.Relief; GCaveF[I] = Bk.Flow;
            GCaveP[I] = (float)BakeLerp(P, FQ, [&](size_t n) { return P.Province[n] / 63.0; });
        }
    });
    GBase.assign(Cells, 0.0f);
    ParallelFor(6 * Side, [&](int Row) {
        const int F = Row / Side, j0 = Row % Side;
        for (int i = 0; i < Side; i++)
        {
            float Lo = 3e38f;
            for (int dj = -2; dj <= 2; dj++)
                for (int di = -2; di <= 2; di++)
                {
                    if (di * di + dj * dj > 5) continue;
                    const int j = j0 + dj, k = i + di;
                    if (j < 0 || j >= Side || k < 0 || k >= Side) continue;
                    Lo = std::min(Lo, std::max(GCaveH[(size_t)F * Side * Side + (size_t)j * Side + k], 0.0f));
                }
            GBase[(size_t)F * Side * Side + (size_t)j0 * Side + i] = Lo;
        }
    });
    // K1: packed as the engine packs it (FVoxelTerrestrialDensity::MakeCaveBake, VoxelCavePackWord).
    GCaveWords.assign(2 * Cells, 0u);
    for (size_t I = 0; I < Cells; I++)
    {
        GCaveWords[2 * I] = asuint(GCaveH[I]);
        GCaveWords[2 * I + 1] = VoxelCavePackWord(GBase[I], GCaveF[I], GCaveR[I], GCaveP[I]);
    }
    CaveBake = GCaveWords.data();
    // The lattice: cave cells of about 192 m (CAVE_CELL) on the bake's.
    const double BakeCellM = P.Cell * 0.01;
    GM = std::max(5, (int)std::lround(BakeCellM / EnvD("CAVE_CELL", 192.0)));
    CaveCellsPerBake = (uint)GM;
    CaveCellM = (float)(BakeCellM / GM);
    // The karst threshold: the province above which KARST_COVERAGE of the land lies
    // (FVoxelLandBake::ProvinceThreshold's rule).
    GKarstCoverage = EnvD("KARST_COVERAGE", 0.25);
    {
        uint64_t Count[64] = {}, Total = 0;
        for (size_t i = 0; i < GCaveP.size(); i++) if (GCaveH[i] > 0.0f) { Count[std::clamp((int)std::lround(GCaveP[i] * 63.0f), 0, 63)]++; Total++; }
        const double Wanted = GKarstCoverage * (double)Total;
        double Above = 0.0;
        GKarstThreshold = 2.0;
        for (int q = 63; q >= 0; q--)
        {
            const double C = (double)Count[q];
            if (Above + C >= Wanted && C > 0.0) { GKarstThreshold = (q + 0.5 - (Wanted - Above) / C) / 63.0; break; }
            Above += C;
        }
    }
    CaveKarstLo = (float)(GKarstThreshold - 0.02); CaveKarstHi = (float)(GKarstThreshold + 0.02);
    CAVE_PARAM_U(CaveOn); CAVE_PARAM_U(CaveSeed); CAVE_PARAM_U(CaveLevels); CAVE_PARAM_U(CaveRoughCells); CAVE_PARAM_U(CaveRoughOctaves);
    CAVE_PARAM_F(CaveLevelM); CAVE_PARAM_F(CaveTableShare); CAVE_PARAM_F(CaveFloorClearM);
    CAVE_PARAM_F(CavePhiWaveCells); CAVE_PARAM_F(CavePhiNoiseM); CAVE_PARAM_F(CavePhiJitterM); CAVE_PARAM_F(CavePhiNoiseSlope);
    CAVE_PARAM_F(CaveTrunkFlowLo); CAVE_PARAM_F(CaveTrunkFlowHi);
    CAVE_PARAM_F(CaveRadiusM); CAVE_PARAM_F(CaveRadiusSpread); CAVE_PARAM_F(CaveTrunkGain); CAVE_PARAM_F(CaveRadiusMaxM);
    CAVE_PARAM_F(CaveKarstLo); CAVE_PARAM_F(CaveKarstHi); CAVE_PARAM_F(CaveKarstSlack);
    CAVE_PARAM_F(CaveActiveShare); CAVE_PARAM_F(CaveTrunkActive);
    CAVE_PARAM_F(CaveChamberShare); CAVE_PARAM_F(CaveChamberRadiusM); CAVE_PARAM_F(CaveChamberSquash);
    CAVE_PARAM_F(CaveShaftShare); CAVE_PARAM_F(CaveShaftRadiusM); CAVE_PARAM_F(CaveMouthShare);
    CAVE_PARAM_F(CaveDolineShare); CAVE_PARAM_F(CaveDolineRadiusM); CAVE_PARAM_F(CaveDolineHeadM); CAVE_PARAM_F(CaveDolineFunnel); CAVE_PARAM_F(CaveDolineBowlM);
    CAVE_PARAM_F(CaveDeepCellM); CAVE_PARAM_F(CaveDepthM); CAVE_PARAM_F(CaveShallowDepthM);
    CAVE_PARAM_F(CaveRoughM); CAVE_PARAM_F(CaveRoughGain); CAVE_PARAM_F(CaveBeddingM); CAVE_PARAM_F(CaveBeddingWaveM);
    CAVE_PARAM_F(CaveBlendM); CAVE_PARAM_F(CaveBandSlackM);
    CAVE_PARAM_F(CaveSeaShare); CAVE_PARAM_F(CaveSeaCoastM); CAVE_PARAM_F(CaveSeaZM); CAVE_PARAM_F(CaveSeaRadiusM); CAVE_PARAM_F(CaveSeaReliefM);
    CAVE_PARAM_F(CaveSeaTall); CAVE_PARAM_F(CaveSeaFloorM); CAVE_PARAM_F(CaveSeaRoofM); CAVE_PARAM_F(CaveSeaCoverM); CAVE_PARAM_U(CaveSeaCells);
    CAVE_PARAM_F(CaveFloorFill); CAVE_PARAM_F(CaveCanyonDepth); CAVE_PARAM_F(CaveCanyonWidth);
    CAVE_PARAM_F(CaveMeander); CAVE_PARAM_F(CaveMeanderZM); CAVE_PARAM_F(CaveCoverM); CAVE_PARAM_F(CaveCoverSlope);
    // The largest radius any primitive takes (VoxelLand::CaveConstantsFor's rule).
    CaveMaxRadiusM = std::max({ CaveRadiusMaxM, CaveChamberRadiusM * 1.5f * CaveChamberSquash, CaveShaftRadiusM * 1.4f, CaveDolineRadiusM * 1.4f, CaveSeaRadiusM * 1.5f });
}

// ---------------------------------------------------------------------------------------------
// Samples
// ---------------------------------------------------------------------------------------------

static double SeaM(const FPlanet& P) { return P.SeaRadius * 0.01; }

/** A direction's face, its J and K (bake texel-centre units), and the face's tan coordinates. */
static FFaceAt FaceAt(const FPlanet& P, const double D[3])
{
    int Axis = 0;
    for (int a = 1; a < 3; a++) if (std::fabs(D[a]) > std::fabs(D[Axis])) Axis = a;
    FFaceAt F;
    F.Face = (uint)(Axis * 2 + (D[Axis] < 0 ? 1 : 0));
    const double Den = std::fabs(D[Axis]);
    F.A = D[VoxelShader::VoxelCubeUAxis(F.Face)] / Den;
    F.B = D[VoxelShader::VoxelCubeVAxis(F.Face)] / Den;
    const double S = std::atan(F.A) * 4.0 / M_PI, T = std::atan(F.B) * 4.0 / M_PI;
    F.J = (S + 1.0) * 0.5 * P.N - 0.5;
    F.K = (T + 1.0) * 0.5 * P.N - 0.5;
    return F;
}

/** A bake field at a direction, bilinear between texel centres. */
template <typename FGet>
static double BakeLerp(const FPlanet& P, const FFaceAt& F, FGet Get)
{
    const int Side = P.Side();
    const int J = (int)std::floor(F.J), K = (int)std::floor(F.K);
    const double FX = F.J - J, FY = F.K - K;
    auto At = [&](int j, int k) { j = std::clamp(j, -P.Pad, P.N + P.Pad - 1); k = std::clamp(k, -P.Pad, P.N + P.Pad - 1);
        return Get((size_t)F.Face * Side * Side + (size_t)(k + P.Pad) * Side + (size_t)(j + P.Pad)); };
    return (At(J, K) * (1 - FX) + At(J + 1, K) * FX) * (1 - FY) + (At(J, K + 1) * (1 - FX) + At(J + 1, K + 1) * FX) * FY;
}

/** A column: a direction and the land there (heights in m about sea level). */
struct FColumn { double Dir[3]; double Land = 0, Bake = 0; float Province = 0; };

static FColumn ColumnAt(const FPlanet& P, const double D[3], double FootM)
{
    FColumn C;
    for (int a = 0; a < 3; a++) C.Dir[a] = D[a];
    const FTerms T = Terms(P, D, FootM * 100.0);
    C.Land = T.Height; C.Bake = T.Bake;
    const FFaceAt F = FaceAt(P, D);
    C.Province = (float)BakeLerp(P, F, [&](size_t i) { return (double)GCaveP[i]; });
    return C;
}

static FVoxelCaveSample SampleAt(const FPlanet& P, const FColumn& Col, double Z, double FootM)
{
    using namespace VoxelShader;
    const FFaceAt F = FaceAt(P, Col.Dir);
    FVoxelCaveSample S;
    S.Face = F.Face;
    const double JU = F.J * GM, JV = F.K * GM;
    S.IU = (int)std::floor(JU); S.IV = (int)std::floor(JV);
    S.FU = (float)(JU - S.IU); S.FV = (float)(JV - S.IV);
    S.Z = (float)Z;
    const FVoxelLandFaceFrame Fr = VoxelLandFaceFrameAt(F.Face, (float)F.A, (float)F.B);
    const double Scale = (SeaM(P) + Z) * (M_PI / (2.0 * P.N)) / GM;
    S.EU = Fr.TU * (float)Scale;
    S.EV = Fr.TV * (float)Scale;
    S.Up = float3((float)Col.Dir[0], (float)Col.Dir[1], (float)Col.Dir[2]);
    S.Land = (float)Col.Land; S.Bake = (float)Col.Bake; S.Province = Col.Province;
    S.Footprint = (float)FootM;
    return S;
}

struct FDensity { float D; float Cave; uint Kind; uint Stage; uint Open; };

/** The carved density (m, positive in air: the vertical distance above the land, then the caves). */
static FDensity DensityAt(const FPlanet& P, const FColumn& Col, double Z, double FootM)
{
    const FVoxelCaveSample S = SampleAt(P, Col, Z, FootM);
    const FVoxelCaveResult R = VoxelShader::VoxelCaveDistance(S);
    FDensity Out;
    Out.Cave = R.Distance; Out.Kind = R.Kind; Out.Stage = R.Stage; Out.Open = R.Open;
    Out.D = VoxelShader::VoxelCaveCarve((float)(Z - Col.Land), R.Distance, 1.0f);
    return Out;
}

static void Mul(const double A[3], double S, double O[3]) { for (int a = 0; a < 3; a++) O[a] = A[a] * S; }

/** The direction at tangent offset (X, Y) m from C (gnomonic, as the views' grids). */
static void DirAt(const FPlanet& P, const double C[3], const double E1[3], const double E2[3], double X, double Y, double O[3])
{
    const double R = SeaM(P);
    for (int a = 0; a < 3; a++) O[a] = C[a] + (E1[a] * X + E2[a] * Y) / R;
    Normalize(O);
}

static void Point(const FPlanet& P, const double D[3], double Z, double O[3]) { Mul(D, SeaM(P) + Z, O); }

/** The water table at a direction (level 0 before the potential's noise). */
static double TableAt(const FPlanet& P, const double D[3])
{
    const FFaceAt F = FaceAt(P, D);
    const double H = BakeLerp(P, F, [&](size_t i) { return (double)GCaveH[i]; });
    const double B = BakeLerp(P, F, [&](size_t i) { return (double)GBase[i]; });
    return std::max(B + VoxelShader::CaveTableShare * std::max(H - B, 0.0), (double)VoxelShader::CaveFloorClearM);
}

// ---------------------------------------------------------------------------------------------
// Sites
// ---------------------------------------------------------------------------------------------

/** The direction of a bake texel's centre. */
static void TexelDir(const FPlanet& P, int Face, int J, int K, double O[3])
{
    const double S = (J + 0.5) * 2.0 / P.N - 1.0, T = (K + 0.5) * 2.0 / P.N - 1.0;
    const double A = std::tan(S * M_PI / 4), B = std::tan(T * M_PI / 4);
    const uint N = VoxelShader::VoxelCubeNormalAxis(Face), U = VoxelShader::VoxelCubeUAxis(Face), V = VoxelShader::VoxelCubeVAxis(Face);
    O[N] = (Face & 1) ? -1.0 : 1.0; O[U] = A; O[V] = B;
    Normalize(O);
}

struct FSite { double Dir[3]; double Height, Relief, Flow, Province; };

/** Sites: 0 karst valleys (a medium river through karst with relief), 1 cliff coasts, 2 karst
 *  uplands. At least 50 km apart. */
static std::vector<FSite> FindSites(const FPlanet& P, int Which, int Count)
{
    const int Side = P.Side();
    std::vector<std::pair<double, size_t>> Ranked;
    for (int F = 0; F < 6; F++)
        for (int K = 3; K < P.N - 3; K++)
            for (int J = 3; J < P.N - 3; J++)
            {
                const size_t I = (size_t)F * Side * Side + (size_t)(K + P.Pad) * Side + (J + P.Pad);
                const double H = GCaveH[I], R = GCaveR[I], Pv = GCaveP[I];
                const double Fl = GCaveF[I];
                double Score = -1;
                if (Which == 0 && Pv > GKarstThreshold + 0.06 && H > 150 && H < 1500 && Fl > 27 && Fl < 31.5) Score = R;
                if (Which == 1 && H > 2 && H < 60 && R > 400)
                {
                    bool bSea = false;
                    for (int d = -1; d <= 1; d += 2) bSea |= GCaveH[I + d] < -5 || GCaveH[I + d * Side] < -5;
                    if (bSea) Score = R;
                }
                if (Which == 2 && Pv > GKarstThreshold + 0.06 && H > 300 && R > 150 && R < 700 && Fl < 26) Score = Pv;
                if (Score > 0) Ranked.push_back({ -Score, I });
            }
    std::sort(Ranked.begin(), Ranked.end());
    std::vector<FSite> Out;
    for (auto& [S, I] : Ranked)
    {
        const int F = (int)(I / ((size_t)Side * Side)), K = (int)((I / Side) % Side) - P.Pad, J = (int)(I % Side) - P.Pad;
        FSite Site;
        TexelDir(P, F, J, K, Site.Dir);
        bool bFar = true;
        for (auto& O : Out) { double Dt = 0; for (int a = 0; a < 3; a++) Dt += O.Dir[a] * Site.Dir[a]; if (Dt > std::cos(50e3 / SeaM(P))) bFar = false; }
        if (!bFar) continue;
        Site.Height = GCaveH[I]; Site.Relief = GCaveR[I]; Site.Flow = GCaveF[I]; Site.Province = GCaveP[I];
        Out.push_back(Site);
        if ((int)Out.size() >= Count) break;
    }
    return Out;
}

// ---------------------------------------------------------------------------------------------
// A local grid of columns (the views, mouths and the cache columns): Px x Px over WidthM.
// ---------------------------------------------------------------------------------------------

struct FGrid
{
    double C[3], E1[3], E2[3];
    double Width = 0, Pix = 0; int Px = 0;
    std::vector<FColumn> Cols;
    const FColumn& At(int x, int y) const { return Cols[(size_t)y * Px + x]; }
};

static FGrid MakeGrid(const FPlanet& P, const double C[3], double WidthM, int Px, double FootM)
{
    FGrid G;
    for (int a = 0; a < 3; a++) G.C[a] = C[a];
    Frame(G.C, G.E1, G.E2);
    G.Width = WidthM; G.Px = Px; G.Pix = WidthM / Px;
    G.Cols.resize((size_t)Px * Px);
    ParallelFor(Px, [&](int y) {
        for (int x = 0; x < Px; x++)
        {
            double D[3];
            DirAt(P, G.C, G.E1, G.E2, (x + 0.5) * G.Pix - 0.5 * WidthM, (y + 0.5) * G.Pix - 0.5 * WidthM, D);
            G.Cols[(size_t)y * Px + x] = ColumnAt(P, D, FootM);
        }
    });
    return G;
}

/** The grid's column at a direction (bilinear land, bake and province; the direction itself). */
static FColumn GridColumn(const FPlanet& P, const FGrid& G, const double D[3])
{
    double Dc = 0, X = 0, Y = 0;
    for (int a = 0; a < 3; a++) { Dc += D[a] * G.C[a]; X += D[a] * G.E1[a]; Y += D[a] * G.E2[a]; }
    X = X / Dc * SeaM(P); Y = Y / Dc * SeaM(P);
    const double GX = std::clamp((X + 0.5 * G.Width) / G.Pix - 0.5, 0.0, G.Px - 1.001), GY = std::clamp((Y + 0.5 * G.Width) / G.Pix - 0.5, 0.0, G.Px - 1.001);
    const int IX = (int)GX, IY = (int)GY;
    const double FX = GX - IX, FY = GY - IY;
    const FColumn& A = G.At(IX, IY); const FColumn& B = G.At(IX + 1, IY); const FColumn& C = G.At(IX, IY + 1); const FColumn& E = G.At(IX + 1, IY + 1);
    FColumn O;
    for (int a = 0; a < 3; a++) O.Dir[a] = D[a];
    auto L = [&](double a, double b, double c, double e) { return (a * (1 - FX) + b * FX) * (1 - FY) + (c * (1 - FX) + e * FX) * FY; };
    O.Land = L(A.Land, B.Land, C.Land, E.Land); O.Bake = L(A.Bake, B.Bake, C.Bake, E.Bake);
    O.Province = (float)L(A.Province, B.Province, C.Province, E.Province);
    return O;
}

/** The carved density at a point (m from the planet's centre) through a grid. */
static FDensity DensityGrid(const FPlanet& P, const FGrid& G, const double X[3], double FootM)
{
    const double R = std::sqrt(X[0] * X[0] + X[1] * X[1] + X[2] * X[2]);
    double D[3] = { X[0] / R, X[1] / R, X[2] / R };
    return DensityAt(P, GridColumn(P, G, D), R - SeaM(P), FootM);
}

// ---------------------------------------------------------------------------------------------
// Images
// ---------------------------------------------------------------------------------------------

static float3 KindColour(uint Kind)
{
    switch (Kind)
    {
    case 1: return float3(0.12f, 0.32f, 0.88f);   // tube
    case 2: return float3(0.10f, 0.70f, 0.90f);   // keyhole
    case 3: return float3(0.88f, 0.25f, 0.72f);   // chamber
    case 4: return float3(0.98f, 0.68f, 0.10f);   // shaft
    case 5: return float3(0.98f, 0.15f, 0.10f);   // doline
    case 6: return float3(0.10f, 0.82f, 0.35f);   // sea cave
    default: return float3(0.5f, 0.5f, 0.5f);
    }
}

static void SavePPM(const std::string& Name, int W, int H, const std::vector<float3>& Px)
{
    WritePPM(Name, W, H, Px);
    printf("  wrote %s (%d x %d)\n", Name.c_str(), W, H);
}

/** A vertical section WidthM long along E (unit tangent at C), ZLo..ZHi m about sea level. */
static void Section(const FPlanet& P, const std::string& Name, const double C[3], const double E[3], double WidthM, double ZLo, double ZHi, int W, int H, double FootM)
{
    std::vector<float3> Img((size_t)W * H);
    ParallelFor(W, [&](int x) {
        double D[3];
        for (int a = 0; a < 3; a++) D[a] = C[a] + E[a] * (((x + 0.5) / W - 0.5) * WidthM) / SeaM(P);
        Normalize(D);
        const FColumn Col = ColumnAt(P, D, FootM);
        const double Table = TableAt(P, D);
        const double Px = (ZHi - ZLo) / H;
        for (int y = 0; y < H; y++)
        {
            const double Z = ZHi - (y + 0.5) * Px;
            const FDensity Ds = DensityAt(P, Col, Z, FootM);
            float3 Cl;
            if (Ds.D > 0.0f)
            {
                if (Z > Col.Land) Cl = Z < 0 ? float3(0.15f, 0.35f, 0.65f) : float3(0.80f, 0.88f, 0.96f);
                else { Cl = KindColour(Ds.Kind); if (Z < 0) Cl = Cl * 0.6f + float3(0.0f, 0.1f, 0.35f); }
            }
            else
            {
                const float Shade = (float)std::clamp(1.0 - (Col.Land - Z) / 1500.0, 0.3, 1.0);
                Cl = (Col.Province >= GKarstThreshold ? float3(0.70f, 0.67f, 0.58f) : float3(0.50f, 0.47f, 0.44f)) * Shade;
                if (std::fabs(Z - Table) < 0.5 * Px) Cl = float3(0.25f, 0.55f, 0.95f);   // the water table
            }
            if (std::fabs(Z) < 0.5 * Px) Cl = float3(0.0f, 0.15f, 0.55f);                // sea level
            Img[(size_t)y * W + x] = Cl;
        }
    });
    SavePPM(Name, W, H, Img);
}

/** A plan view: hillshaded land, the caves projected (the shallowest's kind, darker deeper),
 *  mouths red. */
static void Plan(const FPlanet& P, const std::string& Name, const FGrid& G, double FootM, double ZStep)
{
    const int Px = G.Px;
    std::vector<float3> Img((size_t)Px * Px);
    ParallelFor(Px, [&](int y) {
        for (int x = 0; x < Px; x++)
        {
            const FColumn& Col = G.At(x, y);
            const double HX = G.At(std::min(x + 1, Px - 1), y).Land - G.At(std::max(x - 1, 0), y).Land;
            const double HY = G.At(x, std::min(y + 1, Px - 1)).Land - G.At(x, std::max(y - 1, 0)).Land;
            const double Sl = std::clamp(0.7 + (HX - HY) / (4.0 * G.Pix), 0.2, 1.2);
            float3 Cl = Col.Land < 0 ? float3(0.2f, 0.35f, 0.6f) : (Col.Province >= GKarstThreshold ? float3(0.82f, 0.80f, 0.72f) : float3(0.60f, 0.67f, 0.50f));
            Cl = Cl * (float)Sl;
            const FDensity Top = DensityAt(P, Col, Col.Land - 0.5, FootM);
            if (Col.Land > 0 && Top.D > 0) { Img[(size_t)y * Px + x] = float3(1.0f, 0.1f, 0.05f); continue; }
            // PLAN_LEVEL k: only the band of level k (its water table altitude +-14 m).
            const int Lv = (int)EnvD("PLAN_LEVEL", -1);
            const double T = TableAt(P, Col.Dir) + Lv * VoxelShader::CaveLevelM;
            const double ZTop = Lv >= 0 ? std::min(Col.Land - 2.0, T + 14.0) : Col.Land - 2.0;
            const double ZBot = Lv >= 0 ? T - 14.0 : Col.Land - 700.0;
            for (double Z = ZTop; Z > ZBot && Z > -5.0; Z -= ZStep)
            {
                const FDensity Ds = DensityAt(P, Col, Z, FootM);
                if (Ds.Stage == 0 && Z < Col.Bake - 900) break;
                if (Ds.D > 0)
                {
                    const float Deep = (float)std::clamp((Col.Land - Z) / 500.0, 0.0, 1.0);
                    Cl = KindColour(Ds.Kind) * (1.0f - 0.65f * Deep) + Cl * 0.15f;
                    break;
                }
            }
            Img[(size_t)y * Px + x] = Cl;
        }
    });
    SavePPM(Name, Px, Px, Img);
}

/** A ray-marched view through a grid: camera at Eye (m from the centre) looking along Fwd. */
static void March(const FPlanet& P, const std::string& Name, const FGrid& G, const double Eye[3], const double Fwd[3], double FovDeg, int W, int H, double FootM, double MaxDist, bool bHeadlight)
{
    double Up[3]; { const double R = std::sqrt(Eye[0] * Eye[0] + Eye[1] * Eye[1] + Eye[2] * Eye[2]); for (int a = 0; a < 3; a++) Up[a] = Eye[a] / R; }
    double F[3] = { Fwd[0], Fwd[1], Fwd[2] }; Normalize(F);
    double Rt[3] = { F[1] * Up[2] - F[2] * Up[1], F[2] * Up[0] - F[0] * Up[2], F[0] * Up[1] - F[1] * Up[0] }; Normalize(Rt);
    double U[3] = { Rt[1] * F[2] - Rt[2] * F[1], Rt[2] * F[0] - Rt[0] * F[2], Rt[0] * F[1] - Rt[1] * F[0] };
    const double T = std::tan(FovDeg * M_PI / 360.0);
    double Sun[3]; for (int a = 0; a < 3; a++) Sun[a] = Up[a] * 0.8 + Rt[a] * 0.45 + F[a] * 0.2; Normalize(Sun);
    std::vector<float3> Img((size_t)W * H);
    ParallelFor(H, [&](int y) {
        for (int x = 0; x < W; x++)
        {
            const double SX = ((x + 0.5) / W * 2 - 1) * T, SY = (1 - (y + 0.5) / H * 2) * T * H / W;
            double Dir[3]; for (int a = 0; a < 3; a++) Dir[a] = F[a] + Rt[a] * SX + U[a] * SY; Normalize(Dir);
            double Tt = 0.05; bool bHit = false; FDensity Last{};
            double X[3];
            for (int i = 0; i < 700 && Tt < MaxDist; i++)
            {
                for (int a = 0; a < 3; a++) X[a] = Eye[a] + Dir[a] * Tt;
                Last = DensityGrid(P, G, X, FootM);
                if (Last.D < 0.02 * (1.0 + Tt * 0.002)) { bHit = true; break; }
                Tt += std::max(Last.D * 0.38, 0.03 + Tt * 0.0005);
            }
            float3 Cl;
            double Ux = 0; for (int a = 0; a < 3; a++) Ux += Dir[a] * Up[a];
            const float3 Sky = float3(0.62f, 0.75f, 0.92f) * (float)(0.8 + 0.2 * std::max(Ux, 0.0));
            if (!bHit) Cl = bHeadlight ? float3(0.0f, 0.0f, 0.0f) : Sky;
            else
            {
                double N[3]; const double Hd = std::max(0.08, Tt * 0.002);
                for (int a = 0; a < 3; a++)
                {
                    double A1[3] = { X[0], X[1], X[2] }, A2[3] = { X[0], X[1], X[2] };
                    A1[a] += Hd; A2[a] -= Hd;
                    N[a] = DensityGrid(P, G, A1, FootM).D - DensityGrid(P, G, A2, FootM).D;
                }
                Normalize(N);
                const bool bCave = Last.Cave < 0.5f + (float)Hd;
                double NU = 0, NS = 0, NV = 0; for (int a = 0; a < 3; a++) { NU += N[a] * Up[a]; NS += N[a] * Sun[a]; NV -= N[a] * Dir[a]; }
                float3 Albedo = bCave ? float3(0.62f, 0.57f, 0.50f) : (NU > 0.75 ? float3(0.42f, 0.52f, 0.30f) : float3(0.56f, 0.53f, 0.48f));
                const double Zp = std::sqrt(X[0] * X[0] + X[1] * X[1] + X[2] * X[2]) - SeaM(P);
                if (!bCave && Zp < 0.3) Albedo = float3(0.25f, 0.35f, 0.55f);
                double Light;
                if (bHeadlight) Light = (0.12 + 0.88 * std::max(NV, 0.0)) * std::min(1.0, 900.0 / (Tt * Tt + 30.0));
                else
                {
                    double Sh = 1.0, St = 0.5;
                    for (int s = 0; s < 48 && St < 300 && NS > 0; s++)
                    {
                        double Y[3]; for (int a = 0; a < 3; a++) Y[a] = X[a] + N[a] * 0.1 + Sun[a] * St;
                        const float Dd = DensityGrid(P, G, Y, FootM).D;
                        if (Dd < 0.05f) { Sh = 0.0; break; }
                        Sh = std::min(Sh, 8.0 * Dd / St);
                        St += std::max((double)Dd * 0.4, 0.2);
                    }
                    Light = 0.22 + 0.15 * NU + 0.75 * std::max(NS, 0.0) * std::clamp(Sh, 0.0, 1.0);
                }
                Cl = Albedo * (float)Light;
                if (!bHeadlight) { const float Fog = (float)(1.0 - std::exp(-Tt / 6000.0)); Cl = Cl * (1.0f - Fog) + Sky * Fog; }
            }
            Img[(size_t)y * W + x] = Cl;
        }
    });
    SavePPM(Name, W, H, Img);
}

// ---------------------------------------------------------------------------------------------
// Mouths
// ---------------------------------------------------------------------------------------------

struct FMouthStats { int Components = 0; double KarstKm2 = 0, AreaKm2 = 0; std::map<uint, int> ByKind; int OpenTexels = 0; std::vector<std::pair<int, int>> Centres; };

static FMouthStats CountMouths(const FPlanet& P, const FGrid& G, double FootM, std::vector<uint8_t>* OutMask = nullptr)
{
    const int Px = G.Px;
    std::vector<uint8_t> Mask((size_t)Px * Px, 0), KindOf((size_t)Px * Px, 0);
    std::atomic<int> Karst{ 0 };
    ParallelFor(Px, [&](int y) {
        for (int x = 0; x < Px; x++)
        {
            const FColumn& Col = G.At(x, y);
            if (Col.Province >= GKarstThreshold && Col.Land > 0) Karst++;
            if (Col.Land <= 0.2) continue;
            // Open where the point 0.7 m into the rock along the surface's normal is air (so
            // openings in steep walls and cliffs count too).
            const double HX = (G.At(std::min(x + 1, Px - 1), y).Land - G.At(std::max(x - 1, 0), y).Land) / (2 * G.Pix);
            const double HY = (G.At(x, std::min(y + 1, Px - 1)).Land - G.At(x, std::max(y - 1, 0)).Land) / (2 * G.Pix);
            const double NL = std::sqrt(1 + HX * HX + HY * HY);
            double X[3], Dd[3]; DirAt(P, G.C, G.E1, G.E2, (x + 0.5) * G.Pix - 0.5 * G.Width + 0.7 * HX / NL, (y + 0.5) * G.Pix - 0.5 * G.Width + 0.7 * HY / NL, Dd);
            const FColumn Cn = GridColumn(P, G, Dd);
            Point(P, Dd, Col.Land - 0.7 / NL, X);
            const FDensity D = DensityAt(P, Cn, Col.Land - 0.7 / NL, FootM);
            if (D.D > 0) { Mask[(size_t)y * Px + x] = 1; KindOf[(size_t)y * Px + x] = (uint8_t)D.Kind; }
        }
    });
    FMouthStats S;
    S.AreaKm2 = (G.Width * G.Width) * 1e-6;
    S.KarstKm2 = Karst * G.Pix * G.Pix * 1e-6;
    std::vector<int> Label((size_t)Px * Px, -1);
    for (int i = 0; i < Px * Px; i++)
    {
        if (!Mask[i]) continue;
        S.OpenTexels++;
        if (Label[i] >= 0) continue;
        std::vector<int> Stack = { i }; Label[i] = S.Components;
        std::map<uint, int> Kinds; long SX = 0, SY = 0, N = 0;
        while (!Stack.empty())
        {
            const int k = Stack.back(); Stack.pop_back();
            Kinds[KindOf[k]]++;
            const int x = k % Px, y = k / Px; SX += x; SY += y; N++;
            for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++)
            {
                const int X = x + dx, Y = y + dy;
                if (X < 0 || Y < 0 || X >= Px || Y >= Px) continue;
                const int n = Y * Px + X;
                if (Mask[n] && Label[n] < 0) { Label[n] = S.Components; Stack.push_back(n); }
            }
        }
        uint Best = 0; int BestN = -1; for (auto& [K, Nk] : Kinds) if (Nk > BestN) { BestN = Nk; Best = K; }
        S.ByKind[Best]++;
        S.Centres.push_back({ (int)(SX / N), (int)(SY / N) });
        S.Components++;
    }
    if (OutMask) *OutMask = Mask;
    return S;
}

// ---------------------------------------------------------------------------------------------
// The brick tree (FVoxelBrickTerrain's descent, reduced to a box around the camera)
// ---------------------------------------------------------------------------------------------

struct FBrickCount { std::vector<int> Wanted, Surface; std::vector<int64_t> Samples; };

static FBrickCount Bricks(const FPlanet& P, const double Cam[3], int Top, double BoxM, int CellsPerBrick, double Factor, double Slope)
{
    const double Finest = P.Finest * 0.01;
    const double Root = (P.R + P.NA) * 1.05 * 0.01;
    FBrickCount Out;
    Out.Wanted.assign(Top + 1, 0); Out.Surface.assign(Top + 1, 0); Out.Samples.assign(Top + 1, 0);
    // CaveFoot: the footprint the cave term is evaluated at (the land's is FootM). A brick the
    // camera will split is tested with the caves at the finest footprint (their LOD and depth
    // gates are per level, so a coarser level's field doesn't bound its children's: testing a
    // split brick with its own would cull a cave the finer levels have), unless BRICK_NAIVE.
    auto Density = [&](const double X[3], double FootM, double CaveFoot) {
        const double R = std::sqrt(X[0] * X[0] + X[1] * X[1] + X[2] * X[2]);
        double D[3] = { X[0] / R, X[1] / R, X[2] / R };
        const FColumn Col = ColumnAt(P, D, FootM);
        const FVoxelCaveResult Cv = VoxelShader::VoxelCaveDistance(SampleAt(P, Col, R - SeaM(P), CaveFoot));
        return VoxelShader::VoxelCaveCarve((float)(R - SeaM(P) - Col.Land), Cv.Distance, 1.0f);
    };
    const bool bNaive = getenv("BRICK_NAIVE") != nullptr;
    struct FKey { int L; int64_t X, Y, Z; };
    std::vector<FKey> Level;
    {
        const double B = Finest * (1 << Top) * CellsPerBrick;
        int64_t Lo[3], Hi[3];
        for (int a = 0; a < 3; a++) { Lo[a] = (int64_t)std::floor((Cam[a] - BoxM + Root) / B); Hi[a] = (int64_t)std::floor((Cam[a] + BoxM + Root) / B); }
        for (int64_t z = Lo[2]; z <= Hi[2]; z++) for (int64_t y = Lo[1]; y <= Hi[1]; y++) for (int64_t x = Lo[0]; x <= Hi[0]; x++) Level.push_back({ Top, x, y, z });
    }
    while (!Level.empty())
    {
        const int L = Level[0].L;
        const double C = Finest * (1 << L), B = C * CellsPerBrick;
        std::vector<uint8_t> Visit(Level.size(), 0), Surf(Level.size(), 0);   // 0 cull, 1 want, 2 split
        std::atomic<int64_t> Samples{ 0 };
        ParallelFor((int)Level.size(), [&](int i) {
            const FKey& K = Level[i];
            const double Min[3] = { K.X * B - Root, K.Y * B - Root, K.Z * B - Root };
            // The test region: the brick with 2 cells below and 1 above (TestRegion).
            double Ctr[3]; for (int a = 0; a < 3; a++) Ctr[a] = Min[a] - 2 * C + 0.5 * (B + 3 * C);
            const double Rad = std::sqrt(3.0) * 0.5 * (B + 3 * C);
            double Dist2 = 0;
            for (int a = 0; a < 3; a++) { const double Dd = std::max({ Min[a] - Cam[a], 0.0, Cam[a] - (Min[a] + B) }); Dist2 += Dd * Dd; }
            const bool bSplit = L > 0 && std::sqrt(Dist2) < Factor * B;
            const float D = Density(Ctr, C, bSplit && !bNaive ? Finest : C);
            Samples++;
            if (std::fabs(D) > Slope * Rad) return;
            if (bSplit) { Visit[i] = 2; return; }
            Visit[i] = 1;
            // The mesher's coarse test, reduced: 4^3 quarter-brick blocks, then their halves; a
            // brick has surface when some half block isn't proven empty.
            const int NB = 4; const double SB = B / NB;
            for (int bz = 0; bz < NB; bz++) for (int by = 0; by < NB; by++) for (int bx = 0; bx < NB; bx++)
            {
                double Q[3] = { Min[0] + (bx + 0.5) * SB, Min[1] + (by + 0.5) * SB, Min[2] + (bz + 0.5) * SB };
                const float Dq = Density(Q, C, C);
                Samples++;
                if (std::fabs(Dq) > Slope * std::sqrt(3.0) * 0.5 * SB) continue;
                for (int s = 0; s < 8; s++)
                {
                    double R2[3] = { Q[0] + ((s & 1) ? 0.25 : -0.25) * SB, Q[1] + ((s & 2) ? 0.25 : -0.25) * SB, Q[2] + ((s & 4) ? 0.25 : -0.25) * SB };
                    const float Dr = Density(R2, C, C);
                    Samples++;
                    if (std::fabs(Dr) <= Slope * std::sqrt(3.0) * 0.25 * SB) { Surf[i] = 1; return; }
                }
            }
        });
        std::vector<FKey> Next;
        for (size_t i = 0; i < Level.size(); i++)
        {
            if (Visit[i] == 1) { Out.Wanted[L]++; Out.Surface[L] += Surf[i]; }
            if (Visit[i] == 2)
                for (int c = 0; c < 8; c++) Next.push_back({ L - 1, Level[i].X * 2 + (c & 1), Level[i].Y * 2 + ((c >> 1) & 1), Level[i].Z * 2 + ((c >> 2) & 1) });
        }
        Out.Samples[L] = Samples;
        Level.swap(Next);
    }
    return Out;
}

// ---------------------------------------------------------------------------------------------

static void PrintParams()
{
    using namespace VoxelShader;
    printf("caves: cell %.0f m (%u per bake cell), %u levels %.0f m apart from the water table (share %.2f), radius %.1f m (spread %.2f, trunks x%.1f, max %.0f m)\n",
        CaveCellM, CaveCellsPerBake, CaveLevels, CaveLevelM, CaveTableShare, CaveRadiusM, CaveRadiusSpread, 1 + CaveTrunkGain, CaveRadiusMaxM);
    printf("       karst above province %.3f (%.0f%% of the land); active %.2f (+%.2f trunks); chambers %.2f (%.0f m), shafts %.2f, mouths %.2f, dolines %.2f\n",
        GKarstThreshold, 100 * GKarstCoverage, CaveActiveShare, CaveTrunkActive, CaveChamberShare, CaveChamberRadiusM, CaveShaftShare, CaveMouthShare, CaveDolineShare);
    printf("       depth %.0f m at cells to %.0f m, %.0f m above; cover %.0f m (slope %.2f); detail %.2f m at %.1f m (%u octaves, gain %.2f), bedding %.2f m at %.1f m; blend %.1f m\n",
        CaveDepthM, CaveDeepCellM, CaveShallowDepthM, CaveCoverM, CaveCoverSlope, CaveRoughM, CaveCellM / CaveRoughCells, CaveRoughOctaves, CaveRoughGain, CaveBeddingM, CaveBeddingWaveM, CaveBlendM);
    printf("       sea caves %.2f a %u-cell node at |h| < %.0f m, relief > %.0f m: radius %.1f m (x%.1f tall), roof %.0f m, into the cliff to %.0f m of overburden\n", CaveSeaShare, CaveSeaCells, CaveSeaCoastM, CaveSeaReliefM, CaveSeaRadiusM, CaveSeaTall, CaveSeaRoofM, CaveSeaCoverM);
}

/** A point inside a cave near a site (for the underground camera and views). */
static bool FindInside(const FPlanet& P, const double C[3], double SearchM, double Want, double Out[3], double* OutZ, uint KindWanted = 0, int Seed = 5)
{
    std::mt19937 Rng(Seed);
    std::uniform_real_distribution<double> U(-1, 1);
    double E1[3], E2[3]; Frame(C, E1, E2);
    for (int i = 0; i < 60000; i++)
    {
        double D[3]; DirAt(P, C, E1, E2, U(Rng) * SearchM, U(Rng) * SearchM, D);
        const double Table = TableAt(P, D);
        const double Z = Table + (U(Rng) * 0.5 + 0.5) * VoxelShader::CaveLevelM * VoxelShader::CaveLevels;
        const FColumn Col = ColumnAt(P, D, P.Finest * 0.01);
        if (Col.Land - Z < 40) continue;
        const FDensity Ds = DensityAt(P, Col, Z, P.Finest * 0.01);
        if (Ds.Cave < -Want && (KindWanted == 0 || Ds.Kind == KindWanted))
        {
            for (int a = 0; a < 3; a++) Out[a] = D[a];
            *OutZ = Z;
            return true;
        }
    }
    return false;
}

int main(int argc, char** argv)
{
    if (argc < 5) { printf("usage: cave_harness Tag N RadiusM info|probe|slope|cost|brickcost|micro|mouths|seaprobe|bricks|views|k1\n"); return 1; }
    const std::string Tag = argv[1], Mode = argv[4];
    FPlanet P;
    Load(P, Tag, atoi(argv[2]), atof(argv[3]), 0, HarnessGullyStrength());
    std::vector<FVoxelOctave> Gpu;
    SetGpuGlobals(P, Gpu);
    SetupCaves(P);
    PrintParams();
    const double Fine = P.Finest * 0.01;
    const auto Valleys = FindSites(P, 0, 6), Cliffs = FindSites(P, 1, 6), Uplands = FindSites(P, 2, 6);
    const std::string Out = Tag + "_cave_";

    if (Mode == "info")
    {
        uint64_t Land = 0, Karst = 0;
        for (size_t i = 0; i < GCaveH.size(); i++) if (GCaveH[i] > 0) { Land++; if (GCaveP[i] >= GKarstThreshold) Karst++; }
        printf("land cells %llu, karst %llu (%.1f%%); bake cell %.0f m, finest lattice cell %.2f m\n", (unsigned long long)Land, (unsigned long long)Karst, 100.0 * Karst / std::max<uint64_t>(Land, 1), P.Cell * 0.01, Fine);
        const char* Names[] = { "valley", "coast", "upland" };
        int k = 0;
        for (auto* Set : { &Valleys, &Cliffs, &Uplands })
        {
            for (auto& S : *Set)
            {
                const FColumn Col = ColumnAt(P, S.Dir, Fine);
                printf("  %-6s h %5.0f m relief %5.0f m flow %4.1f province %.2f table %5.0f m; land %6.0f m, bake there %6.0f m\n", Names[k], S.Height, S.Relief, S.Flow, S.Province, TableAt(P, S.Dir), Col.Land, Col.Bake);
            }
            k++;
        }
    }
    else if (Mode == "probe")
    {
        // Columns around the first valley and upland: per column, the band's stages and how many
        // metres of it are cave.
        for (auto* Set : { &Valleys, &Uplands })
        {
            const FSite& Site = (*Set)[0];
            double E1[3], E2[3]; Frame(Site.Dir, E1, E2);
            std::mt19937 Rng(3); std::uniform_real_distribution<double> U(-1, 1);
            int Stage[3] = {}; double CaveM = 0, BandM = 0; float MinC = 1e9f; int Cols = 0;
            for (int c = 0; c < 300; c++)
            {
                double D[3]; DirAt(P, Site.Dir, E1, E2, U(Rng) * 3000, U(Rng) * 3000, D);
                const FColumn Col = ColumnAt(P, D, Fine);
                const double T = TableAt(P, D);
                Cols++;
                for (double Z = T - 30; Z < std::min(Col.Land, T + 200.0); Z += 1.0)
                {
                    const FDensity Ds = DensityAt(P, Col, Z, Fine);
                    Stage[Ds.Stage]++; BandM += 1; MinC = std::min(MinC, Ds.Cave);
                    if (Ds.D > 0) CaveM += 1;
                }
            }
            printf("probe %s: %d columns, band %.0f m: stages %d/%d/%d, cave %.0f m (%.2f%%), min distance %.2f m\n", Set == &Valleys ? "valley" : "upland", Cols, BandM, Stage[0], Stage[1], Stage[2], CaveM, 100 * CaveM / std::max(BandM, 1.0), MinC);
        }
    }
    else if (Mode == "micro")
    {
        // The pieces' cost (ns a call), at karst points of the valley site.
        using namespace VoxelShader;
        const FSite& Site = Valleys[0];
        const FColumn Col = ColumnAt(P, Site.Dir, Fine);
        FVoxelCaveSample S = SampleAt(P, Col, TableAt(P, Site.Dir) + 10, Fine);
        const int N = 200000; double Sink = 0;
        double T0 = Now();
        for (int i = 0; i < N; i++) { S.IU += (i & 1) ? 1 : -1; const FVoxelCavePatch Pt = VoxelCavePatchAt(S.Face, S.IU, S.IV); Sink += Pt.H[i % 9]; }
        printf("patch %.1f ns\n", (Now() - T0) / N * 1e9);
        const FVoxelCavePatch Pt = VoxelCavePatchAt(S.Face, S.IU, S.IV);
        T0 = Now();
        for (int i = 0; i < N; i++) { const FVoxelCaveLattice L = VoxelCaveLatticeAt(S, (uint)(i % 3)); Sink += L.V[i % 16]; }
        printf("lattice %.1f ns\n", (Now() - T0) / N * 1e9);
        const FVoxelCaveLattice L = VoxelCaveLatticeAt(S, 0);
        T0 = Now();
        for (int i = 0; i < N; i++) { const FVoxelCaveNode Nd = VoxelCavePlace(Pt, L, S, i % 3 - 1, (i / 3) % 3 - 1, 0); Sink += Nd.Z; }
        printf("place %.1f ns\n", (Now() - T0) / N * 1e9);
        const FVoxelCaveNode Pl = VoxelCavePlace(Pt, L, S, 0, 0, 0);
        T0 = Now();
        for (int i = 0; i < N; i++) { const FVoxelCaveNode Nd = VoxelCaveDress(Pl, S, i % 3 - 1, (i / 3) % 3 - 1, 0); Sink += Nd.R; }
        printf("dress %.1f ns\n", (Now() - T0) / N * 1e9);
        T0 = Now();
        for (int i = 0; i < N; i++) { Sink += VoxelCavePassage(float3(1.0f + i * 1e-6f, 2, 3), float3(40, 5, 2), 3, 4, float3(0, 0, 1), 0.4f, 0.4f, 0.0f); }
        printf("passage %.1f ns\n", (Now() - T0) / N * 1e9);
        T0 = Now();
        for (int i = 0; i < N; i++) { S.FU = (i % 1000) * 1e-3f; Sink += VoxelCaveDetailAt(S, true).Value; }
        printf("detail %.1f ns  [%g]\n", (Now() - T0) / N * 1e9, Sink * 1e-30);
    }
    else if (Mode == "seaprobe")
    {
        // Sea caves around the cliff sites: gathered primitives, and how much of the band they carve.
        using namespace VoxelShader;
        for (const FSite& Si : Cliffs)
        {
            double E1[3], E2[3]; Frame(Si.Dir, E1, E2);
            std::mt19937 Rng(3); std::uniform_real_distribution<double> U(-1, 1);
            int Prims = 0, Lists = 0, Air = 0, Rock = 0, Gates = 0;
            for (int c = 0; c < 400; c++)
            {
                double D[3]; DirAt(P, Si.Dir, E1, E2, U(Rng) * 2000, U(Rng) * 2000, D);
                const FColumn Col = ColumnAt(P, D, Fine);
                const FVoxelCaveSample S = SampleAt(P, Col, 4.0, Fine);
                FVoxelCaveRegion R = VoxelCaveCellRegion(S, 4.0f, 4.0f); R.Footprint = (float)Fine;
                const FVoxelCaveList L = VoxelCaveGather(R);
                Lists++; for (uint p = 0; p < L.Count; p++) if (L.Prims[p].Kind == VOXEL_CAVE_KIND_SEA) Prims++;
                if (L.Stage == 0) Gates++;
                const FDensity Dn = DensityAt(P, Col, 4.0, Fine);
                if (Dn.Kind == VOXEL_CAVE_KIND_SEA && Dn.Cave < 0) { if (Col.Land > 4.0) Rock++; else Air++; }
            }
            printf("coast h %.0f relief %.0f: %d columns, %d gated off, %.2f sea primitives a list; carved at 4 m: %d under land, %d in open air\n", Si.Height, Si.Relief, Lists, Gates, (double)Prims / Lists, Rock, Air);
            // Along each sea passage of the site's own list: the land and the carve on its axis.
            {
                const FColumn Col = ColumnAt(P, Si.Dir, Fine);
                const FVoxelCaveSample S0 = SampleAt(P, Col, 4.0, Fine);
                FVoxelCaveRegion R = VoxelCaveCellRegion(S0, 4.0f, 4.0f); R.Footprint = (float)Fine;
                const FVoxelCaveList L = VoxelCaveGather(R);
                for (uint p = 0; p < L.Count; p++)
                {
                    const FVoxelCavePrim& Pr = L.Prims[p];
                    if (Pr.Kind != VOXEL_CAVE_KIND_SEA) continue;
                    std::string Line;
                    for (int k = 0; k <= 60; k++)
                    {
                        const double T = k / 60.0;
                        // The axis point: cells from the list's origin -> a direction (through texel coords).
                        const double CU = R.OU + Pr.AU + (Pr.BU - Pr.AU) * T, CV = R.OV + Pr.AV + (Pr.BV - Pr.AV) * T;
                        const double Jq = CU / GM, Kq = CV / GM;
                        const double Sx = (Jq + 0.5) * 2.0 / P.N - 1.0, Tx = (Kq + 0.5) * 2.0 / P.N - 1.0;
                        double D[3]; const uint Nn = VoxelCubeNormalAxis(R.Face), Uu = VoxelCubeUAxis(R.Face), Vv = VoxelCubeVAxis(R.Face);
                        D[Nn] = (R.Face & 1) ? -1.0 : 1.0; D[Uu] = std::tan(Sx * M_PI / 4); D[Vv] = std::tan(Tx * M_PI / 4); Normalize(D);
                        const FColumn Ck = ColumnAt(P, D, Fine);
                        const double Z = Pr.AZ + (Pr.BZ - Pr.AZ) * T;
                        const FDensity Dn = DensityAt(P, Ck, Z, Fine);
                        Line += Ck.Land < Z ? (Ck.Land < 0 ? '~' : '.') : (Dn.D > 0 ? 'C' : (Ck.Land - Z < VoxelShader::CaveSeaRoofM ? 'r' : (Ck.Land - Z > VoxelShader::CaveSeaCoverM ? '#' : 'x')));
                    }
                    printf("   axis %s\n", Line.c_str());
                }
            }
        }
    }
    else if (Mode == "slope")
    {
        // Points near walls (|cave| < 6 m) around the sites; central differences at 5 cm along the
        // planet's axes, each offset point with its own column.
        std::mt19937 Rng(11);
        std::uniform_real_distribution<double> U(-1, 1);
        const int Want = (int)EnvD("SLOPE_N", 6000);
        std::vector<std::array<double, 4>> Pts;
        for (auto* Set : { &Valleys, &Uplands, &Cliffs })
            for (const auto& Site : *Set)
            {
                double E1[3], E2[3]; Frame(Site.Dir, E1, E2);
                int Got = 0;
                for (int t = 0; t < 200000 && Got < Want / 18; t++)
                {
                    double D[3]; DirAt(P, Site.Dir, E1, E2, U(Rng) * 2500, U(Rng) * 2500, D);
                    const double Table = TableAt(P, D);
                    const double Z = Set == &Cliffs ? 1.0 + 12.0 * (0.5 + 0.5 * U(Rng)) : Table - 20 + (0.5 + 0.5 * U(Rng)) * (VoxelShader::CaveLevelM * VoxelShader::CaveLevels + 60);
                    // A cheap pre-test with the bake for the land (the cover gate off).
                    FColumn C; for (int a = 0; a < 3; a++) C.Dir[a] = D[a];
                    const FFaceAt F = FaceAt(P, D);
                    C.Bake = BakeLerp(P, F, [&](size_t i) { return (double)GCaveH[i]; }); C.Land = C.Bake + 400;
                    C.Province = (float)BakeLerp(P, F, [&](size_t i) { return (double)GCaveP[i]; });
                    const FVoxelCaveResult R = VoxelShader::VoxelCaveDistance(SampleAt(P, C, Z, Fine));
                    if (std::fabs(R.Distance) < 30) { Pts.push_back({ D[0], D[1], D[2], Z }); Got++; }
                }
            }
        // Near the surface: around the mouths of a valley and an upland grid (the cover gate and
        // the doline bowls at work), 0..45 m below the land.
        for (const FSite* Si : { &Valleys[0], &Uplands[0] })
        {
            const FGrid G = MakeGrid(P, Si->Dir, 3000, 600, Fine);
            const FMouthStats M = CountMouths(P, G, Fine);
            for (auto [MX, MY] : M.Centres)
                for (int k = 0; k < 120; k++)
                {
                    const int X = std::clamp(MX + (int)(U(Rng) * 8), 0, G.Px - 1), Y = std::clamp(MY + (int)(U(Rng) * 8), 0, G.Px - 1);
                    const FColumn& Col = G.At(X, Y);
                    Pts.push_back({ Col.Dir[0], Col.Dir[1], Col.Dir[2], Col.Land - 45.0 * (0.5 + 0.5 * U(Rng)) });
                }
        }
        std::vector<double> GC, GD, GCr;
        std::mutex Mx;
        auto Measure = [&](std::vector<double>& Into, bool bDensity) {
            ParallelFor((int)Pts.size(), [&](int i) {
                const double* D = Pts[i].data(); const double Z = Pts[i][3];
                double X[3]; Point(P, D, Z, X);
                auto Eval = [&](const double Y[3]) { const double R = std::sqrt(Y[0] * Y[0] + Y[1] * Y[1] + Y[2] * Y[2]); double Dd[3] = { Y[0] / R, Y[1] / R, Y[2] / R };
                    return DensityAt(P, ColumnAt(P, Dd, Fine), R - SeaM(P), Fine); };
                const FDensity C0 = Eval(X);
                if (std::fabs(C0.Cave) > 6) return;
                const double H = 0.05;
                double G[3];
                for (int a = 0; a < 3; a++)
                {
                    double A1[3] = { X[0], X[1], X[2] }, A2[3] = { X[0], X[1], X[2] }; A1[a] += H; A2[a] -= H;
                    const FDensity F1 = Eval(A1), F2 = Eval(A2);
                    // The cave distance alone is FAR where an exact skip left it (no effect on the
                    // density): those points are left out of its statistic.
                    if (!bDensity && std::max(F1.Cave, F2.Cave) > 1e8f) return;
                    G[a] = bDensity ? (F1.D - F2.D) / (2 * H) : (F1.Cave - F2.Cave) / (2 * H);
                }
                const double g = std::sqrt(G[0] * G[0] + G[1] * G[1] + G[2] * G[2]);
                std::lock_guard<std::mutex> Lk(Mx); Into.push_back(g);
            });
        };
        Measure(GC, false);
        Measure(GD, true);
        const float Rough = VoxelShader::CaveRoughM, Bed = VoxelShader::CaveBeddingM;
        VoxelShader::CaveRoughM = 0; VoxelShader::CaveBeddingM = 0;
        Measure(GCr, false);
        VoxelShader::CaveRoughM = Rough; VoxelShader::CaveBeddingM = Bed;
        auto Report = [](const char* What, std::vector<double>& V) {
            if (V.empty()) { printf("  %s: none\n", What); return; }
            std::sort(V.begin(), V.end());
            auto Q = [&](double q) { return V[std::min(V.size() - 1, (size_t)(q * V.size()))]; };
            printf("  %-58s %5zu points: median %.3f, 99%% %.3f, 99.9%% %.3f, max %.3f\n", What, V.size(), Q(0.5), Q(0.99), Q(0.999), V.back());
        };
        printf("slope near walls (|cave| < 6 m):\n");
        Report("|grad cave distance|, detail off", GCr);
        Report("|grad cave distance|", GC);
        Report("|grad carved density| (the land's vertical distance too)", GD);
        {
            double Mx2 = 0; std::mt19937 R2(3); std::uniform_real_distribution<float> F01(0, 1);
            for (int i = 0; i < 2000000; i++)
            {
                const int IX = (int)(R2() % 1000), IY = (int)(R2() % 1000), IZ = (int)(R2() % 1000);
                const float X = F01(R2) * 0.998f + 0.001f, Y = F01(R2) * 0.998f + 0.001f, Z = F01(R2) * 0.998f + 0.001f, h = 1e-3f;
                const float gx = (VoxelShader::VoxelCaveNoise(IX, IY, IZ, X + h, Y, Z, 7) - VoxelShader::VoxelCaveNoise(IX, IY, IZ, X - h, Y, Z, 7)) / (2 * h);
                const float gy = (VoxelShader::VoxelCaveNoise(IX, IY, IZ, X, Y + h, Z, 7) - VoxelShader::VoxelCaveNoise(IX, IY, IZ, X, Y - h, Z, 7)) / (2 * h);
                const float gz = (VoxelShader::VoxelCaveNoise(IX, IY, IZ, X, Y, Z + h, 7) - VoxelShader::VoxelCaveNoise(IX, IY, IZ, X, Y, Z - h, 7)) / (2 * h);
                Mx2 = std::max(Mx2, (double)std::sqrt(gx * gx + gy * gy + gz * gz));
            }
            using namespace VoxelShader;
            double Sum = 0, A = CaveRoughM; double Wave = CaveCellM / CaveRoughCells;
            for (uint o = 0; o < CaveRoughOctaves; o++) { Sum += A * Mx2 / Wave; A *= CaveRoughGain; Wave *= 0.5; }
            printf("  noise gradient bound %.3f a lattice unit: the detail adds at most %.3f (octaves) + %.3f (bedding, across the beds)\n", Mx2, Sum, CaveBeddingM * Mx2 / CaveBeddingWaveM);
        }
    }
    else if (Mode == "cost")
    {
        std::mt19937 Rng(17);
        std::uniform_real_distribution<double> U(-1, 1);
        struct FSet { const char* Name; std::vector<std::pair<FColumn, double>> Pts; };
        std::vector<FSet> Sets = { { "above the land (5 m)", {} }, { "1.5 km below the land", {} }, { "100 m below, outside karst", {} },
            { "karst, in the levels' band", {} }, { "karst, near walls (|cave| < 4 m)", {} } };
        auto Fill = [&](FSet& S, auto&& Pick, int Want) {
            for (int t = 0; t < 400000 && (int)S.Pts.size() < Want; t++) { auto R = Pick(); if (R.second > -1e8) S.Pts.push_back(R); }
        };
        const std::vector<FSite>* All[] = { &Valleys, &Uplands };
        auto Around = [&](double Span) { const auto& Set = *All[Rng() % 2]; const FSite& Si = Set[Rng() % Set.size()]; double E1[3], E2[3]; Frame(Si.Dir, E1, E2);
            double D[3]; DirAt(P, Si.Dir, E1, E2, U(Rng) * Span, U(Rng) * Span, D); return ColumnAt(P, D, Fine); };
        const int N = (int)EnvD("COST_N", 3000);
        Fill(Sets[0], [&] { FColumn C = Around(3000); return std::make_pair(C, C.Land + 5.0); }, N);
        Fill(Sets[1], [&] { FColumn C = Around(3000); return std::make_pair(C, C.Land - 1500.0); }, N);
        Fill(Sets[2], [&] { double D[3] = { U(Rng), U(Rng), U(Rng) }; Normalize(D); FColumn C = ColumnAt(P, D, Fine);
            return std::make_pair(C, (C.Land > 50 && C.Province < GKarstThreshold - 0.1) ? C.Land - 100.0 : -1e9); }, N);
        Fill(Sets[3], [&] { FColumn C = Around(3000); const double T = TableAt(P, C.Dir); const double Z = T + (0.5 + 0.5 * U(Rng)) * VoxelShader::CaveLevelM * VoxelShader::CaveLevels;
            return std::make_pair(C, (C.Land - Z > 40) ? Z : -1e9); }, N);
        Fill(Sets[4], [&] { FColumn C = Around(3000); const double T = TableAt(P, C.Dir); const double Z = T + (0.5 + 0.5 * U(Rng)) * VoxelShader::CaveLevelM * VoxelShader::CaveLevels;
            if (C.Land - Z < 40) return std::make_pair(C, -1e9); const FDensity Dd = DensityAt(P, C, Z, Fine); return std::make_pair(C, std::fabs(Dd.Cave) < 4 ? Z : -1e9); }, N / 3);
        printf("CPU cost per sample, one thread (the land chain at the finest footprint; the cave term alone):\n");
        for (auto& S : Sets)
        {
            if (S.Pts.empty()) { printf("  %s: no points\n", S.Name); continue; }
            double Sink = 0; int Stages[3] = {};
            double TLand = 1e9, TCave = 1e9;
            for (int Rep = 0; Rep < 3; Rep++)
            {
                double T0 = Now();
                for (auto& [C, Z] : S.Pts) Sink += Terms(P, C.Dir, P.Finest).Height;
                TLand = std::min(TLand, (Now() - T0) / S.Pts.size());
                T0 = Now();
                for (auto& [C, Z] : S.Pts) { const FVoxelCaveResult R = VoxelShader::VoxelCaveDistance(SampleAt(P, C, Z, Fine)); Sink += R.Distance; if (Rep == 0) Stages[R.Stage]++; }
                TCave = std::min(TCave, (Now() - T0) / S.Pts.size());
            }
            GCount = FCaveCounters();
            for (auto& [C, Z] : S.Pts) Sink += VoxelShader::VoxelCaveDistance(SampleAt(P, C, Z, Fine)).Distance;
            const double Np = (double)S.Pts.size();
            printf("  %-36s %5zu points: land %6.2f us, caves %6.3f us (%5.1f%%); skipped %d, patch %d, nodes %d; a sample: %.2f levels, %.2f near, %.2f edges, %.2f detail  [%g]\n",
                S.Name, S.Pts.size(), TLand * 1e6, TCave * 1e6, 100 * TCave / TLand, Stages[0], Stages[1], Stages[2],
                GCount.Levels / Np, GCount.Near / Np, GCount.Edges / Np, GCount.Detail / Np, Sink * 1e-30);
        }
    }
    else if (Mode == "brickcost")
    {
        // The brick path: one gather for a brick's cells (VoxelCaveGather), then each sample
        // evaluated (VoxelCaveEvaluate). Level-0 bricks (16 cells of the finest) at karst points
        // in the levels' band; 512 samples a brick (8^3 of its 17^3 lattice).
        using namespace VoxelShader;
        std::mt19937 Rng(23); std::uniform_real_distribution<double> U(-1, 1);
        const int Cells = (int)EnvD("BRICK_CELLS", 16); const int Level = (int)EnvD("BRICK_LEVEL", 0);
        const double Cf = Fine * (1 << Level), Bs = Cf * Cells;
        double TG = 0, TE = 0, TS = 0; int Bricks = 0; long Samples = 0, Prims = 0, Over = 0; double Sink = 0;
        GCount = FCaveCounters();
        for (int b = 0; b < (int)EnvD("BRICKS", 300); b++)
        {
            const auto& Set = (b & 1) ? Uplands : Valleys; const FSite& Si = Set[(size_t)(U(Rng) * 0.5 + 0.5) * Set.size() % Set.size()];
            double E1[3], E2[3]; Frame(Si.Dir, E1, E2);
            double D[3]; DirAt(P, Si.Dir, E1, E2, U(Rng) * 3000, U(Rng) * 3000, D);
            const FColumn Col = ColumnAt(P, D, Cf);
            const double Z0 = TableAt(P, D) + (0.5 + 0.5 * U(Rng)) * CaveLevelM * CaveLevels;
            if (Col.Land - Z0 < 40) continue;
            // The brick's cells: its corners' cave cells (the brick is small: one or two cells).
            std::vector<FVoxelCaveSample> Ss;
            for (int k = 0; k < 512; k++)
            {
                const double X = ((k % 8) / 7.0 - 0.5) * Bs, Y = (((k / 8) % 8) / 7.0 - 0.5) * Bs, Z = ((k / 64) / 7.0 - 0.5) * Bs;
                double Dk[3]; DirAt(P, D, E1, E2, X, Y, Dk);
                FColumn Ck = Col; for (int a = 0; a < 3; a++) Ck.Dir[a] = Dk[a];
                Ss.push_back(SampleAt(P, Ck, Z0 + Z, Cf));
            }
            FVoxelCaveRegion R;
            R.Face = Ss[0].Face; R.OU = Ss[0].IU; R.OV = Ss[0].IV; int U1 = R.OU, V1 = R.OV;
            bool bOneFace = true;
            for (auto& S : Ss) { bOneFace &= S.Face == R.Face; R.OU = std::min(R.OU, S.IU); R.OV = std::min(R.OV, S.IV); U1 = std::max(U1, S.IU); V1 = std::max(V1, S.IV); }
            if (!bOneFace) continue;
            R.NU = U1 - R.OU + 1; R.NV = V1 - R.OV + 1; R.ZMin = (float)(Z0 - Bs / 2); R.ZMax = (float)(Z0 + Bs / 2); R.Footprint = (float)Cf;
            R.Margin = VoxelCaveMargin(R.Footprint); R.Reach = VOXEL_CAVE_FAR; R.Valid = 1u;
            double T0 = Now();
            FVoxelCaveList L;
            for (int r = 0; r < 10; r++) L = VoxelCaveGather(R);
            TG += (Now() - T0) / 10;
            T0 = Now();
            for (auto& S : Ss) Sink += VoxelCaveEvaluate(L, S).Distance;
            TE += Now() - T0;
            T0 = Now();
            for (auto& S : Ss) Sink += VoxelCaveDistance(S).Distance;
            TS += Now() - T0;
            // The two paths agree (the list is a superset of each sample's own).
            for (auto& S : Ss) { const float A = VoxelCaveCarve(Col.Land - S.Z, VoxelCaveEvaluate(L, S).Distance, 1.0f), B2 = VoxelCaveCarve(Col.Land - S.Z, VoxelCaveDistance(S).Distance, 1.0f); if (std::fabs(A - B2) > 1e-3f) { printf("  paths differ by %g at a sample\n", A - B2); break; } }
            Bricks++; Samples += (long)Ss.size(); Prims += L.Count; Over += L.Overflow;
        }
        printf("brick path at level %d (%.0f m bricks), %d bricks in the karst band: gather %.2f us a brick (%.1f primitives, %ld overflowed), evaluate %.3f us a sample; per-sample path %.3f us  [%g]\n",
            Level, Bs, Bricks, TG / Bricks * 1e6, (double)Prims / Bricks, Over, TE / Samples * 1e6, TS / Samples * 1e6, Sink * 1e-30);
        printf("  a brick of 17^3 = 4913 samples: %.3f us a sample with the gather shared\n", (TG / Bricks + TE / Samples * 4913) / 4913 * 1e6);
    }
    else if (Mode == "k1")
    {
        // K1's engine paths against the per-sample path (VoxelCaveDistance on SampleAt's sample).
        using namespace VoxelShader;
        const double SeaCm = P.SeaRadius;
        const double Half = EnvD("HALF_FINEST", 50.0);       // cm: the lattice (the engine's 1 m finest cell)
        const int NCells = (int)EnvD("BRICK_CELLS", 16);
        std::mt19937 Rng(29);
        std::uniform_real_distribution<double> U(-1, 1);
        auto Point3 = [&](const double D[3], double Z, double X[3]) { for (int a = 0; a < 3; a++) X[a] = D[a] * (SeaCm + Z * 100.0); };
        auto Len3 = [](const double X[3]) { return std::sqrt(X[0] * X[0] + X[1] * X[1] + X[2] * X[2]); };

        // 1. The sample from the density's double-float position (VoxelCaveSampleDF: VoxelLandAxisAt's
        //    exact fraction) against SampleAt (the harness's double face coordinates).
        {
            int Count = 0, CellDiff = 0, FaceDiff = 0, DistDiff = 0;
            double MaxF = 0, MaxZ = 0, MaxE = 0, MaxP = 0, MaxD = 0;
            for (int i = 0; i < (int)EnvD("K1_SAMPLES", 4000); i++)
            {
                const auto& Set = (i % 3 == 0) ? Valleys : ((i % 3 == 1) ? Uplands : Cliffs);
                const FSite& Si = Set[(size_t)i % Set.size()];
                double E1[3], E2[3]; Frame(Si.Dir, E1, E2);
                double D[3]; DirAt(P, Si.Dir, E1, E2, U(Rng) * 3000, U(Rng) * 3000, D);
                const FColumn Col = ColumnAt(P, D, Fine);
                const double Z = (i % 3 == 2) ? 1.0 + (U(Rng) * 0.5 + 0.5) * 30.0 : TableAt(P, D) + (U(Rng) * 0.5 + 0.5) * CaveLevelM * CaveLevels;
                const FVoxelCaveSample A = SampleAt(P, Col, Z, Fine);
                double X[3]; Point3(D, Z, X);
                const FVoxelCaveSample B = VoxelCaveSampleDF(DF(X[0]), DF(X[1]), DF(X[2]), DF(Len3(X)), DF(SeaCm), (float)Col.Land, (float)Col.Bake, (float)Fine);
                Count++;
                if (A.Face != B.Face) { FaceDiff++; continue; }
                const double UA = A.IU + (double)A.FU, UB = B.IU + (double)B.FU, VA = A.IV + (double)A.FV, VB = B.IV + (double)B.FV;
                if (A.IU != B.IU || A.IV != B.IV) CellDiff++;
                MaxF = std::max({ MaxF, std::fabs(UA - UB), std::fabs(VA - VB) });
                MaxZ = std::max(MaxZ, (double)std::fabs(A.Z - B.Z));
                MaxE = std::max({ MaxE, (double)std::fabs(A.EU.x - B.EU.x), (double)std::fabs(A.EU.y - B.EU.y), (double)std::fabs(A.EU.z - B.EU.z),
                    (double)std::fabs(A.EV.x - B.EV.x), (double)std::fabs(A.EV.y - B.EV.y), (double)std::fabs(A.EV.z - B.EV.z) });
                MaxP = std::max(MaxP, (double)std::fabs(A.Province - B.Province));
                const float CA = VoxelCaveDistance(A).Distance, CB = VoxelCaveDistance(B).Distance;
                if ((CA < 1e8f) != (CB < 1e8f)) DistDiff++;
                else if (CA < 1e8f) MaxD = std::max(MaxD, (double)std::fabs(CA - CB));
            }
            printf("k1 sample: %d points: face differs %d, cell differs %d (a boundary within the cells' difference), |d cell| max %.2e cells, |dZ| max %.2e m, |dE| max %.2e m, |d province| max %.2e; distance found by one only %d, |d distance| max %.2e m\n",
                Count, FaceDiff, CellDiff, MaxF, MaxZ, MaxE, MaxP, DistDiff, MaxD);
        }

        // 2. The mesher's brick lists: the gather pass's box (the brick and VOXEL_CAVE_BRICK_MARGIN of
        //    its cells), its face slots gathered (VoxelCaveBoxRegion, VoxelCaveGather), then samples
        //    anywhere in the box at the brick's footprint and twice it (stitched) through
        //    VoxelCaveDistanceDF's stored path, against the per-sample path.
        {
            const char* LevelList = getenv("K1_LEVELS");
            std::vector<int> Levels = { 0, 1, 2, 3, 4 };
            if (LevelList) { Levels.clear(); for (const char* c = LevelList; *c; c++) if (*c >= '0' && *c <= '9') Levels.push_back(*c - '0'); }
            for (const int Level : Levels)
            {
                const int Scale = 1 << Level;
                const long long BrickIdx = 2LL * NCells * Scale;
                const double FootM = Half * 2.0 * Scale * 0.01;
                int Bricks = 0, Slots = 0, Invalid = 0, Overflow = 0, MaxNU = 0, CaveBricks = 0;
                long Samples = 0, Near = 0, NearDiff = 0, Deep = 0, DeepDiff = 0, Inside = 0, Prims = 0, MaxPrims = 0;
                double MaxNearDiff = 0, TGather = 0;
                for (int b = 0; b < (int)EnvD("K1_BRICKS", 60); b++)
                {
                    // Half the bricks around a point inside a cave, half at random in the band.
                    const auto& Set = (b % 3 == 0) ? Valleys : ((b % 3 == 1) ? Uplands : Cliffs);
                    const FSite& Si = Set[(size_t)(b / 3) % Set.size()];
                    double D[3]; double Z = 0;
                    if ((b & 1) == 0 && b % 3 != 2)
                    {
                        if (!FindInside(P, Si.Dir, 3000, 0.5, D, &Z, 0, 100 + b)) continue;
                    }
                    else
                    {
                        double E1[3], E2[3]; Frame(Si.Dir, E1, E2);
                        DirAt(P, Si.Dir, E1, E2, U(Rng) * 3000, U(Rng) * 3000, D);
                        Z = (b % 3 == 2) ? 2.0 + (U(Rng) * 0.5 + 0.5) * 20.0 : TableAt(P, D) + (U(Rng) * 0.5 + 0.5) * CaveLevelM * CaveLevels;
                    }
                    double X[3]; Point3(D, Z, X);
                    long long MinIndex[3];
                    for (int a = 0; a < 3; a++)
                    {
                        const long long I = (long long)std::floor(X[a] / Half);
                        MinIndex[a] = (I >= 0 ? I / BrickIdx : -((-I + BrickIdx - 1) / BrickIdx)) * BrickIdx;
                    }
                    const long long Lo = -2LL * VOXEL_CAVE_BRICK_MARGIN * Scale, Hi = (2LL * NCells + 2LL * VOXEL_CAVE_BRICK_MARGIN) * Scale;
                    GHeads.assign(VOXEL_CAVE_SLOTS, FVoxelCaveListHead{});
                    GPrims.assign(VOXEL_CAVE_SLOTS * VOXEL_CAVE_MAX_PRIMS, VoxelCavePrimNone());
                    const double T0 = Now();
                    bool bAny = false;
                    for (uint Slot = 0; Slot < VOXEL_CAVE_SLOTS; Slot++)
                    {
                        const FVoxelCaveRegion R = VoxelCaveBoxRegion(DF((MinIndex[0] + Lo) * Half), DF((MinIndex[1] + Lo) * Half), DF((MinIndex[2] + Lo) * Half),
                            DF((MinIndex[0] + Hi) * Half), DF((MinIndex[1] + Hi) * Half), DF((MinIndex[2] + Hi) * Half), DF(SeaCm), (float)FootM, Slot);
                        FVoxelCaveListHead H{};
                        H.Face = R.Face; H.Valid = R.Valid;
                        if (R.Face != VOXEL_CAVE_NO_FACE) { Slots++; if (R.Valid == 0u) Invalid++; MaxNU = std::max({ MaxNU, R.NU, R.NV }); }
                        if (R.Valid != 0u && FootM < CaveMaxRadiusM)
                        {
                            const FVoxelCaveList L = VoxelCaveGather(R);
                            H = VoxelCaveHeadOf(R, L);
                            for (uint p = 0; p < L.Count; p++) GPrims[Slot * VOXEL_CAVE_MAX_PRIMS + p] = L.Prims[p];
                            Prims += L.Count; MaxPrims = std::max<long>(MaxPrims, L.Count); Overflow += L.Overflow > 0 ? 1 : 0;
                            bAny |= L.Count > 0;
                        }
                        else if (R.Valid != 0u) H = VoxelCaveHeadOf(R, FVoxelCaveList{});
                        GHeads[Slot] = H;
                    }
                    TGather += Now() - T0;
                    Bricks++; CaveBricks += bAny ? 1 : 0;
                    for (int k = 0; k < (int)EnvD("K1_PER_BRICK", 300); k++)
                    {
                        long long I[3]; double XS[3];
                        for (int a = 0; a < 3; a++) { I[a] = MinIndex[a] + Lo + (long long)((U(Rng) * 0.5 + 0.5) * (double)(Hi - Lo)); XS[a] = (double)I[a] * Half; }
                        const double Len = Len3(XS);
                        double DS[3] = { XS[0] / Len, XS[1] / Len, XS[2] / Len };
                        const double Foot = (k % 4 == 3) ? 2.0 * FootM : FootM;
                        const FColumn Col = ColumnAt(P, DS, Foot);
                        const double ZS = Len * 0.01 - SeaCm * 0.01;
                        const float Stored = VoxelCaveDistanceDF(DF(XS[0]), DF(XS[1]), DF(XS[2]), DF(Len), DF(SeaCm), (float)Col.Land, (float)Col.Bake, (float)Foot, 0u);
                        const FVoxelCaveSample S = VoxelCaveSampleDF(DF(XS[0]), DF(XS[1]), DF(XS[2]), DF(Len), DF(SeaCm), (float)Col.Land, (float)Col.Bake, (float)Foot);
                        const float PerSample = VoxelCaveDistance(S).Distance;
                        const float DL = (float)(ZS - Col.Land);
                        const float A = VoxelCaveCarve(DL, Stored, 1.0f), B2 = VoxelCaveCarve(DL, PerSample, 1.0f);
                        Samples++;
                        if (B2 > 0.0f && DL < 0.0f) Inside++;
                        // Exact within the levels' margin (VoxelCaveMargin: 4 cells and the blend) of a
                        // wall; deeper the paths may miss different primitives (both read too solid).
                        if (std::fabs(B2) < 4.0f * Foot)
                        {
                            Near++;
                            if (std::fabs(A - B2) > 1e-3f) { NearDiff++; MaxNearDiff = std::max(MaxNearDiff, (double)std::fabs(A - B2)); }
                        }
                        else
                        {
                            Deep++;
                            if (std::fabs(A - B2) > 1e-3f) DeepDiff++;
                        }
                    }
                }
                printf("k1 bricks level %d (%.0f m cells, %.0f m bricks + %d cells): %d bricks (%d with caves), %d face slots (%d too large), regions up to %d cells; %.1f primitives a list (max %ld, %d overflowed); gather %.1f us a brick\n",
                    Level, FootM, FootM * NCells, VOXEL_CAVE_BRICK_MARGIN, Bricks, CaveBricks, Slots, Invalid, MaxNU, (double)Prims / std::max(Slots - Invalid, 1), MaxPrims, Overflow, TGather / std::max(Bricks, 1) * 1e6);
                printf("    %ld samples (%ld in caves): near the surface (|d| < 4 cells) %ld, differing %ld (max %.3g m); deeper %ld, differing %ld\n",
                    Samples, Inside, Near, NearDiff, MaxNearDiff, Deep, DeepDiff);
            }
        }

        // 3. The CPU density's cache (FVoxelTerrestrialDensity::SampleRange): a cell and 32 m of
        //    altitude gathered while consecutive samples stay in them, over brick-like grids at
        //    caves, against the per-sample path. Exact: the same value.
        {
            long Samples = 0, Diff = 0, Gathers = 0, Evaluated = 0;
            double TCache = 0, TPer = 0;
            for (int b = 0; b < (int)EnvD("K1_GRIDS", 12); b++)
            {
                const FSite& Si = Valleys[(size_t)b % Valleys.size()];
                double D0[3]; double Z0 = 0;
                if (!FindInside(P, Si.Dir, 3000, 0.5, D0, &Z0, 0, 300 + b)) continue;
                double E1[3], E2[3]; Frame(D0, E1, E2);
                std::vector<FVoxelCaveSample> Ss;
                for (int k = 0; k < 16 * 16 * 16; k++)
                {
                    double D[3]; DirAt(P, D0, E1, E2, ((k % 16) - 8) * Fine, (((k / 16) % 16) - 8) * Fine, D);
                    const FColumn Col = ColumnAt(P, D, Fine);
                    const double Z = Z0 + ((k / 256) - 8) * Fine;
                    double X[3]; Point3(D, Z, X);
                    Ss.push_back(VoxelCaveSampleDF(DF(X[0]), DF(X[1]), DF(X[2]), DF(Len3(X)), DF(SeaCm), (float)Col.Land, (float)Col.Bake, (float)Fine));
                }
                std::vector<float> A(Ss.size()), B2(Ss.size());
                double T0 = Now();
                FVoxelCaveRegion Region{};
                FVoxelCaveList List{};
                bool bList = false;
                for (size_t k = 0; k < Ss.size(); k++)
                {
                    const FVoxelCaveSample& S = Ss[k];
                    A[k] = VOXEL_CAVE_FAR;
                    if (VoxelCaveSkip(S)) continue;
                    if (!bList || S.Face != List.Face || S.IU != List.OU || S.IV != List.OV || S.Z < Region.ZMin || S.Z > Region.ZMax)
                    {
                        const float ZB = std::floor(S.Z / 32.0f) * 32.0f;
                        Region = VoxelCaveCellRegion(S, ZB, ZB + 32.0f);
                        List = VoxelCaveGather(Region);
                        bList = true;
                        Gathers++;
                    }
                    A[k] = VoxelCaveEvaluate(List, S).Distance;
                    Evaluated++;
                }
                TCache += Now() - T0;
                T0 = Now();
                for (size_t k = 0; k < Ss.size(); k++) B2[k] = VoxelCaveDistance(Ss[k]).Distance;
                TPer += Now() - T0;
                for (size_t k = 0; k < Ss.size(); k++) { Samples++; if (A[k] != B2[k]) Diff++; }
            }
            printf("k1 CPU cache: %ld samples on 16^3 grids at caves, %ld evaluated, %ld gathers: differing %ld; %.3f us a sample cached, %.3f us one gather a sample\n",
                Samples, Evaluated, Gathers, Diff, TCache / std::max(Samples, 1L) * 1e6, TPer / std::max(Samples, 1L) * 1e6);
        }


        // 5. The empty tests' regions (MayHoldSurfaceInSolid: VoxelCaveBoxMayHold): brick test
        //    regions (the brick and its stitch cells) at levels 0..4 around caves; the centre
        //    sample's test (culled when the density is below -3.5 x the radius), a 10^3 grid of
        //    samples for the surface the region really holds, and the box test. A culled region
        //    with surface and no box test is a hole; the box test must never miss one.
        {
            for (int Level = 0; Level <= 4; Level++)
            {
                const double C = Fine * (1 << Level), Side = (NCells + 3) * C, Radius = 0.5 * Side * std::sqrt(3.0);
                int Boxes = 0, Culled = 0, CulledSurface = 0, Kept = 0, Missed = 0;
                double TBox = 0;
                for (int b = 0; b < (int)EnvD("K1_BOXES", 80); b++)
                {
                    const FSite& Si = (b & 1) ? Uplands[(size_t)b % Uplands.size()] : Valleys[(size_t)b % Valleys.size()];
                    double D0[3]; double Z0 = 0;
                    if (!FindInside(P, Si.Dir, 3000, 0.5, D0, &Z0, 0, 500 + b)) continue;
                    double E1[3], E2[3]; Frame(D0, E1, E2);
                    // The box's centre within a radius and a half of the cave point.
                    double D[3]; DirAt(P, D0, E1, E2, U(Rng) * 1.5 * Radius, U(Rng) * 1.5 * Radius, D);
                    const double ZC = Z0 + U(Rng) * 1.5 * Radius;
                    double XC[3]; Point3(D, ZC, XC);
                    const FColumn Col = ColumnAt(P, D, C);
                    const FVoxelCaveSample SC = VoxelCaveSampleDF(DF(XC[0]), DF(XC[1]), DF(XC[2]), DF(Len3(XC)), DF(SeaCm), (float)Col.Land, (float)Col.Bake, (float)C);
                    const float DC = VoxelCaveCarve((float)(ZC - Col.Land), VoxelCaveDistance(SC).Distance, 1.0f);
                    Boxes++;
                    if (!(DC < -3.5 * Radius)) continue;
                    Culled++;
                    // The box (axis-aligned in the planet frame, as the brick tree's).
                    double Lo[3], Hi[3];
                    for (int a = 0; a < 3; a++) { Lo[a] = XC[a] - 50.0 * Side; Hi[a] = XC[a] + 50.0 * Side; }
                    const double T0 = Now();
                    const bool bMay = VoxelCaveBoxMayHold(DF(Lo[0]), DF(Lo[1]), DF(Lo[2]), DF(Hi[0]), DF(Hi[1]), DF(Hi[2]), DF(SeaCm), (float)C);
                    TBox += Now() - T0;
                    Kept += bMay ? 1 : 0;
                    bool bSurface = false;
                    for (int k = 0; k < 1000 && !bSurface; k++)
                    {
                        double XS[3]; for (int a = 0; a < 3; a++) XS[a] = Lo[a] + ((k / (a == 0 ? 1 : (a == 1 ? 10 : 100))) % 10 + 0.5) / 10.0 * (Hi[a] - Lo[a]);
                        const double L = Len3(XS); double DS[3] = { XS[0] / L, XS[1] / L, XS[2] / L };
                        const FColumn CS = ColumnAt(P, DS, C);
                        const double ZS = L * 0.01 - SeaCm * 0.01;
                        const FVoxelCaveSample S = VoxelCaveSampleDF(DF(XS[0]), DF(XS[1]), DF(XS[2]), DF(L), DF(SeaCm), (float)CS.Land, (float)CS.Bake, (float)C);
                        bSurface = VoxelCaveCarve((float)(ZS - CS.Land), VoxelCaveDistance(S).Distance, 1.0f) > 0.0f;
                    }
                    CulledSurface += bSurface ? 1 : 0;
                    Missed += (bSurface && !bMay) ? 1 : 0;
                }
                printf("k1 solid regions level %d (%.0f m, radius %.0f m): %d boxes near caves, %d proved solid by the centre sample: %d hold cave surface (holes without the box test), box test keeps %d, misses %d; %.1f us a box test\n",
                    Level, Side, Radius, Boxes, Culled, CulledSurface, Kept, Missed, TBox / std::max(Culled, 1) * 1e6);
            }
        }

        // 4. The cave bake's packing: the largest rounding of each field.
        {
            double MaxB = 0, MaxF = 0, MaxR = 0, MaxP = 0;
            for (size_t I = 0; I < GCaveH.size(); I++)
            {
                const FVoxelCaveTexel T = VoxelCaveTexelAt((uint)I);
                MaxB = std::max(MaxB, (double)std::fabs(T.Base - std::min(GBase[I], 8191.5f)));
                if (GCaveF[I] >= 16.0f && GCaveF[I] <= 47.5f) MaxF = std::max(MaxF, (double)std::fabs(T.Flow - GCaveF[I]));
                if (GCaveR[I] <= 3000.0f) MaxR = std::max(MaxR, (double)std::fabs(T.Relief - GCaveR[I]) / std::max(GCaveR[I], 10.0f));
                MaxP = std::max(MaxP, (double)std::fabs(T.Province - GCaveP[I]));
            }
            printf("k1 packing: base within %.3f m, drainage within %.3f (log2 m^2, 16..47.5), relief within %.1f%% (above 10 m), province within %.4f; %.0f MB for %zu cells\n",
                MaxB, MaxF, MaxR * 100.0, MaxP, GCaveWords.size() * 4.0 / 1048576.0, GCaveH.size());
        }
    }
    else if (Mode == "mouths")
    {
        const double W = EnvD("MOUTH_W", 5000), Pix = EnvD("MOUTH_PIX", 4);
        double TotalKarst = 0; int TotalMouths = 0; std::map<uint, int> Kinds;
        int Flagged = 0, FlagMouth = 0, FlagBuried = 0, Texels = 0, Clean = 0;
        const int Sites = (int)EnvD("MOUTH_SITES", 2);
        const int First = (int)EnvD("MOUTH_FIRST", 0);
        for (auto* Set : { &Uplands, &Valleys, &Cliffs })
            for (int s = First; s < std::min<int>((int)Set->size(), First + Sites); s++)
            {
                const FGrid G = MakeGrid(P, (*Set)[s].Dir, W, (int)(W / Pix), Fine);
                std::vector<uint8_t> Mask;
                const FMouthStats M = CountMouths(P, G, Fine, &Mask);
                const char* Name = Set == &Uplands ? "upland" : (Set == &Valleys ? "valley" : "coast");
                printf("  %-6s %d: %5.1f km^2 karst of %.1f; %3d mouths (%d open texels):", Name, s, M.KarstKm2, M.AreaKm2, M.Components, M.OpenTexels);
                for (auto& [K, Nk] : M.ByKind) printf(" kind %u x%d", K, Nk);
                printf("\n");
                if (Set != &Cliffs) { TotalKarst += M.KarstKm2; TotalMouths += M.Components; for (auto& [K, Nk] : M.ByKind) Kinds[K] += Nk; }
                // The cache's column (E2) every 8 m: 49 samples 1 m apart over +-24 m about the land;
                // flagged unless exactly one crossing. At a mouth: an open texel within 12 m.
                if (EnvD("MOUTH_CACHE", 1) == 0) continue;
                const int Step = std::max(1, (int)std::lround(8.0 / Pix));
                std::atomic<int> AF{ 0 }, AM{ 0 }, AB{ 0 }, AT{ 0 }, AC{ 0 };
                ParallelFor(G.Px / Step, [&](int yy) {
                    const int y = yy * Step;
                    for (int x = 0; x < G.Px; x += Step)
                    {
                        const FColumn& Col = G.At(x, y);
                        if (Col.Land <= 0) continue;
                        AT++;
                        int Cross = 0; float Prev = 0; bool bOpenAir = false;
                        for (int k = 0; k < 49; k++)
                        {
                            const FDensity Dk = DensityAt(P, Col, Col.Land - 24.0 + k, Fine);
                            const float D = Dk.D;
                            if (k > 0 && (D > 0) != (Prev > 0)) Cross++;
                            if (Dk.Cave < 0.5f && Dk.Open) bOpenAir = true;
                            Prev = D;
                        }
                        if (Cross == 1) { AC++; continue; }
                        AF++;
                        bool bNear = bOpenAir;
                        const int R = (int)std::ceil(12.0 / Pix);
                        for (int dy = -R; dy <= R && !bNear; dy++) for (int dx = -R; dx <= R && !bNear; dx++)
                        {
                            const int X = x + dx, Y = y + dy;
                            if (X >= 0 && Y >= 0 && X < G.Px && Y < G.Px && Mask[(size_t)Y * G.Px + X]) bNear = true;
                        }
                        (bNear ? AM : AB)++;
                    }
                });
                printf("          cache columns: %d land texels, %d one crossing, %d flagged: %d at mouths, %d elsewhere\n", (int)AT, (int)AC, (int)AF, (int)AM, (int)AB);
                Flagged += AF; FlagMouth += AM; FlagBuried += AB; Texels += AT; Clean += AC;
            }
        printf("mouths (karst sites): %d over %.1f km^2 of karst: %.1f per 1,000 km^2 (Earth's karst: 6-63);", TotalMouths, TotalKarst, 1000.0 * TotalMouths / std::max(TotalKarst, 1e-9));
        for (auto& [K, Nk] : Kinds) printf(" kind %u x%d", K, Nk);
        printf("\ncache columns: %d land texels, %d flagged: %d at mouths, %d elsewhere\n", Texels, Flagged, FlagMouth, FlagBuried);
    }
    else if (Mode == "bricks")
    {
        const int CellsPerBrick = (int)EnvD("BRICK_CELLS", 16);
        const double Factor = EnvD("BRICK_FACTOR", 2.5), Slope = EnvD("SLOPE_BOUND", 3.5);
        const int Top = (int)EnvD("BRICK_TOP", 8);
        const double Box = EnvD("BRICK_BOX", 6000);
        const FSite& Site = Valleys[(size_t)EnvD("BRICK_SITE", 0) % Valleys.size()];
        struct FCam { std::string Name; double X[3]; };
        std::vector<FCam> Cams;
        const std::string Which = getenv("BRICK_CAMS") ? getenv("BRICK_CAMS") : "above,inside";
        if (Which.find("above") != std::string::npos)
        {
            FCam A{ "camera 2 m above a karst valley", {} };
            const FColumn Col = ColumnAt(P, Site.Dir, Fine);
            Point(P, Site.Dir, Col.Land + 2.0, A.X);
            Cams.push_back(A);
        }
        if (Which.find("inside") != std::string::npos)
        {
            double D[3], Z;
            if (FindInside(P, Site.Dir, 3000, 2.0, D, &Z))
            {
                const FColumn C2 = ColumnAt(P, D, Fine);
                FCam B{ "camera inside a passage, " + std::to_string((int)(C2.Land - Z)) + " m below the land", {} };
                for (double F : { Fine, 2 * Fine, 4 * Fine, 8 * Fine })
                {
                    const FColumn Cf = ColumnAt(P, D, F);
                    const FDensity Dn = DensityAt(P, Cf, Z, F);
                    printf("  at the camera, footprint %.1f m: density %.2f, cave %.2f (kind %u, stage %u), land %.1f bake %.1f\n", F, Dn.D, Dn.Cave, Dn.Kind, Dn.Stage, Cf.Land, Cf.Bake);
                }
                if (getenv("BRICK_DEBUG"))
                {
                    double X0[3]; Point(P, D, Z, X0);
                    for (int k = -4; k <= 4; k++)
                    {
                        double X[3] = { X0[0] + k * 8.0, X0[1], X0[2] };
                        const double Rr = std::sqrt(X[0] * X[0] + X[1] * X[1] + X[2] * X[2]); double Dd[3] = { X[0] / Rr, X[1] / Rr, X[2] / Rr };
                        const FDensity Dn = DensityAt(P, ColumnAt(P, Dd, 2 * Fine), Rr - SeaM(P), 2 * Fine);
                        printf("   x %+3d m: density %8.2f cave %12.2f kind %u\n", k * 8, Dn.D, Dn.Cave, Dn.Kind);
                    }
                }
                Point(P, D, Z, B.X);
                Cams.push_back(B);
            }
        }
        for (auto& Cam : Cams)
        {
            printf("%s (%d cells a brick, factor %.1f, slope bound %.1f, levels %d..0 in +-%.0f km):\n", Cam.Name.c_str(), CellsPerBrick, Factor, Slope, Top, Box * 1e-3);
            FBrickCount A, B;
            VoxelShader::CaveOn = 0;
            A = Bricks(P, Cam.X, Top, Box, CellsPerBrick, Factor, Slope);
            VoxelShader::CaveOn = 1;
            B = Bricks(P, Cam.X, Top, Box, CellsPerBrick, Factor, Slope);
            int SA = 0, SB = 0, WA = 0, WB = 0; int64_t NA = 0, NB = 0;
            printf("  level  cell m   wanted land/caves   surface land/caves   samples land/caves\n");
            for (int L = Top; L >= 0; L--)
            {
                printf("  %4d %8.2f   %7d %7d     %7d %7d     %9lld %9lld\n", L, Fine * (1 << L), A.Wanted[L], B.Wanted[L], A.Surface[L], B.Surface[L], (long long)A.Samples[L], (long long)B.Samples[L]);
                SA += A.Surface[L]; SB += B.Surface[L]; WA += A.Wanted[L]; WB += B.Wanted[L]; NA += A.Samples[L]; NB += B.Samples[L];
            }
            printf("  total wanted %d -> %d (%+.1f%%), surface %d -> %d (%+.1f%%), test samples %lld -> %lld\n", WA, WB, 100.0 * (WB - WA) / std::max(WA, 1), SA, SB, 100.0 * (SB - SA) / std::max(SA, 1), (long long)NA, (long long)NB);
        }
    }
    else if (Mode == "views")
    {
        const std::string Which = getenv("VIEWS") ? getenv("VIEWS") : "sec,plan,march";
        auto Has = [&](const char* W) { return ("," + Which + ",").find("," + std::string(W) + ",") != std::string::npos; };
        auto Tangent = [&](const double C[3], double Angle, double E[3]) { double E1[3], E2[3]; Frame(C, E1, E2); for (int a = 0; a < 3; a++) E[a] = E1[a] * std::cos(Angle) + E2[a] * std::sin(Angle); };
        const int VS = (int)EnvD("VIEW_SITE", 0);
        const FSite& Valley = Valleys[VS % Valleys.size()];
        const FSite& Cliff = Cliffs[VS % Cliffs.size()];
        const FSite& Upland = Uplands[VS % Uplands.size()];
        if (Has("sec"))
        {
            for (int k = 0; k < 2; k++)
            {
                double E[3]; Tangent(Valley.Dir, k * M_PI / 2, E);
                const double T = TableAt(P, Valley.Dir), L = ColumnAt(P, Valley.Dir, Fine).Land;
                Section(P, Out + "sec_valley" + std::to_string(k) + ".ppm", Valley.Dir, E, 3000, std::min(T, L) - 80, std::max(L, T) + 260, 1200, 300, Fine);
            }
            {
                double E[3]; Tangent(Upland.Dir, 0, E);
                const double T = TableAt(P, Upland.Dir), L = ColumnAt(P, Upland.Dir, Fine).Land;
                Section(P, Out + "sec_upland.ppm", Upland.Dir, E, 3000, T - 80, L + 80, 1200, 300, Fine);
            }
            {
                double D[3], Z;
                if (FindInside(P, Valley.Dir, 2500, 3.0, D, &Z))
                {
                    double E[3]; Tangent(D, 0.3, E);
                    Section(P, Out + "sec_close.ppm", D, E, 400, Z - 60, Z + 60, 1000, 300, Fine);
                }
                else printf("  no passage found for the close section\n");
            }
            {
                double E[3]; Tangent(Cliff.Dir, 0, E);
                Section(P, Out + "sec_coast.ppm", Cliff.Dir, E, 1200, -40, 160, 1200, 200, Fine);
            }
        }
        if (Has("plan"))
        {
            const int Px = (int)EnvD("PLAN_PX", 500);
            const FGrid G1 = MakeGrid(P, Valley.Dir, EnvD("PLAN_W", 4000), Px, Fine);
            Plan(P, Out + "plan_valley.ppm", G1, Fine, EnvD("PLAN_STEP", 4.0));
            const FGrid G2 = MakeGrid(P, Upland.Dir, EnvD("PLAN_W", 4000), Px, Fine);
            Plan(P, Out + "plan_upland.ppm", G2, Fine, EnvD("PLAN_STEP", 4.0));
            const FGrid G3 = MakeGrid(P, Cliff.Dir, 2000, Px, Fine);
            Plan(P, Out + "plan_coast.ppm", G3, Fine, 2.0);
        }
        const int W = (int)EnvD("MARCH_W", 400), H = W * 9 / 16;
        if (Has("passage") || Has("chamber"))
        {
            double D[3], Z;
            const uint Kind = Has("chamber") ? VOXEL_CAVE_KIND_CHAMBER : 0u;
            if (FindInside(P, Has("upland") ? Upland.Dir : Valley.Dir, 2500, Kind ? 8.0 : 3.0, D, &Z, Kind, (int)EnvD("FIND_SEED", 5)))
            {
                const FGrid G = MakeGrid(P, D, 500, 500, Fine);
                double E1[3], E2[3]; Frame(D, E1, E2);
                double X0[3]; Point(P, D, Z, X0);
                // Look along the cave: the horizontal direction with the longest free run.
                double BestA = 0, BestD = -1e9;
                for (int k = 0; k < 24; k++)
                {
                    double Dir[3]; for (int a = 0; a < 3; a++) Dir[a] = E1[a] * std::cos(k * M_PI / 12) + E2[a] * std::sin(k * M_PI / 12);
                    double Tt = 0;
                    for (int i = 0; i < 300 && Tt < 150; i++) { double X[3]; for (int a = 0; a < 3; a++) X[a] = X0[a] + Dir[a] * Tt; const float Dd = DensityGrid(P, G, X, Fine).D; if (Dd < 0.05f) break; Tt += std::max(0.3, Dd * 0.4); }
                    if (Tt > BestD) { BestD = Tt; BestA = k * M_PI / 12; }
                }
                double Fwd[3]; for (int a = 0; a < 3; a++) Fwd[a] = E1[a] * std::cos(BestA) + E2[a] * std::sin(BestA) - D[a] * 0.08;
                March(P, Out + (Kind ? "march_chamber.ppm" : "march_passage.ppm"), G, X0, Fwd, 80, W, H, Fine, 400, true);
            }
            else printf("  no cave found for the march view\n");
        }
        if (Has("mouth") || Has("doline"))
        {
            // Find a mouth on a wide grid (6 m), then render it from a fine one (2 m) around it.
            const FGrid Wide = MakeGrid(P, Has("upland") ? Upland.Dir : Valley.Dir, EnvD("MOUTH_VIEW_W", 8000), (int)(EnvD("MOUTH_VIEW_W", 8000) / 6), Fine);
            const FMouthStats MW = CountMouths(P, Wide, Fine);
            printf("  wide grid: %d mouths\n", MW.Components);
            const int Pick = (int)EnvD("MOUTH_PICK", 0);
            if (MW.Components > 0)
            {
                const auto [WX, WY] = MW.Centres[Pick % MW.Components];
                const FGrid G = MakeGrid(P, Wide.At(WX, WY).Dir, 1500, 750, Fine);
                const int MX = G.Px / 2, MY = G.Px / 2;
                const FColumn& Col = G.At(MX, MY);
                double E1[3], E2[3]; Frame(Col.Dir, E1, E2);
                // From the downhill side, 40 m up and 110 m out, looking at the mouth.
                const double GX = G.At(std::min(MX + 2, G.Px - 1), MY).Land - G.At(std::max(MX - 2, 0), MY).Land;
                const double GY = G.At(MX, std::min(MY + 2, G.Px - 1)).Land - G.At(MX, std::max(MY - 2, 0)).Land;
                double Ang = std::atan2(-GY, -GX);
                double DE[3]; DirAt(P, Col.Dir, E1, E2, 110 * std::cos(Ang), 110 * std::sin(Ang), DE);
                const double Ground = GridColumn(P, G, DE).Land;
                double Eye[3]; Point(P, DE, std::max(Ground, Col.Land) + 35, Eye);
                double Target[3]; Point(P, Col.Dir, Col.Land - 4, Target);
                double Fwd[3]; for (int a = 0; a < 3; a++) Fwd[a] = Target[a] - Eye[a];
                March(P, Out + (getenv("MOUTH_NAME") ? getenv("MOUTH_NAME") : "march_mouth.ppm"), G, Eye, Fwd, 60, W, H, Fine, 2500, false);
            }
        }
        if (Has("coastview"))
        {
            const FGrid G = MakeGrid(P, Cliff.Dir, 2000, 800, Fine);
            const FMouthStats M = CountMouths(P, G, Fine);
            printf("  coast grid: %d mouths\n", M.Components);
            if (M.Components > 0)
            {
                const auto [MX, MY] = M.Centres[(int)EnvD("MOUTH_PICK", 0) % M.Components];
                const FColumn& Col = G.At(MX, MY);
                double E1[3], E2[3]; Frame(Col.Dir, E1, E2);
                // The sea side: the lowest ground 250 m around.
                double Best2 = 1e9, Sea[3] = {};
                for (int k = 0; k < 24; k++)
                {
                    double D[3]; DirAt(P, Col.Dir, E1, E2, 250 * std::cos(k * M_PI / 12), 250 * std::sin(k * M_PI / 12), D);
                    const double Hh = GridColumn(P, G, D).Land;
                    if (Hh < Best2) { Best2 = Hh; for (int a = 0; a < 3; a++) Sea[a] = D[a]; }
                }
                double Eye[3]; Point(P, Sea, 5.0, Eye);
                double Target[3]; Point(P, Col.Dir, 3.0, Target);
                double Fwd[3]; for (int a = 0; a < 3; a++) Fwd[a] = Target[a] - Eye[a];
                March(P, Out + "march_coast.ppm", G, Eye, Fwd, 70, W, H, Fine, 1500, false);
            }
        }
    }
    else { printf("unknown mode %s\n", Mode.c_str()); return 1; }
    return 0;
}
