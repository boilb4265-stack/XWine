# Contributing to Project XWine

## Engineering Standards

1. **Honesty Over Hype:** Never claim a feature, API, or game works on Xbox Series S without empirical verification on physical hardware. Maintain strict separation between `Implemented`, `Experimentally Verified`, `Planned`, and `Technically Unsupported`.
2. **Strict Compliance:** Never submit code, instructions, or workarounds that bypass Xbox platform security, modify firmware, circumvent Steam/third-party DRM, or interfere with anti-cheat systems.
3. **Never Commit Third-Party Game Files:** Keep third-party executables, extracted game assets, and personal Steam data outside the repository. Tests use minimal synthetic fixtures constructed in code.
4. **Build & Test Discipline:** Every PR or commit must compile cleanly under C++20 (`-Wall -Wextra -Wpedantic`) with zero warnings and 100% passing `ctest --output-on-failure`.
5. **Platform Isolation:** Keep macOS-specific code under `platform/macos/` and Xbox/UWP code under `platform/xbox/`. Shared domain logic must remain strictly platform-independent.
