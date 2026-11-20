# Architecture Overview

See the full architectural specification in [docs/architecture.md](docs/architecture.md).

## Layering & Platform Isolation

Project XWine separates portable C++20 domain logic from host-specific platform implementations:

```text
launcher/          -> Native SDL3 + FreeType gaming UI & testable UI state machine
  ├── input/       -> SDL3 gamepad hotplug, deadzone/curve math, remapping, XInput snapshot
  ├── steam/       -> Valve VDF/ACF parser, Steam library scanner, honest DRM/anti-cheat checks
  ├── graphics/    -> Decoupled IGraphicsBackend & D3D9/10/11/12/Vulkan Xbox route evaluators
  └── runtime/     -> PE32/PE32+ static inspector, sandboxed library, W^X memory model, Win32 API catalog
        └── platform/
              ├── common/  -> Logger, RFC 8259 JSON, ConfigManager, BenchmarkTracker, IPlatform
              ├── macos/   -> Apple Silicon Mach RSS/CPU telemetry & ~/Library/Application Support/XWine
              └── xbox/    -> Xbox Series S UWP Dev Mode profiles, constraints & Phase 6 WinRT hooks
```

## Non-Negotiable Engineering Invariants

1. **No False Compatibility Claims:** Static PE inspection and Steam metadata import never mark an unverified binary as `Verified_Xbox` or `can_execute_locally = true`.
2. **Decoupled Graphics Layer:** `graphics/` has zero dependency on Wine or SDL3 UI internals.
3. **Path Sandboxing:** `runtime/filesystem` enforces root confinement (`is_path_within_root`) to align with Xbox AppContainer storage restrictions.
4. **Honest Telemetry:** `platform/macos` measures real RSS (`mach_task_basic_info`), CPU (`getrusage`), and frame times (`steady_clock`), and reports unavailable metrics (such as per-process macOS GPU utilization) as omitted rather than estimated.
