# AGENTS.md — The Last Shift

Unreal Engine 5.8 (fixed — do not change the EngineAssociation in the .uproject), C++ first.

Shared instructions for every AI coding agent working in this repo (Claude Code, Gemini CLI, others). This file is the single source of truth: tool-specific files (`CLAUDE.md`, `.gemini/settings.json`) only import it. Put project rules here, never in those files.

First-person exploration game for GitHub Game Off 2026 (theme: SHIFT). 20–30 minutes, one location (a derelict twin megastructure), four areas = four acts, each area its own map.

Core loop: the player stops time; while stopped the screen is black except for lidar scans (points + wireframe that fade): the first fires automatically, the rest the player triggers on a cooldown; tachyonic residues exist only in scans and are collected by holding contact; an entity closes in on a silent beat, whether the player scans or not; a voluntary Resume costs nothing; being caught by the entity makes the outside world jump forward and drains the home battery; the story is told only through a text channel with the home AI that degrades as the battery drops.

**Rule zero:** the core is pause + scans + entity. If moving blind on a fading wireframe with something approaching is not tense and readable, nothing else matters. Prioritise accordingly.

## Deadlines (2026)

*   Nov 20: MVP playable. Nice-to-haves only after this.
*   Nov 26: feature freeze (bugs, build, itch.io page only).
*   Nov 29: target submission. Hard deadline Dec 1, 13:37 PT.

## Source of truth

Design docs live in ClickUp (Italian, internal reference; "Ultimo Turno" was the working title). Workspace `90122202168` → space "Space":

*   Folder **GDD**: "Ultimo Turno — GDD (GameOff 2026, tema SHIFT)" — doc `2kxv7b1r-292`
*   Folder **Documentazione**: "Game Off 2026 — Workflow di preparazione" (`2kxv7b1r-172`), "Game Off 2026 — Submission" (`2kxv7b1r-192`)

If a ClickUp connection (MCP) is available to you, read the GDD before making design decisions. If this file and the GDD disagree, ask. The GDD's technical notes contain leftovers from before the engine choice (Godot MultiMesh, Unity LineRenderer): ignore them; the "Engine: Unreal Engine 5" section is authoritative. The GDD's "Decisioni post-playtest" section overrides the body of the GDD.

## Language

*   Everything in the repo is English: code, identifiers, file names, comments, commit messages, README, and all player-facing text.
*   Communicate with the developers in English too.

## License zones — read before creating or moving any file

The repo is public, but not everything in it has the same license. Every file belongs to exactly one zone.

| Zone | Paths | License | Notes |
| ---| ---| ---| --- |
| Lidar (future licensed product) | `Plugins/LidarScan/**` | Proprietary, own `Plugins/LidarScan/LICENSE` (terms set by the owner) | Must stay fully separable from the game |
| StdCore (reusable framework) | `Plugins/StdCore/**` | Proprietary, own `Plugins/StdCore/LICENSE` (same terms as LidarScan) | Pre-existing plugin: generic UMG widgets, level transitions, input binding, settings, menus |
| Narrative (view only) | `Narrative/**`, `Content/TheLastShift/Narrative/**` | All rights reserved, `LICENSE-NARRATIVE` | Dialogue, AI messages, logs, fragments, endings |
| Game | `Source/TheLastShift/**`, `Content/TheLastShift/**` (except Narrative, Art, Audio), `Config/`, scripts | MIT, root `LICENSE` | Game logic, Blueprints, maps, UI, tuning data |
| Assets | `Content/TheLastShift/Art/**`, `Content/TheLastShift/Audio/**` | TBD, `LICENSE-ASSETS` | Meshes, materials, sounds, music |

License files (`LICENSE*`, `CREDITS`) are written by the developer at the end of the jam. Until then default copyright applies; the zone a file belongs to is still fixed by this table.

Hard rules:

1. **Dependency direction is one way:** `TheLastShift → LidarScan` and `TheLastShift → StdCore`. `LidarScan` and `StdCore` never depend on each other or on `TheLastShift`; each must compile and work in an empty project. No game concepts (residue, entity, figure, trail, act, battery, home, AI, debt) in either plugin's class names, code, comments, or content. Game behaviour plugs in through the plugins' generic extension points.
2. **Never copy code between zones.** If the game needs lidar-like behaviour the plugin lacks, add a generic feature to the plugin or wrap it in the game module. Don't duplicate plugin code in `Source/TheLastShift`.
3. **Narrative text lives only in the Narrative zone.** No story text hardcoded in C++ or Blueprints. Short UI labels ("Hold E", menu items) are fine in the Game zone. Code that _processes_ text (message queue, typewriter, degradation function) is Game code.
4. **Tuning data and narrative data never share a table.** Separate row structs, separate DataTables, separate folders. Narrative rows may carry only what is needed to select a message (key, act, trigger, order); no numeric tuning columns. Tuning rows never carry player-facing text.
5. **Do not create, edit, or delete `LICENSE*`, `CREDITS`, or license headers unless explicitly asked.** When unsure which zone a new file belongs to, ask before creating it.
6. File headers:
    *   Plugin sources (LidarScan, StdCore): `// Copyright <YEAR> <OWNER>. All rights reserved. See Plugins/<PluginName>/LICENSE.`
    *   Game sources: `// SPDX-License-Identifier: MIT`
7. No Starter Content, no Fab/Marketplace assets. Any external asset must be added to `CREDITS` with author and license; ask the developer to confirm the license before importing.

## Repository layout

```
TheLastShift.uproject
Config/
Source/TheLastShift/           Game module (MIT)
  Public/ Private/
    Time/                      Time state, cumulative stopped time, time jumps, home battery
    Scan/                      Scan orchestration on top of LidarScan (director: cooldown, beat, stop anchor, rewind)
    Residue/                   Residue actors, collection
    Entity/                    Entity actor (point source), procedural figure shape, trail (walls: post-MVP)
    Energy/                    Spending on doors, elevators, terminals, twin AI
    TextChannel/               Message queue, degradation, typewriter
    Narrative/                 Narrative row structs (code is MIT, data is not)
    Data/                      Tuning row structs
    Area/                      Area route planner, generator, validator
    Run/                       Run state across maps (act index, map order), area exits
    Debug/                     Test harness and console commands, compiled out of Shipping
Plugins/LidarScan/             Lidar plugin (proprietary)
  LidarScan.uplugin
  LICENSE
  Source/LidarScan/
  Content/                     Plugin's own Niagara systems and materials
Plugins/StdCore/               Reusable framework plugin (proprietary): UI widgets,
  StdCore.uplugin              transitions, input binding, settings, menus
  LICENSE
  Source/StdCore/
Narrative/                     CSV sources of all narrative DataTables (all rights reserved)
Content/TheLastShift/
  Maps/ Blueprints/ UI/ Input/
  Data/                        Tuning DataTables and data assets
  Narrative/                   Narrative DataTables only, imported from /Narrative/*.csv
  PCG/                         PCG graphs that build area geometry (prefix PCG_)
  Art/ Audio/
```

The approved title is **The Last Shift**: use it in player-facing text, README, and the itch.io page. `TheLastShift` is the code name for the module, folders, build targets, and the `.uproject`.

## LidarScan plugin

Generic, engine-level scanning effect. The per-particle attribute contract is the contract comment in `LidarScanComponent.cpp`: read it before changing `NS_LidarPoints` and keep it up to date. Any LidarScan change must be explicitly requested in a brief.

*   At scan time a `SceneCaptureComponent2D` captures scene depth; a Niagara system reads the render target and spawns one point per sample in world space.
*   Neighbour lines (not built: the plugin has no links yet, so link overrides do not exist either): connect each point to its k nearest neighbours within a max distance (spatial grid, no complex structures). Lines grow over 1–2 s. Do not build them unless a brief asks.
*   **Points are a display:** every point is rendered at a constant size in screen pixels (recomputed each frame from the current camera distance) and at constant intensity regardless of depth. Depth is read from parallax as the player moves inside a scan, never from brightness. Because the eye sums light over area, point intensity is scaled per frame by `(SampleDepth / ViewDepth)^DensityCompensation × (cos θ_view / cos θ_capture)^AngleCompensation` (clamped), where θ is the angle between the surface normal (from the depth gradient at spawn) and the viewing direction at capture and now, so a scan seen from away from its origin neither compacts into a bright sheet nor thins into invisible dots; from the scan's own pose the factor is 1. Per-scan sample jitter prevents moiré. Injected points carry a sample depth along the capture's forward axis and no normal (angle term 1), so the depth rule applies to them.
*   **Occlusion:** scans are occluded by the real geometry through the engine's depth test, so a scan never shows through a wall. Each captured point is drawn offset along its surface normal by a fixed world distance (default 1.5 cm) so that it does not sink into the surface. Never offset a point toward the viewer by a distance that grows with the view distance: that is what leaked points through walls. Points without a normal (injected points) are offset toward the viewer, capped by `ViewOffsetMaxCm`.
*   **Near thinning:** close to the scan origin samples are thinned, not cut: a sample is kept with probability `min(1, (depth / reference depth)²)`, reference depth 2.5 m by default, and nothing nearer than `MinSampleDepth` (default 40 cm) is sampled. The capture angle is not narrowed to reduce density. Neither rule applies to injected points.
*   **Two emitters:** `NS_LidarPoints` has a Points emitter (captured samples) and an Injected emitter that duplicates its size, compensation and offset formulas. A rendering change goes into both, or the two kinds of point stop matching.
*   Each scan has an age; alpha fades to a non-zero floor. Older scans stay as ghosts up to the ghost cap (`ULidarScanComponent::MaxAliveScans`, default 3, clamp 1–16; the game uses 16); when a slot is reused, the scan it held is removed in both emitters.
*   **Channels:** a captured point takes the colour and intensity multiplier of its channel. The channel is the custom depth stencil value of the surface the sample hit, read at the sample's own texel; value 0 is the default channel and looks exactly like a scan without channels. Channels are off by default, and when off the capture and the points are identical to a plugin without them. Channel colours are set on the scan component and can be changed at runtime. Per-channel density and links are not built. The project must enable custom depth with stencil; the plugin warns once if it is missing. The plugin knows channel numbers, not what they mean. With channels on, the capture is a full unlit render (SCS_FinalColorHDR) through the post-process material M_LidarDepthStencil into an RGBA32f target (R depth, G stencil), instead of the depth prepass used when channels are off. Its cost grows with the scene: measure it in the heaviest map. The channel capture keeps a view state and marks every scan as a camera cut, because the engine skips post-process materials without one.
*   **Injected points:** things that are never rendered provide points through the generic `ILidarPointSource` interface, registered on the scan component and queried at scan time with a scan context (capture origin, forward, coverage, scan index, timestamp). Each source returns world positions plus its own `FLidarSourceSettings`: colour, intensity multiplier, a fade mode: follow the scan fade (`ScanFade`), ignore it (`None`), transient (`Transient`: full for `HoldSeconds`, then to zero over `VanishSeconds`, regardless of later scans), or until the next scan (`UntilNextScan`: full while their scan is the newest, then to zero over `VanishSeconds` once a later scan starts, and removed), `bOccludeByCapture` (points outside the capture coverage or behind the captured depth are dropped, tested on the source's world position rather than the drawn one: the lidar does not see through walls), and link overrides where links exist. Injected points belong to the same scan instance as captured points (same ghost cap, same clear, same screen-space size and intensity). Sources register with a priority (higher first, default 0, ties in registration order). A scan accepts at most 4096 injected points; when the cap is reached the lowest priorities are cut, and the number of dropped points of the last scan can be read from the component. The plugin has no idea what a source represents; the game's entity is one implementation.
*   **Events:** `OnScanStarted` / `OnScanCompleted` delegates with scan index and timestamp. The game's scan director re-broadcasts them; nothing else in the game subscribes to the plugin directly.
*   Every tunable is a `UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lidar|...")` with `ClampMin`/`ClampMax`, so balancing happens in the editor without recompiling.
*   The plugin references every asset it loads at runtime in a way the cooker follows. A project never adds a cook setting for the plugin's content.

## StdCore plugin

Pre-existing, generic, and it grows with this project. Use it for everything it already covers — main menu, settings, pause menu, key rebinding, level transitions — instead of reimplementing in `Source/TheLastShift`. When the game needs a UI element or utility that is generic (a radial progress ring, a fade, a key prompt), it is added to StdCore, not to the game, with no game terms in it; the game only wraps or configures it. Do not add game logic to StdCore. Any StdCore change must be explicitly requested in a brief. Read its public headers before building UI or input.

The game's main menu is the game's own: its map (`Content/TheLastShift/Maps/MainMenu`), its title and its look live in the Game zone and are built on StdCore's menu widgets. StdCore's own maps are never the game's startup or menu map, and StdCore assets are never duplicated into the Game zone.

Every gameplay map's game mode uses StdCore's `StdPlayerController`: it restores game input when a level starts. StdCore's pause menu is available in every level.

The covered screen of a level transition can show a progress bar, moved only by progress the transition and the project report, and a marker that crosses the screen over the covered time; a project switches them on and sets their look through StdCore's settings stored as text in `Config/`.

A project registers its own input actions for the rebinding screen, with their labels, and hides the StdCore rows it does not use, through StdCore's settings stored as text in `Config/`. Rebinding to a key held by another action swaps the two; reserved keys are refused.

**Naming:** "pause" in StdCore means the menu pause. The time-stop mechanic is never called "pause" in code or assets: use `TimeState` / `ETimeState::Stopped` / `Stop` / `Resume` (`UTimeStateSubsystem`, `OnTimeStateChanged`, ...). "Pause" stays in design docs and player-facing text only.

## Game systems (Source/TheLastShift)

C++ for systems; Blueprints only for per-level scripting (wall placement, residue triggers), Niagara parameters, UMG widgets. **Everything tunable is exposed to Blueprints:** every game tunable is a `UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="...")` with `ClampMin`/`ClampMax`, every event a `BlueprintAssignable` dynamic multicast delegate, every state query or action `BlueprintCallable` or `BlueprintPure`. Balancing happens in the editor, never by recompiling.

**One owner per value.** Every tunable lives in exactly one place — a component, a subsystem, or a tuning DataTable row — and everything else reads it from there through a BlueprintPure getter or receives it through an event. Never copy a value into a widget, a Blueprint default, a Niagara user parameter, or a second component: widgets display fractions and states they are given, Niagara parameters are pushed by the owning component, and a constant that appears twice is a bug. Changing a value on its owner in the editor must change every behaviour that depends on it, with no other edit.

*   **Pause (time stop):** one key toggles Stop/Resume, any time, no direct cost. On Stop the flashlight goes out (black screen = no lights), the first scan fires automatically after ~1 s, then the player triggers scans manually (default Space) on a **cooldown** (default 4.5 s, measured from the last scan). Pressing during cooldown does nothing and is not queued. A minimal "scan ready" cue (a radial ring fed the cooldown fraction; no text; audio later). The cue never uses the scan colour; its defaults live in C++. On Resume scans clear with a short fade-out (default 0.4 s) and the flashlight returns to its previous state.
*   **Beat:** while Stopped, a silent timer (`BeatInterval`, default 4.5 s, measured from the Stop) broadcasts `OnBeat`. The entity moves continuously while Stopped, whether the player scans or not, and refreshes its path on beats; scanning only reveals where it is. Cooldown and beat have the same default period but are independent timers. The `UScanDirectorComponent` owns both and is the only game object that talks to the lidar plugin (including point-source registration); other systems subscribe to the director.
*   **Time stop implementation (decided):** no global time dilation. A `UTimeStateSubsystem` (world subsystem, `Source/TheLastShift/Time/`) holds `ETimeState { Normal, Stopped }`, accumulates `CumulativeStoppedSeconds` (real time while Stopped; the entity threshold reads it here), and broadcasts `OnTimeStateChanged` with an `EStopExitReason { Resumed, Rewound, Jumped, RewoundAndJumped }` on exits. The few time-dependent systems check the state; scan timers, Niagara effects shown while stopped, and UI run on unscaled real time (`GetRealTimeSeconds`, timers with `bIgnoreTimeDilation`). The player keeps moving in both states. The director records the owner's transform as `StopAnchor` on every Stop, exposes `GetRecentScanOrigins()`, ends a Stop with `ForceExit(Reason)` (Rewound and RewoundAndJumped first teleport the owner to the anchor, location, rotation and view, velocity cleared; Resumed and Jumped resume where it stands; every reason then calls `Resume(Reason)`, which restores the flashlight, clears the timers and fades the scans out as on any Resume; `Rewind()` is `ForceExit(Rewound)`) and re-broadcasts `OnStopExited(Reason, StoppedSeconds)`.
*   **Maps and run state:** the game is four maps, one per act, travelled in order. A map change goes through StdCore's level transitions over a black screen with a thin bar along the bottom edge that shows the real loading progress, a marker that crosses the screen on top of the bar over the covered time (it stands for the walk; the protagonist's walk cycle when the art exists) and, when the AI has one, the area entry line; no other text: it stands for a long walk the player does not play. A world subsystem resets with its map (cumulative stopped seconds are per act on purpose). Anything that must survive a map change lives in a game-instance subsystem: banked energy, home time, battery and act index. Actors never carry state across maps.
*   **Areas:** Areas are generated: an area map holds no level geometry. An `AAreaGenerator` builds the area at load from the act's recipe and seed in `UTheLastShiftSettings` (`Config/DefaultGame.ini`), deterministically, behind the travel screen: C++ plans the route and places everything the player interacts with; PCG graphs build the geometry and the backdrop from that plan. A seed is used in the game only if `tls.AreaValidate` passes for it. To change a level, change its recipe, its seed, the planner or a graph; never place geometry by hand in an area map. `TestLidar` stays hand-built as the test bench.
*   **Residues:** invisible in normal time, visible only in scans as dense bright clusters; collected by holding contact 2–3 s; 3–6 per area; 1 energy each. They are injected point sources registered through the director (no mesh). Collected residues are banked only on a voluntary Resume; on any contact they return to their places. A regenerating residue near each mandatory gate prevents dead ends.
*   **Entity:** one `AEntity` actor per level, no mesh, implements `ILidarPointSource` and registers through the director. Dormant until `CumulativeStoppedSeconds` reaches a threshold (default 18 s = 4 beats; per act later, from `ActTuning` in `UTheLastShiftSettings`); the check runs on beats, so it always awakens on a beat. Once awake it spawns on every Stop at a navmesh point at path distance `SpawnDistance` with no line of sight from the camera, preferring points not visible from any alive scan origin and in the player's forward hemisphere, picked at random among the valid candidates and away from the previous spawn: it is met at the threshold of unscanned space. Two forms in the MVP, both scan-only, both in the entity's own colour, both shown by the latest scan only: they stay at full intensity until the next scan starts, then disappear, so there is never more than one snapshot of the entity on screen and with `bOccludeByCapture`: the **trail** (points along the navigation path ahead of the figure for `TrailReach`, jittered: the tell) and the **figure** (a procedural humanoid point cloud refracted in four copies around the head axis: the chaser). The figure moves continuously at `Speed` (default 0.667 m/s = 3 m per beat) along a navigation path to the player, recomputed on every beat; it never moves in Normal state. It is only ever seen at scan instants, so the distance between two scans of it is proportional to the time between them. Contact (`ContactRadius`, checked every frame while Stopped) triggers the act's `ContactResponse`: Rewind (act 1: back to the Stop anchor, normal time), Jump (acts 2–3: time resumes where the player stands and the outside world jumps forward) or RewindAndJump (act 4). Any contact loses the residues collected during this Stop. No death. Next spawn distance = `clamp(DistanceAtExit − ApproachPerExit, MinSpawnDistance, InitialSpawnDistance)`, so each pause starts closer than the last exit; after any contact it starts at the minimum. `GetProximity01()` is 1 after any contact. Walls (barriers on hand-authored nodes, active only while Stopped) are post-MVP.
*   **Time jumps:** a voluntary Resume costs nothing. Jumps are acts of the entity: on contact (per-act jump size and battery drop in `UTheLastShiftSettings`, `ActTuning`, stored as text in `Config/DefaultGame.ini`) and, smaller, on a graze (exiting with the figure within `GrazeRadius`; 0 = off). A jump advances home time and drains the home battery. The battery starts full and never recharges. At zero the run is lost. A snapshot of the run state (battery, home time, banked energy) is taken at every area entry and saved to disk; after a loss, or from the main menu, the player restarts from any act already reached with that act's entry snapshot, and the snapshots of later acts are discarded.
*   **Energy:** one currency. Spend in the twin structure (door, elevator, terminal, twin AI, a group of lights) or keep it for home. Spent energy never comes back; kept energy feeds the ending. Devices are paid at a terminal in Normal time and may stay powered only for `PoweredSeconds` of Normal time; the timer stands still while Stopped. A time jump switches every powered device off.
*   **Readability:** Interactables (terminals, doors) show in scans in their own colour. Terminals and doors are meshes tagged with the stencil value `InteractableScanChannel` (one owner: `UTheLastShiftSettings`); the scan director pushes `ScanColors.Interactable` to that channel. They are not point sources. Source priorities, set in one place in the director: entity, then residues, then anything else. Every colour a scan can show (scan, ready cue, residue, entity, interactable) has one owner: `ScanColors` in `UTheLastShiftSettings`, stored as text in `Config/DefaultGame.ini`. The Interact prompt is suit UI: it shows the key bound to the action through StdCore's key prompt. Key names never appear in narrative text.
*   **Controls:** Rebindable with keyboard and mouse buttons: the four move directions, Stop/Resume, Scan, Flashlight, Interact. Mouse look, the pause-menu key and menu navigation are fixed. Each binding has one owner: StdCore's binding store. Seven defaults live in the game's `IMC_TestLidar` in `Content/TheLastShift/Input/`; Interact's default lives in StdCore's `IMC_Default`, because the pawn uses StdCore's `IA_Interact`. Gamepad support is out of the jam.
*   **Text channel:** the home AI speaks only in Normal time: the channel is hidden while Stopped, a message interrupted by a Stop restarts from its first character on return to Normal, and the channel is suspended while the pause menu is open, while a map transition covers the screen, and once the run is lost. A `UTextChannelComponent` holds the queue; it owns neither the battery nor the time state: each push carries a context and the game suspends it. Five states driven by the battery (Full, NoArticles, Abbreviated, Scattered, Silent), produced by an automatic function from the full text unless a row provides explicit variants; the level is fixed when a message starts. Typewriter speed slows as the battery drops. Return messages fire on time jumps (contact and graze), not on every Resume. The area entry line is the label of the travel screen. Triggers are wired in one place. The loss text is not an AI message: it is the `Loss` row of `DT_Endings`, shown in full on the loss screen.

## Narrative data

*   **Source of truth is CSV in `/Narrative/`** (text, diffable). DataTables in `Content/TheLastShift/Narrative/` are imported from those CSVs.
*   Agents edit the CSV, never the `.uasset`. After any CSV change, reimport the DataTable through the Unreal MCP; without it, remind the developer to reimport in the editor.
*   Tables: `DT_AIMessages` (return, area entry, area start, first energy, spending comments), `DT_Endings` (`Inject`, `Open`, `Loss`). Planned: `DT_Logs` (twin humanity logs, at least 5 and up to 8, shown in reverse order), `DT_Fragments` (6–8), `DT_Dialogue` (2–3 option choices). Row structs live in `Source/TheLastShift/Public/Narrative/`.
*   **Do not write or rewrite narrative content unless explicitly asked.** Use `[PLACEHOLDER]` rows to test systems. Style when asked: English written directly, short; "still" appears in every return message ("You have been still for 212 years").
*   Narrative text is drafted in a role-play session, when a developer asks for one: the agent plays the home AI, the developer plays the engineer, and the developer's comments before or after a line steer the scene or correct earlier lines. Only lines the developer approves go into the CSV. Write the full text only: degradation is automatic.
*   The other humanity's writing is made of rectangles that form barcodes. The suit translates it in real time: the translation is typed out over the glyphs, on the surface where they are written. Logs and any other writing of the twin structure are written in plain English, as the suit's translation; the barcodes are generated by the game, never authored.

## Working in Unreal as an agent

*   **Editor access through the Unreal MCP server.** When an Unreal Engine MCP connection is available to you, the editor is running with `TheLastShift.uproject` open and you do editor work yourself through it: create and edit assets, levels, actors, Blueprints, materials and Niagara systems, import DataTables from CSV, run PIE and read `stat` output. Check the connection first; if it is missing, or the server lacks a capability you need, fall back to short numbered steps for the developer with exact asset names and paths. Do not work around a missing capability with hacks (editing `.uasset` bytes, scripting the editor through other channels). Known limitations so far: no "new empty level" (copy a template map and strip it), no "create asset" (duplicate an existing asset of the right class), simulated UI key presses don't reach PIE (see below), console commands sent right after an editor restart can be silently dropped until the UI has been re-scanned (verify the first one took effect before measuring), Niagara compiles triggered through the MCP land one edit behind (save and restart PIE after Niagara edits).
*   **Editor assets are binary.** Anything you create or change through the MCP (`.uasset`, `.umap`) is not diffable, so every report must list each asset created, changed or deleted, with its path and what you did to it. Ask before deleting or renaming an existing asset. Each asset still belongs to a license zone: create it in the zone's folder, never elsewhere.
*   **Text first.** Project settings go in `Config/*.ini` as text, not through the editor's Project Settings UI, so they are diffable. Narrative CSV in `/Narrative/` stays the source of truth: never edit a narrative DataTable in the editor; edit the CSV, then reimport through the MCP.
*   **Order of work when a brief has both C++ and editor parts:** C++ → full build (editor closed, see below) → open the editor → editor work through the MCP → PIE check. You may close and reopen the editor yourself for a full build, after saving all assets through the MCP and saying so in the report; if closing or reopening fails, stop and ask the developer. Never change a header while the editor is open.
*   **Simulated input:** key presses sent through the MCP's UI tools do not reach the PIE viewport. Use the `tls.*` console commands in `Source/TheLastShift/Debug/` (`tls.Key`, `tls.Report`, `tls.CycleTest`, `tls.ScanTest`, `tls.EntityTest`, ...) and add to them when a brief needs a new automated check.
*   **Entering a level:** a test driven only by console commands does not prove that input works. A brief that changes how a level is entered (menu, travel, restart) also checks the input section of `tls.Report` on that path, and the developer checks real keys in a package.
*   **Package checks:** a package check reads the package log for errors and for assets that failed to load, not only for the lines the test expects.
*   Package with `-AdditionalCookerOptions="-noxgeshadercompile"`: without it the cook hangs on shader jobs sent to the XGE controller on this machine.
*   Regression runs of `tls.EntityTest` last 60 s so that the contact exit check runs, and every test result is reported as passed and failed counts.
*   Each regression test starts on a fresh PIE session, on the map the previous brief used for it, and the report names the map.
*   Screenshots capture the game or editor window only, never the desktop or another window.
*   Build (`UE_ROOT` = the UE 5.8 install, e.g. `C:\Program Files\Epic Games\UE_5.8`): `"<UE_ROOT>\Engine\Build\BatchFiles\Build.bat" TheLastShiftEditor Win64 Development -Project="<REPO>\TheLastShift.uproject" -WaitMutex`
*   After header changes, new `UCLASS`/`USTRUCT`/`UPROPERTY`/`UFUNCTION`, or `.Build.cs` changes: close the editor and do a full build. Don't rely on Live Coding for reflection changes. `.cpp`-only changes may go through Live Coding while the editor is open.
*   UE coding standard: type prefixes (`A`, `U`, `F`, `I`, `E`), `TObjectPtr` for UObject members, `UPROPERTY` on every UObject reference, no raw `new`/`delete` for UObjects. Keep `.Build.cs` dependencies minimal.
*   Asset prefixes: `BP_`, `WBP_`, `DT_`, `DA_`, `NS_`, `M_`, `MI_`, `SM_`, `SK_`, `T_`, `PCG_`.
*   Rendering: no Lumen, no Nanite, no textures, no complex materials, no bloom, fixed exposure (auto exposure off): the point display must not change brightness with how many points are on screen. Darkness and a flashlight in normal time; emissive points and lines while stopped. Target: Windows, Shipping.
*   Navigation in area maps is generated at runtime (dynamic navmesh, set in `Config/DefaultEngine.ini`).
*   Don't add engine plugins or third-party code without asking. The PCG plugin is enabled and may be used for area generation. PCG graphs are binary: describe each graph you create or change in the report (inputs, what it produces, parameters).

## Git

*   Commit only when asked. One commit per brief, made when the developer accepts the brief. Subject: `Brief <id>: <what the brief does>`, in English, imperative mood. Body: one line for each zone touched (`lidar:`, `stdcore:`, `game:`, `narrative:`, `build:`) saying what changed there.
*   LFS for `.uasset`, `.umap` and binary media. C++, CSV, `.ini`, `.uplugin` stay as plain text.
*   Never commit `Binaries/`, `Intermediate/`, `Saved/`, `DerivedDataCache/`, IDE files, packaged builds, or `.blend` sources. Builds go to itch.io via butler.
*   **Two agents at once:** only one agent works with the editor open. An agent working in parallel uses its own branch and a pull request, changes text files only (C++, CSV, `.ini`), creates no `.uasset` or `.umap`, and adds new files rather than editing files another brief is working on: binary assets cannot be merged. If a brief does not say which agent has the editor, ask.

## Scope

**MVP:** first-person movement + flashlight, 4 maps, one per act, generated at load from seeded recipes and validated, travelled in order; pause with black screen, automatic first scan and manual scans on a cooldown; scan-only residues with hold-to-collect; entity with threshold, continuous advance, trail and figure as injected points, per-act contact response; time jumps on contact and graze, home battery updated on each jump; loss at zero battery, act selection on the loss screen and in the main menu, progress saved; text channel in Normal time only, with return messages, area entry lines, the AI's explanations in act 1 and 4-level degradation; a text-only prologue before act 1; residue spending on at least 3 systems (bulkhead, terminal, twin AI); Interact prompt, interactables' scan colour; reverse-order logs (at least 5, up to 8); two text endings; a fixed beacon in open spaces.

**After Nov 20.** Required before submission: scan points behind geometry shown attenuated instead of hidden (lidar plugin, generic; switched off it looks like today). Decided by Nov 20, built only if the look with 16 alive scans does not hold: density-based scan memory in the lidar plugin. Nice-to-have, in this order: the prologue's two hand-built maps, minimal audio, memory fragments on contact, a richer backdrop, the colour legend, entity walls (point barriers on hand-authored nodes, blocking only while Stopped), a modelled low-poly figure replacing the procedural point cloud, visible erosion of visited areas, dialogue choices affecting the ending, scan wave and edge lines.

**Cut — point it out if asked:** time acceleration/slow-down as player powers, enemies or hazards in normal time, separate suit battery, gameplay areas beyond four (the prologue's maps are not gameplay areas), detailed environment modelling, gamepad support.

## Open decisions — don't decide for the team

Engineer's name and gender; LidarScan and StdCore license terms; license for the Assets zone; entity thresholds, speed and spawn distances per act, and jump multipliers (tuned in playtest).
