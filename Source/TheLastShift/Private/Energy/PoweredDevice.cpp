// SPDX-License-Identifier: MIT

#include "Energy/PoweredDevice.h"

#include "Energy/EnergySubsystem.h"
#include "Engine/World.h"
#include "Time/TimeStateSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PoweredDevice)

namespace PoweredDeviceTuning
{
	// Mirror of the UPROPERTY clamp: Blueprint and C++ writes bypass ClampMin/ClampMax.
	static float Seconds(float V) { return FMath::Clamp(V, 0.f, 600.f); }
}

APoweredDevice::APoweredDevice()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	SetCanBeDamaged(false);
}

void APoweredDevice::BeginPlay()
{
	Super::BeginPlay();
	TimeState = GetWorld() ? GetWorld()->GetSubsystem<UTimeStateSubsystem>() : nullptr;
	if (TimeState)
	{
		TimeState->OnTimeStateChanged.AddUniqueDynamic(this, &APoweredDevice::HandleTimeStateChanged);
	}
	OnPowerStateChanged(false);
	ReceivePowerStateChanged(false);
}

void APoweredDevice::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (TimeState)
	{
		TimeState->OnTimeStateChanged.RemoveDynamic(this, &APoweredDevice::HandleTimeStateChanged);
		TimeState = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

double APoweredDevice::GetNow() const
{
	// The run's clock: it stands still while the pause menu freezes the run, so the powered time does too.
	if (TimeState)
	{
		return TimeState->GetRunSeconds();
	}
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

bool APoweredDevice::IsTimeStopped() const
{
	return TimeState && TimeState->IsStopped();
}

bool APoweredDevice::IsTimed() const
{
	return bPowered && PoweredDuration > 0.f;
}

float APoweredDevice::GetRemainingSeconds() const
{
	if (!IsTimed())
	{
		return 0.f;
	}
	const double Elapsed = IsTimeStopped() ? 0.0 : GetNow() - MarkTime;
	return static_cast<float>(FMath::Max(RemainingAtMark - Elapsed, 0.0));
}

float APoweredDevice::GetPoweredFraction() const
{
	if (!bPowered)
	{
		return 0.f;
	}
	return IsTimed() ? FMath::Clamp(GetRemainingSeconds() / PoweredDuration, 0.f, 1.f) : 1.f;
}

void APoweredDevice::PowerOn()
{
	PoweredDuration = PoweredDeviceTuning::Seconds(PoweredSeconds);
	RemainingAtMark = PoweredDuration;
	MarkTime = GetNow();
	SetActorTickEnabled(PoweredDuration > 0.f);
	SetPowered(true, TEXT("paid"));
}

void APoweredDevice::PowerOff()
{
	SetPowered(false, TEXT("switched off"));
}

void APoweredDevice::SetPowered(bool bNewPowered, const TCHAR* Why)
{
	const bool bWasPowered = bPowered;
	bPowered = bNewPowered;
	if (!bNewPowered)
	{
		RemainingAtMark = 0.0;
		PoweredDuration = 0.f;
		SetActorTickEnabled(false);
	}

	UE_LOG(LogEnergy, Log, TEXT("%s: %s (%s), %s."), *GetName(), bNewPowered ? TEXT("powered") : TEXT("unpowered"), Why,
		bNewPowered ? *FString::Printf(TEXT("PoweredSeconds %.2f"), PoweredDuration) : TEXT("off"));

	OnPowerStateChanged(bNewPowered);
	ReceivePowerStateChanged(bNewPowered);
	if (bNewPowered)
	{
		OnPowered.Broadcast();
	}
	else if (bWasPowered)
	{
		OnUnpowered.Broadcast();
	}
}

void APoweredDevice::HandleTimeStateChanged(ETimeState NewState, ETimeState OldState, EStopExitReason Reason)
{
	const double Now = GetNow();
	if (IsTimed())
	{
		if (NewState == ETimeState::Stopped)
		{
			// Freeze the timer at the Stop.
			RemainingAtMark = FMath::Max(RemainingAtMark - (Now - MarkTime), 0.0);
			UE_LOG(LogEnergy, Log, TEXT("%s: timer frozen at %.3f s."), *GetName(), RemainingAtMark);
		}
		// Normal again: the timer runs from now (also the mark of the Stop, unused while Stopped).
		MarkTime = Now;
	}

	if (bPowered && NewState == ETimeState::Normal && OldState == ETimeState::Stopped && bResetOnJump && IsJumpExit(Reason))
	{
		SetPowered(false, *FString::Printf(TEXT("time jump: %s"), LexToString(Reason)));
	}
	else if (IsTimed() && NewState == ETimeState::Normal)
	{
		UE_LOG(LogEnergy, Log, TEXT("%s: timer resumes at %.3f s (%s)."), *GetName(), RemainingAtMark, LexToString(Reason));
	}

	OnDeviceTimeStateChanged(NewState);
}

void APoweredDevice::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (IsTimed() && !IsTimeStopped() && !(TimeState && TimeState->IsRunFrozen()) && GetRemainingSeconds() <= 0.f)
	{
		SetPowered(false, TEXT("time ran out"));
	}
}
