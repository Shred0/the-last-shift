# The Last Shift

A short first-person exploration game made for GitHub Game Off 2026 (theme: SHIFT). The player
explores a derelict twin megastructure, stops time and finds their way through lidar scans, while
every stop costs time back home. Built with Unreal Engine 5.8, C++ first.

## Build

Requirements: Unreal Engine 5.8, Visual Studio 2022 with the "Game development with C++" workload,
Git LFS (`git lfs install` once, before cloning).

```
git clone <repo-url>
cd TheLastShift
"<UE_ROOT>\Engine\Build\BatchFiles\Build.bat" TheLastShiftEditor Win64 Development -Project="%CD%\TheLastShift.uproject" -WaitMutex
```

`<UE_ROOT>` is your local Unreal Engine 5.8 install folder. Then open `TheLastShift.uproject`.
The code name `TheLastShift` is used for the project, module, targets and content root
(`Content/TheLastShift/`).

## Layout

- `Source/TheLastShift/` -> game module
- `Plugins/LidarScan/` -> scanning effect plugin
- `Plugins/StdCore/` -> menus, settings, input and level transitions plugin
- `Narrative/` -> CSV sources of the narrative data tables

Licensing differs per folder; see `AGENTS.md` for the zones.
