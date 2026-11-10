// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Area/AreaTypes.h"
#include "AreaGenerator.generated.h"

class ADoorDevice;
class AEntity;
class ALightGroupDevice;
class APowerTerminal;
class AResidue;
class UMaterialInterface;
class UStaticMesh;

/**
 * Editor-only actor that lays out a whole area from a recipe (its properties) and a seed, and bakes it into the map:
 * Generate() spawns the geometry, the residues, the gates (door + terminal + regenerating residue), the light groups,
 * the entity, the PlayerStart, the exit and a NavMeshBoundsVolume fitted to the route, then builds the navmesh. The
 * spawned actors are saved with the map; nothing is generated at runtime. Same recipe and seed, same area.
 *
 * Every spawned actor carries AAreaInfo::GeneratedTag, so Clear() removes exactly what Generate() made. Never move a
 * generated actor by hand: change the recipe or the seed and regenerate. The generation code is compiled only in
 * editor builds and the actor is stripped from cooked maps.
 *
 * Layout: an axis-aligned grid of half-metre cells. The route is a one-way chain of segments heading +X, turning +Y/-Y
 * but never back; closed segments get walls and a ceiling, open segments get a curb on every edge (taller than the
 * pawn's step, so the player cannot fall); all walkable floors are at Z = 0. Segments never touch except where they
 * join, keeping SegmentClearance from each other, so the route has no shortcut.
 */
UCLASS(Blueprintable)
class THELASTSHIFT_API AAreaGenerator : public AActor
{
	GENERATED_BODY()

public:
	AAreaGenerator();

	// --- Recipe ---

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Recipe", meta = (ClampMin = "0", ClampMax = "999999"))
	int32 Seed = 1;

	/** The route, in order, from the PlayerStart to the exit. The Gate entries are the area's gates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Recipe")
	TArray<FAreaSegmentSpec> Segments;

	/** Residues in the area, the gates' regenerating residues included (one per gate). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Recipe", meta = (ClampMin = "0", ClampMax = "32"))
	int32 ResidueCount = 6;

	/** Groups of lights paid at their own terminal, placed in closed segments. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Recipe", meta = (ClampMin = "0", ClampMax = "4"))
	int32 LightGroupCount = 1;

	/** Navigation path length from the PlayerStart to the exit that the validator accepts (rule 1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Recipe", meta = (ClampMin = "0.0", ClampMax = "500000.0", Units = "cm"))
	float RouteLengthMin = 25000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Recipe", meta = (ClampMin = "0.0", ClampMax = "500000.0", Units = "cm"))
	float RouteLengthMax = 55000.f;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Classes")
	TSubclassOf<AEntity> EntityClass;

	/** Block mesh of every piece of geometry (the engine's 100 cm cube). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Classes")
	TObjectPtr<UStaticMesh> BlockMesh;

	/** Material of every piece of geometry (the greybox material). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Classes")
	TObjectPtr<UMaterialInterface> GreyboxMaterial;

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

	/** Number of Gate entries in Segments (the area's gate count). */
	UFUNCTION(BlueprintPure, Category = "Area")
	int32 GetGateCount() const;

#if WITH_EDITOR
	/** Clears the previous generation and bakes the area for Seed. Logs the result; see LastError on failure. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Area")
	void Generate();

	/** Removes every actor this level's generation made (AAreaInfo::GeneratedTag). */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "Area")
	void Clear();

	/** Generate() that reports: false and OutError when the recipe cannot be laid out for Seed. */
	bool GenerateArea(FString& OutError, bool bBuildNavigation = true);

	/** Removes the generated actors; returns how many. */
	int32 ClearArea();

	/** Synchronous navmesh build of this world; returns the seconds it took (-1 when there is no navigation system). */
	float BuildNavigation();
#endif

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area|State")
	FString LastError;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area|State")
	int64 LastLayoutHash = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area|State")
	int32 LastActorCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area|State", meta = (Units = "s"))
	float LastGenerationSeconds = 0.f;
};
