# Build Instructions

Full macOS instructions: [docs/build-macos.md](docs/build-macos.md).
Xbox Developer Mode packaging prerequisites: [docs/deployment.md](docs/deployment.md).

## Apple Silicon Mac (M1/M2/M3/M4)

```bash
brew install cmake ninja sdl3 freetype libpng
./scripts/build_macos.sh
```

Built binaries:
- `build/launcher/xwine-launcher` — Native SDL3 + FreeType gaming launcher (1920x1080 logical + Retina/4K HiDPI)
- `build/tools/diagnostics/xwine-diag` — CLI PE inspector, Steam scanner, sysinfo, and micro-benchmark tool
- `build/xwine_unit_tests`, `build/xwine_integration_tests`, `build/xwine_compatibility_tests`, `build/xwine_launcher_tests` — Automated test executables

## Windows / Xbox UWP Packaging Note

There is no official macOS SDK capable of building or signing Xbox `.appx` / `.msix` / `.xvc` packages. Xbox UWP compilation requires a Windows 10/11 x64 build host with Visual Studio 2022 (UWP C++ workload) and Windows SDK 10.0.22621.0+ (`makeappx.exe`, `signtool.exe`).
