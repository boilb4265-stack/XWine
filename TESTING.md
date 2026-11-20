# Testing & Quality Control

## Running All Automated Tests

```bash
./scripts/run_tests.sh
# or directly with verbose output:
ctest --test-dir build --output-on-failure -V
```

## Automated Test Suites (6 Suites / 20 Test Cases)

1. **`XWineUnitTests`** (`tests/unit/test_core_unit.cpp` — 10 tests): JSON, Logger, ConfigManager, PE32/PE32+ static inspector, VirtualMemoryBudgetModel W^X & 5 GB cap, Valve VDF parser, Controller deadzones/curves/XInput bitmasks, LibraryManager sandbox security, BenchmarkTracker.
2. **`XWineRuntimeShimsTests`** (`tests/unit/test_runtime_shims.cpp` — 4 tests): Persistent `VirtualRegistry` (all value types + JSON persistence), `PathVirtualizer` jail & traversal blocking, `Win32ShimEnvironment` (`kernel32` file I/O + heap + QPC + `ExitProcess`, `user32` virtual HWND + queue, `advapi32` registry, `xinput1_4` live snapshot), and `PeImageMapper` with real `.reloc` DIR64 fixups + IAT thunk patching + missing symbol telemetry + W^X RWX rejection.
3. **`XWineGraphicsTests`** (`tests/unit/test_graphics_bridge.cpp` — 1 test): `CoreWindowSwapchainBridge` Game vs App mode rules, 4K resize UMA tracking, Vulkan rejection, 5 GB OOM protection.
4. **`XWineIntegrationTests`** (`tests/integration/test_pipeline_integration.cpp` — 1 test): End-to-end Steam ACF scan → synthetic PE32+ inspection → library save → honest launch evaluation → isolated save directory creation.
5. **`XWineCompatibilityTests`** (`tests/compatibility/test_xbox_constraints.cpp` — 3 tests): Enforces honesty guards (`hardware_verified == false` on macOS, Vulkan unsupported on Xbox, upstream Linux Wine unsupported on Xbox UWP).
6. **`XWineLauncherTests`** (`launcher/tests/test_launcher_state.cpp` — 1 test): Headless UI navigation across all 7 tabs, search, game detail dry-run, sample Steam ACF parsing, settings persistence, and diagnostic JSON export.

## Standalone Verification Binaries

```bash
./build/tools/graphics-bench/xwine-gfx-test
./build/tools/diagnostics/xwine-diag sysinfo
./build/tools/diagnostics/xwine-diag shims
./build/tools/diagnostics/xwine-diag bench
```
