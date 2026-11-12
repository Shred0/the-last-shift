// SPDX-License-Identifier: MIT

#include "Time/TimeStateSubsystem.h"

#include "Engine/World.h"
#include "Framework/StdPlayerController.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TimeStateSubsystem)

DEFINE_LOG_CATEGORY(LogTimeState);

bool UTimeStateSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

double UTimeStateSubsystem::GetRunSeconds() const
{
	// World time does not advance while the world is paused, and StdCore's pause menu pauses it (Brief 7, Part 0).
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

bool UTimeStateSubsystem::IsRunFrozen() const
{
	const UWorld* World = GetWorld();
	const AStdPlayerController* PC = World ? Cast<AStdPlayerController>(World->GetFirstPlayerController()) : nullptr;
	return PC && PC->IsPausedByMenu();
}

void UTimeStateSubsystem::Stop()
{
	if (State == ETimeState::Stopped)
	{
		UE_LOG(LogTimeState, Warning, TEXT("Stop ignored: time is already stopped."));
		return;
	}

	StopStartTime = GetRunSeconds();
	UE_LOG(LogTimeState, Log, TEXT("Stop at t=%.3f (cumulative stopped so far %.3f s)."), StopStartTime, CompletedStoppedSeconds);
	SetState(ETimeState::Stopped, EStopExitReason::Resumed);
}

void UTimeStateSubsystem::Resume(EStopExitReason Reason)
{
	if (State == ETimeState::Normal)
	{
		UE_LOG(LogTimeState, Warning, TEXT("Resume ignored: time is not stopped."));
		return;
	}

	const double Now = GetRunSeconds();
	LastStopDuration = FMath::Max(Now - StopStartTime, 0.0);
	LastExitReason = Reason;
	CompletedStoppedSeconds += GetCountedCurrentStop(Now);
	UE_LOG(LogTimeState, Log, TEXT("Resume (%s) at t=%.3f after %.3f s stopped (cumulative %.3f s)."),
		LexToString(Reason), Now, LastStopDuration, CompletedStoppedSeconds);
	SetState(ETimeState::Normal, Reason);
}

void UTimeStateSubsystem::Toggle()
{
	if (IsRunFrozen())
	{
		UE_LOG(LogTimeState, Log, TEXT("Stop/Resume ignored: the run is frozen by the pause menu."));
		return;
	}
	if (IsStopped())
	{
		Resume(EStopExitReason::Resumed);
	}
	else
	{
		Stop();
	}
}

float UTimeStateSubsystem::GetCurrentStopDuration() const
{
	return IsStopped() ? static_cast<float>(FMath::Max(GetRunSeconds() - StopStartTime, 0.0)) : 0.f;
}

double UTimeStateSubsystem::GetCountedCurrentStop(double Now) const
{
	return IsStopped() ? FMath::Max(Now - FMath::Max(StopStartTime, CumulativeResetTime), 0.0) : 0.0;
}

float UTimeStateSubsystem::GetCumulativeStoppedSeconds() const
{
	return static_cast<float>(CompletedStoppedSeconds + GetCountedCurrentStop(GetRunSeconds()));
}

void UTimeStateSubsystem::ResetCumulativeStoppedSeconds()
{
	CompletedStoppedSeconds = 0.0;
	CumulativeResetTime = GetRunSeconds();
	UE_LOG(LogTimeState, Log, TEXT("Cumulative stopped time reset at t=%.3f."), CumulativeResetTime);
}

void UTimeStateSubsystem::SetState(ETimeState NewState, EStopExitReason Reason)
{
	const ETimeState OldState = State;
	State = NewState;
	OnTimeStateChanged.Broadcast(NewState, OldState, Reason);
}
