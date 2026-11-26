// SPDX-License-Identifier: MIT

#include "Epilogue/EpilogueChamber.h"

#include "Energy/DoorDevice.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Epilogue/ChoiceTerminal.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Prologue/PrologueSlidingPanel.h"
#include "Run/HomeAIComponent.h"
#include "Run/RunSubsystem.h"
#include "Run/TheLastShiftSettings.h"
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
		case EEpilogueChamberState::Lines: return TEXT("Lines");
		case EEpilogueChamberState::Offering: return TEXT("Offering");
		case EEpilogueChamberState::HatchOpening: return TEXT("HatchOpening");
		case EEpilogueChamberState::Ended: return TEXT("Ended");
		}
		return TEXT("Unknown");
	}

	static APawn* PawnOf(const UWorld* World)
	{
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		return PC ? PC->GetPawn() : nullptr;
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
	bLastSeen = bTravelAsked = false;
	HatchSeconds = 0.0;
	LastRunSeconds = LinesStartRun = OfferedRun = ChosenRun = EndingRun = -1.0;
	for (AChoiceTerminal* Place : TArray<AChoiceTerminal*>{ Intake.Get(), HatchTerminal.Get() })
	{
		if (Place)
		{
			Place->SetOffering(false);
			Place->OnChoiceMade.AddUniqueDynamic(this, &AEpilogueChamber::HandleChoiceMade);
		}
	}
	UE_LOG(LogTheLastShift, Log, TEXT("%s: the battery's chamber: intake %s, outer hatch %s with its terminal %s, entry door %s (run on its way home: %d)."), *GetName(),
		*GetNameSafe(Intake), *GetNameSafe(OuterHatch), *GetNameSafe(HatchTerminal), *GetNameSafe(EntryDoor),
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

void AEpilogueChamber::SetPlacesOffering(bool bOffering)
{
	for (AChoiceTerminal* Place : TArray<AChoiceTerminal*>{ Intake.Get(), HatchTerminal.Get() })
	{
		if (Place)
		{
			Place->SetOffering(bOffering);
		}
	}
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
	UE_LOG(LogTheLastShift, Log, TEXT("%s: the places offer their choice (%s, %.2f s of the run's clock after the lines began)."), *GetName(), Why, Now - LinesStartRun);
	SetState(EEpilogueChamberState::Offering);
	SetPlacesOffering(true);
}

void AEpilogueChamber::HandleChoiceMade(AChoiceTerminal* Terminal)
{
	if (State != EEpilogueChamberState::Offering || !Terminal)
	{
		return;
	}
	Chosen = Terminal->Ending;
	ChosenRun = TimeState ? TimeState->GetRunSeconds() : -1.0;
	// Nothing in the room answers once a choice is made.
	SetPlacesOffering(false);
	UE_LOG(LogTheLastShift, Log, TEXT("%s: %s chosen at %s."), *GetName(), *Chosen.ToString(), *Terminal->GetName());
	if (Terminal == HatchTerminal && OuterHatch)
	{
		HatchSeconds = 0.0;
		SetState(EEpilogueChamberState::HatchOpening);
		return;
	}
	SetState(EEpilogueChamberState::Ended);
}

void AEpilogueChamber::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const UWorld* World = GetWorld();
	const double Now = TimeState ? TimeState->GetRunSeconds() : (World ? World->GetTimeSeconds() : 0.0);
	const double Step = LastRunSeconds >= 0.0 ? FMath::Max(Now - LastRunSeconds, 0.0) : 0.0;
	LastRunSeconds = Now;
	const bool bStill = TimeState && (TimeState->IsStopped() || TimeState->IsRunFrozen());

	switch (State)
	{
	case EEpilogueChamberState::Waiting:
	{
		const APawn* Pawn = EpilogueChamberImpl::PawnOf(World);
		if (Pawn && Pawn->FindComponentByClass<UTextChannelComponent>())
		{
			StartLines(Now);
		}
		break;
	}
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
	case EEpilogueChamberState::HatchOpening:
	{
		if (!bStill)
		{
			HatchSeconds += Step;
		}
		const float Seconds = UTheLastShiftSettings::Get()->GetChamberHatchOpenSeconds();
		OuterHatch->SetOpenFraction(FMath::Clamp(static_cast<float>(HatchSeconds / Seconds), 0.f, 1.f));
		if (HatchSeconds >= Seconds)
		{
			UE_LOG(LogTheLastShift, Log, TEXT("%s: the outer hatch has opened on the dark (%.2f s of Normal time)."), *GetName(), Seconds);
			SetState(EEpilogueChamberState::Ended);
		}
		else
		{
			break;
		}
		// Opened: the ending is asked in this same frame.
		[[fallthrough]];
	}
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
