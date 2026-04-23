# CrossPlatformScreenshot

A high-performance cross-platform screenshot tool built with **DXGI Desktop Duplication** and **OpenGL ES (via ANGLE)** rendering.

> **Status**: Phase 1 — Windows capture pipeline + ANGLE rendering foundation is complete.

---

## ✨ Features

- **GPU-accelerated capture** — Uses DXGI Desktop Duplication API for zero-copy screen capture
- **OpenGL ES rendering** — Hardware-accelerated overlay and annotation rendering via ANGLE
- **Region selection** — Click and drag to select any screen region with real-time dim mask
- **Annotation tools** — Draw rectangles, arrows, and lines on captured regions
- **Export options** — Save to file or copy to clipboard
- **State machine driven** — Clean, testable workflow logic separated from platform code
- **Cross-platform ready** — Abstracted platform layer (Win32 implementation complete)

---

## 🏗️ Architecture

```
┌─────────────┐     ┌─────────────────┐     ┌──────────────┐
│   Win32 UI  │────▶│  Application    │────▶│ StateMachine │
│  (Overlay)  │◄────│   (Orchestrator)│◄────│  (Core logic)│
└─────────────┘     └─────────────────┘     └──────────────┘
         │                   │
         ▼                   ▼
┌─────────────┐     ┌─────────────────┐
│ DXGI Capture│     │ ANGLE Renderer  │
│  (GPU →Tex) │     │ (GLES overlay)  │
└─────────────┘     └─────────────────┘
```

| Module | Purpose |
|--------|---------|
| `src/app` | Main application loop, toolbar, event handling |
| `src/core` | State machine, annotation model, command history, types |
| `src/platform` | Platform abstraction (capture, overlay, input, clipboard, tray) |
| `src/renderer` | OpenGL ES renderer (sprite batch, shape renderer, framebuffer) |

---

## 🛠️ Build Requirements

- **CMake** ≥ 3.25
- **C++17** compiler (MSVC 2022+ on Windows)
- **ANGLE** — Pre-built binaries or via vcpkg

### Dependencies

| Dependency | Purpose | Source |
|------------|---------|--------|
| ANGLE | OpenGL ES + EGL runtime | [Pre-built](https://github.com/google/angle) or vcpkg |
| STB | Image encoding (PNG/JPG) | Bundled in `third_party/stb` |

---

## 🚀 Build Instructions

### Option 1: Pre-built ANGLE (Recommended)

Download ANGLE binaries and place them in:

```
third_party/angle/
  ├── include/   # EGL, GLES2 headers
  ├── lib/       # .lib files
  └── bin/       # .dll files
```

Then build:

```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

### Option 2: vcpkg

```bash
vcpkg install angle
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=[vcpkg-root]/scripts/buildsystems/vcpkg.cmake
cmake --build . --config Release
```

### Run Tests

```bash
cd build
ctest --output-on-failure
```

---

## 📁 Project Structure

```
├── src/
│   ├── app/           # Application entry point and UI orchestration
│   ├── core/          # Business logic and state management
│   ├── platform/      # Platform abstraction layer
│   │   └── win32/     # Windows-specific implementations
│   └── renderer/      # OpenGL ES rendering subsystem
├── tests/             # Unit tests (state machine, core logic)
├── third_party/       # External dependencies
│   ├── angle/         # ANGLE binaries (not in git)
│   └── stb/           # STB image library
├── docs/              # Architecture decisions and feature discussions
└── CMakeLists.txt     # Root build configuration
```

---

## 🎯 Development Roadmap

- [x] Phase 1: Project skeleton + DXGI capture + ANGLE rendering pipeline
- [ ] Phase 2: Cross-platform abstraction (macOS/Linux stubs)
- [ ] Phase 3: Advanced annotations (text, blur, mosaic)
- [ ] Phase 4: Settings persistence and multi-monitor support

---

## 📝 License

MIT License — see [LICENSE](LICENSE) for details.

---

> Built with modern C++17, CMake, and a focus on GPU-accelerated performance.
