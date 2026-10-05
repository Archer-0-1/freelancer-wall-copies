# Freelancer Keep Weapons

Take a copy of gear you own in the Freelancer safehouse. Bring the copy on a mission and leave the original at home.

[Download 1.4.9](https://github.com/Archer-0-1/freelancer-wall-copies/releases/latest/download/LeadingTiger9.FreelancerKeepWeapons.zip)

## Install

Requires Windows HITMAN World of Assassination, [Simple Mod Framework](https://www.nexusmods.com/hitman3/mods/200) and the [ZHM Mod SDK 4.1.1 runtime](https://github.com/OrfeasZ/ZHMModSDK/releases/tag/v4.1.1). Tested with the Steam game, version 3.280.0.0, and SMF 2.33.42.

1. Close HITMAN. Install SMF and extract the SDK's **ZHMModSDK-Release.zip** into the game's `Retail` folder, following its [installation instructions](https://github.com/OrfeasZ/ZHMModSDK/blob/v4.1.1/README.md). Keep its files together. GlacierKit and the SDK's Assets/Editor plugins are unnecessary.
2. Import **LeadingTiger9.FreelancerKeepWeapons.zip** into SMF. Enable **Freelancer Keep Weapons** and **Spawn copy of owned wall item (experimental)**, then **Apply**. The installer checks the tested SDK core, backs up an older helper and copies the DLL into `Retail/mods`.
3. Launch HITMAN with **FreelancerOwnedWallCopy** selected in the SDK chooser. In the safehouse, select an owned wall item and take a copy.

SMF checks updates at `raw.githubusercontent.com` and downloads releases from `github.com`, both on its trusted-host list. The installer script has SMF's separate external-code notice. Keep only one active helper DLL in `Retail/mods`; check other plugins before replacing an SDK.

## Controls

By default, click/Enter copies the selected item and **P** takes the original off the wall.

| SMF setting | When enabled |
| --- | --- |
| Spawn copy of owned wall item (experimental) | Enables wall copying. |
| Default Keybinds | Click/Enter takes the original; P copies. |
| Hide notifications | Hides copy-status messages. |
| Hide P key hint | Hides the shortcut hint without disabling P. |

The last three settings default off. Close HITMAN before changing settings and applying them.

Ordinary gear cannot be copied again while you are carrying it. Freelancer toolbox items can be copied repeatedly. There is a 150 ms debounce to prevent accidental double requests. Taking an original and returning it to the wall allows copying again.

## Limits

Copies obey normal mission loss and consumption rules. The original stays home only when you copy it; taking the original still removes it. This does not protect against campaign-failure toolbox wipes or recover previously lost items. It contains no campaign-rule changes. Rogueless is optional, with combined compatibility still unverified.

The wall prompt uses the existing game UI. Status text is drawn by the SDK, without audio. Its latest appearance and timing still need in-game verification. Close the SDK panel with `~`; **F11** hides all SDK drawing, including status text. Text and the P shortcut are currently English and keyboard-based.

To uninstall, disable/remove the mod in SMF and Apply, then remove `Retail/mods/FreelancerOwnedWallCopy.dll` with HITMAN closed. Keep the SDK if other mods use it.

## Source

The entity patch marks the wall action. The helper validates the owned wall item, intercepts its pickup action and asks the game's inventory system to create a separate copy. The original's pickup is skipped. All item creation runs on the game update thread.

Version 1.4.9 changes packaging only; the helper and game patches match 1.4.8. Source and build instructions are in [BUILD.md](BUILD.md). Original code is licensed under **GPL-3.0-or-later**; dependency notices are in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

Concept, design and gameplay testing: **LeadingTiger9**. Built with ZHMModSDK and Simple Mod Framework.

*Code generated with AI tools; design and gameplay testing by LeadingTiger9.*
