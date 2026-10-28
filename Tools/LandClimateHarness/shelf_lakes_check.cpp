// R1 (BAKE-2): VoxelLandSea::FloodLakes, the ice shelf's lake removal by connectivity: a lake the
// shelf reaches goes whole, another lake at the same level stays, a lake across a face edge is one
// lake, and the apron cells carry their interior cells' marks.
//
//   g++ -O2 -std=c++20 $(<VoxelPlanets>/Tools/include_flags.sh) shelf_lakes_check.cpp -o shelf_lakes_check && ./shelf_lakes_check
//
// Prints the counts and exits 0 when they are as expected.
#include "VoxelLandSea.h"
#include <cstdio>
int main()
{
    const int N = 8, Pad = 4, S = N + 2 * Pad;
    std::vector<float> Water(size_t(6) * S * S, -3.0e38f);
    auto P = [S, Pad](int F, int I, int J) { return (F * S + J + Pad) * S + I + Pad; };
    // Lake A: face 0, cells (1..2, 1..2) at level 5; lake B: face 3, (5..6, 5..6) at level 5 too.
    for (int j = 1; j <= 2; j++) for (int i = 1; i <= 2; i++) Water[P(0, i, j)] = 5.0f;
    for (int j = 5; j <= 6; j++) for (int i = 5; i <= 6; i++) Water[P(3, i, j)] = 5.0f;
    // Lake C across a face edge: face 0 column N-1 rows 4..5 and whatever face lies past +I at level 7.
    for (int j = 4; j <= 5; j++) Water[P(0, N - 1, j)] = 7.0f;
    int F2, I2, J2; VoxelLandSea::Detail::CellOf(0, N, 4, N, F2, I2, J2); Water[P(F2, I2, J2)] = 7.0f;
    std::vector<int> Seeds = { P(0, 1, 1), P(0, N - 1, 4) };
    std::vector<unsigned char> M;
    VoxelLandSea::FloodLakes(N, Pad, Water.data(), Seeds, M);
    int A = 0, B = 0, C = 0, Other = 0;
    for (int j = 1; j <= 2; j++) for (int i = 1; i <= 2; i++) A += M[P(0, i, j)];
    for (int j = 5; j <= 6; j++) for (int i = 5; i <= 6; i++) B += M[P(3, i, j)];
    C = M[P(0, N - 1, 4)] + M[P(0, N - 1, 5)] + M[P(F2, I2, J2)];
    for (size_t k = 0; k < M.size(); k++) Other += M[k];
    // The apron copy of lake C's far cell: face 0, I = N, J = 4 is an apron cell of face 0.
    const int Apron = M[P(0, N, 4)];
    printf("A %d/4 (expect 4), B %d/4 (expect 0), C %d/3 (expect 3), apron %d (expect 1), marked in all %d\n", A, B, C, Apron, Other);
    return (A == 4 && B == 0 && C == 3 && Apron == 1) ? 0 : 1;
}
