# Freelancer Keep Weapons 1.0.0

Copy gear you own from the Freelancer safehouse wall. The original stays home while you take the copy on a mission.

[Download 1.0.0](https://github.com/Archer-0-1/freelancer-wall-copies/releases/latest/download/LeadingTiger9.FreelancerKeepWeapons.zip)

## Install

Requires Windows HITMAN World of Assassination, [Simple Mod Framework](https://www.nexusmods.com/hitman3/mods/200), and the [ZHM Mod SDK 4.1.1 runtime](https://github.com/OrfeasZ/ZHMModSDK/releases/tag/v4.1.1). Tested with Steam game 3.280.0.0 and SMF 2.33.42.

1. Close HITMAN. Extract the SDK release archive into the game's `Retail` folder, following the [SDK instructions](https://github.com/OrfeasZ/ZHMModSDK/blob/v4.1.1/README.md). Keep its files together; Assets and Editor are not needed.
2. Import this ZIP into SMF, enable **Freelancer Keep Weapons** and **Spawn copy of owned wall item (experimental)**, then apply. The installer checks the SDK core and backs up a different helper before installing the included DLL.
3. Launch HITMAN with **FreelancerOwnedWallCopy** enabled in the SDK chooser. Select an owned wall item in the safehouse to take its copy.

SMF checks updates through its trusted `raw.githubusercontent.com` host and downloads releases from `github.com`. Keep only one active helper DLL in `Retail/mods`.

## Controls and options

By default, click or Enter copies the item; **P** takes the original off the wall. **Default Keybinds** swaps those actions. **Hide notifications** hides status messages, and **Hide P key hint** hides the prompt hint while leaving P active. These three settings default off.

Already-carried ordinary gear cannot be copied again. Freelancer toolbox items remain copyable. A 150 ms debounce prevents accidental repeat requests. Taking an original and returning it to the wall makes it available again.

## Limits and removal

Copies follow normal mission loss and consumption rules. Copying leaves the original at home; taking the original removes it. This does not restore lost items or protect against campaign-failure toolbox wipes. Rogueless is optional; combined compatibility is unverified. The SDK feedback popup's latest appearance and timing need in-game verification. F11 hides SDK drawing, including the popup; text and shortcuts are English and keyboard-based.

To uninstall, disable or remove the mod in SMF and apply with HITMAN closed, then remove `Retail/mods/FreelancerOwnedWallCopy.dll` if desired. Keep the SDK if other mods use it.

## Source and build

The entity patch marks the wall action. The C++ helper validates the owned item, intercepts pickup, and asks the game's inventory system to create a separate copy on the game update thread. The success message means the game accepted the request; it does not confirm an item appeared.

Source: [v1.0.0 source tree](https://github.com/Archer-0-1/freelancer-wall-copies/tree/v1.0.0). Build on Windows with Visual Studio 2022 x64, CMake, and the separate official [ZHM Mod SDK 4.1.1 DevPkg](https://github.com/OrfeasZ/ZHMModSDK/releases/tag/v4.1.1):

```powershell
cmake -S native/owned-wall-copy -B build -G "Visual Studio 17 2022" -A x64 -DZHMMODSDK_DEVPKG_DIR="<path-to-ZHMModSDK-4.1.1-DevPkg>"
cmake --build build --config Release --target FreelancerOwnedWallCopy FreelancerCopyPolicyTests
.\build\Release\FreelancerCopyPolicyTests.exe
```

The SDK package is not included. Published DLLs are reviewed separately; a local rebuild may have a different hash. The installer verifies its pinned helper and SDK hashes.

Third-party dependency terms and notices are in [native/THIRD-PARTY-NOTICES.txt](native/THIRD-PARTY-NOTICES.txt).

*Code generated with AI tools; design and gameplay testing by LeadingTiger9.*

Copyright (C) 2026 LeadingTiger9. Credit LeadingTiger9 when modifying, reusing or redistributing this mod.
