// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/TimerHandle.h"
#include "Time/TimeState.h"
#include "ScanDirectorComponent.generated.h"

class ULidarScanComponent;
class UTimeStateSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDirectedScanCompleted, int32, ScanIndex, float, Timestamp);

/**
 * Drives the owner's ULidarScanComponent from the time state: while Stopped it fires a first scan after
 * FirstScanDelay and then one every ScanInterval; on Resume it stops and fades out every scan.
 * Game systems subscribe to OnScanCompleted here, never to the plugin component directly.
 */
UCLASS(ClassGroup = (TheLastShift), meta = (BlueprintSpawnableComponent))
class THELASTSHIFT_API UScanDirectorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UScanDirectorComponent();

	/** Delay between Stop and the first scan. Read at each Stop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scan", meta = (ClampMin = "0.2", ClampMax = "3.0", Units = "s"))
	float FirstScanDelay = 1.0f;

	/** Time between consecutive scans while Stopped. Read before every scan, so live edits apply immediately. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scan", meta = (ClampMin = "1.0", ClampMax = "10.0", Units = "s"))
	float ScanInterval = 3.5f;

	/** Fade-out applied to all scans on Resume. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scan", meta = (ClampMin = "0.0", ClampMax = "2.0", Units = "s"))
	float ResumeFadeOut = 0.4f;

	/** Re-broadcast of the lidar component's OnScanCompleted (every scan, automatic or manual). */
	UPROPERTY(BlueprintAssignable, Category = "Scan")
	FOnDirectedScanCompleted OnScanCompleted;

	UFUNCTION(BlueprintPure, Category = "Scan")
	ULidarScanComponent* GetLidarScan() const { return LidarScan; }

	/** True while a scan timer is pending (i.e. the automatic cadence is running). */
	UFUNCTION(BlueprintPure, Category = "Scan")
	bool IsScanCycleActive() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void HandleTimeStateChanged(ETimeState NewState, ETimeState OldState);

	UFUNCTION()
	void HandleLidarScanCompleted(int32 ScanIndex, float Timestamp);

	void StartScanCycle();
	void StopScanCycle();
	void FireScan();

	UPROPERTY(Transient)
	TObjectPtr<ULidarScanComponent> LidarScan;

	UPROPERTY(Transient)
	TObjectPtr<UTimeStateSubsystem> TimeState;

	FTimerHandle ScanTimer;
};
