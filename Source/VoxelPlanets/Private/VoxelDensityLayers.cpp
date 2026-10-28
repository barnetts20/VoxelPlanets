#include "VoxelDensityLayers.h"
#include "VoxelDensityGPU.h"
#include "VoxelShaderCompat.h"

namespace VoxelShader
{
#include "VoxelDensityCore.ush"
}

namespace VoxelDensityLayersDetail
{
    /** Standard deviation of one octave of VoxelGradientNoise (measured over 400k points). */
    constexpr double SingleOctaveSigma = 0.1914;

    /** A coordinate in 32.32 fixed point (VoxelShader::FVoxelFixed). |V| below 2^31. */
    VoxelShader::FVoxelFixed ToFixed(double V)
    {
        const double Floor = FMath::FloorToDouble(V);
        VoxelShader::FVoxelFixed Out;
        Out.Cell = (uint32)(int64)Floor;
        Out.Frac = (uint32)FMath::Min((V - Floor) * 4294967296.0, 4294967295.0);
        return Out;
    }

    /** Splits a lattice coordinate into a cell (wrapped to 32 bits) and the fraction in it. */
    void SplitLattice(const FVector& L, VoxelShader::uint3& OutCell, VoxelShader::float3& OutFrac)
    {
        const double FX = FMath::FloorToDouble(L.X);
        const double FY = FMath::FloorToDouble(L.Y);
        const double FZ = FMath::FloorToDouble(L.Z);
        OutCell = VoxelShader::uint3((uint32)(int64)FX, (uint32)(int64)FY, (uint32)(int64)FZ);
        OutFrac = VoxelShader::float3((float)(L.X - FX), (float)(L.Y - FY), (float)(L.Z - FZ));
    }
}

VoxelDensityLayers::FLayer VoxelDensityLayers::MakeLayer(uint32 Kind, double WavelengthFraction, double InPlanetRadius, double InLacunarity, double InGain, int32 InOctaves, uint32 InSeed)
{
    using namespace VoxelDensityLayersDetail;

    FLayer Layer;
    Layer.Kind = Kind;
    const double Lacunarity = FMath::Max(InLacunarity, 1.01);
    const int32 NumOctaves = FMath::Max(InOctaves, 0);

    double Frequency = 1.0 / FMath::Max(WavelengthFraction * InPlanetRadius, 1e-3);
    // Lacunarity 2 keeps every octave's frequency the finest one's over a power of two. The
    // finest coordinate (up to the radius times its frequency) must fit the 32-bit cell.
    const bool bFixed = Lacunarity == 2.0 && NumOctaves > 0 && NumOctaves <= 32
        && InPlanetRadius * Frequency * FMath::Pow(2.0, (double)(NumOctaves - 1)) < 1073741824.0;
    float Amplitude = 1.0f;
    double AmpSum = 0.0;
    double AmpSquareSum = 0.0;
    double Amp = 1.0;
    for (int32 o = 0; o < NumOctaves; o++)
    {
        AmpSum += Amp;
        AmpSquareSum += Amp * Amp;
        Amp *= InGain;

        FOctave& Octave = Layer.Octaves.AddDefaulted_GetRef();
        Octave.Frequency = Frequency;
        Octave.Wavelength = (float)(1.0 / Frequency);
        Octave.Amplitude = Amplitude;
        Octave.Seed = InSeed + (uint32)o * 0x9e3779b9u;
        // A per-octave shift of the lattice, so octaves don't share zero crossings.
        const uint32 H = VoxelShader::VoxelHashSeed(InSeed, 0x51ed27u + (uint32)o);
        Octave.Offset = FVector((H & 0x3ffu) * 0.618034, ((H >> 10) & 0x3ffu) * 0.414214, ((H >> 20) & 0x3ffu) * 0.732051);
        if (bFixed)
        {
            Octave.Shift = (uint32)(NumOctaves - 1 - o);
            for (int32 Axis = 0; Axis < 3; Axis++)
            {
                const VoxelShader::FVoxelFixed F = ToFixed(Octave.Offset[Axis]);
                Octave.OffsetCell[Axis] = F.Cell;
                Octave.OffsetFrac[Axis] = F.Frac;
            }
        }

        Frequency *= Lacunarity;
        Amplitude *= (float)InGain;
    }
    // Ridged sums are each in [0, 1], so dividing by the amplitude sum keeps [0, 1]. fBm sums
    // are scaled to a standard deviation of 0.4, so about [-1, 1] holds 98% of the surface
    // (dividing by the amplitude sum would leave them in about +-0.25).
    if (Kind == VoxelShader::VoxelLayerRidged)
        Layer.InvNorm = AmpSum > 0.0 ? 1.0 / AmpSum : 0.0;
    else
        Layer.InvNorm = AmpSquareSum > 0.0 ? 1.0 / (2.5 * SingleOctaveSigma * FMath::Sqrt(AmpSquareSum)) : 0.0;
    return Layer;
}

double VoxelDensityLayers::EvaluateLayer(const FLayer& Layer, const FVector& SurfacePoint, double Footprint, float RidgeGain, float SlopeDamping)
{
    return EvaluateLayerGradient(Layer, SurfacePoint, Footprint, RidgeGain, SlopeDamping, nullptr);
}

double VoxelDensityLayers::EvaluateLayerGradient(const FLayer& Layer, const FVector& SurfacePoint, double Footprint, float RidgeGain, float SlopeDamping, FVector* OutGradient)
{
    using namespace VoxelShader;
    using VoxelDensityLayersDetail::ToFixed;
    using VoxelDensityLayersDetail::SplitLattice;

    // The core keeps its running sum in float; each octave's contribution is taken from a
    // zeroed sum and added here in double, so the total keeps double precision.
    FVoxelFbmState State = VoxelFbmBegin();
    double Sum = 0.0;
    FVector Gradient = FVector::ZeroVector;
    if (OutGradient) *OutGradient = FVector::ZeroVector;
    if (Layer.Octaves.Num() == 0) return 0.0;

    // Lacunarity 2: the finest octave's coordinate once, in fixed point, as the GPU takes it.
    const bool bFixed = Layer.Octaves[0].Shift != VoxelNoFixedPoint;
    FVoxelFixed Finest[3] = {};
    if (bFixed)
    {
        const FVector Coord = SurfacePoint * Layer.Octaves.Last().Frequency;
        for (int32 Axis = 0; Axis < 3; Axis++)
            Finest[Axis] = ToFixed(Coord[Axis]);
    }
    for (const FOctave& Octave : Layer.Octaves)
    {
        const float Band = VoxelOctaveBandWeight(Octave.Wavelength, (float)Footprint);
        if (Band <= 0.0f) break;

        uint3 Cell;
        float3 Frac;
        if (bFixed)
        {
            FVoxelFixed L[3];
            for (int32 Axis = 0; Axis < 3; Axis++)
                L[Axis] = VoxelFixedLattice(Finest[Axis], Octave.Shift, Octave.OffsetCell[Axis], Octave.OffsetFrac[Axis]);
            Cell = uint3(L[0].Cell, L[1].Cell, L[2].Cell);
            Frac = float3(VoxelFixedFraction(L[0].Frac), VoxelFixedFraction(L[1].Frac), VoxelFixedFraction(L[2].Frac));
        }
        else
        {
            SplitLattice(SurfacePoint * Octave.Frequency + Octave.Offset, Cell, Frac);
        }
        const FVoxelNoiseSample N = VoxelGradientNoise(Cell, Frac, Octave.Seed);

        State.Sum = 0.0f;
        State = VoxelLayerOctave(State, Layer.Kind, N, Octave.Amplitude * Band, RidgeGain, SlopeDamping);
        Sum += State.Sum;
        if (OutGradient)
            Gradient += FVector(N.Gradient.x, N.Gradient.y, N.Gradient.z) * ((double)(Octave.Amplitude * Band) * Octave.Frequency);
    }
    if (OutGradient) *OutGradient = Gradient * Layer.InvNorm;
    return Sum * Layer.InvNorm;
}

void VoxelDensityLayers::AppendGPUOctaves(const FLayer& Layer, TArray<FVoxelDensityGPUOctave>& Out)
{
    for (const FOctave& Octave : Layer.Octaves)
    {
        FVoxelDensityGPUOctave& G = Out.AddDefaulted_GetRef();
        G.Frequency = VoxelDoubleFloat(Octave.Frequency);
        G.OffsetX = VoxelDoubleFloat(Octave.Offset.X);
        G.OffsetY = VoxelDoubleFloat(Octave.Offset.Y);
        G.OffsetZ = VoxelDoubleFloat(Octave.Offset.Z);
        G.Wavelength = Octave.Wavelength;
        G.Amplitude = Octave.Amplitude;
        G.Seed = Octave.Seed;
        G.Shift = Octave.Shift;
        for (int32 Axis = 0; Axis < 3; Axis++)
        {
            G.OffsetCell[Axis] = Octave.OffsetCell[Axis];
            G.OffsetFrac[Axis] = Octave.OffsetFrac[Axis];
        }
    }
}

double VoxelDensityLayers::FinestFrequency(const FLayer& Layer)
{
    return Layer.Octaves.Num() > 0 ? Layer.Octaves.Last().Frequency : 0.0;
}
