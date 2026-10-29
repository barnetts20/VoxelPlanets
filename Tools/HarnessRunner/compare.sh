#!/bin/bash
# compare.sh - compare two run_harnesses.sh output folders.
#
#   compare.sh [-v] <OutA> <OutB>
#
# Per harness (every <name>.rc in either folder): the exit code, the output text with the
# volatile parts masked (volatile.sed, REPORT.md), and the hashes of the files the run
# wrote (<name>.files). Prints "identical" or the differences (a unified diff of the masked text,
# capped at 40 lines; -v for all of it). Exit status 0 when every harness is identical.
set -u
VERBOSE=0
[ "${1:-}" = "-v" ] && { VERBOSE=1; shift; }
[ $# -eq 2 ] || { echo "usage: $0 [-v] <OutA> <OutB>"; exit 2; }
A=$1; B=$2
[ -d "$A" ] && [ -d "$B" ] || { echo "no such folder"; exit 2; }

# The volatile parts (all wall-clock timings) are masked by volatile.sed, next to this script;
# its comments list each one. Masked values read "<T>".
SED=$(dirname "$0")/volatile.sed
[ -f "$SED" ] || { echo "missing $SED"; exit 2; }
mask() { [ -f "$1" ] && sed -E -f "$SED" "$1"; }

fail=0
names=$( { (cd "$A" && ls *.rc); (cd "$B" && ls *.rc); } 2>/dev/null | sed 's/\.rc$//' | sort -u)
for n in $names; do
  msgs=()
  ra=$(cat "$A/$n.rc" 2>/dev/null || echo "(missing)"); rb=$(cat "$B/$n.rc" 2>/dev/null || echo "(missing)")
  [ "$ra" != "$rb" ] && msgs+=("exit: $ra -> $rb")
  if [[ $ra == skipped* && $rb == skipped* ]]; then printf '%-22s skipped in both\n' "$n"; continue; fi
  # Run on one side only (--slow / --gen-data / --only differ): not a difference, just noted.
  if [[ $ra == skipped* || $ra == "(missing)" ]]; then printf '%-22s not compared (A: %s)\n' "$n" "$ra"; continue; fi
  if [[ $rb == skipped* || $rb == "(missing)" ]]; then printf '%-22s not compared (B: %s)\n' "$n" "$rb"; continue; fi
  d=$(diff -u --label "$A/$n.txt" --label "$B/$n.txt" <(mask "$A/$n.txt") <(mask "$B/$n.txt"))
  [ -n "$d" ] && msgs+=("output differs ($(echo "$d" | grep -c '^[-+][^-+]') lines):")
  fd=""
  if [ -f "$A/$n.files" ] || [ -f "$B/$n.files" ]; then
    fd=$(diff <(cat "$A/$n.files" 2>/dev/null) <(cat "$B/$n.files" 2>/dev/null) | grep '^[<>]')
  fi
  if [ ${#msgs[@]} -eq 0 ] && [ -z "$fd" ]; then printf '%-22s identical\n' "$n"; continue; fi
  fail=1
  printf '%-22s DIFFERENT\n' "$n"
  for m in "${msgs[@]}"; do echo "    $m"; done
  if [ -n "$d" ]; then
    if [ $VERBOSE = 1 ]; then echo "$d" | sed 's/^/      /'; else echo "$d" | tail -n +3 | head -40 | sed 's/^/      /'; fi
  fi
  [ -n "$fd" ] && { echo "    files differ (< $A, > $B):"; echo "$fd" | sed 's/^/      /'; }
done
exit $fail
