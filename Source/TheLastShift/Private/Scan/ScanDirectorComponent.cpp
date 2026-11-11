// SPDX-License-Identifier: MIT

#include "Scan/ScanDirectorComponent.h"

#include "Engine/World.h"
#include "Entity/Entity.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "LidarScanComponent.h"
#include "Residue/Residue.h"
#include "Run/TheLastShiftSettings.h"
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

	/** Point source priorities (higher is served first when the lidar's injected-point cap is reached). */
	static constexpr int32 EntityPriority = 2;
	static constexpr int32 ResiduePriority = 1;
	static constexpr int32 OtherPriority = 0;
}

FOnScanDirectorLifecycle UScanDirectorComponent::OnDirectorBeganPlay;
FOnScanDirectorLifecycle UScanDirectorComponent::OnDirectorEndingPlay;

UScanDirectorComponent::UScanDirectorComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UScanDirectorComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!ResolveLidarScan())
	{
		UE_LOG(LogTheLastShift, Warning, TEXT("%s: owner has no LidarScanComponent, scans disabled."), *GetPathName());
		return;
	}
	LidarScan->OnScanCompleted.AddDynamic(this, &UScanDirectorComponent::HandleLidarScanCompleted);
	ApplyScanColor();
	// Channels were just switched on: idle scan slots get their channel targets now, not at their first scan.
	LidarScan->ApplyLiveSettings();

	TimeState = GetWorld() ? GetWorld()->GetSubsystem<UTimeStateSubsystem>() : nullptr;
	if (!TimeState)
	{
		UE_LOG(LogTheLastShift, Warning, TEXT("%s: no TimeStateSubsystem in this world, scans disabled."), *GetPathName());
		return;
	}
	TimeState->OnTimeStateChanged.AddDynamic(this, &UScanDirectorComponent::HandleTimeStateChanged);

	if (TimeState->IsStopped())
	{
		RecordStopAnchor();
		StartCycle();
	}
	OnDirectorBeganPlay.Broadcast(this);
}

ULidarScanComponent* UScanDirectorComponent::ResolveLidarScan()
{
	// Lazy, so sources can register before this component's BeginPlay.
	if (!LidarScan && GetOwner())
	{
		LidarScan = GetOwner()->FindComponentByClass<ULidarScanComponent>();
	}
	return LidarScan;
}

void UScanDirectorComponent::RegisterPointSource(TScriptInterface<ILidarPointSource> Source)
{
	if (ULidarScanComponent* Lidar = ResolveLidarScan())
	{
		const int32 Priority = GetPointSourcePriority(Source.GetObject());
		Lidar->RegisterPointSource(Source, Priority);
		UE_LOG(LogTheLastShift, Log, TEXT("ScanDirector: point source %s registered at priority %d (%d sources)."),
			*GetNameSafe(Source.GetObject()), Priority, Lidar->GetPointSourceCount());
	}
	else
	{
		UE_LOG(LogTheLastShift, Warning, TEXT("ScanDirector: cannot register %s, owner has no LidarScanComponent."), *GetNameSafe(Source.GetObject()));
	}
}

int32 UScanDirectorComponent::GetPointSourcePriority(const UObject* Source)
{
	if (Cast<AEntity>(Source))
	{
		return ScanDirector::EntityPriority;
	}
	if (Cast<AResidue>(Source))
	{
		return ScanDirector::ResiduePriority;
	}
	return ScanDirector::OtherPriority;
}

#if !UE_BUILD_SHIPPING
bool UScanDirectorComponent::bChannelsOffForTest = false;

void UScanDirectorComponent::RegisterPointSourceAtPriorityForTest(TScriptInterface<ILidarPointSource> Source, int32 Priority)
{
	if (ULidarScanComponent* Lidar = ResolveLidarScan())
	{
		Lidar->RegisterPointSource(Source, Priority);
		UE_LOG(LogTheLastShift, Log, TEXT("ScanDirector: test point source %s registered at priority %d (%d sources)."),
			*GetNameSafe(Source.GetObject()), Priority, Lidar->GetPointSourceCount());
	}
}
#endif

void UScanDirectorComponent::UnregisterPointSource(TScriptInterface<ILidarPointSource> Source)
{
	if (ULidarScanComponent* Lidar = ResolveLidarScan())
	{
		Lidar->UnregisterPointSource(Source);
	}
}

TArray<FLidarScanContext> UScanDirectorComponent::GetRecentScanOrigins() const
{
	return LidarScan ? LidarScan->GetAliveScans() : TArray<FLidarScanContext>();
}

void UScanDirectorComponent::RecordStopAnchor()
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	StopAnchor = Owner->GetActorTransform();
	StopAnchorViewRotation = Owner->GetActorRotation();
	if (const APawn* Pawn = Cast<APawn>(Owner))
	{
		if (const AController* Controller = Pawn->GetController())
		{
			StopAnchorViewRotation = Controller->GetControlRotation();
		}
	}
	bHasStopAnchor = true;
}

bool UScanDirectorComponent::Rewind()
{
	return ForceExit(EStopExitReason::Rewound);
}

bool UScanDirectorComponent::ForceExit(EStopExitReason Reason)
{
	if (!bCycleActive || !TimeState || !TimeState->IsStopped())
	{
		return false;
	}

	// Jumped resumes in place: no teleport. Every exit restores the flashlight and clears the scans through the
	// time state change, as on a voluntary Resume.
	if (AActor* Owner = GetOwner(); Owner && bHasStopAnchor && IsRewindExit(Reason))
	{
		Owner->SetActorLocationAndRotation(StopAnchor.GetLocation(), StopAnchor.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);
		if (APawn* Pawn = Cast<APawn>(Owner))
		{
			if (UPawnMovementComponent* Movement = Pawn->GetMovementComponent())
			{
				Movement->StopMovementImmediately();
			}
			if (AController* Controller = Pawn->GetController())
			{
				Controller->SetControlRotation(StopAnchorViewRotation);
			}
		}
		UE_LOG(LogTheLastShift, Log, TEXT("ScanDirector: rewind to %s (view %s)."), *StopAnchor.GetLocation().ToString(), *StopAnchorViewRotation.ToString());
	}

	TimeState->Resume(Reason);
	return true;
}

void UScanDirectorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	OnDirectorEndingPlay.Broadcast(this);

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

void UScanDirectorComponent::HandleTimeStateChanged(ETimeState NewState, ETimeState OldState, EStopExitReason Reason)
{
	if (NewState == ETimeState::Stopped)
	{
		RecordStopAnchor();
		StartCycle();
		return;
	}

	StopCycle();
	if (OldState == ETimeState::Stopped)
	{
		const float StoppedSeconds = TimeState ? TimeState->GetLastStopDuration() : 0.f;
		UE_LOG(LogTheLastShift, Log, TEXT("ScanDirector: stop exited (%s) after %.3f s."),
			LexToString(Reason), StoppedSeconds);
		OnStopExited.Broadcast(Reason, StoppedSeconds);
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

void UScanDirectorComponent::ApplyScanColor()
{
	if (LidarScan)
	{
		const FScanColors& Colors = UTheLastShiftSettings::GetScanColors();
		LidarScan->PointColor = Colors.Scan;
#if !UE_BUILD_SHIPPING
		LidarScan->bUseChannels = !bChannelsOffForTest;
#else
		LidarScan->bUseChannels = true;
#endif
		LidarScan->SetChannel(UTheLastShiftSettings::GetInteractableScanChannel(), Colors.Interactable, 1.f);
	}
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

	ApplyScanColor();
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
