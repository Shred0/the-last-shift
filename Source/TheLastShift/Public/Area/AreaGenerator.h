// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Area/AreaTypes.h"
#include "Area/AreaValidator.h"
#include "AreaGenerator.generated.h"

enum class EAreaLoadStep : uint8;

class ADoorDevice;
class AEntity;
class ALightGroupDevice;
class ALogTerminal;
class ANavMeshBoundsVolume;
class ATwinAITerminal;
class APawn;
class APowerTerminal;
class AResidue;
class UBoxComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UPCGComponent;
class UPCGGraphInterface;
class UStaticMesh;

/** Where the generation of the current area stands. */
UENUM(BlueprintType)
enum class EAreaGenerationState : uint8
{
	Idle,
	/** PCG is building the geometry; the gameplay actors are placed. */
	BuildingWorld,
	/** Geometry done, the navmesh is being rebuilt. */
	BuildingNavigation,
	Ready,
	/** Every seed tried failed (see LastError). */
	Failed
};

/** A kind of box the plan publishes to the PCG graphs. */
UENUM(BlueprintType)
enum class EAreaPieceKind : uint8
{
	Floors,
	Walls,
	Curbs,
	Pillars,
	Ceilings,
	Backdrop
};

THELASTSHIFT_API const TCHAR* LexToString(EAreaPieceKind Kind);

/** The boxes of one kind, as the plan lays them out (structure kinds: XY footprint only; backdrop: full box). */
USTRUCT()
struct THELASTSHIFT_API FAreaPieceList
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FBox> Boxes;
};

/** Heights structure footprints are extruded with (the generator's tunables). */
struct THELASTSHIFT_API FAreaPieceHeights
{
	float FloorThickness = -1.f;
	float WallHeight = -1.f;
	float CurbHeight = -1.f;

	bool IsValid() const { return FloorThickness > 0.f && WallHeight > 0.f && CurbHeight > 0.f; }

	/**
	 * World transform of one engine cube (100 cm) filling Box as Kind: floors from -FloorThickness to 0, walls and
	 * pillars from -FloorThickness to WallHeight, curbs from -FloorThickness to CurbHeight, ceilings from WallHeight to
	 * WallHeight + FloorThickness; backdrop boxes keep their own Z. The one formula used for the published plan, the
	 * layout hash and the plan check.
	 */
	FTransform MakeTransform(EAreaPieceKind Kind, const FBox& Box) const;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAreaReady);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAreaGenerationFinished, class AAreaGenerator* /*Generator*/, bool /*bPassed*/);

/**
 * Builds the map's area at load, behind the travel screen, from the act's recipe and seed in UTheLastShiftSettings
 * (the test recipe outside the map list). Same recipe and seed, same area; nothing generated is saved in the map.
 *
 * On BeginPlay: (1) read the recipe and seed; (2) plan the route with the planner; (3) publish the plan as one array
 * of box transforms per kind (the Plan* properties, one engine cube of 100 cm per box) and run the PCG graphs on it
 * with the same seed (StructureGraph: floors, walls, curbs, pillars, ceilings, and the kit's meshes over the modelled
 * segments, whose boxes it spawns hidden: collision only; BackdropGraph: the far masses and, Brief 28, their modules); (4) spawn the gameplay actors in their slots (residues,
 * gates with door, terminal and regenerating residue, light groups, log and twin AI terminals, beacons, exit, area info) and move the map's PlayerStart
 * and entity to theirs; (5) fit the map's NavMeshBoundsVolume to the route, wait for PCG and for the dynamic navmesh;
 * (6) run the validator; (7) broadcast OnAreaReady. Until then the player pawn cannot move, look or stop time and
 * StdCore's transition screen is held black. A seed that cannot be built or fails validation is logged and the next
 * seed is tried, up to SeedAttempts times.
 *
 * The planner works on an axis-aligned grid of half-metre cells. The route is a one-way chain of segments heading +X,
 * turning +Y/-Y but never back; closed segments get walls and a ceiling, open segments get a curb on every edge
 * (taller than the pawn's step, so the player cannot fall); all walkable floors are at Z = 0. Segments never touch
 * except where they join, keeping SegmentClearance from each other, so the route has no shortcut. C++ decides where
 * everything the player interacts with goes; PCG only builds the boxes of the plan.
 *
 * In the editor, Generate() and Clear() preview the area of the map's act; the preview is transient and saves nothing.
 */
UCLASS(Blueprintable)
class THELASTSHIFT_API AAreaGenerator : public AActor
{
	GENERATED_BODY()

public:
	AAreaGenerator();

	/** The generator of World, or null. */
	static AAreaGenerator* Find(const UWorld* World);

	/** True when World has no area generator, or its area is ready. */
	static bool IsAreaReady(const UWorld* World);

	// --- Generation ---

	/** Seeds tried at load, from the act's seed upwards, before the generation gives up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Generation", meta = (ClampMin = "1", ClampMax = "32"))
	int32 SeedAttempts = 8;

	/** Builds the floors, walls, curbs, pillars and ceilings from the published plan (PCG_AreaStructure). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Generation")
	TObjectPtr<UPCGGraphInterface> StructureGraph;

	/** Builds the far backdrop from the published plan (PCG_AreaBackdrop). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Generation")
	TObjectPtr<UPCGGraphInterface> BackdropGraph;

	/** Longest wait for PCG and for the navmesh before the attempt fails. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Generation", meta = (ClampMin = "1.0", ClampMax = "120.0", Units = "s"))
	float BuildTimeoutSeconds = 30.f;

	// --- Backdrop ---

	/** Non-walkable masses are instanced around open segments out to this distance (the lidar's capture range). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Backdrop", meta = (ClampMin = "0.0", ClampMax = "100000.0", Units = "cm"))
	float BackdropRadius = 30000.f;

	/** Masses per hectare (100 m x 100 m) inside BackdropRadius. 0 = no backdrop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Backdrop", meta = (ClampMin = "0.0", ClampMax = "20.0"))
	float BackdropDensity = 2.f;

	/** Horizontal gap kept between a tower and any walkable floor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Backdrop", meta = (ClampMin = "500.0", ClampMax = "20000.0", Units = "cm"))
	float BackdropClearance = 1500.f;

	/** Towers rise from this far below the floor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Backdrop", meta = (ClampMin = "1000.0", ClampMax = "100000.0", Units = "cm"))
	float BackdropDepth = 20000.f;

	/** Share of masses that are horizontal slabs (far above or below the floor) instead of towers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Backdrop", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BackdropSlabShare = 0.3f;

	// --- Shape ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Shape", meta = (ClampMin = "250.0", ClampMax = "2000.0", Units = "cm"))
	float WallHeight = 400.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Shape", meta = (ClampMin = "5.0", ClampMax = "100.0", Units = "cm"))
	float WallThickness = 20.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Shape", meta = (ClampMin = "5.0", ClampMax = "100.0", Units = "cm"))
	float FloorThickness = 10.f;

	/** Height of the curb on every open edge. Must be above the pawn's MaxStepHeight to block (the validator checks it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Shape", meta = (ClampMin = "20.0", ClampMax = "150.0", Units = "cm"))
	float CurbHeight = 55.f;

	/** Gap kept between segments that do not join. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Shape", meta = (ClampMin = "100.0", ClampMax = "2000.0", Units = "cm"))
	float SegmentClearance = 300.f;

	/** Wall niches along closed corridors (hiding places for the entity), alternating sides. 0 = none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Shape", meta = (ClampMin = "0.0", ClampMax = "5000.0", Units = "cm"))
	float NicheSpacing = 400.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Shape", meta = (ClampMin = "50.0", ClampMax = "1000.0", Units = "cm"))
	float NicheDepth = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Shape", meta = (ClampMin = "100.0", ClampMax = "1000.0", Units = "cm"))
	float NicheWidth = 200.f;

	/** Corridor dead-end side passages: length range. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Shape", meta = (ClampMin = "200.0", ClampMax = "5000.0", Units = "cm"))
	float DeadEndLengthMin = 400.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Shape", meta = (ClampMin = "200.0", ClampMax = "5000.0", Units = "cm"))
	float DeadEndLengthMax = 800.f;

	/** Room pillars: grid spacing and side. Pillars keep clear of the route centreline and the walls. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Shape", meta = (ClampMin = "200.0", ClampMax = "5000.0", Units = "cm"))
	float PillarSpacing = 400.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Shape", meta = (ClampMin = "50.0", ClampMax = "1000.0", Units = "cm"))
	float PillarSize = 200.f;

	/** Distance kept between the route centreline and pillars. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Shape", meta = (ClampMin = "100.0", ClampMax = "2000.0", Units = "cm"))
	float PillarRouteClearance = 150.f;

	/** Web hall: distance between platform centres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Shape", meta = (ClampMin = "1000.0", ClampMax = "20000.0", Units = "cm"))
	float HallWebSpacing = 3000.f;

	/** Web hall: side of the junction platforms. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Shape", meta = (ClampMin = "200.0", ClampMax = "3000.0", Units = "cm"))
	float HallNodeSize = 600.f;

	/** Web hall: width of the catwalks between platforms. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Shape", meta = (ClampMin = "200.0", ClampMax = "2000.0", Units = "cm"))
	float HallCatwalkWidth = 300.f;

	/** Web hall: chance that a catwalk outside the spanning tree is kept (loops). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Shape", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HallLoopChance = 0.35f;

	/** Spurs (catwalks and halls): length of the spur catwalk before its platform. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Shape", meta = (ClampMin = "200.0", ClampMax = "10000.0", Units = "cm"))
	float SpurLengthMin = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Shape", meta = (ClampMin = "200.0", ClampMax = "10000.0", Units = "cm"))
	float SpurLengthMax = 1500.f;

	/** Spurs: side of the detached platform at the spur's end. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Shape", meta = (ClampMin = "300.0", ClampMax = "3000.0", Units = "cm"))
	float SpurPlatformMin = 500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Shape", meta = (ClampMin = "300.0", ClampMax = "3000.0", Units = "cm"))
	float SpurPlatformMax = 800.f;

	/** Spur catwalk width. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Shape", meta = (ClampMin = "200.0", ClampMax = "1000.0", Units = "cm"))
	float SpurWidth = 200.f;

	/** Web hall: the route through the web may take at most this many catwalks more than the Manhattan distance; beyond, a shortcut is added. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Shape", meta = (ClampMin = "0", ClampMax = "20"))
	int32 HallMaxDetourEdges = 2;

	/**
	 * How much shorter the walked navigation path is expected to be than the planned route centreline (it cuts corners
	 * in rooms and halls). A layout is kept only if its centreline is at least RouteLengthMin / (1 - RouteCornerCut),
	 * so the validator's path (rule 1) does not fall under the recipe's minimum.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Shape", meta = (ClampMin = "0.0", ClampMax = "0.3"))
	float RouteCornerCut = 0.1f;

	/** Whole layouts tried (each from a seed derived from Seed) until one fits and its route length is in range. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Shape", meta = (ClampMin = "1", ClampMax = "32"))
	int32 LayoutAttempts = 8;

	/** Placement attempts per segment before the generation fails. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Shape", meta = (ClampMin = "1", ClampMax = "64"))
	int32 PlacementAttempts = 16;

	// --- The way out (Brief 26): the exit corridor and the twin AI's room, after the recipe's segments, no random draw ---

	/** Width of the exit corridor (the area corridors' width). Its depth and run are the recipe's (ExitTriggerDepthM, ExitRunLengthM). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Way Out", meta = (ClampMin = "200.0", ClampMax = "1000.0", Units = "cm"))
	float ExitCorridorWidth = 400.f;

	/** Side of the twin AI's room (square, on the plan's grid). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Way Out", meta = (ClampMin = "1000.0", ClampMax = "1400.0", Units = "cm"))
	float TwinRoomSide = 1200.f;

	/** The short passage, as wide as the exit corridor, that leads into the twin AI's room (part of the room's segment). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Way Out", meta = (ClampMin = "300.0", ClampMax = "1000.0", Units = "cm"))
	float TwinRoomPassageLength = 400.f;

	// --- Placement ---

	/** No two residues closer than this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Placement", meta = (ClampMin = "100.0", ClampMax = "10000.0", Units = "cm"))
	float MinResidueSpacing = 1000.f;

	/** Residue centre above the floor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Placement", meta = (ClampMin = "20.0", ClampMax = "250.0", Units = "cm"))
	float ResidueHeight = 110.f;

	/**
	 * Gate terminal distance along the route, as a multiple of the walk the door's PoweredSeconds allow at the pawn's
	 * walk speed. Above 1 so the walk in Normal time cannot be made; the margin covers corners the path cuts.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Placement", meta = (ClampMin = "1.05", ClampMax = "5.0"))
	float TerminalDistanceFactor = 1.5f;

	/**
	 * Gates whose door stays open once paid (PoweredSeconds 0): the terminal stands at least this far back along the
	 * route from the door, so it never blocks the doorway.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Placement", meta = (ClampMin = "200.0", ClampMax = "5000.0", Units = "cm"))
	float UntimedTerminalDistance = 600.f;

	/** The gate's regenerating residue sits this far back along the route from its terminal. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Placement", meta = (ClampMin = "100.0", ClampMax = "5000.0", Units = "cm"))
	float RegenResidueOffset = 800.f;

	/** Terminals stand this far to the side of the route centreline, facing it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Placement", meta = (ClampMin = "50.0", ClampMax = "500.0", Units = "cm"))
	float TerminalSideOffset = 120.f;

	/** A terminal's standing place is this far in front of it (gate and light-group terminals, the twin AI's terminal). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Placement", meta = (ClampMin = "50.0", ClampMax = "140.0", Units = "cm"))
	float TerminalStandDistance = 90.f;

	/**
	 * Log terminals: the reading place is this far in front of the screen (1 to 1.5 m), free, with nothing between it and
	 * the screen. The planner keeps it free and the validator checks it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Placement", meta = (ClampMin = "100.0", ClampMax = "150.0", Units = "cm"))
	float LogReadingDistance = 110.f;

	/** Terminals against a wall (log, twin AI) look for their wall up to this far from the route centreline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Placement", meta = (ClampMin = "200.0", ClampMax = "5000.0", Units = "cm"))
	float WallTerminalSearchDistance = 3000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Placement", meta = (ClampMin = "1", ClampMax = "12"))
	int32 LightsPerGroup = 3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Placement", meta = (ClampMin = "100.0", ClampMax = "3000.0", Units = "cm"))
	float LightSpacing = 600.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Placement", meta = (ClampMin = "50.0", ClampMax = "1500.0", Units = "cm"))
	float LightHeight = 250.f;

	/** Intensity of the generated lights, in lumens (as the hand-placed lights of TestLidar). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Placement", meta = (ClampMin = "0.0", ClampMax = "100000.0"))
	float LightIntensity = 800.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Placement", meta = (ClampMin = "100.0", ClampMax = "5000.0", Units = "cm"))
	float LightRadius = 700.f;

	/** PoweredSeconds of the generated light groups. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Placement", meta = (ClampMin = "0.0", ClampMax = "600.0", Units = "s"))
	float LightGroupPoweredSeconds = 30.f;

	// --- Beacons (FAreaRecipe::BeaconCount): a light group with one light, hung above a Hall; powered for LightGroupPoweredSeconds ---

	/** The head's mount above the open segment's highest floor or catwalk, over the centre of the space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Beacon", meta = (ClampMin = "300.0", ClampMax = "3000.0", Units = "cm"))
	float BeaconHeight = 800.f;

	/** The rod goes up to the first geometry above the mount; where there is none, it has this length. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Beacon", meta = (ClampMin = "100.0", ClampMax = "20000.0", Units = "cm"))
	float BeaconRodMaxLength = 6000.f;

	/** Half-angle of the beacon's cone (the spot light's outer cone angle). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Beacon", meta = (ClampMin = "1.0", ClampMax = "45.0", Units = "deg"))
	float BeaconConeAngle = 12.f;

	/** Luminous intensity on the beacon's axis, in candelas (does not change with the cone). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Beacon", meta = (ClampMin = "0.0", ClampMax = "100000000.0"))
	float BeaconIntensity = 500000.f;

	/** Range of the beacon's light (its attenuation radius); the validator checks that it reaches the point it lights. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Beacon", meta = (ClampMin = "1000.0", ClampMax = "50000.0", Units = "cm"))
	float BeaconRange = 20000.f;

	/**
	 * Validator rule 8: nothing may stand on the line from the beacon's light to this height above the point where the
	 * route leaves its segment. The beacon still aims at the floor point; this only places the end of the clear path.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Beacon", meta = (ClampMin = "0.0", ClampMax = "400.0", Units = "cm"))
	float BeaconClearPathHeight = 150.f;

	// --- Dressing (Brief 16): the kit's meshes the structure graph places over the boxes of the modelled segments ---
	// Where every cell along a box belongs to a modelled segment (Corridor, Room, Gate, Catwalk, Hall), the box is kept for collision and
	// navigation only (Plan<Kind>Collision: not rendered, not seen by the scan's capture) and the kit's meshes are placed
	// over it, look only (no collision, no navigation). Computed from the plan, with no random draw: the plan and the
	// layout hash do not change. The sizes below are the meshes' own (in the graph); the dressing scales them to fit.

	/** SM_KitWallPanel's size: X thickness (ribs towards +X), Y width, Z height (pivot at its base centre). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Dressing")
	FVector DressWallPanelSize = FVector(20.f, 100.f, 350.f);

	/** SM_KitFloorPlate's size: X long side, Y short side, Z thickness (pivot at its base centre). Floors and ceilings (turned over). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Dressing")
	FVector DressFloorPlateSize = FVector(200.f, 100.f, 10.f);

	/** SM_KitCornerPost's size: X and Y the post's side, Z its height (pivot at its base centre). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Dressing")
	FVector DressCornerPostSize = FVector(20.f, 20.f, 100.f);

	/** SM_KitCurb's size: X thickness (towards +X the deck), Y length, Z height from its base (pivot at its base centre). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Dressing")
	FVector DressCurbSize = FVector(20.f, 100.f, 125.f);

	/** SM_KitCeilingBeam: its length (along Y; pivot at its top centre). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Dressing", meta = (ClampMin = "50.0", ClampMax = "1000.0", Units = "cm"))
	float DressBeamLength = 300.f;

	/** Ceiling beams across corridors, this far apart along each leg. 0 = none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Dressing", meta = (ClampMin = "0.0", ClampMax = "2000.0", Units = "cm"))
	float DressBeamSpacing = 300.f;

	/** SM_KitConduit: its length (along Y; pivot at its back, at the base). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Dressing", meta = (ClampMin = "50.0", ClampMax = "1000.0", Units = "cm"))
	float DressConduitLength = 200.f;

	/** SM_KitConduit: its depth from its back (mounted on the wall panels' slab, the wall box's middle). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Dressing", meta = (ClampMin = "1.0", ClampMax = "100.0", Units = "cm"))
	float DressConduitDepth = 29.f;

	/**
	 * Conduits run along the walls of corridors and gates (not inside niches or dead ends) at this height, above the
	 * player, one continuous run per wall line: it goes straight on across a niche's or a dead end's mouth to the wall on
	 * its far side, with every bracket on a wall; it ends against a wall's or a gate frame's face, or meets the run on the
	 * wall that turns there.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Dressing", meta = (ClampMin = "250.0", ClampMax = "1000.0", Units = "cm"))
	float DressConduitHeight = 300.f;

	/** SM_KitGateFrame's depth along the passage: the gate's wall panels stop on each side of it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Dressing", meta = (ClampMin = "10.0", ClampMax = "200.0", Units = "cm"))
	float DressGateFrameDepth = 60.f;

	/** SM_KitGateFrame's size across the passage (outer faces) and its height (from the floor's underside). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Dressing")
	FVector2D DressGateFrameSize = FVector2D(480.f, 420.f);

	/** SM_KitCatwalkFrame: its span across the deck (along Y; pivot at its top centre, hung under the deck). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Dressing", meta = (ClampMin = "50.0", ClampMax = "2000.0", Units = "cm"))
	float DressFrameSpan = 340.f;

	/** Frames under catwalks and their platforms, about this far apart. 0 = none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Dressing", meta = (ClampMin = "0.0", ClampMax = "2000.0", Units = "cm"))
	float DressFrameSpacing = 200.f;

	/**
	 * Brief 26: the twin AI's room's conduits and cables (SM_KitTwinConduit, SM_KitTwinCable: this long along +X, pivot at
	 * the centre of their start; SM_KitTwinConduitBend: a quarter turn from +X to +Z of this radius at its centreline).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Dressing", meta = (ClampMin = "10.0", ClampMax = "500.0", Units = "cm"))
	float DressTwinPieceLength = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Dressing", meta = (ClampMin = "10.0", ClampMax = "200.0", Units = "cm"))
	float DressTwinBendRadius = 40.f;

	/** SM_KitCatwalkFrame: from its pivot (the deck's underside) down to its lowest point, the tip of the triangle it makes (Brief 28: the pillars' tops). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Dressing", meta = (ClampMin = "0.0", ClampMax = "1000.0", Units = "cm"))
	float DressFrameTipDepth = 165.f;

	// --- The backdrop's dressing (Brief 28): the masses' modules, the levels of walkways above and below, the pillars ---
	// Each part's switch and values are the recipe's (FAreaRecipe::Backdrop, Config/DefaultGame.ini). Below: the space every
	// part keeps clear and the modules' sizes. Look only, from the finished plan with no random draw.

	/** Nothing of the backdrop's dressing stands within this distance of a walkable volume's sides. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Backdrop Dressing", meta = (ClampMin = "0.0", ClampMax = "2000.0", Units = "cm"))
	float DecorSideClearance = 200.f;

	/** Nor within this distance above or below a walkable deck or floor (the pillars under the walked level's own frames excepted). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Backdrop Dressing", meta = (ClampMin = "0.0", ClampMax = "5000.0", Units = "cm"))
	float DecorVerticalClearance = 800.f;

	/** The masses' modules: their side, the grid they tile a face on (SM_Backdrop*: 4 m x 4 m, pivot at the bottom middle of their back, +X out of the face). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Backdrop Dressing", meta = (ClampMin = "100.0", ClampMax = "2000.0", Units = "cm"))
	float DecorModuleSize = 400.f;

	/** The deepest module's relief off its face: a cell closer than this to another mass gets none, and the levels keep this far from a mass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Backdrop Dressing", meta = (ClampMin = "0.0", ClampMax = "1000.0", Units = "cm"))
	float DecorModuleDepth = 230.f;

	/** The range a module is scaled within, along and up, to fit its face. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Backdrop Dressing", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float DecorModuleMinFactor = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Backdrop Dressing", meta = (ClampMin = "1.0", ClampMax = "4.0"))
	float DecorModuleMaxFactor = 1.25f;

	/** Instances of the dressing (all pieces). */
	UFUNCTION(BlueprintPure, Category = "Area")
	int32 GetDressingInstanceCount() const;

	/** True when every box of Kind along the segment's cells is kept for collision only (the segment type is modelled). */
	static bool IsDressedSegmentType(EAreaSegmentType Type);

	// --- Classes and assets ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Classes")
	TSubclassOf<AResidue> ResidueClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Classes")
	TSubclassOf<APowerTerminal> TerminalClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Classes")
	TSubclassOf<ADoorDevice> DoorClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Classes")
	TSubclassOf<ALightGroupDevice> LightGroupClass;

	/** The recipe's log terminals (FAreaRecipe::LogTerminalCount). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Classes")
	TSubclassOf<ALogTerminal> LogTerminalClass;

	/** The recipe's twin AI terminal (FAreaRecipe::TwinAITerminalCount). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Classes")
	TSubclassOf<ATwinAITerminal> TwinAITerminalClass;

	/** The beacons' lamp head (SM_BeaconHead: pivot at its mount, +X the direction it lights). None: the beacon is a light alone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Classes")
	TObjectPtr<UStaticMesh> BeaconHeadMesh;

	/** The gate doors' modelled panel (SM_KitGatePanel, 4 x 3 m, pivot at its bottom centre), given to the doors this generator spawns. None: the door's own. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Classes")
	TObjectPtr<UStaticMesh> GatePanelMesh;

	/** The beacons' rod (SM_BeaconRod: one metre along +Z, pivot at its lower end), stretched to the length it needs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Classes")
	TObjectPtr<UStaticMesh> BeaconRodMesh;

	// --- Validation (tls.AreaValidate) ---

	/** Rule 4: closed segments are sampled along the route this often. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Validation", meta = (ClampMin = "100.0", ClampMax = "5000.0", Units = "cm"))
	float HidingSampleSpacing = 500.f;

	/** Rule 4 passes when at least this share of the samples has a hiding place. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Validation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HidingPassShare = 0.9f;

	/** Rule 5: open edges are probed this often. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Validation", meta = (ClampMin = "10.0", ClampMax = "500.0", Units = "cm"))
	float CurbSampleSpacing = 50.f;

	// --- Actions and state ---

	/** Act of this map (1-4), 0 outside the map list (the test recipe). Set when the generation starts. */
	UFUNCTION(BlueprintPure, Category = "Area")
	int32 GetAct() const { return Act; }

	/** The recipe being built, read from UTheLastShiftSettings (null when the act has none). */
	const FAreaRecipe* GetRecipe() const;

	/** Number of Gate entries in the recipe (the area's gate count). */
	UFUNCTION(BlueprintPure, Category = "Area")
	int32 GetGateCount() const;

	UFUNCTION(BlueprintPure, Category = "Area")
	bool IsReady() const { return State == EAreaGenerationState::Ready; }

	UFUNCTION(BlueprintPure, Category = "Area")
	EAreaGenerationState GetGenerationState() const { return State; }

	/** True while the player is held (no input, no movement) until the area is ready. */
	UFUNCTION(BlueprintPure, Category = "Area")
	bool IsHoldingPlayer() const { return HeldPawn.IsValid(); }

	/** Hash of the generated layout (plan, gameplay actors, start and entity poses); 0 before the first ready. */
	UFUNCTION(BlueprintPure, Category = "Area")
	int64 GetLayoutHash() const { return LayoutHash; }

	/** Route centreline length of the current plan. */
	UFUNCTION(BlueprintPure, Category = "Area", meta = (Units = "cm"))
	float GetRouteLength() const { return RouteLength; }

	/** Route centreline length before the way out (the twin AI's room and the exit corridor): the recipe's own route. */
	UFUNCTION(BlueprintPure, Category = "Area", meta = (Units = "cm"))
	float GetRouteLengthBeforeWayOut() const { return RouteLengthBeforeWayOut; }

	/** The exit corridor's end edge: the collision-only box that keeps the player from walking off it (no dressing, never rendered). */
	const TArray<FBox>& GetOpenEndBoxes() const { return OpenEndBoxes; }

	/** Seed of the current (or last) attempt. */
	UFUNCTION(BlueprintPure, Category = "Area")
	int32 GetSeed() const { return Seed; }

	/** Instances PCG built for this area (all kinds). */
	UFUNCTION(BlueprintPure, Category = "Area")
	int32 GetPCGInstanceCount() const;

	/** Instances PCG built of one kind. */
	int32 GetPCGInstanceCountOf(EAreaPieceKind Kind) const;

	/** The PCG-built instanced components of Kind on this actor (identified by the component tag "Area<Kind>"). */
	TArray<UInstancedStaticMeshComponent*> GetPCGComponents(EAreaPieceKind Kind) const;

	/** Real-time seconds from the start of the generation to ready (all attempts). */
	UFUNCTION(BlueprintPure, Category = "Area", meta = (Units = "s"))
	float GetTimeToReadySeconds() const { return TimeToReadySeconds; }

	/** The boxes of Kind in the current plan. */
	const TArray<FBox>& GetPublishedPieces(EAreaPieceKind Kind) const;

	/** The heights structure footprints are extruded with (this generator's tunables). */
	FAreaPieceHeights GetPieceHeights() const;

	// --- The plan as the PCG graphs read it (Get Actor Property on Self): one engine-cube transform per box ---

	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Area|Plan")
	TArray<FTransform> PlanFloors;

	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Area|Plan")
	TArray<FTransform> PlanWalls;

	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Area|Plan")
	TArray<FTransform> PlanCurbs;

	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Area|Plan")
	TArray<FTransform> PlanPillars;

	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Area|Plan")
	TArray<FTransform> PlanCeilings;

	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Area|Plan")
	TArray<FTransform> PlanBackdrop;

	// The boxes of the modelled segments: collision and navigation only, never rendered (the graph spawns them hidden).
	// Plan<Kind> holds the boxes still shown; the two together are the plan's boxes of Kind.

	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Area|Plan")
	TArray<FTransform> PlanFloorsCollision;

	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Area|Plan")
	TArray<FTransform> PlanWallsCollision;

	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Area|Plan")
	TArray<FTransform> PlanCurbsCollision;

	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Area|Plan")
	TArray<FTransform> PlanPillarsCollision;

	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Area|Plan")
	TArray<FTransform> PlanCeilingsCollision;

	// The dressing: one array per kit mesh, read by the structure graph (component tag AreaDressing).

	/** SM_KitWallPanel over walls, scaled along and up to fit. */
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Area|Dressing")
	TArray<FTransform> DressWallPanels;

	/** SM_KitFloorPlate over floors, catwalk decks and halls (the floors' family). */
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Area|Dressing")
	TArray<FTransform> DressFloorPlates;

	/** SM_KitFloorPlate turned over under ceilings: the same plates in the panels' family (Brief 21). */
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Area|Dressing")
	TArray<FTransform> DressCeilingPlates;

	/** SM_KitCornerPost in the corners of walls and curbs. */
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Area|Dressing")
	TArray<FTransform> DressCornerPosts;

	/** SM_KitCeilingBeam across corridors. */
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Area|Dressing")
	TArray<FTransform> DressCeilingBeams;

	/** SM_KitConduit along corridor walls. */
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Area|Dressing")
	TArray<FTransform> DressConduits;

	/** SM_KitCurb over curbs. */
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Area|Dressing")
	TArray<FTransform> DressCurbs;

	/** SM_KitCatwalkFrame under catwalk decks and under a web hall's walkways and platforms. */
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Area|Dressing")
	TArray<FTransform> DressCatwalkFrames;

	/** SM_KitGateFrame around each gate's door. */
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Area|Dressing")
	TArray<FTransform> DressGateFrames;

	/** SM_KitTwinConduit: the thick conduits running to the twin AI's terminal (Brief 26). */
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Area|Dressing")
	TArray<FTransform> DressTwinConduits;

	/** SM_KitTwinConduitBend: where those conduits turn (down into the floor). */
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Area|Dressing")
	TArray<FTransform> DressTwinBends;

	/** SM_KitTwinCable: the bundles of cables running to the twin AI's terminal. */
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Area|Dressing")
	TArray<FTransform> DressTwinCables;

	// The backdrop's dressing (Brief 28), one array per mesh: the masses' modules (read by the backdrop graph), the levels'
	// deck plates, curbs and frames and the pillars (read by the structure graph). Component tags "AreaDecor" and "Area<array>".

	/** Area 1's modules (SM_BackdropHullRib, SM_BackdropHullPlate, SM_BackdropHullFrame). */
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Area|Backdrop Dressing")
	TArray<FTransform> DecorMassHullRib;

	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Area|Backdrop Dressing")
	TArray<FTransform> DecorMassHullPlate;

	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Area|Backdrop Dressing")
	TArray<FTransform> DecorMassHullFrame;

	/** Area 2's modules (SM_BackdropServerStack, SM_BackdropServerBays, SM_BackdropServerRiser). */
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Area|Backdrop Dressing")
	TArray<FTransform> DecorMassServerStack;

	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Area|Backdrop Dressing")
	TArray<FTransform> DecorMassServerBays;

	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Area|Backdrop Dressing")
	TArray<FTransform> DecorMassServerRiser;

	/** Area 3's modules (SM_BackdropMachColumn, SM_BackdropMachPipes, SM_BackdropMachRing). */
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Area|Backdrop Dressing")
	TArray<FTransform> DecorMassMachColumn;

	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Area|Backdrop Dressing")
	TArray<FTransform> DecorMassMachPipes;

	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Area|Backdrop Dressing")
	TArray<FTransform> DecorMassMachRing;

	/** The levels' decks (SM_KitFloorPlate). */
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Area|Backdrop Dressing")
	TArray<FTransform> DecorLayerPlates;

	/** The levels' curbs (SM_KitCurb). */
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Area|Backdrop Dressing")
	TArray<FTransform> DecorLayerCurbs;

	/** The levels' frames (SM_KitCatwalkFrame). */
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Area|Backdrop Dressing")
	TArray<FTransform> DecorLayerFrames;

	/** The pillars (SM_KitCornerPost, scaled along its length). */
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Area|Backdrop Dressing")
	TArray<FTransform> DecorPillars;

	/** The decor arrays by index (the order above). */
	static constexpr int32 DecorHullRib = 0;
	static constexpr int32 DecorHullPlate = 1;
	static constexpr int32 DecorHullFrame = 2;
	static constexpr int32 DecorServerStack = 3;
	static constexpr int32 DecorServerBays = 4;
	static constexpr int32 DecorServerRiser = 5;
	static constexpr int32 DecorMachColumn = 6;
	static constexpr int32 DecorMachPipes = 7;
	static constexpr int32 DecorMachRing = 8;
	static constexpr int32 DecorLayerPlate = 9;
	static constexpr int32 DecorLayerCurb = 10;
	static constexpr int32 DecorLayerFrame = 11;
	static constexpr int32 DecorPillar = 12;
	static constexpr int32 DecorMeshCount = 13;

	/** What a pillar's foot stands on. */
	static constexpr uint8 PillarFootGeometry = 0;
	static constexpr uint8 PillarFootMaxLength = 1;
	static constexpr uint8 PillarFootCut = 2;

	/** The property name of a decor array ("DecorMassHullRib"...), also its component tag after "Area". */
	static const TCHAR* GetDecorArrayName(int32 Mesh);

	/** 0 the masses, 1 the levels, 2 the pillars. */
	static int32 GetDecorPart(int32 Mesh);

	const TArray<FTransform>& GetDecorArray(int32 Mesh) const;
	TArray<FTransform>* MutableDecorArray(int32 Mesh);

	/** The PCG-built components of one decor array (Mesh < 0: all of them). */
	TArray<UInstancedStaticMeshComponent*> GetDecorComponents(int32 Mesh) const;

	/** Instances PCG built of the backdrop's dressing. */
	UFUNCTION(BlueprintPure, Category = "Area")
	int32 GetDecorInstanceCount() const;

	/** One mass face of the last build: whether it is turned to the route, its cells within reach and what they got. */
	struct FDecorFaceRecord
	{
		int32 Mass = -1;
		/** 0..5: +X, -X, +Y, -Y, +Z, -Z. */
		int32 Face = -1;
		FVector Normal = FVector::ZeroVector;
		FVector Centre = FVector::ZeroVector;
		FVector2D Size = FVector2D::ZeroVector;
		bool bTowardRoute = false;
		int32 CellsInReach = 0;
		int32 Placed = 0;
		int32 SkippedMass = 0;
		int32 SkippedClear = 0;
		EAreaBackdropFamily Family = EAreaBackdropFamily::None;
	};

	/** One level of walkways of the last build. */
	struct FDecorLevelRecord
	{
		int32 Segment = -1;
		/** +1, +2... above, -1, -2... below. */
		int32 Level = 0;
		float Z = 0.f;
		int32 Turns = 0;
		int32 Plates = 0;
		int32 Curbs = 0;
		int32 Frames = 0;
		int32 PiecesCut = 0;
		FBox2D Bounds = FBox2D(ForceInit);
		/** The farthest corner from the segment's centre: the level's and the walked level's. */
		double Reach = 0.0;
		double WalkedReach = 0.0;
	};

	struct FDecorPillarRecord
	{
		FVector Top = FVector::ZeroVector;
		float Length = 0.f;
		uint8 Foot = 0;
		bool bWalked = false;
	};

	/** A view for tls.Views areabackdrop: a module of a family (or a pillar's top) and the way it faces. */
	struct FDecorShotPick
	{
		FString Family;
		FVector At = FVector::ZeroVector;
		FVector Normal = FVector::ZeroVector;
	};

	TArray<FDecorFaceRecord> DecorFaces;
	TArray<FDecorLevelRecord> DecorLevels;
	TArray<FDecorPillarRecord> DecorPillarRecords;
	TArray<FDecorShotPick> DecorMassPicks;
	FDecorShotPick DecorPillarPick;
	/** The space kept clear round the walkable floors, and the exit corridors' line and clearance (2D), as the last build used them. */
	TArray<FBox> DecorForbidden;
	TArray<FBox2D> DecorExitClearance;

	/** What the backdrop's dressing placed at the last build (logged at ready, BACKDROP line). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area|State")
	FString LastBackdropReport;

	/** The floor strips of the last dressing (Brief 26): DressFloorPlates from this index on fill the floor to the wall panels' slab. */
	int32 DressFloorStripsFrom = 0;

	/** A wall panel the last dressing placed: its centre (on the wall box's middle line), the way it faces, its factor along. */
	struct FDressPanelPick
	{
		FVector2D At = FVector2D::ZeroVector;
		FVector2D Facing = FVector2D::ZeroVector;
		double Factor = 0.0;
	};

	/** The most squeezed and the most stretched wall panel of the last dressing (tls.Views areawalls shoots them). */
	FDressPanelPick DressMostSqueezedPanel;
	FDressPanelPick DressMostStretchedPanel;

	/** The first niche or dead-end mouth a conduit run crosses: its middle on the wall line, the way into the corridor, its width (Factor, cm). */
	FDressPanelPick DressFirstConduitMouth;

	/** What the last dressing placed, with the largest and smallest factors it scaled the kit's pieces by (logged at ready). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area|State")
	FString LastDressingReport;

	/**
	 * Clears and regenerates the area for InSeed in this world (runtime: tls.AreaGenerate, seed sweeps). With
	 * bInTryNextSeeds, a seed that fails is followed by the next ones as at load. Ends with OnGenerationFinished.
	 */
	void Regenerate(int32 InSeed, bool bInTryNextSeeds);

	/** Rebuilds the navmesh of the current content, then validates (tls.AreaBreak). Ends with OnGenerationFinished. */
	void RevalidateAfterNavigationRebuild();

	/** Removes the generated actors and the PCG output; returns the actors removed. */
	int32 ClearArea();

	/**
	 * Hash of the generated layout, in the form of Brief 4b-1 (so an unchanged plan gives the same hash): every actor
	 * with AAreaInfo::GeneratedTag, the plan's boxes per kind in plan order, the PlayerStart, the entity, the navmesh
	 * bounds position and the area info. OutLines is the number of items hashed; OutText (optional) receives them.
	 */
	int64 ComputeLayoutHash(int32& OutLines, TArray<FString>* OutText = nullptr) const;

	/** Result of the last validation (load, regeneration or revalidation). */
	const FAreaValidationResult& GetLastValidation() const { return LastValidation; }

	/** True when PCG built exactly the plan's boxes (per kind, compared as sets of rounded transforms). */
	bool DoesPCGMatchPlan(FString& OutWhy) const;

#if WITH_EDITOR
	/** Editor preview of the map's act and seed (transient: saves nothing, no navmesh wait, no validation). */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Area")
	void Generate();

	/** Removes the editor preview. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Area")
	void Clear();
#endif

	/** The area is generated, validated and playable (the player is released). */
	UPROPERTY(BlueprintAssignable, Category = "Area")
	FOnAreaReady OnAreaReady;

	/** Every generation or revalidation end (also failed ones), with the validation result. */
	FOnAreaGenerationFinished OnGenerationFinished;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area|State")
	FString LastError;

	/** Items hashed by the last layout hash. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area|State")
	int32 LastActorCount = 0;

	/** Planning and spawning time of the last attempt (PCG, navmesh and validation excluded). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area|State", meta = (Units = "s"))
	float LastGenerationSeconds = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area|State", meta = (Units = "s"))
	float LastPCGSeconds = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area|State", meta = (Units = "s"))
	float LastNavigationSeconds = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area|State", meta = (Units = "s"))
	float LastValidationSeconds = 0.f;

	/** Seeds tried by the last generation (1 = the first one passed). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area|State")
	int32 LastSeedsTried = 0;

	/** Result of the last validation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area|State")
	bool bLastValidationPassed = false;

	/** Whether PCG built exactly the plan, at the last ready. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area|State")
	bool bLastPCGMatchedPlan = false;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Bounds of the actor for PCG (which refuses an actor without bounds): no collision, no navigation, never drawn. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area")
	TObjectPtr<UBoxComponent> PCGBounds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area")
	TObjectPtr<UPCGComponent> StructurePCG;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area")
	TObjectPtr<UPCGComponent> BackdropPCG;

private:
	void StartGeneration(int32 InSeed, bool bInTryNextSeeds);
	/** Starts InSeed, and the next seeds while they cannot be planned (and bTryNextSeeds allows). */
	void TryAttempt(int32 InSeed);
	/** Every allowed seed failed: the player is released on an invalid area, with an error. */
	void GiveUp();
	/** Plans and spawns everything for InSeed and starts PCG; false (and LastError) when the plan cannot be built. */
	bool StartAttempt(int32 InSeed, FString& OutError);
	void FailAttempt(const FString& Why);
	void FinishWithValidation();
	void BecomeReady();
	void StartNavigationRebuild();
	void RunPCG(UPCGComponent* Component, UPCGGraphInterface* Graph);
	void HoldPlayer();
	void ReleasePlayer();
	void SetCoverHold(bool bHold);
	/** Tells the run subsystem that a real step of the build is done (the travel screen's bar). */
	void ReportLoadStep(EAreaLoadStep Step);
	void FitNavigationBounds();
	void HandlePCGGenerated(UPCGComponent* Component);

	EAreaGenerationState State = EAreaGenerationState::Idle;
	int32 Act = 0;
	int32 Seed = 0;
	int32 FirstSeed = 0;
	bool bTryNextSeeds = true;
	bool bRevalidateOnly = false;
	int64 LayoutHash = 0;
	float RouteLength = 0.f;
	float RouteLengthBeforeWayOut = 0.f;
	TArray<FBox> OpenEndBoxes;
	float TimeToReadySeconds = 0.f;
	double GenerationStartTime = 0.0;
	double PhaseStartTime = 0.0;
	int32 NavigationIdleTicks = 0;
	bool bNavigationRequested = false;
	bool bStructureDone = false;
	bool bBackdropDone = false;
	bool bCoverHeld = false;
	TWeakObjectPtr<APawn> HeldPawn;

	FAreaValidationResult LastValidation;

	/** The current plan's boxes, by EAreaPieceKind. */
	UPROPERTY(Transient)
	TArray<FAreaPieceList> PublishedPieces;

	/** Bounds the navmesh is fitted to (the walkable route, grown). */
	FBox NavigationBounds = FBox(ForceInit);
	FVector PlannedStart = FVector::ZeroVector;
	FRotator PlannedStartRotation = FRotator::ZeroRotator;
	FVector PlannedEntity = FVector::ZeroVector;
};
