# volatile.sed - masks the run-to-run volatile parts of the harness outputs (used by compare.sh).
# Found by running the baseline twice (and a third probe run): every difference was a wall-clock
# timing. The rules are anchored on their context words, so deterministic times in the outputs
# (wave periods "a 6 s wave", "over 300 s", "height 1 ms across the wrap", ...) stay compared.

# "<verb> in <t> ms|us|s": plates 6x128^2 in 0.9 s / built in 2.3 ms / a key builds in 97 ms /
# emulated for 8 slots in 92 ms / emulated on the CPU in 0.09 s / solved in 0.29 s /
# planned in 0.001 s / shore field built in 281 ms / Coastal field: made ..., in 4.1 s
s/\bin [0-9]+(\.[0-9]+)? (ms|us|ns|s)\b/in <T> \2/g

# solve_harness, solve_dump, look_harness: "43 steps, final residual 0.0197, 3.8 s;"
s/(final residual [0-9.]+, )[0-9.]+ s;/\1<T> s;/g

# coast_check: "reached, 2.55 s (0.43 us a cell)", "  pack: 0.22 s", "(the whole coast 2.55 s)"
s/reached, [0-9.]+ s \([0-9.]+ us a cell\)/reached, <T> s (<T> us a cell)/
s/^(  pack: )[0-9.]+ s$/\1<T> s/
s/\(the whole coast [0-9.]+ s\)/(the whole coast <T> s)/g

# hook_check: "Evaluate 9.0 us per point", "frames 3.5 ms in a calm, 3.7 ms in the storm"
s/Evaluate [0-9.]+ us per point/Evaluate <T> us per point/
s/frames [0-9.]+ ms in a calm, [0-9.]+ ms in the storm/frames <T> ms in a calm, <T> ms in the storm/

# edit_harness: "CPU: 116 ns a sample near the edits"
s/CPU: [0-9.]+ ns a sample/CPU: <T> ns a sample/

# river_harness: "R1 reference: ... 287661 km (0.01 s)"
s/ km \([0-9.]+ s\)$/ km (<T> s)/

# shore_check: "1 inland basins; 3.4 ms", "7 jump passes (first step 32), 500 ms single-threaded"
s/basins; [0-9.]+ ms$/basins; <T> ms/
s/[0-9.]+ ms single-threaded/<T> ms single-threaded/

# spectrum_check: "N 128: 1.277 ms per slot (6 long slots at 15 Hz: 11.5% of a core)"
s/: [0-9.]+ ms per slot \(([0-9]+ long slots at [0-9]+ Hz): [0-9.]+% of a core\)/: <T> ms per slot (\1: <T>% of a core)/

# erode_tile: "... (deepest 0.1489) texels; 30 s"
s/texels; [0-9.]+ s$/texels; <T> s/
