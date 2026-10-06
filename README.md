# Freelancer Keep Weapons 1.0.1

Copy owned wall gear from the Freelancer safehouse wall. Originals stay in place.

[Download 1.0.1](https://github.com/Archer-0-1/freelancer-wall-copies/releases/latest/download/LeadingTiger9.FreelancerKeepWeapons.zip)

## Requirements

Windows HITMAN World of Assassination, Simple Mod Framework 2.33.42, and the ZHM Mod SDK 4.1.1 core files in the game's `Retail` folder. Follow the [SDK install instructions](https://github.com/OrfeasZ/ZHMModSDK/blob/v4.1.1/README.md). The wall-copy helper is included.

## Install

1. Close HITMAN and install the pinned SDK core files in `Retail`.
2. Import this ZIP into SMF. Enable **Freelancer Keep Weapons** and **Spawn copy of owned wall item (experimental)**, then apply.
3. Launch HITMAN with **FreelancerOwnedWallCopy** enabled in the SDK chooser.

## Controls and behaviour

By default, the wall's Take/Enter action copies the item; **P** takes the original. **Default Keybinds** swaps those actions. **Hide notifications** hides copy status messages. **Hide P key hint** hides the prompt hint while keeping P active. Already-carried ordinary gear cannot be copied. Freelancer tools can be copied repeatedly.

On a safehouse restart, copied inventory clears and wall guns return. Copies follow normal mission loss and consumption rules. Other mod combinations are unverified.

## Source and credits

Built for Windows with Visual Studio 2022 and CMake; the separate [ZHM Mod SDK 4.1.1 DevPkg](https://github.com/OrfeasZ/ZHMModSDK/releases/tag/v4.1.1) is required to build. Third-party terms are in [native/THIRD-PARTY-NOTICES.txt](native/THIRD-PARTY-NOTICES.txt).

AI-assisted code; design and gameplay testing by LeadingTiger9. Credit LeadingTiger9 when modifying, reusing or redistributing this mod. Copyright (C) 2026 LeadingTiger9.
