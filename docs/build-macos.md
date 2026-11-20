# Building Project XWine on Apple Silicon (M1/M2/M3/M4 macOS)

This document details how to configure, compile, test, and run Project XWine on an Apple Silicon MacBook running macOS.

---

## 1. Prerequisites

Install Apple Command Line Tools and Homebrew packages:

```bash
xcode-select --install
brew install cmake ninja sdl3 freetype libpng
```

Verified reference environment (M1 MacBook Air, `arm64-apple-darwin`):
- **Compiler:** Apple Clang 21.0.0 (`-std=c++20`)
- **Build System:** CMake 4.5+ & Ninja 1.13+
- **Libraries:** SDL3 3.4+, FreeType2, libpng

---

## 2. Configure & Build

```bash
export PATH="/opt/homebrew/bin:$PATH"
cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_PREFIX_PATH="$(brew --prefix)"
cmake --build build -j
```

Or use the provided helper script:

```bash
./scripts/build_macos.sh
```

---

## 3. Run Automated Tests

```bash
ctest --test-dir build --output-on-failure
```

Or via helper script:

```bash
./scripts/run_tests.sh
```

---

## 4. Launch the Native macOS Application

```bash
./build/launcher/xwine-launcher
```

### Supported CLI Flags for `xwine-launcher`
- `--width <px>` / `--height <px>`: Initial window dimensions (default `1280x720`, internal logical canvas `1920x1080`, automatic Retina/4K HiDPI scaling).
- `--data-dir <path>`: Override storage directory (defaults to `~/Library/Application Support/XWine`).
- `--capture-screenshots <dir>`: Renders all key launcher screens offscreen/live to BMP/PNG images in `<dir>` and exits cleanly (used for visual QA).
- `--demo`: Seeds safe, clearly labeled **unexecuted / inspection-only** catalog fixtures if the library is empty (default on first run so the UI can be explored immediately; never claims games are executable).

### Controls (macOS)
- **Xbox / DualSense / Switch Controller (via SDL3):**
  - `LB` / `RB`: Switch top navigation tabs
  - `D-Pad` / `Left Stick`: Navigate cards, lists, and settings
  - `A`: Select / Open Game Detail / Toggle setting
  - `B`: Back / Close modal
  - `X`: Cycle Library filter / Inspect PE
  - `Y`: Cycle Sort order / Toggle Favorite
  - `Menu (Start)`: Focus Search / Settings shortcut
- **Keyboard & Mouse:**
  - `Q` / `E` or `Tab` / `Shift+Tab`: Switch navigation tabs
  - `Arrow Keys` / `WASD`: Navigate
  - `Enter` / `Space`: Select / Activate
  - `Esc` / `Backspace`: Back / Close
  - `F` / `/`: Focus Search box
  - Full mouse click, hover, and scroll-wheel support

---

## 5. Run the CLI Diagnostic Tool

```bash
./build/tools/diagnostics/xwine-diag sysinfo
./build/tools/diagnostics/xwine-diag inspect /path/to/program.exe
./build/tools/diagnostics/xwine-diag steam "/Users/$USER/Library/Application Support/Steam/steamapps"
./build/tools/diagnostics/xwine-diag bench
```

---

## 6. Note on Xbox Packaging from macOS

**macOS CANNOT produce signed Xbox `.appx` / `.msix` / `.xvc` packages.** No official Microsoft Xbox UWP or GDK SDK exists for macOS. See [deployment.md](./deployment.md) for the exact Windows 10/11 SDK requirements for Phase 6 Xbox hardware deployment.
