// SPDX-License-Identifier: MIT

#include "Epilogue/ChoiceTerminal.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Run/RunSceneSubsystem.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"
#include "UI/ChoiceDialogWidget.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ChoiceTerminal)

AChoiceTerminal::AChoiceTerminal()
{
	bModelledBody = true;
	Cost = 0;
	// Reaches a player standing 2 m in front of its front face (the reach is measured from its centre, on the floor).
	InteractRadius = 230.f;
}

void AChoiceTerminal::BeginPlay()
{
	Super::BeginPlay();
	const UWorld* World = GetWorld();
	TimeState = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
	bOffering = bChosen = false;
	Dialog = nullptr;
}

void AChoiceTerminal::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UChoiceDialogWidget* Open = Dialog)
	{
		Dialog = nullptr;
		Open->Close(TEXT("the terminal left play"));
	}
	Super::EndPlay(EndPlayReason);
}

void AChoiceTerminal::SetOffering(bool bInOffering)
{
	if (bOffering != bInOffering)
	{
		UE_LOG(LogTheLastShift, Log, TEXT("%s: %s."), *GetName(), bInOffering ? TEXT("may offer the choice") : TEXT("offers nothing any more"));
	}
	bOffering = bInOffering;
	if (!bOffering && Dialog && !bChosen)
	{
		Dialog->Close(TEXT("the terminal no longer offers"));
	}
}

bool AChoiceTerminal::OffersChoice() const
{
	return bOffering && !bChosen && !Dialog && !(TimeState && (TimeState->IsStopped() || TimeState->IsRunFrozen()));
}

TArray<FName> AChoiceTerminal::GetEndings() const
{
	TArray<FName> Endings{ TEXT("Inject") };
	// Only to an engineer who has heard all of what the twin AI said; nothing tells the others it exists.
	const URunSceneSubsystem* Scenes = URunSceneSubsystem::Get(this);
	if (Scenes && Scenes->IsOpenEndingOffered())
	{
		Endings.Add(TEXT("Open"));
	}
	return Endings;
}

FText AChoiceTerminal::GetPromptLabel() const
{
	return URunSceneSubsystem::GetChooseLabel();
}

bool AChoiceTerminal::TryActivate(APawn* InstigatorPawn)
{
	if (!OffersChoice() || !IsInRange(InstigatorPawn))
	{
		return false;
	}
	APlayerController* Player = InstigatorPawn ? Cast<APlayerController>(InstigatorPawn->GetController()) : nullptr;
	Dialog = UChoiceDialogWidget::Open(Player, this);
	if (!Dialog)
	{
		return false;
	}
	++DialogsOpened;
	UE_LOG(LogTheLastShift, Log, TEXT("%s: the choice's dialog opened (%d endings)."), *GetName(), Dialog->GetButtonCount());
	OnDialogChanged.Broadcast(this, true);
	return true;
}

void AChoiceTerminal::NotifyDialogClosed(UChoiceDialogWidget* InDialog)
{
	if (InDialog && InDialog == Dialog)
	{
		Dialog = nullptr;
		UE_LOG(LogTheLastShift, Log, TEXT("%s: the choice's dialog closed%s."), *GetName(), bChosen ? TEXT(", the choice made") : TEXT(" without a choice"));
		OnDialogChanged.Broadcast(this, false);
	}
}

bool AChoiceTerminal::Choose(FName Ending)
{
	if (!bOffering || bChosen || !GetEndings().Contains(Ending))
	{
		UE_LOG(LogTheLastShift, Log, TEXT("%s: %s refused (offering %d, chosen %d)."), *GetName(), *Ending.ToString(), bOffering ? 1 : 0, bChosen ? 1 : 0);
		return false;
	}
	bChosen = true;
	UE_LOG(LogTheLastShift, Log, TEXT("%s: %s chosen: the choice is made."), *GetName(), *Ending.ToString());
	// The terminal accepts what the player did: its sound, as a free activation (no spending comment).
	OnActivated.Broadcast(0);
	OnAnyActivated.Broadcast(this, 0);
	OnChoiceMade.Broadcast(this, Ending);
	return true;
}
