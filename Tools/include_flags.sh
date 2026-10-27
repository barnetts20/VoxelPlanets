#!/bin/bash
# Prints the include flags a harness builds with since the split (Design/PlanetPluginSplit.md, S2):
# VoxelPlanets' Public, Private and Shaders/Private, then VoxelPlugin's (the engine's), so the
# planet's files win over the engine's forwarding stubs of the same names.
#
#   g++ -O2 -std=c++20 $(<VoxelPlanets>/Tools/include_flags.sh) -I. some_check.cpp -o some_check
#
# The Plugins folder holding both plugins is found from this script's place, or given as $1.
P=$(cd "${1:-$(dirname "$0")/../..}" && pwd)
for d in VoxelPlanets/Source/VoxelPlanets/Public VoxelPlanets/Source/VoxelPlanets/Private VoxelPlanets/Shaders/Private \
         VoxelPlugin/Source/VoxelPlugin/Public VoxelPlugin/Source/VoxelPlugin/Private VoxelPlugin/Shaders/Private; do
  printf -- '-I%s ' "$P/$d"
done
echo
