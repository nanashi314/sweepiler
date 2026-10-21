# Third-party notices

Dear ImGui v1.91.9b is used for the UI and Direct3D 11 / Win32 backends.

Upstream: https://github.com/ocornut/imgui/tree/v1.91.9b

License: MIT. The complete upstream license is included in `licenses/imgui-LICENSE.txt`.

Z3 4.13.4 is used as a native constraint solver through its C++/C API.

Upstream: https://github.com/Z3Prover/z3/releases/tag/z3-4.13.4

License: MIT, Copyright (c) Microsoft Corporation. The full upstream license is included in `licenses/z3-LICENSE.txt`. The official release's `libz3.dll` is redistributed beside the executable. The CLI, language bindings and runtime DLLs from the Z3 archive are not bundled.

Direct3D, DXGI, D3DCompiler and Win32 APIs are provided by Windows. Visual C++ runtime redistribution is governed by Microsoft's terms; no runtime DLLs are bundled here.

## Web build (web/)

The web build is compiled with Emscripten and does not include Z3. Its page bundles:

- Dear ImGui v1.91.9b with its SDL2 and OpenGL 3 (WebGL 2) backends (MIT, as above).
- SDL 2 (zlib License) and zlib (zlib License), from Emscripten's ports.
- Emscripten's runtime and system libraries, including LLVM libc++ and musl (MIT / Apache 2.0 with LLVM exception, as listed in Emscripten's LICENSE).
- BIZ UDGothic Regular (SIL Open Font License 1.1, Copyright 2022 The BIZ UDGothic Project Authors), downloaded at build time from https://github.com/google/fonts/tree/main/ofl/bizudgothic. Its `OFL.txt` is published next to the page.
