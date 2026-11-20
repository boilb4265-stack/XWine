# Graphics Compatibility & Translation Architecture

> **Upstream References Studied:**
> - DXVK: `https://github.com/doitsujin/dxvk`
> - VKD3D-Proton: `https://github.com/HansKristian-Work/vkd3d-proton`
> - Microsoft D3D9On12 / D3D11On12 & Mesa Dozen (`dzn`)

---

## 1. Xbox Series S Developer Mode Graphics Capabilities

| Graphics API | Xbox UWP "App" Mode | Xbox UWP "Game" Mode | Native Hardware Driver? |
| :--- | :--- | :--- | :--- |
| **Direct3D 12** | Restricted / Limited | **Supported (Full UWP D3D12)** | **Yes** — native RDNA 2 driver |
| **Direct3D 11** | Feature Level `10_0` only (~45% GPU) | **Supported (FL `11_0`/`11_1`/`12_0`)** | Via Microsoft **D3D11On12** built into Xbox OS |
| **Direct3D 10** | Restricted | Via D3D11 / D3D10-to-11 mapping | No standalone native driver |
| **Direct3D 9** | **Not Provided** | **Not Provided** | No native driver (requires D3D9On12 or custom translation) |
| **Vulkan** | **Not Supported** | **Not Supported** | **No Vulkan ICD exists on Xbox OS** |
| **OpenGL (Desktop)** | **Not Supported** | **Not Supported** | No native driver (requires Mesa Gallium D3D12 or ANGLE) |

---

## 2. Why DXVK and VKD3D-Proton Are Not Primary Backends on Xbox

Both **DXVK** (D3D9/10/11 -> Vulkan) and **VKD3D-Proton** (D3D12 -> Vulkan) require a compliant **Vulkan 1.3 ICD** with advanced extensions (`VK_EXT_graphics_pipeline_library`, `VK_EXT_extended_dynamic_state`, `VK_KHR_dynamic_rendering`, descriptor buffers, timeline semaphores).

On Xbox Developer Mode:
1. **No Vulkan ICD exists.** Calling `vulkan-1.dll` fails unless a user-mode Vulkan-on-D3D12 translation layer (such as Mesa's **Dozen / `dzn`**) is compiled and bundled inside the UWP package.
2. **Translation Sandwich Penalty:**
   ```text
   [D3D11/12 Game] -> [DXVK / VKD3D-Proton] -> [SPIR-V / Vulkan] -> [Mesa Dozen] -> [DXIL / D3D12] -> [RDNA 2 GPU]
   ```
   - Double shader translation (`DXBC/DXIL -> SPIR-V -> DXIL`) spikes CPU usage, compilation stutter, and shader cache footprint.
   - Double resource/descriptor tracking wastes precious memory out of the **5,120 MB UWP Game Mode unified RAM+VRAM cap**.
   - Missing Vulkan extensions in Dozen break modern DXVK/VKD3D-Proton code paths.

---

## 3. Recommended XWine Graphics Architecture (Decoupled Abstraction)

XWine isolates graphics research behind `graphics/abstraction/include/xwine/graphics/graphics_backend.hpp` so graphics backends are never coupled to Wine or the launcher UI.

### Target API Routing Table

| Guest Graphics API | Recommended Xbox UWP Strategy | Shader Pipeline | Memory & Sync Notes | Status |
| :--- | :--- | :--- | :--- | :--- |
| **Direct3D 12** | **Direct Native Pass-Through + DXGI `CoreWindow` Shim** | Native DXIL / DXBC (zero translation) | Pass through `ID3D12Device`, `ID3D12CommandQueue`, `ID3D12Fence`; wrap `CreateSwapChainForHwnd` -> `CreateSwapChainForCoreWindow`; enforce 5 GB UMA budget accounting. | **Planned** |
| **Direct3D 11** | **Native Xbox UWP D3D11 + DXGI `CoreWindow` Shim** | Native DXBC | Delegates directly to Xbox OS D3D11 (D3D11On12); remap desktop fullscreen/monitor enumeration to single `CoreWindow` output. | **Planned** |
| **Direct3D 10** | **D3D10-to-D3D11 Wrapper** | Native DXBC (SM 4.0) | Thin COM interface adapter over UWP D3D11. | **Planned** |
| **Direct3D 9** | **Microsoft D3D9On12** (open-source mapping layer) | DX9 bytecode -> DXIL via D3D9On12 | Avoids Vulkan entirely; requires UWP-compatible build without desktop `HWND` assumptions. | **Planned** |
| **Vulkan** | **Experimental Mesa Dozen (`dzn`) on D3D12** | SPIR-V -> NIR -> DXIL | High complexity & incomplete extension coverage; secondary priority after D3D11/D3D12. | **Planned (Low Priority)** |
| **OpenGL** | **ANGLE (GLES->D3D11)** or **Mesa Gallium D3D12** | GLSL -> HLSL/DXIL | Proven in UWP emulator ports (e.g., RetroArch/Mesa UWP); suitable for older indie OpenGL games. | **Planned** |

---

## 4. macOS Development Workstation Graphics Strategy

On Apple Silicon M1 (`arm64-apple-darwin`):
- **XWine Launcher UI** renders natively using **SDL3 3.4.20 hardware-accelerated Metal backend** (`SDL_Renderer` backed by Metal on macOS) + **FreeType2** crisp HiDPI glyph rendering.
- **Graphics Abstraction Tests** validate capability routing, swapchain/budget constraints, and shader/API classification independently of the launcher without requiring Direct3D hardware on macOS.
