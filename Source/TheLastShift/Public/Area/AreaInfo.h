// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "Area/AreaTypes.h"
#include "AreaInfo.generated.h"

/**
 * One per map, spawned by AAreaGenerator with each generated area: what the validator (tls.AreaValidate) reads back
 * about the layout. Segment bounds and types, the route centreline, the walkable floor rectangles, the seed, the
 * layout hash and the generation timings. No logic; it is generated (tagged) like the rest of the area.
 */
UCLASS(NotBlueprintable)
class THELASTSHIFT_API AAreaInfo : public AInfo
{
	GENERATED_BODY()

public:
	/** Tag carried by every actor an area generator spawns; Clear() removes exactly the actors with it. */
	static const FName GeneratedTag;

	/** The area info of World, or null. */
	static AAreaInfo* Find(const UWorld* World);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area")
	int32 Seed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area")
	int64 LayoutHash = 0;

	/** Items in the layout hash (generated actors, the geometry, start, entity, navmesh bounds, this info). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area")
	int32 GeneratedActorCount = 0;

	/** Act of the map (1-4, 0 = test recipe). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area")
	int32 Act = 0;

	/** Seeds tried until this one passed. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area")
	int32 SeedsTried = 0;

	/** Instances PCG built for the area. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area")
	int32 PCGInstanceCount = 0;

	/** Planning and spawning time of the attempt that passed. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area", meta = (Units = "s"))
	float GenerationSeconds = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area", meta = (Units = "s"))
	float PCGSeconds = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area", meta = (Units = "s"))
	float NavBuildSeconds = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area", meta = (Units = "s"))
	float ValidationSeconds = 0.f;

	/** From the start of the generation (all seeds tried) to ready. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area", meta = (Units = "s"))
	float TimeToReadySeconds = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area")
	TArray<FAreaSegmentRecord> Segments;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area")
	TArray<FAreaFloorRect> FloorRects;

	/** Route centreline from the PlayerStart to the exit, at floor height. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area")
	TArray<FVector> RoutePoints;

	/** Segment of the route piece that starts at RoutePoints[i]. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area")
	TArray<int32> RoutePointSegments;

	/** Floor heights the generator uses: walkable floors have their top at this Z. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Area", meta = (Units = "cm"))
	float FloorZ = 0.f;

	UFUNCTION(BlueprintPure, Category = "Area", meta = (Units = "cm"))
	float GetRouteLength() const;

	/** Point of the route centreline at Distance (clamped), and the segment it belongs to. */
	UFUNCTION(BlueprintPure, Category = "Area")
	FVector GetRoutePointAtDistance(float Distance, int32& OutSegment) const;

	/** Direction of the route at Distance (unit, horizontal). */
	UFUNCTION(BlueprintPure, Category = "Area")
	FVector GetRouteDirectionAtDistance(float Distance) const;

	/** Route distance of the centreline point nearest to Location (2D), its segment and the 2D distance to it. */
	UFUNCTION(BlueprintPure, Category = "Area", meta = (Units = "cm"))
	float ProjectToRoute(const FVector& Location, int32& OutSegment, float& OutOffset) const;

	/** Index of the floor rectangle under Location (2D, grown by Tolerance), or -1. */
	UFUNCTION(BlueprintPure, Category = "Area")
	int32 FindFloorRect(const FVector& Location, float Tolerance = 5.f) const;

	/** First segment of Type, or -1. */
	UFUNCTION(BlueprintPure, Category = "Area")
	int32 FindSegmentOfType(EAreaSegmentType Type) const;
};
