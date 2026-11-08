// SPDX-License-Identifier: MIT

#include "Scan/ScanDirectorComponent.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "LidarScanComponent.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ScanDirectorComponent)

namespace ScanDirector
{
	/** Shortest delay handed to the timer manager; a target already in the past fires on the next tick. */
	static constexpr float MinTimerDelay = 0.0001f;

	// Mirrors of the UPROPERTY clamps: Blueprint and C++ writes bypass ClampMin/ClampMax.
	static float FirstScanDelay(float Value) { return FMath::Clamp(Value, 0.2f, 3.f); }
	static float Cooldown(float Value) { return FMath::Clamp(Value, 0.5f, 15.f); }
	static float Beat(float Value) { return FMath::Clamp(Value, 0.5f, 15.f); }
	static float FadeOut(float Value) { return FMath::Clamp(Value, 0.f, 2.f); }
}

UScanDirectorComponent::UScanDirectorComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UScanDirectorComponent::BeginPlay()
{
	Super::BeginPlay();

	LidarScan = GetOwner() ? GetOwner()->FindComponentByClass<ULidarScanComponent>() : nullptr;
	if (!LidarScan)
	{
		UE_LOG(LogTheLastShift, Warning, TEXT("%s: owner has no LidarScanComponent, scans disabled."), *GetPathName());
		return;
	}
	LidarScan->OnScanCompleted.AddDynamic(this, &UScanDirectorComponent::HandleLidarScanCompleted);

	TimeState = GetWorld() ? GetWorld()->GetSubsystem<UTimeStateSubsystem>() : nullptr;
	if (!TimeState)
	{
		UE_LOG(LogTheLastShift, Warning, TEXT("%s: no TimeStateSubsystem in this world, scans disabled."), *GetPathName());
		return;
	}
	TimeState->OnTimeStateChanged.AddDynamic(this, &UScanDirectorComponent::HandleTimeStateChanged);

	if (TimeState->IsStopped())
	{
		StartCycle();
	}
}

void UScanDirectorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		FTimerManager& Timers = World->GetTimerManager();
		Timers.ClearTimer(FirstScanTimer);
		Timers.ClearTimer(ReadyTimer);
		Timers.ClearTimer(BeatTimer);
	}
	bCycleActive = false;

	if (TimeState)
	{
		TimeState->OnTimeStateChanged.RemoveDynamic(this, &UScanDirectorComponent::HandleTimeStateChanged);
	}
	if (LidarScan)
	{
		LidarScan->OnScanCompleted.RemoveDynamic(this, &UScanDirectorComponent::HandleLidarScanCompleted);
	}

	Super::EndPlay(EndPlayReason);
}

double UScanDirectorComponent::GetNow() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

// --- Queries ---

bool UScanDirectorComponent::IsScanReady() const
{
	return bCycleActive && bHasScanned && GetNow() >= LastScanTime + CurrentCooldown;
}

float UScanDirectorComponent::GetCooldownRemaining() const
{
	if (!bCycleActive)
	{
		return 0.f;
	}
	const double Now = GetNow();
	if (!bHasScanned)
	{
		return static_cast<float>(FMath::Max(FirstScanTime - Now, 0.0) + ScanDirector::Cooldown(ScanCooldown));
	}
	return static_cast<float>(FMath::Max(LastScanTime + CurrentCooldown - Now, 0.0));
}

float UScanDirectorComponent::GetCooldownFraction() const
{
	if (!bCycleActive || !bHasScanned || CurrentCooldown <= 0.0)
	{
		return 0.f;
	}
	return static_cast<float>(FMath::Clamp((GetNow() - LastScanTime) / CurrentCooldown, 0.0, 1.0));
}

float UScanDirectorComponent::GetTimeToNextBeat() const
{
	return bCycleActive ? static_cast<float>(FMath::Max(NextBeatTime - GetNow(), 0.0)) : 0.f;
}

bool UScanDirectorComponent::HasPendingTimers() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	const FTimerManager& Timers = World->GetTimerManager();
	return Timers.IsTimerActive(FirstScanTimer) || Timers.IsTimerActive(ReadyTimer) || Timers.IsTimerActive(BeatTimer);
}

// --- Actions ---

bool UScanDirectorComponent::RequestScan()
{
	if (!bCycleActive || !LidarScan)
	{
		// Normal time: scanning is simply not available, not worth a log line.
		return false;
	}

	const double Now = GetNow();
	if (!IsScanReady())
	{
		UE_LOG(LogTheLastShift, Verbose, TEXT("ScanDirector: scan request at %.3f s into stop ignored (%s, %.3f s left)."),
			GetStopElapsed(Now), bHasScanned ? TEXT("cooldown") : TEXT("first scan pending"), GetCooldownRemaining());
		return false;
	}

	// The cooldown may have elapsed earlier in this frame than the ready timer fires (input ticks before timers):
	// keep OnScanReady strictly before the scan it enables.
	MarkReady();

	UE_LOG(LogTheLastShift, Log, TEXT("ScanDirector: scan request at %.3f s into stop accepted."), GetStopElapsed(Now));
	IssueScan(Now, false);
	return true;
}

// --- Cycle ---

void UScanDirectorComponent::HandleTimeStateChanged(ETimeState NewState, ETimeState OldState)
{
	if (NewState == ETimeState::Stopped)
	{
		StartCycle();
	}
	else
	{
		StopCycle();
	}
}

void UScanDirectorComponent::StartCycle()
{
	if (!GetWorld() || !LidarScan)
	{
		return;
	}

	bCycleActive = true;
	bHasScanned = false;
	bReadyBroadcast = false;
	BeatCount = 0;
	ScansThisStop = 0;

	StopStartTime = GetNow();
	FirstScanTime = StopStartTime + ScanDirector::FirstScanDelay(FirstScanDelay);
	NextBeatTime = StopStartTime + ScanDirector::Beat(BeatInterval);

	SetTimerAt(FirstScanTimer, &UScanDirectorComponent::HandleFirstScanTimer, FirstScanTime);
	SetTimerAt(BeatTimer, &UScanDirectorComponent::HandleBeatTimer, NextBeatTime);
}

void UScanDirectorComponent::StopCycle()
{
	if (UWorld* World = GetWorld())
	{
		FTimerManager& Timers = World->GetTimerManager();
		Timers.ClearTimer(FirstScanTimer);
		Timers.ClearTimer(ReadyTimer);
		Timers.ClearTimer(BeatTimer);
	}

	const bool bWasActive = bCycleActive;
	bCycleActive = false;
	bHasScanned = false;
	bReadyBroadcast = false;
	BeatCount = 0;
	ScansThisStop = 0;

	if (LidarScan)
	{
		LidarScan->ClearScans(ScanDirector::FadeOut(ResumeFadeOut));
	}
	if (bWasActive)
	{
		UE_LOG(LogTheLastShift, Log, TEXT("ScanDirector: cycle stopped, timers cleared."));
	}
}

void UScanDirectorComponent::HandleFirstScanTimer()
{
	if (!bCycleActive)
	{
		return;
	}
	// The cooldown starts from the scheduled time, so the cadence does not depend on frame timing.
	UE_LOG(LogTheLastShift, Log, TEXT("ScanDirector: automatic first scan at %.3f s into stop."), GetStopElapsed(GetNow()));
	IssueScan(FirstScanTime, true);
}

void UScanDirectorComponent::HandleReadyTimer()
{
	if (bCycleActive)
	{
		MarkReady();
	}
}

void UScanDirectorComponent::HandleBeatTimer()
{
	if (!bCycleActive)
	{
		return;
	}

	const double Now = GetNow();
	const double Target = NextBeatTime;
	++BeatCount;

	// Arm the next beat before broadcasting: a listener may Resume, which clears it again.
	NextBeatTime = Target + ScanDirector::Beat(BeatInterval);
	SetTimerAt(BeatTimer, &UScanDirectorComponent::HandleBeatTimer, NextBeatTime);

	UE_LOG(LogTheLastShift, Log, TEXT("ScanDirector: beat %d at %.3f s into stop (scheduled %.3f, late %.1f ms)."),
		BeatCount, GetStopElapsed(Now), GetStopElapsed(Target), (Now - Target) * 1000.0);
	OnBeat.Broadcast(BeatCount);
}

void UScanDirectorComponent::IssueScan(double ScanTime, bool bAutomatic)
{
	if (!LidarScan)
	{
		return;
	}

	bHasScanned = true;
	bReadyBroadcast = false;
	LastScanTime = ScanTime;
	CurrentCooldown = ScanDirector::Cooldown(ScanCooldown);
	++ScansThisStop;

	SetTimerAt(ReadyTimer, &UScanDirectorComponent::HandleReadyTimer, LastScanTime + CurrentCooldown);

	UE_LOG(LogTheLastShift, Log, TEXT("ScanDirector: %s scan %d issued (cooldown %.2f s, ready at %.3f s into stop)."),
		bAutomatic ? TEXT("automatic") : TEXT("requested"), LidarScan->GetScanCount(), CurrentCooldown,
		GetStopElapsed(LastScanTime + CurrentCooldown));
	LidarScan->Scan();
}

void UScanDirectorComponent::MarkReady()
{
	if (bReadyBroadcast)
	{
		return;
	}
	bReadyBroadcast = true;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReadyTimer);
	}

	UE_LOG(LogTheLastShift, Log, TEXT("ScanDirector: scan ready at %.3f s into stop."), GetStopElapsed(GetNow()));
	OnScanReady.Broadcast();
}

void UScanDirectorComponent::SetTimerAt(FTimerHandle& Handle, void (UScanDirectorComponent::*Callback)(), double TargetTime)
{
	if (UWorld* World = GetWorld())
	{
		const float Delay = FMath::Max(static_cast<float>(TargetTime - GetNow()), ScanDirector::MinTimerDelay);
		World->GetTimerManager().SetTimer(Handle, this, Callback, Delay, false);
	}
}

void UScanDirectorComponent::HandleLidarScanCompleted(int32 ScanIndex, float Timestamp)
{
	UE_LOG(LogTheLastShift, Log, TEXT("ScanDirector: scan %d completed at t=%.3f."), ScanIndex, Timestamp);
	OnScanCompleted.Broadcast(ScanIndex, Timestamp);
}
