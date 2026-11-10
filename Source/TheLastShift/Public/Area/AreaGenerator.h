// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Area/AreaTypes.h"
#include "Area/AreaValidator.h"
#include "AreaGenerator.generated.h"

class ADoorDevice;
class AEntity;
class ALightGroupDevice;
class ANavMeshBoundsVolume;
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
 * with the same seed (StructureGraph: floors, walls, curbs, pillars, ceilings; BackdropGraph: the far masses); (4) spawn the gameplay actors in their slots (residues,
 * gates with door, terminal and regenerating residue, light groups, exit, area info) and move the map's PlayerStart
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

	// --- Classes and assets ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Classes")
	TSubclassOf<AResidue> ResidueClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Classes")
	TSubclassOf<APowerTerminal> TerminalClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Classes")
	TSubclassOf<ADoorDevice> DoorClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Classes")
	TSubclassOf<ALightGroupDevice> LightGroupClass;

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

	/** Hash of the generated layout (plan, gameplay actors, start and entity poses); 0 before the first ready. */
	UFUNCTION(BlueprintPure, Category = "Area")
	int64 GetLayoutHash() const { return LayoutHash; }

	/** Route centreline length of the current plan. */
	UFUNCTION(BlueprintPure, Category = "Area", meta = (Units = "cm"))
	float GetRouteLength() const { return RouteLength; }

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
