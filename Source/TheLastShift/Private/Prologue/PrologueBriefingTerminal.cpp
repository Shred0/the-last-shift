// SPDX-License-Identifier: MIT

#include "Prologue/PrologueBriefingTerminal.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/DataTable.h"
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
	Rows.Reset();
	Weights.Reset();
	TotalWeight = 0.f;
	Progress = 0.f;
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
	const UDataTable* Table = UTheLastShiftSettings::Get()->LoadPrologueTable();
	Rows = UHomeAIComponent::FindPrologueRows(Table, BriefingGroup);
	Weights.Reset();
	TotalWeight = 0.f;
	for (const FName& RowName : Rows)
	{
		const FPrologueRow* Row = Table ? Table->FindRow<FPrologueRow>(RowName, TEXT("Briefing"), false) : nullptr;
		Weights.Add(FMath::Max(1.f, Row ? static_cast<float>(Row->Text.ToString().Len()) : 1.f));
		TotalWeight += Weights.Last();
	}
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
	Progress = 1.f;
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
	UpdateProgress(Channel);
	bLastRowSeen |= Channel->GetCurrentRowName() == LastRow;
	if (bLastRowSeen && Channel->IsTypedOut())
	{
		SetBriefed(*FString::Printf(TEXT("%s typed out"), *LastRow.ToString()));
	}
}

void APrologueBriefingTerminal::UpdateProgress(const UTextChannelComponent* Channel)
{
	// The row being typed, if it is one of the briefing's: the rows before it whole, and its own typed share.
	const int32 Index = Rows.IndexOfByKey(Channel->GetCurrentRowName());
	if (Index == INDEX_NONE || TotalWeight <= 0.f)
	{
		return;
	}
	const int32 Length = Channel->GetCurrentText().Len();
	const float Share = Length > 0 ? FMath::Clamp(static_cast<float>(Channel->GetShownCells()) / Length, 0.f, 1.f) : 1.f;
	float Done = 0.f;
	for (int32 I = 0; I < Index; ++I)
	{
		Done += Weights[I];
	}
	Progress = FMath::Max(Progress, FMath::Clamp((Done + Weights[Index] * Share) / TotalWeight, 0.f, 1.f));
}

void APrologueBriefingTerminal::HandlePaid(APawn* InstigatorPawn)
{
	bPaid = true;
	Super::HandlePaid(InstigatorPawn);
}
