// SPDX-License-Identifier: MIT

#include "Time/TimeStateSubsystem.h"

#include "Engine/World.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TimeStateSubsystem)

DEFINE_LOG_CATEGORY(LogTimeState);

bool UTimeStateSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

double UTimeStateSubsystem::GetNow() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

void UTimeStateSubsystem::Stop()
{
	if (State == ETimeState::Stopped)
	{
		UE_LOG(LogTimeState, Warning, TEXT("Stop ignored: time is already stopped."));
		return;
	}

	StopStartTime = GetNow();
	UE_LOG(LogTimeState, Log, TEXT("Stop at t=%.3f (total stopped so far %.3f s)."), StopStartTime, CompletedStoppedSeconds);
	SetState(ETimeState::Stopped);
}

void UTimeStateSubsystem::Resume()
{
	if (State == ETimeState::Normal)
	{
		UE_LOG(LogTimeState, Warning, TEXT("Resume ignored: time is not stopped."));
		return;
	}

	const double Now = GetNow();
	const double Duration = FMath::Max(Now - StopStartTime, 0.0);
	CompletedStoppedSeconds += Duration;
	UE_LOG(LogTimeState, Log, TEXT("Resume at t=%.3f after %.3f s stopped (total %.3f s)."), Now, Duration, CompletedStoppedSeconds);
	SetState(ETimeState::Normal);
}

void UTimeStateSubsystem::Toggle()
{
	if (IsStopped())
	{
		Resume();
	}
	else
	{
		Stop();
	}
}

float UTimeStateSubsystem::GetCurrentStopDuration() const
{
	return IsStopped() ? static_cast<float>(FMath::Max(GetNow() - StopStartTime, 0.0)) : 0.f;
}

float UTimeStateSubsystem::GetTotalStoppedSeconds() const
{
	return static_cast<float>(CompletedStoppedSeconds) + GetCurrentStopDuration();
}

void UTimeStateSubsystem::SetState(ETimeState NewState)
{
	const ETimeState OldState = State;
	State = NewState;
	OnTimeStateChanged.Broadcast(NewState, OldState);
}
