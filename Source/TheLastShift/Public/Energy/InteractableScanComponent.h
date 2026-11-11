// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "LidarPointSource.h"
#include "InteractableScanComponent.generated.h"

class UPrimitiveComponent;
class UScanDirectorComponent;

/**
 * Shows something the player can use (a terminal, a closed door) in scans, in the interactable colour
 * (ScanColors.Interactable of the project settings, its one owner).
 *
 * An injected point source (ILidarPointSource) registered through the player's scan director, like residues and the
 * entity. At every scan it outlines the faces of each target's bounding box that face the capture and lie in its
 * coverage, then fills them with a jittered grid, both at SpacingFactor times the capture's own sample spacing at that
 * depth (so the interactable reads denser than the wall it stands against), within MaxPointsPerScan (OutlineShare of it
 * for the outlines; the fill spacing widens to fit the rest). The lidar drops every point
 * behind the captured depth (bOccludeByCapture): only surfaces the scan saw are drawn, nothing through walls. The points
 * follow their scan's age fade and go with it, like captured points.
 *
 * A hidden target (an open door's panel) gives no points. The points sit on the target's bounding box, so targets are
 * box-shaped meshes. Owners add their targets in their constructor.
 */
UCLASS(ClassGroup = (TheLastShift), meta = (BlueprintSpawnableComponent))
class THELASTSHIFT_API UInteractableScanComponent : public UActorComponent, public ILidarPointSource
{
	GENERATED_BODY()

public:
	UInteractableScanComponent();

	/**
	 * Point spacing as a fraction of the capture's sample spacing at the same depth (after the lidar's near thinning):
	 * 0.5 lays twice as many points per axis as the capture puts on the same surface.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interactable|Scan", meta = (ClampMin = "0.2", ClampMax = "2.0"))
	float SpacingFactor = 0.5f;

	/** Closest spacing between two points. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interactable|Scan", meta = (ClampMin = "1.0", ClampMax = "50.0", Units = "cm"))
	float MinSpacing = 2.f;

	/**
	 * Most points one scan takes from this component; the spacing widens evenly to stay under it. All sources share the
	 * lidar's MaxInjectedPointsPerScan (the entity included), so this stays small.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interactable|Scan", meta = (ClampMin = "0", ClampMax = "4096"))
	int32 MaxPointsPerScan = 500;

	/**
	 * Share of MaxPointsPerScan spent on the outline of each visible face (its four edges, at the fine spacing) before
	 * the fill. Captured points cover a large surface seen up close more densely than this budget can, so the outline
	 * is what keeps a door's shape in the interactable colour; small or far targets get their outline and a dense fill.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interactable|Scan", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float OutlineShare = 0.5f;

	/** Random offset of each point inside its grid cell, as a fraction of the spacing, so the grid does not alias. Seeded per scan. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interactable|Scan", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float Jitter = 0.35f;

	/** Faces seen at a grazing angle (cosine of the angle to the capture ray below this) give no points. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interactable|Scan", meta = (ClampMin = "0.0", ClampMax = "0.9"))
	float MinFacingCosine = 0.05f;

	/** Brightness of the points relative to captured points. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interactable|Scan", meta = (ClampMin = "0.0", ClampMax = "16.0"))
	float IntensityMultiplier = 1.f;

	/** How far behind the captured surface a point may be and still be drawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interactable|Scan", meta = (ClampMin = "0.0", ClampMax = "200.0", Units = "cm"))
	float OcclusionTolerance = 10.f;

	/** Adds a primitive whose surfaces are shown (ignored if null or already there). */
	UFUNCTION(BlueprintCallable, Category = "Interactable|Scan")
	void AddTarget(UPrimitiveComponent* Target);

	UFUNCTION(BlueprintPure, Category = "Interactable|Scan")
	TArray<UPrimitiveComponent*> GetTargets() const;

	/** Points returned by the last GatherLidarPoints (before the lidar's coverage cut, cap and occlusion). */
	UFUNCTION(BlueprintPure, Category = "Interactable|Scan")
	int32 GetLastGatheredPoints() const { return LastGatheredPoints; }

	/** Scan index of the last GatherLidarPoints, -1 before the first. */
	UFUNCTION(BlueprintPure, Category = "Interactable|Scan")
	int32 GetLastGatheredScan() const { return LastGatheredScan; }

	/** The scan director this component is registered with, or null. */
	UScanDirectorComponent* GetDirector() const { return Director.Get(); }

	// --- ILidarPointSource ---

	virtual void GatherLidarPoints_Implementation(const FLidarScanContext& Context, TArray<FLidarInjectedPoint>& OutPoints) override;
	virtual FLidarSourceSettings GetLidarSourceSettings_Implementation() const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Finds the player's scan director and registers there (again if the pawn changed). */
	void EnsureRegistered();
	void Unregister();

	UPROPERTY(Transient)
	TArray<TObjectPtr<UPrimitiveComponent>> Targets;

	TWeakObjectPtr<UScanDirectorComponent> Director;

	int32 LastGatheredPoints = 0;
	int32 LastGatheredScan = INDEX_NONE;
};
