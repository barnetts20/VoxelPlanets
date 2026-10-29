#!/bin/bash
# run_harnesses.sh - build and run the VoxelPlanets / VoxelPlugin C++ harnesses with plain g++,
# so a modified tree can be checked for bit-identical output against a baseline run.
#
#   run_harnesses.sh [options] <PluginsDir> <OutDir>
#
# <PluginsDir> holds VoxelPlanets/ and VoxelPlugin/ (a snapshot of the user's Plugins folder).
# Everything (harness sources, include_flags.sh, engine sources) is
# taken from that tree; nothing in it is written.
#
# Per harness, in <OutDir>:
#   <name>.txt    stdout+stderr of the run (or of the failed build, see <name>.rc)
#   <name>.rc     exit code of the run; "build-failed", "skipped: <why>", "dep-failed: <dep>",
#                 124 = timed out
#   <name>.files  sha256 of every file the run wrote (PNGs hashed by decoded pixels, since
#                 ImageMagick stamps dates into them)
#   <name>.time   wall seconds of the run (volatile, not compared)
#   _build/<name>.log  the compile log;  _summary.txt  one line per harness
#
# Options:
#   --slow          also run the harnesses marked slow (over ~5 min on this machine)
#   --gen-data      also generate the foam .vxnt set with Tools/NoiseTileTool/foam_tile.py
#                   (numpy; ~20 s at N=256, env FOAM_N) and run foam_set_check on it
#   --only "a b"    run only these harnesses (their dependencies are built and run too)
#   -j N            parallel compiles and runs (default: nproc)
#   --build-dir D   where binaries go (default ${TMPDIR:-/tmp}/harness_build/<tree path, mangled>)
#   --keep-work     keep the run folders (<OutDir>/work/<name>; dumps are tens of MB)
#   --list          print the harness table and exit
#   --build-only    compile, don't run
set -u

SLOW=0; GEN=0; BONLY=0; ONLY=""; JOBS=$(nproc); BUILD=""; KEEP=0; LIST=0
while [ $# -gt 0 ]; do
  case "$1" in
    --slow) SLOW=1 ;;
    --gen-data) GEN=1 ;;
    --only) ONLY="$2"; shift ;;
    -j) JOBS="$2"; shift ;;
    --build-dir) BUILD="$2"; shift ;;
    --keep-work) KEEP=1 ;;
    --list) LIST=1 ;;
    --build-only) BONLY=1 ;;
    -h|--help) sed -n '2,30p' "$0"; exit 0 ;;
    -*) echo "unknown option $1"; exit 2 ;;
    *) break ;;
  esac
  shift
done
if [ $LIST = 0 ] && [ $# -ne 2 ]; then echo "usage: $0 [options] <PluginsDir> <OutDir>"; exit 2; fi
P=""; OUT=""
if [ $# -ge 1 ]; then
  P=$(cd "$1" && pwd) || exit 2
  [ -d "$P/VoxelPlanets" ] && [ -d "$P/VoxelPlugin" ] || { echo "$P: no VoxelPlanets/ and VoxelPlugin/"; exit 2; }
fi
[ $# -ge 2 ] && { mkdir -p "$2" && OUT=$(cd "$2" && pwd) || exit 2; }
[ -z "$BUILD" ] && BUILD=${TMPDIR:-/tmp}/harness_build/$(echo "$P" | sed 's#^/##; s#/#_#g')

VP=$P/VoxelPlanets
VT=$VP/Tools
OH=$VT/OceanHarness
PRIV=$VP/Source/VoxelPlanets/Private
# include_flags.sh read through tr (works even if the tree still has CRLF line ends)
INC=""
[ -n "$P" ] && INC=$(tr -d '\r' < "$VT/include_flags.sh" | bash -s "$P")
CXX=${CXX:-g++}
BASE="-O2 -std=c++20"
NOFMA="-ffp-contract=off"

# ---------------------------------------------------------------------------------------------
# The table. H <name> <group> <timeout s> <dir> "<g++ flags and sources>" "<run args>" [<dep>]
#   group: default | slow | gen (needs --gen-data) | skip:<reason>
#   dir:   the harness's own folder (the compile runs there; -I. / -IUEStandIn are written as
#          absolute paths). The run's cwd is its own empty folder <OutDir>/work/<name>, with
#          the dependency's output files symlinked in (those are not hashed).
#   flags "=other" reuses harness "other"'s binary with different run args; "=python" runs
#          python3 -I <dir>/<args>.
# Flags follow each file's head-comment build line (order kept: include order matters, e.g.
# UEStandIn/CoreMinimal.h before OceanHarness/CoreMinimal.h).
# ---------------------------------------------------------------------------------------------
NAMES=(); declare -A GROUP TMO DIR FLAGS ARGS DEP
H() { NAMES+=("$1"); GROUP[$1]=$2; TMO[$1]=$3; DIR[$1]=$4; FLAGS[$1]=$5; ARGS[$1]=$6; DEP[$1]=${7:-}; }

FFT3="$PRIV/VoxelOceanWavesFFT.cpp $PRIV/VoxelOceanWeather.cpp $PRIV/VoxelOceanWeatherTable.cpp"
VOT="-DVOT_HEADER='\"$VP/Source/VoxelPlanets/Public/VoxelOceanTiles.h\"'"

# --- Ocean (Tools/OceanHarness, README.txt) ---
# ocean_tiles_harness's README line (-I. only) predates the plugin split: VoxelOceanTiles.h now
# includes VoxelCubeSphere.h from VoxelPlugin, so it gets the include flags too (after -I., so the
# folder's CoreMinimal.h stand-in still wins). Its default run (600 cameras x 4 factors, then 200
# cameras x the shore-bias cases) takes well over an hour here: it is "slow"; the quick run is
# args "<cameras> <morph start fraction = the default 0.5> <bias cameras>" = 6 0.5 10 (~2 min).
H ocean_tiles_harness  slow    14400 $OH "$VOT -I$OH $INC ocean_tiles_harness.cpp" ""
H ocean_tiles_quick    default 600  $OH "=ocean_tiles_harness" "6 0.5 10"
H ocean_pages_stats    default 300  $OH "$VOT -I$OH $INC ocean_pages_stats.cpp" ""
H waves_check          default 300  $OH "waves_check.cpp" ""
H underwater_check     default 300  $OH "underwater_check.cpp" ""
H ray_check            default 300  $OH "ray_check.cpp" ""
H normal_check         default 300  $OH "normal_check.cpp" ""
H spectrum_check       default 600  $OH "$NOFMA $INC -I$OH spectrum_check.cpp" ""
H spectrum_view        default 300  $OH "$NOFMA $INC -I$OH spectrum_view.cpp" "12 top 2000 0 0 spectrum_view.ppm"
H eq_check             default 300  $OH "$NOFMA $INC -I$OH eq_check.cpp" ""
H godot_match          default 900  $OH "$NOFMA $INC -I$OH godot_match.cpp" ""
H spray_check          default 300  $OH "$INC spray_check.cpp" ""
H cubic_check          default 300  $OH "$INC cubic_check.cpp" ""
H chart_check          default 300  $OH "$NOFMA $INC chart_check.cpp" ""
H hook_check           default 900  $OH "$NOFMA -I$OH/UEStandIn $INC -I$OH hook_check.cpp $FFT3" ""
H hook_view            default 300  $OH "$NOFMA -I$OH/UEStandIn $INC -I$OH hook_view.cpp $FFT3" "12 20 1 hook_view.ppm"
H underwater_map_check default 600  $OH "$NOFMA -I$OH/UEStandIn $INC -I$OH underwater_map_check.cpp $FFT3" ""
H weather_check        default 600  $OH "$NOFMA -I$OH/UEStandIn $INC -I$OH weather_check.cpp $PRIV/VoxelOceanWeather.cpp $PRIV/VoxelOceanWeatherTable.cpp" ""
H weather_view         default 300  $OH "-I$OH/UEStandIn $INC -I$OH weather_view.cpp $PRIV/VoxelOceanWeather.cpp" "0 weather_view.ppm"
H shore_check          default 600  $OH "$NOFMA $INC shore_check.cpp" ""
H shore_hook_check     default 900  $OH "$NOFMA -pthread -I$OH/UEStandIn $INC -I$OH shore_hook_check.cpp $FFT3" ""
H surf_check           default 600  $OH "$NOFMA $INC surf_check.cpp" ""
H foam_check           default 900  $OH "$NOFMA $INC -I$OH foam_check.cpp" ""
H coast_check          default 900  $OH "$NOFMA $INC coast_check.cpp" ""
H clip_check           default 600  $OH "-I$OH/ClipStandIn $INC clip_check.cpp" ""
H shading_check        default 600  $OH "$INC shading_check.cpp" ""
H lace_check           default 600  $OH "$INC lace_check.cpp" "."
# --gen-data: the foam set made by the tree's own foam_tile.py (numpy), N=$FOAM_N (default 256,
# ~20 s; the production 1024 takes 40+ min). At 256 foam_set_check fails one check by design of
# the coarse set ("magnified (cubic) ... covers average their densities" 0.22 > 0.08), so its
# baseline rc is 1; it is still a bit-exact regression check.
FOAM_N=${FOAM_N:-256}
H foam_tile_gen        gen     3600 $VT/NoiseTileTool "=python" "foam_tile.py foam$FOAM_N.vxnt $FOAM_N 1"
H foam_set_check       gen     900  $OH "$INC foam_set_check.cpp" "foam$FOAM_N.vxnt ." foam_tile_gen

# --- Land climate ---
H climate_check        default 600  $VT/LandClimateHarness "$NOFMA $INC climate_check.cpp" ""
H shelf_lakes_check    default 300  $VT/LandClimateHarness "$INC shelf_lakes_check.cpp" ""

# --- Land solve (a 1,000 km planet). solve_dump's fields feed the gully, carve and river runs ---
H solve_harness        default 1200 $VT/LandSolveHarness "$NOFMA -pthread $INC solve_harness.cpp" "128 s128"
H solve_dump           default 900  $VT/LandSolveHarness "=solve_harness" "128 n128 dump 1e6"
H look_harness         default 900  $VT/LandSolveHarness "$NOFMA -pthread $INC look_harness.cpp" "128 look128"
H look_warpfolds       default 600  $VT/LandSolveHarness "=look_harness" "0 x WarpFolds=1"

# --- Land gully / carve / cache (on solve_dump's n128 bake) ---
GH=$VT/LandGullyHarness
H gully_harness        default 900  $GH "$NOFMA -pthread $INC gully_harness.cpp" "n128 128 1e6 look" solve_dump
for m in slope edges lod turn probe agree noise cgrad dgrad seam ref grad rivers peaks pgrad hgrad hills; do
  H gully_$m           default 900  $GH "=gully_harness" "n128 128 1e6 $m" solve_dump
done
H carve_harness        default 1200 $GH "$NOFMA -pthread $INC carve_harness.cpp" "n128 128 1e6 check" solve_dump
H carve_views          default 900  $GH "=carve_harness" "n128 128 1e6 views" solve_dump
H gully_strata         "skip:needs STRATA_SET (an authored strata .vxnt set, absent)" 0 $GH "=gully_harness" "n128 128 1e6 strata" solve_dump
H cache_plan_harness   default 900  $GH "$INC cache_plan_harness.cpp" "1000"
H dune_check           "skip:needs the authored dune / sand detail .vxnt sets (absent)" 0 $GH "$NOFMA -pthread $INC dune_check.cpp" ""
H cache_harness        "skip:does not build (its VoxelShader::float4 clashes with VoxelShaderCompat.h's)" 0 $GH "$NOFMA -pthread $INC cache_harness.cpp" ""
H material_harness     "skip:does not build (material_side.cpp's VoxelShader::float4 clashes with VoxelShaderCompat.h's)" 0 $GH "$NOFMA -pthread $INC material_harness.cpp material_side.cpp" ""

# --- Land rivers ---
H river_harness        default 900  $VT/LandRiverHarness "$NOFMA -pthread $INC -I$VT/LandRiverHarness river_harness.cpp" "n128 128 1e6 200 map" solve_dump

# --- Edits ---
H cache_edit_harness   default 900  $VT/EditHarness "$INC cache_edit_harness.cpp" ""
H edit_harness         default 900  $P/VoxelPlugin/Tools/EditHarness "-I$P/VoxelPlugin/Source/VoxelPlugin/Public -I$P/VoxelPlugin/Shaders/Private edit_harness.cpp" ""

# --- Noise tile generators (outputs hashed) ---
H eroded_peaks         default 900  $VT/NoiseTileTool "$INC eroded_peaks.cpp -pthread" "peaks512.vxnt 512 1"
H erode_tile           default 900  $VT/NoiseTileTool "$INC erode_tile.cpp -pthread" "tile256.vxnt N=256 Massifs=4"

# --- Not C++ harnesses ---
H shader_check         "skip:Tools/ShaderCheck/shader_check.sh needs dxc (DirectXShaderCompiler), not installed" 0 $VT/ShaderCheck "" ""
H land_view            "skip:Tools/LandView/*.py are matplotlib viewers of dumps/exports (look.py picks regions interactively)" 0 $VT/LandView "" ""

if [ $LIST = 1 ]; then
  for n in "${NAMES[@]}"; do printf '%-22s %-8s %5s  %s\n' "$n" "${GROUP[$n]%%:*}" "${TMO[$n]}" "${ARGS[$n]}"; done
  exit 0
fi

# ---------------------------------------------------------------------------------------------
# Selection
# ---------------------------------------------------------------------------------------------
declare -A WANT
want() { local n=$1; WANT[$n]=1; [ -n "${DEP[$n]}" ] && want "${DEP[$n]}"; }
for n in "${NAMES[@]}"; do
  g=${GROUP[$n]}
  if [ -n "$ONLY" ]; then [[ " $ONLY " == *" $n "* ]] && want "$n"; continue; fi
  case $g in
    default) want "$n" ;;
    slow) [ $SLOW = 1 ] && want "$n" ;;
    gen) [ $GEN = 1 ] && want "$n" ;;
  esac
done
bin_of() { local f=${FLAGS[$1]}; if [[ $f == =* ]]; then echo "${f#=}"; else echo "$1"; fi; }

mkdir -p "$OUT/_build" "$BUILD"
SUMMARY=$OUT/_summary.txt; : > "$SUMMARY"
T_ALL=$(date +%s)
echo "tree:  $P"; echo "out:   $OUT"; echo "build: $BUILD"; echo "jobs:  $JOBS"

# Record skipped / not-selected ones up front.
for n in "${NAMES[@]}"; do
  g=${GROUP[$n]}
  [ -n "${WANT[$n]:-}" ] && continue
  rm -f "$OUT/$n".{txt,rc,files,time}
  case $g in
    skip:*) echo "skipped: ${g#skip:}" > "$OUT/$n.rc" ;;
    slow) [ -z "$ONLY" ] && echo "skipped: slow (run with --slow)" > "$OUT/$n.rc" ;;
    gen) [ -z "$ONLY" ] && echo "skipped: needs a generated .vxnt (run with --gen-data)" > "$OUT/$n.rc" ;;
  esac
done

# ---------------------------------------------------------------------------------------------
# Build (parallel)
# ---------------------------------------------------------------------------------------------
build_one() {   # name
  local n=$1 f=${FLAGS[$1]} d=${DIR[$1]} log=$OUT/_build/$1.log
  if [ "$f" = "=python" ]; then echo ok > "$OUT/_build/$n.status"; return; fi
  local t0=$(date +%s.%N)
  # shellcheck disable=SC2086
  if (cd "$d" && eval "$CXX $BASE $f -o '$BUILD/$n'") > "$log" 2>&1; then
    echo ok > "$OUT/_build/$n.status"
  else
    echo failed > "$OUT/_build/$n.status"
  fi
  printf '%.1f\n' "$(echo "$(date +%s.%N) - $t0" | bc)" > "$OUT/_build/$n.secs"
}
declare -A NEEDBIN
for n in "${NAMES[@]}"; do
  [ -n "${WANT[$n]:-}" ] || continue
  [ "${FLAGS[$n]}" = "=python" ] && continue
  NEEDBIN[$(bin_of "$n")]=1
done
BUILDS=(); for n in "${NAMES[@]}"; do [ -n "${NEEDBIN[$n]:-}" ] && BUILDS+=("$n"); done
echo "== building ${#BUILDS[@]} binaries"
T_B=$(date +%s)
running=0
for n in "${BUILDS[@]}"; do
  rm -f "$OUT/_build/$n.status" "$BUILD/$n"
  build_one "$n" &
  running=$((running + 1))
  if [ $running -ge "$JOBS" ]; then wait -n; running=$((running - 1)); fi
done
wait
for n in "${BUILDS[@]}"; do printf '  %-22s %-6s %ss\n' "$n" "$(cat "$OUT/_build/$n.status")" "$(cat "$OUT/_build/$n.secs" 2>/dev/null)"; done
echo "   build wall: $(( $(date +%s) - T_B )) s"
[ $BONLY = 1 ] && exit 0

# ---------------------------------------------------------------------------------------------
# Run (parallel, dependencies first)
# ---------------------------------------------------------------------------------------------
hash_files() {   # workdir -> sha256 list of regular files (not the linked-in dependency files)
  local w=$1
  (cd "$w" && find . -type f | sort | while read -r f; do
    case "$f" in
      *.png) h=$(convert "$f" -depth 8 rgba:- 2>/dev/null | sha256sum | cut -c1-64); echo "$h  $f (pixels)" ;;
      # erode_tile's and look_harness's .json carry the run's wall time ("seconds": 30.4)
      *.json) h=$(sed -E 's/"seconds": *[-0-9.eE+]+/"seconds": <T>/g' "$f" | sha256sum | cut -c1-64); echo "$h  $f (seconds masked)" ;;
      *) sha256sum "$f" ;;
    esac
  done)
}
run_one() {   # name
  local n=$1 b w rc t0
  b=$(bin_of "$n"); w=$OUT/work/$n
  rm -rf "$w"; mkdir -p "$w"
  rm -f "$OUT/$n".{txt,rc,files,time}
  local st=ok
  [ "${FLAGS[$n]}" = "=python" ] || st=$(cat "$OUT/_build/$b.status" 2>/dev/null || echo failed)
  if [ "$st" != ok ]; then
    cp "$OUT/_build/$b.log" "$OUT/$n.txt" 2>/dev/null
    echo build-failed > "$OUT/$n.rc"; return
  fi
  local d=${DEP[$n]}
  if [ -n "$d" ]; then
    if [ "$(cat "$OUT/$d.rc" 2>/dev/null)" != 0 ]; then echo "dep-failed: $d" > "$OUT/$n.rc"; return; fi
    for f in "$OUT/work/$d"/*; do ln -s "$f" "$w/"; done
  fi
  t0=$(date +%s.%N)
  if [ "${FLAGS[$n]}" = "=python" ]; then
    # shellcheck disable=SC2086
    (cd "$w" && timeout "${TMO[$n]}" python3 -I "${DIR[$n]}/"${ARGS[$n]}) > "$OUT/$n.txt" 2>&1
  else
    # shellcheck disable=SC2086
    (cd "$w" && timeout "${TMO[$n]}" "$BUILD/$b" ${ARGS[$n]}) > "$OUT/$n.txt" 2>&1
  fi
  rc=$?
  printf '%.1f\n' "$(echo "$(date +%s.%N) - $t0" | bc)" > "$OUT/$n.time"
  hash_files "$w" > "$OUT/$n.files"
  echo $rc > "$OUT/$n.rc"
}
TORUN=(); for n in "${NAMES[@]}"; do [ -n "${WANT[$n]:-}" ] && TORUN+=("$n"); done
echo "== running ${#TORUN[@]} harnesses"
T_R=$(date +%s)
declare -A DONE STARTED
# Order: the longest (by timeout) first, so a long one doesn't end up alone at the end.
mapfile -t TORUN < <(for n in "${TORUN[@]}"; do echo "${TMO[$n]} $n"; done | sort -rn -s -k1,1 | cut -d' ' -f2)
while :; do
  progress=0; left=0
  for n in "${TORUN[@]}"; do
    [ -n "${STARTED[$n]:-}" ] && continue
    left=1
    d=${DEP[$n]}
    if [ -n "$d" ] && [ -n "${WANT[$d]:-}" ] && [ ! -f "$OUT/$d.rc" ]; then continue; fi
    if [ "$(jobs -rp | wc -l)" -ge "$JOBS" ]; then break; fi
    STARTED[$n]=1; progress=1
    ( run_one "$n"; printf '  %-22s rc=%-14s %6ss\n' "$n" "$(cat "$OUT/$n.rc")" "$(cat "$OUT/$n.time" 2>/dev/null || echo -)" ) &
  done
  [ $left = 0 ] && break
  [ $progress = 0 ] && { wait -n 2>/dev/null || sleep 1; }
done
wait
echo "   run wall: $(( $(date +%s) - T_R )) s"

# ---------------------------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------------------------
pass=0; fail=0; skip=0
for n in "${NAMES[@]}"; do
  [ -f "$OUT/$n.rc" ] || continue
  rc=$(cat "$OUT/$n.rc")
  case $rc in
    0) s=PASS; pass=$((pass + 1)) ;;
    skipped*) s=SKIP; skip=$((skip + 1)) ;;
    124) s=TIMEOUT; fail=$((fail + 1)) ;;
    *) s=FAIL; fail=$((fail + 1)) ;;
  esac
  printf '%-22s %-7s %-8s %s\n' "$n" "$s" "$(cat "$OUT/$n.time" 2>/dev/null || echo -)" "$rc" >> "$SUMMARY"
done
echo "== summary ($SUMMARY)"; cat "$SUMMARY"
echo "pass $pass, fail $fail, skipped $skip; total wall $(( $(date +%s) - T_ALL )) s"
[ $KEEP = 1 ] || rm -rf "$OUT/work"
[ $fail = 0 ]
