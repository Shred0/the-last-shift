// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Time/TimeState.h"
#include "TimeStateSubsystem.generated.h"

THELASTSHIFT_API DECLARE_LOG_CATEGORY_EXTERN(LogTimeState, Log, All);

/**
 * Time state transition. Reason is why the Stop ended when OldState is Stopped; it is Resumed (and meaningless) on a
 * transition into Stopped.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnTimeStateChanged, ETimeState, NewState, ETimeState, OldState, EStopExitReason, Reason);

/**
 * Owns the time-stop state of the world and the stopped-time accounting (current stop, last stop, cumulative).
 * No time dilation is involved: game time keeps running while Stopped and the systems that care about the state query
 * it or listen to OnTimeStateChanged. All durations are world time, which the game never dilates.
 *
 * It is also the one place that knows the run is frozen by StdCore's pause menu (IsRunFrozen), in either time state,
 * and it owns the run's clock (GetRunSeconds), which stands still meanwhile. Every time-dependent system of the game
 * reads its time from GetRunSeconds and refuses actions while IsRunFrozen; none keeps a copy of the flag.
 */
UCLASS()
class THELASTSHIFT_API UTimeStateSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Normal -> Stopped. No-op with a warning when already Stopped. */
	UFUNCTION(BlueprintCallable, Category = "TimeState")
	void Stop();

	/** Stopped -> Normal, for the given reason. No-op with a warning when already Normal. */
	UFUNCTION(BlueprintCallable, Category = "TimeState")
	void Resume(EStopExitReason Reason = EStopExitReason::Resumed);

	/** The player's Stop/Resume: Stop, or Resume with reason Resumed. Refused while the run is frozen. */
	UFUNCTION(BlueprintCallable, Category = "TimeState")
	void Toggle();

	/**
	 * True while StdCore's pause menu is open (the local AStdPlayerController's IsPausedByMenu, read on every call): the
	 * run stands still in either time state and continues from where it was when the menu closes.
	 */
	UFUNCTION(BlueprintPure, Category = "TimeState")
	bool IsRunFrozen() const;

	/**
	 * The run's clock in seconds: world time, which does not advance while the menu pauses the world. Timers, holds and
	 * durations of the game are measured on it, so they stand still while the run is frozen.
	 */
	UFUNCTION(BlueprintPure, Category = "TimeState", meta = (Units = "s"))
	double GetRunSeconds() const;

	UFUNCTION(BlueprintPure, Category = "TimeState")
	ETimeState GetState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "TimeState")
	bool IsStopped() const { return State == ETimeState::Stopped; }

	/** Seconds since the current Stop; 0 when Normal. */
	UFUNCTION(BlueprintPure, Category = "TimeState", meta = (Units = "s"))
	float GetCurrentStopDuration() const;

	/** Duration of the last completed Stop; 0 before the first Resume. */
	UFUNCTION(BlueprintPure, Category = "TimeState", meta = (Units = "s"))
	float GetLastStopDuration() const { return static_cast<float>(LastStopDuration); }

	/** Reason the last completed Stop ended; Resumed before the first one. */
	UFUNCTION(BlueprintPure, Category = "TimeState")
	EStopExitReason GetLastExitReason() const { return LastExitReason; }

	/** Stopped time accumulated since the last reset (or since the world began), including the current stop. */
	UFUNCTION(BlueprintPure, Category = "TimeState", meta = (Units = "s"))
	float GetCumulativeStoppedSeconds() const;

	/** Restarts the cumulative count from zero; if time is Stopped, the current stop counts from now on. */
	UFUNCTION(BlueprintCallable, Category = "TimeState")
	void ResetCumulativeStoppedSeconds();

	UPROPERTY(BlueprintAssignable, Category = "TimeState")
	FOnTimeStateChanged OnTimeStateChanged;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	void SetState(ETimeState NewState, EStopExitReason Reason);

	/** Part of the current stop that counts towards the cumulative total (it starts later after a reset). */
	double GetCountedCurrentStop(double Now) const;

	ETimeState State = ETimeState::Normal;

	/** World time at which the current stop began. */
	double StopStartTime = 0.0;

	/** World time of the last ResetCumulativeStoppedSeconds. */
	double CumulativeResetTime = 0.0;

	/** Counted stopped time of all completed stops since the last reset. */
	double CompletedStoppedSeconds = 0.0;

	double LastStopDuration = 0.0;
	EStopExitReason LastExitReason = EStopExitReason::Resumed;
};
