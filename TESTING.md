# Testing & Quality Control

## Running All Automated Tests

```bash
./scripts/run_tests.sh
# or directly with verbose output:
ctest --test-dir build --output-on-failure -V
```

## Test Suites

1. **`XWineUnitTests`** (`tests/unit/test_core_unit.cpp`)
   - RFC 8259 JSON round-trip escaping and malformed rejection
   - Thread-safe structured `Logger` ring buffer, level filtering, and file sink
   - `ConfigManager` schema validation, bounds clamping, atomic save/load
   - `PeInspector` parsing synthetic **PE32+ (x86-64)** and **PE32 (x86 32-bit)** binary images with real Import Directory Tables, section permissions, CLR/.NET headers, RWX flags, DRM/anti-cheat markers, and malformed/truncated inputs
   - `VirtualMemoryBudgetModel` strict UWP W^X (`RWX` rejection, `RW -> RX` transition) and 5,120 MiB memory cap enforcement
   - Valve `VdfParser` parsing valid/malformed KeyValues `.acf` manifests
   - `ControllerMapper` radial deadzones, sensitivity curves, trigger thresholds, button remapping, and Windows XInput bitmask snapshots
   - `LibraryManager` sandbox path traversal protection, honesty guard (blocking unverified `Verified_Xbox`), and JSON persistence
   - `BenchmarkTracker` frame-time percentile statistics

2. **`XWineIntegrationTests`** (`tests/integration/test_pipeline_integration.cpp`)
   - End-to-end flow: Steam `appmanifest_*.acf` discovery -> synthetic PE32+ executable discovery -> PE static inspection -> `LibraryManager` persistence -> `RuntimeManager` honest launch evaluation -> isolated per-game save directory creation.

3. **`XWineCompatibilityTests`** (`tests/compatibility/test_xbox_constraints.cpp`)
   - Enforces honesty invariants: Xbox Series S profile reports `hardware_verified = false`, 5 GB Game Mode / 1 GB App Mode ceilings, Vulkan flagged `TechnicallyUnsupported`, and upstream Linux Wine flagged unsupported on Xbox UWP.

4. **`XWineLauncherTests`** (`launcher/tests/test_launcher_state.cpp`)
   - Headless verification of `LauncherState`: controller tab cycling across all 7 tabs, search filtering, game detail dry-run evaluation, sample Steam ACF parsing, settings mutation + `config.json` persistence, and diagnostic JSON bundle export.

## Visual UI Verification

Capture lossless PNG renders of all 8 launcher views on macOS:

```bash
./build/launcher/xwine-launcher --width 1280 --height 720 --capture-screenshots /tmp/xwine_ui_qa
```
