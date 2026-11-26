// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Area/AreaTypes.h"
#include "GameFramework/Actor.h"
#include "LidarPointSource.h"
#include "EntityWall.generated.h"

class UBoxComponent;

/** Brief 30: one strand of a wall, in the opening's plane: U across (from the centre), V up (from the floor). */
struct FEntityWallStrand
{
	FVector2D Start = FVector2D::ZeroVector;
	FVector2D End = FVector2D::ZeroVector;
	/** Sagitta of its arc in the plane (cm, signed: which side it bows to); 0 = straight. */
	double Sagitta = 0.0;
	int32 Points = 0;

	/** The point at T (0..1) along the strand: a circular arc through Start and End, Sagitta off the chord at its middle. */
	FVector2D At(double T) const;
};

/** Brief 30: a passage of the area that may hold one of the entity's walls, and why it may not when it never can. */
struct FEntityWallCandidate
{
	/** Index in AAreaGenerator::GetPassages(). */
	int32 Passage = INDEX_NONE;
	FAreaPassage Where;
	/** Never a candidate: in or at the mouth of the exit corridor, at a gate's door, or within WallClearOfDevices of a terminal. */
	bool bExcluded = false;
	FString Why;
};

/** Brief 30: what the last choice of walls did (tls.Entity.Walls, tls.WallCheck). */
struct FEntityWallReport
{
	/** The Stop it was made for (AEntity counts its Stops), and what asked for it. */
	int32 StopSerial = 0;
	FString Why;
	int32 Wanted = 0;
	int32 Raised = 0;
	/** Candidates of the area, and those not excluded. */
	int32 Candidates = 0;
	int32 Usable = 0;
	/** Usable candidates closed off by a limit: on the way to a regenerating residue of a gate ahead; within WallMinDistanceM. */
	int32 Protected = 0;
	int32 TooNear = 0;
	/** Within WallRangeM (and not too near): the random walls' pool before the way-out check. */
	int32 InRange = 0;
	/** Walls left out because they would have closed every way out of the player's segment. */
	int32 RejectedWayOut = 0;
	int32 PlayerSegment = -1;
	float PlayerRouteDistance = 0.f;
	int32 WaysOut = 0;
	/** The strategic wall's target (none: no residue or terminal left), whether it lay ahead, its passage (INDEX_NONE: not raised) and why not. */
	FString StrategicTarget;
	FVector StrategicTargetLocation = FVector::ZeroVector;
	bool bStrategicAhead = false;
	int32 StrategicPassage = INDEX_NONE;
	int32 StrategicCrossed = 0;
	FString StrategicWhy;
	/** The candidates walled, strategic first, and each one's path distance from the player. */
	TArray<int32> Passages;
	TArray<float> PathDistances;
};

/**
 * Brief 30: one of the entity's walls, raised across a passage for one Stop and destroyed when it ends. Its look is a set of
 * strands of points, injected into scans by the entity (AEntity::GatherLidarPoints): never rendered. While it stands it
 * blocks the player's pawn across the whole opening, on the pawn's channel only: it blocks no trace, no scan's capture, no
 * other object, and it never affects the navigation mesh, so the entity's path through it is the path without it.
 */
UCLASS(NotBlueprintable, Transient)
class THELASTSHIFT_API AEntityWall : public AActor
{
	GENERATED_BODY()

public:
	AEntityWall();

	/**
	 * Lays the wall across Passage: the opening HalfWidth either way and Height up from its floor, the blocking volume
	 * BlockDepth thick, Strands strands of Points points in all, bowed by Bow of their length, Thickness thick, drawn from Rng.
	 */
	void Setup(int32 InPassageIndex, const FAreaPassage& InPassage, float InHeight, float InBlockDepth, int32 InStrands, float InBow, float InThickness, int32 InPoints,
		bool bInStrategic, FRandomStream& Rng);

	/** The wall's points for one scan, jittered anew for every scan (as the trail's), all inside its opening. */
	void BuildPoints(int32 ScanIndex, TArray<FLidarInjectedPoint>& OutPoints) const;

	/** True when Point lies inside the opening: across within HalfWidth, from the floor to Height, within Thickness of its plane (each grown by Tolerance). */
	bool IsInsideOpening(const FVector& Point, float Tolerance = 0.f) const;

	/** Distance of Point from the opening's plane along the passage's way through (signed). */
	double GetSignedDistance(const FVector& Point) const;

	int32 GetPassageIndex() const { return PassageIndex; }
	const FAreaPassage& GetPassage() const { return Passage; }
	float GetHeight() const { return Height; }
	float GetThickness() const { return Thickness; }
	int32 GetPointCount() const { return PointCount; }
	bool IsStrategic() const { return bStrategic; }
	const TArray<FEntityWallStrand>& GetStrands() const { return Strands; }
	UBoxComponent* GetVolume() const { return Volume; }

private:
	FVector ToWorld(const FVector2D& UV, double Depth) const;

	UPROPERTY(VisibleAnywhere, Category = "Entity")
	TObjectPtr<UBoxComponent> Volume;

	int32 PassageIndex = INDEX_NONE;
	FAreaPassage Passage;
	float Height = 400.f;
	float Thickness = 4.f;
	int32 PointCount = 0;
	bool bStrategic = false;
	TArray<FEntityWallStrand> Strands;
};
