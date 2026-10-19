# Predictive Safe Click — Windows GD 2.2081 / Geode 5.11.2

## What this implementation does

This is a genuine **Geode mod source project**, not a compiled `.geode` installer.
The testable C++ predictor is implemented. A Geode/Windows build is **not verified** by the tests below.

On a normal cube jump, the mod forecasts a full arc against **static hazard and solid AABBs**.
It does not just buffer landing inputs. If jumping NOW predicts a lethal obstacle collision,
it suppresses the original press and rechecks its predicted trajectory on each upcoming
physics step. At the earliest step it predicts safe, it injects the press using the game's
`GJBaseGameLayer::handleButton` method. If all options remain predicted unsafe, it
**forces the press at the configured limit (default 4 ticks)**. A release that happens
before the delayed press is preserved as a 1-tick press, so brief taps are not lost.

It uses **Geode `$modify` hooks** rather than raw Game.exe RVA patches or injected
Windows hooks. Specifically it hooks `GJBaseGameLayer::handleButton` and
`PlayLayer::postUpdate` with built-in hook chaining. No hard-coded binary addresses.

## Supported / known limits

- Initial implementation intentionally operates only for **classic cube**, normal gravity,
  non-platformer, grounded jump presses. All other gamemodes and airborne presses are
  forwarded unchanged.
- Geometry collision uses object AABBs, not native triangle/saw polygon hitboxes or
  engine-exact collision; a rotated spike may be modeled conservatively (false positive).
- Object positions are snapshots, not future trigger/move results. Dynamic hazards,
  orbs, portals, slopes, dual mode and unusual level mechanics are **not fully predicted**.
- Jump velocity/gravity are seeded from approximately calibrated GD-like cube values
  and automatically adjusted from live player movement. The first jump is approximate.
- To make each tick correspond to a consistent visual update in this build, **test at
  240 FPS with normal 240 TPS**. Lower/variable FPS requires a different scheduling hook
  if `postUpdate` does not execute per physics step on your GD configuration.
- This changes inputs and should be considered an assist/cheat for leaderboard purposes.
- A good prediction is not a guarantee of survival. Test in your own simple editor level
  with one spike before attempting demanding levels.

## Building `.geode` using your existing GitHub Actions Windows runner

1. Extract the ZIP. Upload/commit **the project contents** to the root of the GitHub
   repository used for the build. Keep the `.github/workflows/build.yml` path intact.
   Do not upload the outer ZIP alone, and don't mix this with the old XINPUT proxy source.
2. Open GitHub **Actions > Build Windows Geode mod > Run workflow** (or push to `main`).
3. If the job is green, download its `PredictiveSafeClick-Win64` artifact, then unzip it.
   The produced `.geode` is the installable file (not the source ZIP).
4. Copy `.geode` into your game's `geode/mods/` folder.
5. In Geode mod settings, ensure **Enabled = ON** and **Maximum buffering ticks = 4**.
   For troubleshooting set **Diagnostic log = ON** and inspect the Geode log for
   `SafeClick: buffered press` and `SafeClick: injected click` lines.

If compilation fails, **send the first compiler ERROR and the surrounding 20 lines**, not
just the last line of the entire Actions log. No Windows binary was built in this environment.

**Important:** If you still have the earlier `XINPUT9_1_0.dll` safe-click test proxy
beside `GeometryDash.exe`, temporarily remove *that custom test proxy* before trying
this mod. Do not delete a legitimate game/system DLL.

## Testing the standalone core locally

These tests need only CMake and a C++23 compiler (no GD/Geode SDK):

```bash
cmake -S . -B build-tests -DSAFECLICK_BUILD_TESTS=ON
cmake --build build-tests --config Release
ctest --test-dir build-tests --output-on-failure -C Release
```

The test suite asserts: no obstacle -> instant; unsafe first tick + safe next tick -> 1 tick;
no safe option -> forced max delay; zero delay -> instant.

## Technical notes

- `src/Predictor.hpp`: deterministic independent geometry prediction and earliest-safe
  tick selection. Unit tested on Linux.
- `src/Main.cpp`: Geode adapter with guarded input interception, preserved short-tap
  releases, live cube speed/gravity calibration and static object scanning.
- `tests/predictor_tests.cpp`: SDK-free assertions.
- `.github/workflows/build.yml`: Geode Windows build configuration. This action was
  **not executed** here; it is provided for you to run on Windows via GitHub.
- `CMakeLists.txt`: contains a narrow workaround for the 3 generated-binding source
  compilation failures observed with Geode SDK 5.11.2 + GD 2.2081. If upstream fixes
  those types, the workaround can be removed.
