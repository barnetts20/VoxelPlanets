# Harness runner (R2, 2026-10-28)

Builds and runs every plain-C++ harness in `VoxelPlanets/Tools` and `VoxelPlugin/Tools` with g++, and compares two runs' outputs, so a change can be checked for bit-identical results against a baseline.

    run_harnesses.sh [--slow] [--gen-data] [--only "a b"] [-j N] <PluginsDir> <OutDir>
    compare.sh <OutA> <OutB>      # masks timings and other volatile lines (volatile.sed)

- `--list` prints the harness table. Each harness's own build line is in its head comment; this runner encodes them.
- The scripts are CRLF in the repository: strip the CRs (`sed -i 's/\r$//' *.sh *.sed`) and `chmod +x` before running.
- `shader_check` runs only if `DXC` points at a dxc binary (see `claude/HANDOFF_CONTEXT.md` for the setup).

**Baseline at R2a** (g++ 13.3, 2 cores): 58 pass, 0 fail, 9 skipped, about 11 minutes. Skipped:
- `cache_harness` and `material_harness` (harness drift: their own `VoxelShader::float4`; on `claude/DeadCode.md`);
- `dune_check` and the gully harness's strata mode (they need authored `.vxnt` sets);
- `shader_check` without dxc;
- the LandView viewers (interactive).

`ocean_tiles_harness`'s full run takes hours; the default runs `6 0.5 10`. The gully harness's `ref` mode prints a checksum for bit-identical refactors.
