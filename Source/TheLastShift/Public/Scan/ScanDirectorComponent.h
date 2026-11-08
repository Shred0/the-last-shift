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
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDirectedScanReady);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDirectedBeat, int32, BeatIndex);

/**
 * Drives the owner's ULidarScanComponent from the time state and owns the two clocks of a stop:
 *
 * - Scans: FirstScanDelay after Stop the first scan fires by itself; every later scan comes from RequestScan(),
 *   which only succeeds once ScanCooldown has elapsed since the last scan (requests during the cooldown are
 *   dropped, not queued).
 * - Beat: every BeatInterval from the Stop, OnBeat is broadcast, whether or not the player scans.
 *
 * The two clocks are independent. On Resume every timer is cleared and the scans fade out over ResumeFadeOut.
 * This is the only game object that talks to the lidar plugin; game systems subscribe to the events here.
 * All clocks use world time, the same clock as UTimeStateSubsystem (the game never dilates time).
 */
UCLASS(ClassGroup = (TheLastShift), meta = (BlueprintSpawnableComponent))
class THELASTSHIFT_API UScanDirectorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UScanDirectorComponent();

	// --- Tuning ---

	/** Delay between Stop and the automatic first scan. Read at each Stop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scan|Tuning", meta = (ClampMin = "0.2", ClampMax = "3.0", Units = "s"))
	float FirstScanDelay = 1.0f;

	/** Minimum time between two scans, measured from the last scan (automatic or requested). Read at each scan. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scan|Tuning", meta = (ClampMin = "0.5", ClampMax = "15.0", Units = "s"))
	float ScanCooldown = 4.5f;

	/** Period of OnBeat, measured from the Stop. Read when each beat is armed, so a live edit applies from the beat after the pending one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scan|Tuning", meta = (ClampMin = "0.5", ClampMax = "15.0", Units = "s"))
	float BeatInterval = 4.5f;

	/** Fade-out applied to all scans on Resume. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scan|Tuning", meta = (ClampMin = "0.0", ClampMax = "2.0", Units = "s"))
	float ResumeFadeOut = 0.4f;

	// --- Actions ---

	/**
	 * Scans now if time is Stopped and the cooldown has elapsed, then restarts the cooldown. Returns true if a scan
	 * was issued. Returns false (and does nothing) during the cooldown, before the automatic first scan, and in
	 * Normal time.
	 */
	UFUNCTION(BlueprintCallable, Category = "Scan")
	bool RequestScan();

	// --- State ---

	/** True between a Stop and the following Resume. */
	UFUNCTION(BlueprintPure, Category = "Scan|State")
	bool IsScanCycleActive() const { return bCycleActive; }

	/** True when RequestScan() would issue a scan right now. */
	UFUNCTION(BlueprintPure, Category = "Scan|State")
	bool IsScanReady() const;

	/** Seconds until a scan can be requested; 0 when ready or in Normal time. Before the automatic first scan: time until that scan plus the cooldown after it. */
	UFUNCTION(BlueprintPure, Category = "Scan|State", meta = (Units = "s"))
	float GetCooldownRemaining() const;

	/** Cooldown progress: 0 = just scanned (or first scan pending), 1 = ready. 0 in Normal time. */
	UFUNCTION(BlueprintPure, Category = "Scan|State")
	float GetCooldownFraction() const;

	/** Beats broadcast since the current Stop; 0 in Normal time. */
	UFUNCTION(BlueprintPure, Category = "Scan|State")
	int32 GetBeatCount() const { return BeatCount; }

	/** Seconds until the next OnBeat; 0 in Normal time. */
	UFUNCTION(BlueprintPure, Category = "Scan|State", meta = (Units = "s"))
	float GetTimeToNextBeat() const;

	/** Scans issued since the current Stop (automatic and requested); 0 in Normal time. */
	UFUNCTION(BlueprintPure, Category = "Scan|State")
	int32 GetScansThisStop() const { return ScansThisStop; }

	/** True while any of the director's timers (first scan, cooldown, beat) is pending. False after Resume. */
	UFUNCTION(BlueprintPure, Category = "Scan|State")
	bool HasPendingTimers() const;

	UFUNCTION(BlueprintPure, Category = "Scan|State")
	ULidarScanComponent* GetLidarScan() const { return LidarScan; }

	// --- Events ---

	/** Re-broadcast of the lidar component's OnScanCompleted (every scan, automatic or requested). */
	UPROPERTY(BlueprintAssignable, Category = "Scan|Events")
	FOnDirectedScanCompleted OnScanCompleted;

	/** Fires once per cooldown, when a scan becomes available. Never fires in Normal time. */
	UPROPERTY(BlueprintAssignable, Category = "Scan|Events")
	FOnDirectedScanReady OnScanReady;

	/** Fires every BeatInterval from the Stop while Stopped. BeatIndex starts at 1 and equals GetBeatCount(). */
	UPROPERTY(BlueprintAssignable, Category = "Scan|Events")
	FOnDirectedBeat OnBeat;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void HandleTimeStateChanged(ETimeState NewState, ETimeState OldState);

	UFUNCTION()
	void HandleLidarScanCompleted(int32 ScanIndex, float Timestamp);

	void StartCycle();
	void StopCycle();

	void HandleFirstScanTimer();
	void HandleReadyTimer();
	void HandleBeatTimer();

	/** Issues the scan and restarts the cooldown from ScanTime. */
	void IssueScan(double ScanTime, bool bAutomatic);

	/** Broadcasts OnScanReady if it has not fired for the current cooldown yet. */
	void MarkReady();

	/** Arms a one-shot timer to fire at an absolute world time (next tick if already past). */
	void SetTimerAt(FTimerHandle& Handle, void (UScanDirectorComponent::*Callback)(), double TargetTime);

	double GetNow() const;
	float GetStopElapsed(double Time) const { return static_cast<float>(Time - StopStartTime); }

	UPROPERTY(Transient)
	TObjectPtr<ULidarScanComponent> LidarScan;

	UPROPERTY(Transient)
	TObjectPtr<UTimeStateSubsystem> TimeState;

	FTimerHandle FirstScanTimer;
	FTimerHandle ReadyTimer;
	FTimerHandle BeatTimer;

	bool bCycleActive = false;

	/** A scan has been issued in this stop, so ReadyTime is meaningful. */
	bool bHasScanned = false;

	/** OnScanReady already fired for the current cooldown. */
	bool bReadyBroadcast = false;

	/** World time of the current Stop. */
	double StopStartTime = 0.0;

	/** World time of the last scan and the duration of the cooldown that started with it. */
	double LastScanTime = 0.0;
	double CurrentCooldown = 0.0;

	/** Scheduled world time of the automatic first scan. */
	double FirstScanTime = 0.0;

	/** Scheduled world time of the next beat. Advanced from its own target, not from the late firing time, so beats don't drift. */
	double NextBeatTime = 0.0;

	int32 BeatCount = 0;
	int32 ScansThisStop = 0;
};
