// SPDX-License-Identifier: MIT

#include "Scan/ScanDirectorComponent.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "LidarScanComponent.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"
#include "TimerManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ScanDirectorComponent)

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
		UE_LOG(LogTheLastShift, Warning, TEXT("%s: no TimeStateSubsystem in this world, automatic scans disabled."), *GetPathName());
		return;
	}
	TimeState->OnTimeStateChanged.AddDynamic(this, &UScanDirectorComponent::HandleTimeStateChanged);

	if (TimeState->IsStopped())
	{
		StartScanCycle();
	}
}

void UScanDirectorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ScanTimer);
	}
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

bool UScanDirectorComponent::IsScanCycleActive() const
{
	const UWorld* World = GetWorld();
	return World && World->GetTimerManager().IsTimerActive(ScanTimer);
}

void UScanDirectorComponent::HandleTimeStateChanged(ETimeState NewState, ETimeState OldState)
{
	if (NewState == ETimeState::Stopped)
	{
		StartScanCycle();
	}
	else
	{
		StopScanCycle();
	}
}

void UScanDirectorComponent::StartScanCycle()
{
	UWorld* World = GetWorld();
	if (!World || !LidarScan)
	{
		return;
	}

	World->GetTimerManager().SetTimer(ScanTimer, this, &UScanDirectorComponent::FireScan, FMath::Clamp(FirstScanDelay, 0.2f, 3.f), false);
}

void UScanDirectorComponent::StopScanCycle()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ScanTimer);
	}
	if (LidarScan)
	{
		LidarScan->ClearScans(FMath::Clamp(ResumeFadeOut, 0.f, 2.f));
	}
}

void UScanDirectorComponent::FireScan()
{
	UWorld* World = GetWorld();
	if (!World || !LidarScan)
	{
		return;
	}

	// Re-armed one-shot rather than a looping timer, so ScanInterval edits apply from the next scan on.
	World->GetTimerManager().SetTimer(ScanTimer, this, &UScanDirectorComponent::FireScan, FMath::Clamp(ScanInterval, 1.f, 10.f), false);

	UE_LOG(LogTheLastShift, Log, TEXT("ScanDirector: scan %d issued at t=%.3f (%.3f s into stop)."),
		LidarScan->GetScanCount(), World->GetTimeSeconds(), TimeState ? TimeState->GetCurrentStopDuration() : 0.f);
	LidarScan->Scan();
}

void UScanDirectorComponent::HandleLidarScanCompleted(int32 ScanIndex, float Timestamp)
{
	UE_LOG(LogTheLastShift, Log, TEXT("ScanDirector: scan %d completed at t=%.3f."), ScanIndex, Timestamp);
	OnScanCompleted.Broadcast(ScanIndex, Timestamp);
}
