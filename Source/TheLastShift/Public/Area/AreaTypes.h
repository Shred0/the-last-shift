// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "AreaTypes.generated.h"

class UWorld;

THELASTSHIFT_API DECLARE_LOG_CATEGORY_EXTERN(LogArea, Log, All);

/** Kind of a piece of an area's route. Corridor, Room and Gate are closed (walls, ceiling); Catwalk and Hall are open (curbs over the void). */
UENUM(BlueprintType)
enum class EAreaSegmentType : uint8
{
	/** Closed, with bends, wall niches and optional dead-end side passages (SpurCount). */
	Corridor,
	/** Closed, with pillars. */
	Room,
	/** Open, over the void, with bends and optional spurs ending in detached platforms (SpurCount). */
	Catwalk,
	/** Open: a wide floor, or a web of catwalks between platforms (bWeb), with optional spurs ending in platforms. */
	Hall,
	/** Closed and straight: a door across the route, paid at a terminal placed far back along the route. */
	Gate
};

THELASTSHIFT_API const TCHAR* LexToString(EAreaSegmentType Type);

/** True for the closed segment types (Corridor, Room, Gate). */
THELASTSHIFT_API bool IsClosedAreaSegment(EAreaSegmentType Type);

/** One entry of an area recipe: a piece of the one-way route from the PlayerStart to the exit. */
USTRUCT(BlueprintType)
struct THELASTSHIFT_API FAreaSegmentSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area")
	EAreaSegmentType Type = EAreaSegmentType::Corridor;

	/** Length along the route (Hall: extent along the route). Snapped to the generator's cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area", meta = (ClampMin = "200.0", ClampMax = "60000.0", Units = "cm"))
	float LengthMin = 2000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area", meta = (ClampMin = "200.0", ClampMax = "60000.0", Units = "cm"))
	float LengthMax = 3000.f;

	/** Width across the route (Hall: extent across the route). Snapped to whole metres; ignored by Gate (the door's width). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area", meta = (ClampMin = "200.0", ClampMax = "60000.0", Units = "cm"))
	float WidthMin = 400.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area", meta = (ClampMin = "200.0", ClampMax = "60000.0", Units = "cm"))
	float WidthMax = 400.f;

	/** 90-degree bends (Corridor, Catwalk). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area", meta = (ClampMin = "0", ClampMax = "8"))
	int32 Bends = 0;

	/** Corridor: dead-end side passages. Catwalk, Hall: spurs ending in detached platforms. Others: unused. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area", meta = (ClampMin = "0", ClampMax = "8"))
	int32 SpurCount = 0;

	/** Hall only: a web of catwalks between platforms instead of one wide floor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area", meta = (EditCondition = "Type == EAreaSegmentType::Hall"))
	bool bWeb = true;

	/** Gate only: Normal-time seconds the door stays open once paid (the door's PoweredSeconds); 0 = it stays open once paid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area", meta = (ClampMin = "0.0", ClampMax = "120.0", Units = "s", EditCondition = "Type == EAreaSegmentType::Gate"))
	float PoweredSeconds = 6.f;
};

/** What an area is made of: the route's segments in order, what is placed along it, and the route length accepted. */
USTRUCT(BlueprintType)
struct THELASTSHIFT_API FAreaRecipe
{
	GENERATED_BODY()

	/** The route, in order, from the PlayerStart to the exit. The Gate entries are the area's gates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area")
	TArray<FAreaSegmentSpec> Segments;

	/** Residues in the area, the gates' regenerating residues included (one per gate). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area", meta = (ClampMin = "0", ClampMax = "32"))
	int32 ResidueCount = 6;

	/** Groups of lights paid at their own terminal, placed in closed segments. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area", meta = (ClampMin = "0", ClampMax = "4"))
	int32 LightGroupCount = 1;

	/**
	 * Log terminals (optional, paid once each), against a wall in closed corridors or rooms: the route is split into this
	 * many equal parts and each part gets one, each in its own segment. Placed on top of everything else (no other
	 * placement moves); a seed where one cannot be placed fails validation.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area", meta = (ClampMin = "0", ClampMax = "4"))
	int32 LogTerminalCount = 0;

	/** Twin AI terminals (optional), against a wall in the last closed segment of the route. 0 or 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area", meta = (ClampMin = "0", ClampMax = "1"))
	int32 TwinAITerminalCount = 0;

	/**
	 * Beacons (optional, paid at a terminal at the entrance of their space): one fixed light hung high above each Hall
	 * segment, in route order, aimed at where the route leaves it. Placed on top of everything else (no other placement
	 * moves); a seed where one cannot be placed, or a recipe asking for more than its Hall segments, fails validation.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area", meta = (ClampMin = "0", ClampMax = "4"))
	int32 BeaconCount = 0;

	/** Navigation path length from the PlayerStart to the exit that the validator accepts (rule 1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area", meta = (ClampMin = "0.0", ClampMax = "500000.0", Units = "cm"))
	float RouteLengthMin = 25000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area", meta = (ClampMin = "0.0", ClampMax = "500000.0", Units = "cm"))
	float RouteLengthMax = 55000.f;

	/**
	 * The way out (Brief 26): the route ends in one exit corridor, closed and straight, added by the planner after the
	 * recipe's last segment (after the twin AI's room when there is one) with no random draw, so that nothing else of the
	 * area moves. The travel starts at the area's exit, this far in from the corridor's mouth.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Way Out", meta = (ClampMin = "2.0", ClampMax = "50.0", Units = "m"))
	float ExitTriggerDepthM = 10.f;

	/** The exit corridor goes on past the exit for this long and ends open on the dark, with no wall at its end. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Way Out", meta = (ClampMin = "10.0", ClampMax = "200.0", Units = "m"))
	float ExitRunLengthM = 60.f;

	/**
	 * A room of the twin AI's own (Brief 26), on the route right before the exit corridor: entered on one side, the exit
	 * corridor's mouth in a side wall, the twin AI's terminal set into the wall facing the entrance. With
	 * TwinAITerminalCount 1 the terminal stands there instead of the last closed segment.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Area|Way Out")
	bool bTwinAIRoom = false;

	/** Number of Gate entries in Segments. */
	int32 GetGateCount() const;
};

/** A generated segment, as read back by the validator. */
USTRUCT(BlueprintType)
struct THELASTSHIFT_API FAreaSegmentRecord
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area")
	EAreaSegmentType Type = EAreaSegmentType::Corridor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area")
	bool bClosed = true;

	/** World bounds of the segment's floor (spurs, niches and platforms included). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area")
	FBox Bounds = FBox(ForceInit);

	/** Route distance (along the route centreline) where the segment starts and ends. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area", meta = (Units = "cm"))
	float RouteStart = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area", meta = (Units = "cm"))
	float RouteEnd = 0.f;

	/** Spurs, dead ends or platforms actually placed (the recipe asks for SpurCount). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area")
	int32 SpursPlaced = 0;

	/** Wall niches placed (closed corridors). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area")
	int32 NichesPlaced = 0;

	/** The area's exit corridor (Brief 26): the route's last segment, straight, open at its far end. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area")
	bool bExitCorridor = false;

	/** The twin AI's room (Brief 26), right before the exit corridor. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area")
	bool bTwinAIRoom = false;

	/** Exit corridor: the direction it runs in (unit, horizontal). Twin AI's room: the direction the route enters it in. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area")
	FVector Direction = FVector::ZeroVector;

	/** Exit corridor: the middle of its mouth, at floor height. Twin AI's room: the middle of its entrance. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area")
	FVector Mouth = FVector::ZeroVector;

	/** Exit corridor: its width. Twin AI's room: its side. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area", meta = (Units = "cm"))
	float Width = 0.f;
};

/** A walkable floor rectangle of the generated route (top at Z of the box's max). */
USTRUCT(BlueprintType)
struct THELASTSHIFT_API FAreaFloorRect
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area")
	FBox Box = FBox(ForceInit);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area")
	int32 Segment = -1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area")
	bool bOpen = false;
};

/** What the area rules need to know about the player pawn, read from the world's game mode default pawn (its one owner). */
USTRUCT(BlueprintType)
struct THELASTSHIFT_API FAreaPawnMetrics
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Area", meta = (Units = "cm/s"))
	float WalkSpeed = 600.f;

	UPROPERTY(BlueprintReadOnly, Category = "Area", meta = (Units = "cm"))
	float CapsuleHalfHeight = 88.f;

	UPROPERTY(BlueprintReadOnly, Category = "Area", meta = (Units = "cm"))
	float CapsuleRadius = 34.f;

	/** Eye above the capsule centre (BaseEyeHeight). */
	UPROPERTY(BlueprintReadOnly, Category = "Area", meta = (Units = "cm"))
	float EyeHeight = 64.f;

	UPROPERTY(BlueprintReadOnly, Category = "Area", meta = (Units = "cm"))
	float MaxStepHeight = 45.f;

	/** Name of the pawn class the values came from ("<defaults>" when none was found). */
	UPROPERTY(BlueprintReadOnly, Category = "Area")
	FString Source;

	/** Eye above the floor. */
	float GetEyeAboveFloor() const { return CapsuleHalfHeight + EyeHeight; }
};

/** Metrics of World's game mode default pawn (ACharacter); engine character defaults when there is none. */
THELASTSHIFT_API FAreaPawnMetrics GetAreaPawnMetrics(const UWorld* World);
