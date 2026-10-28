# Stage P1d: the ice shelf bake off the game thread (S1)

The third fix of P1, from `Design/Performance.md` (catalogue item S1). It changes VoxelPlanets only. `StageP1d.zip` is cumulative from R1a (VoxelPlugin + VoxelPlanets: P1a2 and P1c included). CloudAtmosphere stays at `CloudAtmosphere_P1b2.zip`.

## The cost it goes after
**The spawn's hitch:** on every build, the planet's longest game-thread tick was **548–780 ms**. It was the tick the climate gate passed in. In that one tick:
- the ice shelf bake (`VoxelLandClimate::MakeIceShelfBake`): **360–590 ms**;
- the climate field: 65–96 ms;
- the compositor: 46–77 ms;
- the rest of `Initialize`: 13–35 ms.

## What changes
**The shelf bake runs on the thread pool** (`Async(EAsyncExecution::ThreadPool)`), and the build waits for its future the way it already waits for the land bake. `MakeIceShelfBake` is documented as any-thread, and it is pure CPU:
- it reads its inputs, which the task holds by shared pointer (the bake, the climate field) and by value (the settings);
- it takes the base bake's rivers under their lock.

So the same function runs on the same inputs and makes **the same shelf, bit for bit**, a few frames later and off the game thread.

**The tick split, as a result:**
- **The climate tick:** the snapshot lands, then the field is built (65–96 ms) and the shelf task starts.
- **The ticks after it:** the build polls the task (microseconds) until the shelf is in.
- **The `Initialize` tick:** the compositor and the rest of `Initialize`, about 60–110 ms.

**Robustness:**
- A rebuild for other inputs (a new bake, a new field, other shelf settings) starts a new task. The old one finishes unread; it holds its own inputs and nothing of the actor's.
- With no shelf (shelf off, a warm planet, a noise planet) the build goes straight on, as before.

**The spawn timeline's shore mark (the P1a quirk)** now requires the ocean's shore field to be this build's, by its bake key (`AOceanSphereActor::HasShoreFieldFor`). Before, a reseed marked "shore field" as soon as `Initialize` finished, because the old seed's field was still held.

## Expected
- **The longest spawn tick:** 548–780 ms becomes about **100 ms**. The climate tick and the `Initialize` tick are each ~60–110 ms.
- **The spawn's total time:** about unchanged. The shelf now takes its ~0.4–0.6 s on a worker while frames keep rendering, instead of freezing one frame.
- **The timeline line:** "ice shelf" game-thread ms should read ~0, and "the longest" tick should drop. The "shore field" time on reseeds should now be the real one (~0.6–0.7 s later than before).

**Not in this stage:**
- **The field (65–96 ms)** stays on the game thread. Its result feeds the fade, the upload and several call sites, which makes it a larger change; it's worth it only if the climate tick still shows.
- **The compositor (46–77 ms)** stays too: S2 splits `Initialize` if it still shows.

## Files
- `Source/VoxelPlanets/Public/PlanetActor.h`: `UpdateIceShelf` returns whether the shelf is ready; `ShelfFuture`, `ShelfFutureHash`; `#include "Async/Future.h"`.
- `Source/VoxelPlanets/Private/PlanetActor.cpp`:
  - the build waits on `UpdateIceShelf()`;
  - the task launch and poll;
  - the future cleared where the shelf is dropped;
  - the timeline's shore mark by bake key;
  - `#include "Async/Async.h"`.
- `Source/VoxelPlanets/Public/OceanSphereActor.h`, `Private/OceanSphereActor.cpp`: `HasShoreFieldFor(BakeKey)`.

## Checked
- No shader and no harness-compiled file is touched. The shelf's output is unchanged by construction: the same call, the same inputs.
- Not checked here (no toolchain): the build.

## The capture to take with it
1. **Spawns:** reseed three or four times (cached and cold) and paste the `spawn timeline` lines. The figures are:
   - "the longest" tick and "ice shelf" ms;
   - when "initialized" lands, against the baseline (cached 2.13–2.36 s, cold 3.18–3.23 s);
   - the shore field's time.
2. **The look:** the polar ice shelf on a cold seed should look exactly as before. It should be bit-identical; a glance at a shelf front confirms it.
