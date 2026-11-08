# AGENTS.md — The Last Shift

Unreal Engine 5.8 (fixed — do not change the EngineAssociation in the .uproject), C++ first.

Shared instructions for every AI coding agent working in this repo (Claude Code, Gemini CLI, others).
This file is the single source of truth: tool-specific files (`CLAUDE.md`, `.gemini/settings.json`) only
import it. Put project rules here, never in those files.

First-person exploration game for GitHub Game Off 2026 (theme: SHIFT). 20–30 minutes, one location
(a derelict twin megastructure), four areas = four acts.

Core loop: the player stops time; while stopped the screen is black except for lidar scans (points +
wireframe that fade): the first fires automatically, the rest the player triggers on a cooldown;
tachyonic residues exist only in scans and are collected by holding contact; an entity closes in on a
silent beat, whether the player scans or not; every pause ages the outside world (time debt) and drains
the home battery; the story is told only through a text channel with the home AI that degrades as the
battery drops.

**Rule zero:** the core is pause + scans + entity. If moving blind on a fading wireframe with something
approaching is not tense and readable, nothing else matters. Prioritise accordingly.

## Deadlines (2026)

- Nov 20: MVP playable. Nice-to-haves only after this.
- Nov 26: feature freeze (bugs, build, itch.io page only).
- Nov 29: target submission. Hard deadline Dec 1, 13:37 PT.

## Source of truth

Design docs live in ClickUp (Italian, internal reference; "Ultimo Turno" was the working title).
Workspace `90122202168` → space "Space":

- Folder **GDD**: "Ultimo Turno — GDD (GameOff 2026, tema SHIFT)" — doc `2kxv7b1r-292`
- Folder **Documentazione**: "Game Off 2026 — Workflow di preparazione" (`2kxv7b1r-172`),
  "Game Off 2026 — Submission" (`2kxv7b1r-192`)

If a ClickUp connection (MCP) is available to you, read the GDD before making design decisions. If this
file and the GDD disagree, ask. The GDD's technical notes contain leftovers from before the engine choice
(Godot MultiMesh, Unity LineRenderer): ignore them; the "Engine: Unreal Engine 5" section is authoritative.

## Language

- Everything in the repo is English: code, identifiers, file names, comments, commit messages, README,
  and all player-facing text.
- Communicate with the developers in English too.

## License zones — read before creating or moving any file

The repo is public, but not everything in it has the same license. Every file belongs to exactly one zone.

| Zone | Paths | License | Notes |
|---|---|---|---|
| **Lidar** (future licensed product) | `Plugins/LidarScan/**` | Proprietary, own `Plugins/LidarScan/LICENSE` (terms set by the owner) | Must stay fully separable from the game |
| **StdCore** (reusable framework) | `Plugins/StdCore/**` | Proprietary, own `Plugins/StdCore/LICENSE` (same terms as LidarScan) | Pre-existing plugin: generic UMG widgets, level transitions, input binding, settings, menus |
| **Narrative** (view only) | `Narrative/**`, `Content/TheLastShift/Narrative/**` | All rights reserved, `LICENSE-NARRATIVE` | Dialogue, AI messages, logs, fragments, endings |
| **Game** | `Source/TheLastShift/**`, `Content/TheLastShift/**` (except Narrative, Art, Audio), `Config/`, scripts | MIT, root `LICENSE` | Game logic, Blueprints, maps, UI, tuning data |
| **Assets** | `Content/TheLastShift/Art/**`, `Content/TheLastShift/Audio/**` | TBD, `LICENSE-ASSETS` | Meshes, materials, sounds, music |

License files (`LICENSE*`, `CREDITS`) are written by the developer at the end of the jam. Until then
default copyright applies; the zone a file belongs to is still fixed by this table.

Hard rules:

1. **Dependency direction is one way:** `TheLastShift → LidarScan` and `TheLastShift → StdCore`.
   `LidarScan` and `StdCore` never depend on each other or on `TheLastShift`; each must compile and work
   in an empty project. No game concepts (residue, entity, act, battery, home, AI, debt) in either
   plugin's class names, code, comments, or content. Game behaviour plugs in through the plugins'
   generic extension points.
2. **Never copy code between zones.** If the game needs lidar-like behaviour the plugin lacks, add a
   generic feature to the plugin or wrap it in the game module. Don't duplicate plugin code in
   `Source/TheLastShift`.
3. **Narrative text lives only in the Narrative zone.** No story text hardcoded in C++ or Blueprints.
   Short UI labels ("Hold E", menu items) are fine in the Game zone. Code that *processes* text
   (message queue, typewriter, degradation function) is Game code.
4. **Tuning data and narrative data never share a table.** Separate row structs, separate DataTables,
   separate folders. Narrative rows may carry only what is needed to select a message (key, act,
   trigger, order); no numeric tuning columns. Tuning rows never carry player-facing text.
5. **Do not create, edit, or delete `LICENSE*`, `CREDITS`, or license headers unless explicitly asked.**
   When unsure which zone a new file belongs to, ask before creating it.
6. File headers:
   - Plugin sources (LidarScan, StdCore):
     `// Copyright <YEAR> <OWNER>. All rights reserved. See Plugins/<PluginName>/LICENSE.`
   - Game sources: `// SPDX-License-Identifier: MIT`
7. No Starter Content, no Fab/Marketplace assets. Any external asset must be added to `CREDITS` with
   author and license; ask the developer to confirm the license before importing.

## Repository layout

```
TheLastShift.uproject
Config/
Source/TheLastShift/           Game module (MIT)
  Public/ Private/
    Time/                      Time state, time debt, home battery
    Scan/                      Scan orchestration on top of LidarScan
    Residue/                   Residue actors, collection
    Entity/                    Entity controller, trails, walls, figure
    Energy/                    Spending on doors, elevators, terminals, twin AI
    TextChannel/               Message queue, degradation, typewriter
    Narrative/                 Narrative row structs (code is MIT, data is not)
    Data/                      Tuning row structs
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
  Data/                        Tuning DataTables and data assets (DT_ActTuning, ...)
  Narrative/                   Narrative DataTables only, imported from /Narrative/*.csv
  Art/ Audio/
```

The approved title is **The Last Shift**: use it in player-facing text, README, and the itch.io page.
`TheLastShift` is the code name for the module, folders, build targets, and the `.uproject`.

## LidarScan plugin

Generic, engine-level scanning effect. Planned approach (validate in the first prototype):

- At scan time a `SceneCaptureComponent2D` captures scene depth; a Niagara system reads the render
  target and spawns one point per sample in world space.
- Neighbour lines: connect each point to its k nearest neighbours within a max distance (spatial grid,
  no complex structures). Lines grow over 1–2 s.
- Each scan has an age; alpha fades to a non-zero floor. Older scans stay as ghosts, capped at 3–4.
- **Channels:** points are classified by custom depth stencil value → channel index. Per-channel colour,
  density, k, and max link distance come from a data asset. The plugin knows channel numbers, not what
  they mean; the game maps "residue" to a channel.
- **Injected points:** things that are never rendered (the entity) provide points through a generic
  interface/component (e.g. `ILidarPointSource`) queried at scan time, with per-source settings such as
  "ignore fade". The game's entity implements it.
- **Events:** `OnScanStarted` / `OnScanCompleted` delegates with scan index and timestamp. The game's
  scan director re-broadcasts them; nothing else in the game subscribes to the plugin directly.
- Every tunable is a `UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Lidar|...")` with
  `ClampMin`/`ClampMax`, so balancing happens in the editor without recompiling.

## StdCore plugin

Pre-existing, generic. Use it for everything it already covers — main menu, settings, pause menu, key
rebinding, level transitions — instead of reimplementing in `Source/TheLastShift`. Do not add game logic
to it; a change to StdCore is allowed only if it is generic and explicitly requested. Read its public
headers before building UI or input.

**Naming:** "pause" in StdCore means the menu pause. The time-stop mechanic is never called "pause" in
code or assets: use `TimeState` / `ETimeState::Stopped` / `Stop` / `Resume` (`UTimeStateSubsystem`,
`OnTimeStateChanged`, ...). "Pause" stays in design docs and player-facing text only.

## Game systems (Source/TheLastShift)

C++ for systems; Blueprints only for per-level scripting (wall placement, residue triggers), Niagara
parameters, UMG widgets. **Everything tunable is exposed to Blueprints:** every game tunable is a
`UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="...")` with `ClampMin`/`ClampMax`, every event a
`BlueprintAssignable` dynamic multicast delegate, every state query or action `BlueprintCallable` or
`BlueprintPure`. Balancing happens in the editor, never by recompiling.

- **Pause (time stop):** one key toggles Stop/Resume, any time, no direct cost. On Stop the flashlight
  goes out (black screen = no lights), the first scan fires automatically after ~1 s, then the player
  triggers scans manually (default Space) on a **cooldown** (default 4.5 s, measured from the last
  scan). Pressing during cooldown does nothing and is not queued. A minimal "scan ready" cue (no text,
  no bars; audio later). On Resume scans clear with a short fade-out (default 0.4 s) and the flashlight
  returns to its previous state.
- **Beat:** while Stopped, a silent timer (`BeatInterval`, default 4.5 s, measured from the Stop)
  broadcasts `OnBeat`. The entity advances on beats, not on scans, so refusing to scan does not hold it
  back; scanning only reveals where it is. Cooldown and beat have the same default period but are
  independent timers. The `UScanDirectorComponent` owns both and is the only game object that talks to
  the lidar plugin; other systems subscribe to the director.
- **Time stop implementation (decided):** no global time dilation. A `UTimeStateSubsystem` (world
  subsystem, `Source/TheLastShift/Time/`) holds `ETimeState { Normal, Stopped }` and broadcasts
  `OnTimeStateChanged`. The few time-dependent systems check the state; scan timers, Niagara effects
  shown while stopped, and UI run on unscaled real time (`GetRealTimeSeconds`, timers with
  `bIgnoreTimeDilation`). The player keeps moving in both states.
- **Residues:** invisible in normal time, visible only in scans as dense bright clusters; collected by
  holding contact 2–3 s; 3–6 per area; 1 energy each.
- **Entity:** appears after a cumulative stopped-time threshold that shrinks per act. Three forms, all
  scan-only, all with dense wrong-looking links (high k, long distance) and no fade:
  trails (tell, harmless), walls (block a passage only while stopped, placed on hand-authored nodes),
  the figure (humanoid refracted in four directions, advances a fixed distance per beat, ignores walls).
  Figure contact → **rewind**: lose residues from this pause, back to where the pause started, normal
  time, no death. Each new pause the figure starts closer than at the last exit.
- **Time debt:** on exiting a pause,
  `debt = pause_duration × act_multiplier × (1 + entity_proximity_factor)`. Updates home time and battery.
  Per-act numbers live in `DT_ActTuning`.
- **Energy:** one currency. Spend in the twin structure (door, elevator, terminal, twin AI) or keep it for
  home. Spent energy never comes back; kept energy feeds the ending.
- **Text channel:** message queue; four degradation levels driven by battery (full sentences → no
  articles → abbreviations/lowercase → scattered characters → silence), produced by an automatic
  function from the full text unless a row provides explicit variants. Typewriter speed slows as the
  battery drops.

## Narrative data

- **Source of truth is CSV in `/Narrative/`** (text, diffable). DataTables in
  `Content/TheLastShift/Narrative/` are imported from those CSVs.
- Agents edit the CSV, never the `.uasset`. After any CSV change, reimport the DataTable through the
  Unreal MCP; without it, remind the developer to reimport in the editor.
- Planned tables: `DT_AIMessages` (return, area entry, spending comments), `DT_Logs` (twin humanity
  logs, 8 entries, shown in reverse order), `DT_Fragments` (6–8), `DT_Dialogue` (2–3 option choices),
  `DT_Endings`. Row structs live in `Source/TheLastShift/Public/Narrative/`.
- **Do not write or rewrite narrative content unless explicitly asked.** Use `[PLACEHOLDER]` rows to
  test systems. Style when asked: English written directly, short; "still" appears in every return
  message ("You have been still for 212 years").

## Working in Unreal as an agent

- **Editor access through the Unreal MCP server.** When an Unreal Engine MCP connection is available
  to you, the editor is running with `TheLastShift.uproject` open and you do editor work yourself
  through it: create and edit assets, levels, actors, Blueprints, materials and Niagara systems, import
  DataTables from CSV, run PIE and read `stat` output. Check the connection first; if it is missing, or
  the server lacks a capability you need, fall back to short numbered steps for the developer with exact
  asset names and paths. Do not work around a missing capability with hacks (editing `.uasset` bytes,
  scripting the editor through other channels). Known limitations so far: no "new empty level" (copy
  a template map and strip it), no "create asset" (duplicate an existing asset of the right class),
  simulated UI key presses don't reach PIE (see below).
- **Editor assets are binary.** Anything you create or change through the MCP (`.uasset`, `.umap`) is
  not diffable, so every report must list each asset created, changed or deleted, with its path and
  what you did to it. Ask before deleting or renaming an existing asset. Each asset still belongs to a
  license zone: create it in the zone's folder, never elsewhere.
- **Text first.** Project settings go in `Config/*.ini` as text, not through the editor's Project
  Settings UI, so they are diffable. Narrative CSV in `/Narrative/` stays the source of truth: never
  edit a narrative DataTable in the editor; edit the CSV, then reimport through the MCP.
- **Order of work when a brief has both C++ and editor parts:** C++ → full build (editor closed, see
  below) → open the editor → editor work through the MCP → PIE check. You may close and reopen the
  editor yourself for a full build, after saving all assets through the MCP and saying so in the report;
  if closing or reopening fails, stop and ask the developer. Never change a header while the editor is
  open.
- **Simulated input:** key presses sent through the MCP's UI tools do not reach the PIE viewport. Use
  the `tls.*` console commands in `Source/TheLastShift/Debug/` (`tls.Key`, `tls.Report`,
  `tls.CycleTest`) and add to them when a brief needs a new automated check.
- Build (`UE_ROOT` = the UE 5.8 install, e.g. `C:\Program Files\Epic Games\UE_5.8`):
  `"<UE_ROOT>\Engine\Build\BatchFiles\Build.bat" TheLastShiftEditor Win64 Development -Project="<REPO>\TheLastShift.uproject" -WaitMutex`
- After header changes, new `UCLASS`/`USTRUCT`/`UPROPERTY`/`UFUNCTION`, or `.Build.cs` changes: close
  the editor and do a full build. Don't rely on Live Coding for reflection changes. `.cpp`-only changes
  may go through Live Coding while the editor is open.
- UE coding standard: type prefixes (`A`, `U`, `F`, `I`, `E`), `TObjectPtr` for UObject members,
  `UPROPERTY` on every UObject reference, no raw `new`/`delete` for UObjects. Keep `.Build.cs`
  dependencies minimal.
- Asset prefixes: `BP_`, `WBP_`, `DT_`, `DA_`, `NS_`, `M_`, `MI_`, `SM_`, `SK_`, `T_`.
- Rendering: no Lumen, no Nanite, no textures, no complex materials. Darkness and a flashlight in normal
  time; emissive points and lines while stopped. Target: Windows, Shipping.
- Don't add engine plugins or third-party code without asking.

## Git

- Commit only when asked. Small, frequent commits, English, imperative mood, zone prefix:
  `lidar: ...`, `stdcore: ...`, `game: ...`, `narrative: ...`, `assets: ...`, `build: ...`.
- LFS for `.uasset`, `.umap` and binary media. C++, CSV, `.ini`, `.uplugin` stay as plain text.
- Never commit `Binaries/`, `Intermediate/`, `Saved/`, `DerivedDataCache/`, IDE files, packaged builds,
  or `.blend` sources. Builds go to itch.io via butler.

## Scope

**MVP:** first-person movement + flashlight, 4 connected areas; pause with black screen and timed scans;
scan-only residues with hold-to-collect; entity with threshold, per-scan advance, point rendering,
rewind on contact; time debt + home battery updated on each exit; text channel with return messages
and 4-level degradation; residue spending on at least 3 systems (bulkhead, terminal, twin AI);
8 reverse-order logs; two text endings.

**Nice-to-have (only after Nov 20):** visible erosion of visited areas (swap intact/eroded geometry),
memory fragments on contact, dialogue choices affecting the ending, persistent entity trail, audio.

**Cut — point it out if asked:** time acceleration/slow-down as player powers, enemies or hazards in
normal time, separate suit battery, areas beyond four, detailed environment modelling.

## Open decisions — don't decide for the team

Engineer's name and gender; invented glyphs vs. logs already translated by the suit; LidarScan and
StdCore license terms; license for the Assets zone; entity thresholds and debt multipliers (tuned in
playtest).
