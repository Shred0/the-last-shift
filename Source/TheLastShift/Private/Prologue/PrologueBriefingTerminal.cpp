// SPDX-License-Identifier: MIT

#include "Prologue/PrologueBriefingTerminal.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"
#include "Narrative/NarrativeRows.h"
#include "Prologue/PrologueLineTrigger.h"
#include "Run/HomeAIComponent.h"
#include "Run/TheLastShiftSettings.h"
#include "TextChannel/TextChannelComponent.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"
#include "UObject/ConstructorHelpers.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PrologueBriefingTerminal)

APrologueBriefingTerminal::APrologueBriefingTerminal()
{
	PrimaryActorTick.bCanEverTick = true;
	BriefingGroup = PrologueGroups::Hologram;
	Cost = 0;
	// A terminal built on the power terminal with no body of its own: it shows the project's modelled body.
	bModelledBody = true;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Greybox(TEXT("/Game/TheLastShift/Art/M_Greybox.M_Greybox"));
	if (Greybox.Succeeded())
	{
		Mesh->SetMaterial(0, Greybox.Object);
	}
}

void APrologueBriefingTerminal::BeginPlay()
{
	Super::BeginPlay();
	bAsked = bLastRowSeen = bBriefed = bPaid = false;
	BriefedRun = -1.0;
	LineHandle = APrologueLineTrigger::OnAnyLine.AddUObject(this, &APrologueBriefingTerminal::HandleLine);
	UE_LOG(LogTheLastShift, Log, TEXT("%s: waits for the briefing (group %s) before it works."), *GetName(), *BriefingGroup.ToString());
}

void APrologueBriefingTerminal::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	APrologueLineTrigger::OnAnyLine.Remove(LineHandle);
	LineHandle.Reset();
	Super::EndPlay(EndPlayReason);
}

void APrologueBriefingTerminal::HandleLine(UWorld* World, FName Group)
{
	if (World != GetWorld() || Group != BriefingGroup || bAsked)
	{
		return;
	}
	bAsked = true;
	const TArray<FName> Rows = UHomeAIComponent::FindPrologueRows(UTheLastShiftSettings::Get()->LoadPrologueTable(), BriefingGroup);
	if (Rows.IsEmpty())
	{
		SetBriefed(TEXT("the briefing group has no row"));
		return;
	}
	LastRow = Rows.Last();
	UE_LOG(LogTheLastShift, Log, TEXT("%s: the briefing started (%d rows); works once %s is typed out."), *GetName(), Rows.Num(), *LastRow.ToString());
}

void APrologueBriefingTerminal::SetBriefed(const TCHAR* Why)
{
	bBriefed = true;
	const UTimeStateSubsystem* TimeState = GetWorld() ? GetWorld()->GetSubsystem<UTimeStateSubsystem>() : nullptr;
	BriefedRun = TimeState ? TimeState->GetRunSeconds() : -1.0;
	UE_LOG(LogTheLastShift, Log, TEXT("%s: briefed (%s, run clock %.3f s): it works now."), *GetName(), Why, BriefedRun);
	SetActorTickEnabled(false);
}

void APrologueBriefingTerminal::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bAsked || bBriefed)
	{
		return;
	}
	const APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	const UTextChannelComponent* Channel = Pawn ? Pawn->FindComponentByClass<UTextChannelComponent>() : nullptr;
	if (!Channel)
	{
		SetBriefed(TEXT("no text channel"));
		return;
	}
	bLastRowSeen |= Channel->GetCurrentRowName() == LastRow;
	if (bLastRowSeen && Channel->IsTypedOut())
	{
		SetBriefed(*FString::Printf(TEXT("%s typed out"), *LastRow.ToString()));
	}
}

void APrologueBriefingTerminal::HandlePaid(APawn* InstigatorPawn)
{
	bPaid = true;
	Super::HandlePaid(InstigatorPawn);
}
