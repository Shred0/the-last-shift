// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Time/TimeState.h"
#include "TimeStateSubsystem.generated.h"

THELASTSHIFT_API DECLARE_LOG_CATEGORY_EXTERN(LogTimeState, Log, All);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTimeStateChanged, ETimeState, NewState, ETimeState, OldState);

/**
 * Owns the time-stop state of the world. No time dilation is involved: game time keeps running while Stopped and
 * the systems that care about the state query it or listen to OnTimeStateChanged.
 */
UCLASS()
class THELASTSHIFT_API UTimeStateSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Normal -> Stopped. No-op with a warning when already Stopped. */
	UFUNCTION(BlueprintCallable, Category = "TimeState")
	void Stop();

	/** Stopped -> Normal. No-op with a warning when already Normal. */
	UFUNCTION(BlueprintCallable, Category = "TimeState")
	void Resume();

	UFUNCTION(BlueprintCallable, Category = "TimeState")
	void Toggle();

	UFUNCTION(BlueprintPure, Category = "TimeState")
	ETimeState GetState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "TimeState")
	bool IsStopped() const { return State == ETimeState::Stopped; }

	/** Seconds since the current Stop; 0 when Normal. */
	UFUNCTION(BlueprintPure, Category = "TimeState")
	float GetCurrentStopDuration() const;

	/** Cumulative stopped time in this world, including the current stop. */
	UFUNCTION(BlueprintPure, Category = "TimeState")
	float GetTotalStoppedSeconds() const;

	UPROPERTY(BlueprintAssignable, Category = "TimeState")
	FOnTimeStateChanged OnTimeStateChanged;

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	double GetNow() const;
	void SetState(ETimeState NewState);

	ETimeState State = ETimeState::Normal;

	/** World time at which the current stop began. */
	double StopStartTime = 0.0;

	/** Stopped time of all completed stops. */
	double CompletedStoppedSeconds = 0.0;
};
