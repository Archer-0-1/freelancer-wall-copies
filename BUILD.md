# Build and source

The matching source for this GPL-3.0-or-later binary is in the [v1.4.9 source tree](https://github.com/Archer-0-1/freelancer-wall-copies/tree/v1.4.9). Build from a source checkout; the runtime mod ZIP contains the source link and instructions but not the C++ source.

Build on Windows with Visual Studio 2022 x64 and the official ZHM Mod SDK 4.1.1 DevPkg. The DevPkg is an external SDK package and is not included here. The CMake project defaults to a local research path; pass the DevPkg path explicitly for a portable build:

```powershell
cmake -S native/owned-wall-copy -B build -G "Visual Studio 17 2022" -A x64 -DZHMMODSDK_DEVPKG_DIR="<path-to-ZHMModSDK-4.1.1-DevPkg>"
cmake --build build --config Release --target FreelancerOwnedWallCopy FreelancerCopyPolicyTests
.\build\Release\FreelancerCopyPolicyTests.exe
```

The install script verifies the bundled helper hash and pinned SDK 4.1.1 core hash, confirms HITMAN is closed, backs up a different existing helper, then installs the DLL into `Retail/mods`. SMF runs it when applying the wall-copy option.

The published DLL is reviewed separately from local builds; compiler and SDK details may produce a different hash. If a rebuilt DLL is published, update the installer's pinned helper hash and package together. Do not bypass its hash checks.

The original mod code is GPL-3.0-or-later; see `LICENSE` and the matching source tree. Third-party terms are listed in `THIRD-PARTY-NOTICES.md` and `licenses/`.
