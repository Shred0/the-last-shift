// SPDX-License-Identifier: MIT

#include "Epilogue/ChoiceTerminal.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Run/RunSceneSubsystem.h"
#include "Run/TheLastShiftSettings.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ChoiceTerminal)

AChoiceTerminal::AChoiceTerminal()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bModelledBody = true;
	Cost = 0;
	InteractRadius = 180.f;
}

void AChoiceTerminal::BeginPlay()
{
	Super::BeginPlay();
	const UWorld* World = GetWorld();
	TimeState = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
	bOffering = bHolding = bChosen = false;
	HoldSeconds = 0.0;
	LastRunSeconds = -1.0;
}

void AChoiceTerminal::SetOffering(bool bInOffering)
{
	if (bOffering != bInOffering)
	{
		UE_LOG(LogTheLastShift, Log, TEXT("%s (%s): %s."), *GetName(), *Ending.ToString(), bInOffering ? TEXT("may offer its choice") : TEXT("offers nothing any more"));
	}
	bOffering = bInOffering;
	if (!bOffering && bHolding)
	{
		CancelHold(TEXT("no longer offering"));
	}
}

bool AChoiceTerminal::OffersChoice() const
{
	if (!bOffering || bChosen || (TimeState && (TimeState->IsStopped() || TimeState->IsRunFrozen())))
	{
		return false;
	}
	if (Ending == TEXT("Open"))
	{
		// Only to an engineer who has heard all of what the twin AI said; nothing tells the others it exists.
		const URunSceneSubsystem* Scenes = URunSceneSubsystem::Get(this);
		return Scenes && Scenes->IsOpenEndingOffered();
	}
	return true;
}

FText AChoiceTerminal::GetChoiceLabel() const
{
	return URunSceneSubsystem::GetEndingLabel(Ending);
}

float AChoiceTerminal::GetHoldFraction() const
{
	if (bChosen)
	{
		return 1.f;
	}
	return bHolding ? FMath::Clamp(static_cast<float>(HoldSeconds / UTheLastShiftSettings::Get()->GetChoiceHoldSeconds()), 0.f, 1.f) : 0.f;
}

bool AChoiceTerminal::BeginHold(APawn* Pawn)
{
	if (bHolding || !OffersChoice() || !IsInRange(Pawn))
	{
		return false;
	}
	bHolding = true;
	HoldPawn = Pawn;
	HoldSeconds = 0.0;
	LastRunSeconds = TimeState ? TimeState->GetRunSeconds() : -1.0;
	++HoldsStarted;
	UE_LOG(LogTheLastShift, Log, TEXT("%s (%s): hold started (%.2f s)."), *GetName(), *Ending.ToString(), UTheLastShiftSettings::Get()->GetChoiceHoldSeconds());
	OnHoldProgress.Broadcast(0.f);
	return true;
}

void AChoiceTerminal::EndHold()
{
	if (bHolding)
	{
		CancelHold(TEXT("Interact released"));
	}
}

void AChoiceTerminal::CancelHold(const TCHAR* Why)
{
	bHolding = false;
	HoldPawn.Reset();
	++HoldsCancelled;
	UE_LOG(LogTheLastShift, Log, TEXT("%s (%s): hold cancelled at %.2f s (%s)."), *GetName(), *Ending.ToString(), HoldSeconds, Why);
	HoldSeconds = 0.0;
	OnHoldProgress.Broadcast(0.f);
}

void AChoiceTerminal::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bHolding)
	{
		return;
	}
	const double Now = TimeState ? TimeState->GetRunSeconds() : (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0);
	const double Step = LastRunSeconds >= 0.0 ? FMath::Max(Now - LastRunSeconds, 0.0) : 0.0;
	LastRunSeconds = Now;
	// Behind the pause menu the hold stands still (the run's clock does too).
	if (TimeState && TimeState->IsRunFrozen())
	{
		return;
	}
	if (TimeState && TimeState->IsStopped())
	{
		CancelHold(TEXT("time stopped"));
		return;
	}
	if (!HoldPawn.IsValid() || !IsInRange(HoldPawn.Get()) || !OffersChoice())
	{
		CancelHold(TEXT("out of reach or no longer offered"));
		return;
	}
	HoldSeconds += Step;
	const float Seconds = UTheLastShiftSettings::Get()->GetChoiceHoldSeconds();
	OnHoldProgress.Broadcast(FMath::Clamp(static_cast<float>(HoldSeconds / Seconds), 0.f, 1.f));
	if (HoldSeconds >= Seconds)
	{
		bHolding = false;
		bChosen = true;
		HoldPawn.Reset();
		UE_LOG(LogTheLastShift, Log, TEXT("%s (%s): held through (%.2f s): the choice is made."), *GetName(), *Ending.ToString(), HoldSeconds);
		// The terminal accepts what the player did: its sound, as a free activation (no spending comment).
		OnActivated.Broadcast(0);
		OnAnyActivated.Broadcast(this, 0);
		OnChoiceMade.Broadcast(this);
	}
}
