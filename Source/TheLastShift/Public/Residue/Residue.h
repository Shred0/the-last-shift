// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LidarPointSource.h"
#include "Time/TimeState.h"
#include "Residue.generated.h"

class APawn;
class UScanDirectorComponent;
class UTimeStateSubsystem;

THELASTSHIFT_API DECLARE_LOG_CATEGORY_EXTERN(LogResidue, Log, All);

UENUM(BlueprintType)
enum class EResidueState : uint8
{
	/** In place: shows in scans and can be collected while Stopped. */
	Available,
	/** Collected during the current Stop: its energy is session energy, banked only by a voluntary Resume. */
	Collected,
	/** Its energy was banked: gone (a regenerating residue may come back). */
	Spent
};

inline const TCHAR* LexToString(EResidueState State)
{
	switch (State)
	{
	case EResidueState::Available: return TEXT("Available");
	case EResidueState::Collected: return TEXT("Collected");
	case EResidueState::Spent: return TEXT("Spent");
	}
	return TEXT("Unknown");
}

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnResidueCollectProgress, float, Fraction);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnResidueEvent);

/**
 * A tachyonic residue: no mesh, it exists only as a dense cluster of points injected into lidar scans (ILidarPointSource,
 * registered through the player's scan director), occluded by the captured depth like everything the lidar sees.
 *
 * Collected by holding Interact within CollectRadius for CollectSeconds while time is Stopped; releasing, moving out of
 * range or the Stop ending resets the progress. A collected residue adds EnergyValue session energy (UEnergySubsystem).
 * If the Stop ends with a voluntary Resume that energy is banked and the residue is spent; on any other exit the energy is
 * lost and the residue returns to its place. A regenerating residue (bRegenerates) that is spent comes back whenever
 * banked plus session energy is below RegenerateBelowEnergy, so a mandatory gate never becomes a dead end.
 */
UCLASS(Blueprintable)
class THELASTSHIFT_API AResidue : public AActor, public ILidarPointSource
{
	GENERATED_BODY()

public:
	AResidue();

	// --- Collection ---

	/** Distance from the pawn's view point (eyes) to the residue within which it can be collected. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Residue|Collection", meta = (ClampMin = "20.0", ClampMax = "1000.0", Units = "cm"))
	float CollectRadius = 120.f;

	/** Interact must be held this long, without interruption, to collect. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Residue|Collection", meta = (ClampMin = "0.1", ClampMax = "10.0", Units = "s"))
	float CollectSeconds = 2.5f;

	/** Energy one residue is worth. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Residue|Collection", meta = (ClampMin = "1", ClampMax = "10"))
	int32 EnergyValue = 1;

	/** Comes back once spent, whenever banked + session energy is below RegenerateBelowEnergy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Residue|Collection")
	bool bRegenerates = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Residue|Collection", meta = (ClampMin = "1", ClampMax = "99", EditCondition = "bRegenerates"))
	int32 RegenerateBelowEnergy = 1;

	// --- Render ---

	/** Points of the cluster. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Residue|Render", meta = (ClampMin = "1", ClampMax = "2000"))
	int32 PointCount = 150;

	/** Radius of the cluster; the points are densest at the centre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Residue|Render", meta = (ClampMin = "1.0", ClampMax = "500.0", Units = "cm"))
	float ClusterRadius = 25.f;

	/** Seed of the cluster's point set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Residue|Render")
	int32 ClusterSeed = 7;

	/** Brightness of residue points relative to captured points. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Residue|Render", meta = (ClampMin = "0.0", ClampMax = "16.0"))
	float IntensityMultiplier = 1.5f;

	/** How far behind the captured surface a residue point may be and still be drawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Residue|Render", meta = (ClampMin = "0.0", ClampMax = "200.0", Units = "cm"))
	float OcclusionTolerance = 10.f;

	// --- Actions ---

	/** Starts the hold for Collector. Fails (false) unless Available, time Stopped and Collector within CollectRadius. */
	UFUNCTION(BlueprintCallable, Category = "Residue")
	bool BeginCollect(APawn* Collector);

	/** Interact released: an unfinished hold resets. */
	UFUNCTION(BlueprintCallable, Category = "Residue")
	void EndCollect();

	// --- Queries ---

	UFUNCTION(BlueprintPure, Category = "Residue")
	EResidueState GetState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "Residue")
	bool IsCollecting() const { return Collector.IsValid(); }

	/** 0..1 of the current hold; 0 when not collecting. */
	UFUNCTION(BlueprintPure, Category = "Residue")
	float GetCollectProgress() const;

	UFUNCTION(BlueprintPure, Category = "Residue")
	bool IsInCollectRange(const APawn* Pawn) const;

	/** Colour of the residue's points and of its collect cue: ScanColors.Residue of the project settings (its one owner). */
	UFUNCTION(BlueprintPure, Category = "Residue")
	static FLinearColor GetResidueColor();

	/** Points returned by the last GatherLidarPoints (before the lidar's occlusion). */
	UFUNCTION(BlueprintPure, Category = "Residue")
	int32 GetLastGatheredPoints() const { return LastGatheredPoints; }

	/** The Available residue nearest to Pawn's view point within its CollectRadius, or null. */
	static AResidue* FindCollectable(const APawn* Pawn);

	// --- Events ---

	/** Every frame of a hold, and 0 when a hold resets. */
	UPROPERTY(BlueprintAssignable, Category = "Residue|Events")
	FOnResidueCollectProgress OnCollectProgress;

	UPROPERTY(BlueprintAssignable, Category = "Residue|Events")
	FOnResidueEvent OnCollected;

	/** Back in place: its energy was lost to a contact, or it regenerated. */
	UPROPERTY(BlueprintAssignable, Category = "Residue|Events")
	FOnResidueEvent OnReturned;

	// --- ILidarPointSource ---

	virtual void GatherLidarPoints_Implementation(const FLidarScanContext& Context, TArray<FLidarInjectedPoint>& OutPoints) override;
	virtual FLidarSourceSettings GetLidarSourceSettings_Implementation() const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	UFUNCTION()
	void HandleTimeStateChanged(ETimeState NewState, ETimeState OldState, EStopExitReason Reason);

	UFUNCTION()
	void HandleSessionBanked(int32 BankedAmount);

	UFUNCTION()
	void HandleSessionLost(int32 LostEnergy, EStopExitReason Reason);

	UFUNCTION()
	void HandleEnergyChanged(int32 BankedEnergy, int32 SessionEnergy);

	/** Finds the time state, the energy subsystem and the player's scan director; subscribes and registers. */
	void EnsureBound();
	void Unbind();

	void CancelCollect(const TCHAR* Why);
	void CompleteCollect();
	void Return(const TCHAR* Why);
	void CheckRegenerate(int32 TotalEnergy);
	void BuildCluster();
	FVector GetCollectorViewPoint(const APawn* Pawn) const;

	UPROPERTY(Transient)
	TObjectPtr<UTimeStateSubsystem> TimeState;

	TWeakObjectPtr<UScanDirectorComponent> Director;
	bool bEnergyBound = false;

	EResidueState State = EResidueState::Available;

	TWeakObjectPtr<APawn> Collector;
	double CollectStartTime = 0.0;

	/** Cluster offsets from the actor and the tunables they were built from. */
	TArray<FVector> Cluster;
	int32 ClusterBuiltCount = -1;
	float ClusterBuiltRadius = -1.f;
	int32 ClusterBuiltSeed = 0;

	int32 LastGatheredPoints = 0;
};
