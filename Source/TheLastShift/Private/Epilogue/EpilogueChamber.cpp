// SPDX-License-Identifier: MIT

#include "Epilogue/EpilogueChamber.h"

#include "Components/LightComponent.h"
#include "Energy/DoorDevice.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Epilogue/ChamberBattery.h"
#include "Epilogue/ChoiceTerminal.h"
#include "Flow/StdFlowSubsystem.h"
#include "Prologue/PrologueSlidingPanel.h"
#include "Run/TheLastShiftSettings.h"
#include "UObject/UObjectIterator.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Run/HomeAIComponent.h"
#include "Run/RunSubsystem.h"
#include "TextChannel/TextChannelComponent.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(EpilogueChamber)

namespace EpilogueChamberImpl
{
	static const TCHAR* LexToString(EEpilogueChamberState State)
	{
		switch (State)
		{
		case EEpilogueChamberState::Waiting: return TEXT("Waiting");
		case EEpilogueChamberState::Lock: return TEXT("Lock");
		case EEpilogueChamberState::Lines: return TEXT("Lines");
		case EEpilogueChamberState::Offering: return TEXT("Offering");
		case EEpilogueChamberState::Aftermath: return TEXT("Aftermath");
		case EEpilogueChamberState::Hold: return TEXT("Hold");
		case EEpilogueChamberState::Ended: return TEXT("Ended");
		}
		return TEXT("Unknown");
	}

	static APawn* PawnOf(const UWorld* World)
	{
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		return PC ? PC->GetPawn() : nullptr;
	}

	/** The travel screen covers the view (StdCore's level transition under way). */
	static bool IsTravelling(const UWorld* World)
	{
		const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
		const UStdFlowSubsystem* Flow = GI ? GI->GetSubsystem<UStdFlowSubsystem>() : nullptr;
		return Flow && Flow->IsTransitioning();
	}

	/** Smooth from 0 to 1. */
	static float Ease(float X)
	{
		X = FMath::Clamp(X, 0.f, 1.f);
		return X * X * (3.f - 2.f * X);
	}
}

AEpilogueChamber::AEpilogueChamber()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

AEpilogueChamber* AEpilogueChamber::Find(const UWorld* World)
{
	if (World)
	{
		for (TActorIterator<AEpilogueChamber> It(const_cast<UWorld*>(World)); It; ++It)
		{
			return *It;
		}
	}
	return nullptr;
}

void AEpilogueChamber::BeginPlay()
{
	Super::BeginPlay();
	const UWorld* World = GetWorld();
	TimeState = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
	State = EEpilogueChamberState::Waiting;
	LastRow = Chosen = NAME_None;
	bLastSeen = bTravelAsked = bDoorOpened = false;
	LastRunSeconds = LinesStartRun = OfferedRun = ChosenRun = EndingRun = HoldStartRun = -1.0;
	LockStartRun = LockOpeningRun = LockOpenRun = -1.0;
	LockElapsed = AftermathElapsed = 0.0;
	LightShare = 1.f;
	// The hall's lights at their level now: Open's aftermath brings them down from there.
	Lights.Reset();
	for (TObjectIterator<ULightComponent> It; It; ++It)
	{
		// The hall's own lights: not the pawn's flashlight.
		if (It->GetWorld() == World && It->IsRegistered() && !Cast<APawn>(It->GetOwner()))
		{
			FHallLight L;
			L.Light = *It;
			L.Intensity = It->Intensity;
			Lights.Add(L);
		}
	}
	if (LockPanel)
	{
		LockPanel->SetOpenAtOnce(0.f);
	}
	if (Terminal)
	{
		Terminal->SetOffering(false);
		Terminal->OnChoiceMade.AddUniqueDynamic(this, &AEpilogueChamber::HandleChoiceMade);
	}
	UE_LOG(LogTheLastShift, Log, TEXT("%s: the battery's hall: its terminal %s, the lock's door %s and panel %s, the battery %s, %d lights (run on its way home: %d)."), *GetName(),
		*GetNameSafe(Terminal), *GetNameSafe(EntryDoor), *GetNameSafe(LockPanel), *GetNameSafe(Battery), Lights.Num(),
		URunSubsystem::Get(this) && URunSubsystem::Get(this)->IsReturningHome() ? 1 : 0);
}

void AEpilogueChamber::SetState(EEpilogueChamberState NewState)
{
	if (State == NewState)
	{
		return;
	}
	State = NewState;
	UE_LOG(LogTheLastShift, Log, TEXT("%s: %s (run clock %.3f s)."), *GetName(), EpilogueChamberImpl::LexToString(NewState), TimeState ? TimeState->GetRunSeconds() : -1.0);
	OnStateChanged.Broadcast(NewState);
}

void AEpilogueChamber::StartLines(double Now)
{
	const APawn* Pawn = EpilogueChamberImpl::PawnOf(GetWorld());
	UHomeAIComponent* AI = Pawn ? Pawn->FindComponentByClass<UHomeAIComponent>() : nullptr;
	const UTextChannelComponent* Channel = Pawn ? Pawn->FindComponentByClass<UTextChannelComponent>() : nullptr;
	LinesStartRun = Now;
	const TArray<FName> Rows = UHomeAIComponent::GetChamberRows();
	LastRow = Rows.Num() > 0 ? Rows.Last() : NAME_None;
	const int32 Queued = AI ? AI->SayChamberLines() : 0;
	SetState(EEpilogueChamberState::Lines);
	UE_LOG(LogTheLastShift, Log, TEXT("%s: the chamber's lines: %d rows, %d queued, the last %s."), *GetName(), Rows.Num(), Queued, *LastRow.ToString());
	// The home AI pushes within SayChamberLines: nothing in the channel now means nothing was said.
	if (LastRow.IsNone() || !Channel || !Channel->IsBusy())
	{
		Offer(Now, LastRow.IsNone() ? TEXT("the chamber has no row") : TEXT("no line was said"));
	}
}

void AEpilogueChamber::Offer(double Now, const TCHAR* Why)
{
	OfferedRun = Now;
	UE_LOG(LogTheLastShift, Log, TEXT("%s: the terminal offers the choice (%s, %.2f s of the run's clock after the lines began)."), *GetName(), Why, Now - LinesStartRun);
	SetState(EEpilogueChamberState::Offering);
	if (Terminal)
	{
		Terminal->SetOffering(true);
	}
}

void AEpilogueChamber::HandleChoiceMade(AChoiceTerminal* InTerminal, FName Ending)
{
	if (State != EEpilogueChamberState::Offering || !InTerminal || InTerminal != Terminal || Ending.IsNone())
	{
		return;
	}
	Chosen = Ending;
	ChosenRun = TimeState ? TimeState->GetRunSeconds() : -1.0;
	// Nothing in the room answers once a choice is made; the hall answers the choice first (the aftermath).
	Terminal->SetOffering(false);
	AftermathElapsed = AftermathStill = 0.0;
	HoldStartRun = -1.0;
	UE_LOG(LogTheLastShift, Log, TEXT("%s: %s chosen at %s: the aftermath, %.2f s."), *GetName(), *Chosen.ToString(), *InTerminal->GetName(),
		UTheLastShiftSettings::Get()->GetChoiceAftermathSeconds());
	if (Chosen == TEXT("Open"))
	{
		// The structure lets go: the lock's panel stays open and the door behind it opens.
		if (LockPanel && !LockPanel->IsFullyOpen())
		{
			LockPanel->SetOpenFraction(1.f);
		}
		if (EntryDoor && !EntryDoor->IsPowered())
		{
			EntryDoor->PowerOn();
			bDoorOpened = true;
		}
	}
	SetState(EEpilogueChamberState::Aftermath);
}

float AEpilogueChamber::GetAftermathAlpha() const
{
	if (State == EEpilogueChamberState::Hold || State == EEpilogueChamberState::Ended)
	{
		return 1.f;
	}
	if (State != EEpilogueChamberState::Aftermath)
	{
		return 0.f;
	}
	const float Seconds = UTheLastShiftSettings::Get()->GetChoiceAftermathSeconds();
	return Seconds <= 0.f ? 1.f : FMath::Clamp(static_cast<float>(AftermathElapsed) / Seconds, 0.f, 1.f);
}

void AEpilogueChamber::ApplyLightShare(float Share)
{
	LightShare = Share;
	for (const FHallLight& L : Lights)
	{
		if (ULightComponent* Light = L.Light.Get())
		{
			Light->SetIntensity(L.Intensity * Share);
		}
	}
}

void AEpilogueChamber::TickLock(double Now, double Delta, bool bStill)
{
	const UTheLastShiftSettings* Settings = UTheLastShiftSettings::Get();
	if (!LockPanel)
	{
		StartLines(Now);
		return;
	}
	if (!(TimeState && TimeState->IsStopped()))
	{
		LockElapsed += Delta;
	}
	const float Delay = Settings->GetLockOpenDelaySeconds();
	if (LockElapsed < Delay)
	{
		return;
	}
	if (LockOpeningRun < 0.0)
	{
		LockOpeningRun = Now;
		UE_LOG(LogTheLastShift, Log, TEXT("%s: the lock's panel opens (%.2f s of the run's clock after the travel)."), *GetName(), Now - LockStartRun);
	}
	const float Fraction = FMath::Clamp(static_cast<float>((LockElapsed - Delay) / Settings->GetLockOpenSeconds()), 0.f, 1.f);
	LockPanel->SetOpenFraction(Fraction);
	if (Fraction >= 1.f)
	{
		LockOpenRun = Now;
		UE_LOG(LogTheLastShift, Log, TEXT("%s: the lock's panel is open (%.2f s after it began)."), *GetName(), Now - LockOpeningRun);
		StartLines(Now);
	}
}

void AEpilogueChamber::TickAftermath(double Now, double Delta, bool bStill)
{
	const UTheLastShiftSettings* Settings = UTheLastShiftSettings::Get();
	// The run's clock stands still behind the pause menu by itself; a Stop holds the aftermath too.
	if (TimeState && TimeState->IsStopped())
	{
		AftermathStill += Delta;
	}
	AftermathElapsed = FMath::Max(0.0, Now - ChosenRun - AftermathStill);
	const float Aftermath = Settings->GetChoiceAftermathSeconds();
	if (State == EEpilogueChamberState::Aftermath)
	{
		if (Chosen == TEXT("Open"))
		{
			ApplyLightShare(FMath::Lerp(1.f, Settings->GetAftermathLightShare(), EpilogueChamberImpl::Ease(GetAftermathAlpha())));
		}
		if (AftermathElapsed < Aftermath)
		{
			return;
		}
		// Every part of the answer at its end state: the hall holds still as it is now.
		if (Chosen == TEXT("Open"))
		{
			ApplyLightShare(Settings->GetAftermathLightShare());
		}
		HoldStartRun = Now;
		UE_LOG(LogTheLastShift, Log, TEXT("%s: the aftermath of %s is over (%.2f s of the run's clock after the choice): the hall holds still for %.2f s."), *GetName(),
			*Chosen.ToString(), Now - ChosenRun, Settings->GetAftermathHoldSeconds());
		SetState(EEpilogueChamberState::Hold);
	}
	// The hold: nothing changes and nothing answers; Stopped time does not count, as in the aftermath.
	if (State == EEpilogueChamberState::Hold && AftermathElapsed >= Aftermath + Settings->GetAftermathHoldSeconds())
	{
		UE_LOG(LogTheLastShift, Log, TEXT("%s: the hold after %s is over (%.2f s of the run's clock after the choice)."), *GetName(), *Chosen.ToString(), Now - ChosenRun);
		SetState(EEpilogueChamberState::Ended);
	}
}

void AEpilogueChamber::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const UWorld* World = GetWorld();
	const double Now = TimeState ? TimeState->GetRunSeconds() : (World ? World->GetTimeSeconds() : 0.0);
	const double Delta = LastRunSeconds >= 0.0 ? FMath::Max(Now - LastRunSeconds, 0.0) : 0.0;
	LastRunSeconds = Now;
	const bool bStill = TimeState && (TimeState->IsStopped() || TimeState->IsRunFrozen());

	switch (State)
	{
	case EEpilogueChamberState::Waiting:
	{
		const APawn* Pawn = EpilogueChamberImpl::PawnOf(World);
		if (Pawn && Pawn->FindComponentByClass<UTextChannelComponent>() && !EpilogueChamberImpl::IsTravelling(World))
		{
			// Brief 36B: the player in the lock, the travel screen gone: the lock's panel opens by itself, then the lines.
			LockStartRun = Now;
			LockElapsed = 0.0;
			SetState(EEpilogueChamberState::Lock);
			TickLock(Now, 0.0, bStill);
		}
		break;
	}
	case EEpilogueChamberState::Lock:
		TickLock(Now, Delta, bStill);
		break;
	case EEpilogueChamberState::Lines:
	{
		const APawn* Pawn = EpilogueChamberImpl::PawnOf(World);
		const UTextChannelComponent* Channel = Pawn ? Pawn->FindComponentByClass<UTextChannelComponent>() : nullptr;
		if (!Channel)
		{
			Offer(Now, TEXT("no text channel"));
			break;
		}
		bLastSeen |= Channel->GetCurrentRowName() == LastRow;
		// The last row shown, or gone from the queue without being shown, and everything before it typed out.
		if ((bLastSeen || !Channel->IsQueued(LastRow)) && Channel->IsTypedOut())
		{
			Offer(Now, *FString::Printf(TEXT("%s typed out"), *LastRow.ToString()));
		}
		break;
	}
	case EEpilogueChamberState::Offering:
		break;
	case EEpilogueChamberState::Aftermath:
	case EEpilogueChamberState::Hold:
		TickAftermath(Now, Delta, bStill);
		break;
	case EEpilogueChamberState::Ended:
		// The ending's travel, in Normal time only: asked again each frame until it starts.
		if (!bTravelAsked && !bStill && !Chosen.IsNone())
		{
			URunSubsystem* Run = URunSubsystem::Get(this);
			bTravelAsked = Run && Run->TravelToChosenEnding(Chosen);
			if (bTravelAsked)
			{
				EndingRun = Now;
			}
		}
		break;
	}
}
