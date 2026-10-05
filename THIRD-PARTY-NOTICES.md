# Third-party notices

The packaged native helper links against the ZHM Mod SDK 4.1.1 and statically links Dear ImGui, fmt, and ImPlot from its DevPkg. Their corresponding licence texts are included in `licenses/`.

- **ZHM Mod SDK 4.1.1**: Orfeas Zafeiris and contributors, GPL-3.0-or-later. [Pinned source](https://github.com/OrfeasZ/ZHMModSDK/tree/v4.1.1) and [runtime/DevPkg release](https://github.com/OrfeasZ/ZHMModSDK/releases/tag/v4.1.1).
- **Dear ImGui 1.92.8**: Omar Cornut and contributors, MIT. [Upstream source](https://github.com/ocornut/imgui/tree/v1.92.8). The SDK's [ImGui build port and patches](https://github.com/OrfeasZ/ZHMModSDK/tree/v4.1.1/cmake/vcpkg-ports/imgui) describe its modified library build.
- **fmt 12.2.0**: Victor Zverovich and contributors, MIT. [Upstream source](https://github.com/fmtlib/fmt/tree/12.2.0).
- **ImPlot 1.0**: Evan Perez and contributors, MIT. [Upstream source](https://github.com/epezent/implot/tree/v1.0).

Dependency versions were checked against the headers in the official 4.1.1 DevPkg used for this helper. The SDK tag includes its build scripts, vcpkg dependency manifest and submodule declarations. When rebuilding the SDK from source, initialise its submodules and use that tag's dependency recipe. These dependencies and their source are available separately through the links above; the mod ZIP does not bundle the SDK runtime or DevPkg.

The original Freelancer Keep Weapons code is licensed under GNU GPL version 3 or (at your option) any later version; see `LICENSE`. The licence files below apply to their respective third-party components.
