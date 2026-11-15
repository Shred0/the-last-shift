// SPDX-License-Identifier: MIT

#include "Run/RunSceneSubsystem.h"

#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Run/RunEndingWidget.h"
#include "Run/RunSubsystem.h"
#include "Run/RunTextSceneWidget.h"
#include "Run/TheLastShiftSettings.h"
#include "TextChannel/TextChannelComponent.h"
#include "Time/HomeStateSubsystem.h"
#include "UI/StdScreenWidget.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RunSceneSubsystem)

DEFINE_LOG_CATEGORY(LogScenes);

URunSceneSubsystem* URunSceneSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<URunSceneSubsystem>() : nullptr;
}

TArray<FName> URunSceneSubsystem::FindSceneRows(const UDataTable* Table, FName Scene)
{
	TArray<TPair<int32, FName>> Found;
	if (Table && Table->GetRowStruct() && Table->GetRowStruct()->IsChildOf(FSceneRow::StaticStruct()))
	{
		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			const FSceneRow* Row = reinterpret_cast<const FSceneRow*>(Pair.Value);
			if (Row && Row->Scene == Scene)
			{
				Found.Emplace(Row->Order, Pair.Key);
			}
		}
	}
	Found.Sort([](const TPair<int32, FName>& A, const TPair<int32, FName>& B)
	{
		return A.Key != B.Key ? A.Key < B.Key : A.Value.LexicalLess(B.Value);
	});
	TArray<FName> Names;
	for (const TPair<int32, FName>& Pair : Found)
	{
		Names.Add(Pair.Value);
	}
	return Names;
}

TArray<FRunSceneLine> URunSceneSubsystem::BuildScene(FName Scene, const FTextChannelContext& Context) const
{
	const UTheLastShiftSettings* Settings = UTheLastShiftSettings::Get();
	const UDataTable* Table = Settings->LoadScenesTable();
	TArray<FRunSceneLine> Lines;
	for (const FName& RowName : FindSceneRows(Table, Scene))
	{
		const FSceneRow* Row = Table->FindRow<FSceneRow>(RowName, TEXT("Scenes"), false);
		if (!Row)
		{
			continue;
		}
		FRunSceneLine Line;
		Line.RowName = RowName;
		Line.Voice = Row->Voice;
		Line.Cue = Row->Cue;
		if (Row->Voice == ESceneVoice::HomeAI)
		{
			// The home AI's lines degrade exactly like its messages on the channel: same function, same thresholds, same tokens.
			Line.Level = Settings->GetTextLevel(Context.BatteryFraction);
			FAIMessageRow Message;
			Message.Text = Row->Text;
			Line.Text = UTextChannelComponent::ComposeMessage(Message, Line.Level, Context);
		}
		else
		{
			Line.Level = ETextDegradation::Full;
			Line.Text = Row->Text.ToString();
		}
		if (Line.Text.IsEmpty())
		{
			UE_LOG(LogScenes, Log, TEXT("Scene %s: line %s left out (%s at battery %.3f)."), *Scene.ToString(), *RowName.ToString(), LexToString(Line.Level),
				Context.BatteryFraction);
			continue;
		}
		Lines.Add(MoveTemp(Line));
	}
	UE_LOG(LogScenes, Log, TEXT("Scene %s: %d lines from %s (battery %.3f)."), *Scene.ToString(), Lines.Num(), *GetNameSafe(Table), Context.BatteryFraction);
	return Lines;
}

FTextChannelContext URunSceneSubsystem::MakeNewRunContext()
{
	// A new run: the battery starts full and never recharges; no home time has passed.
	FTextChannelContext Context;
	Context.BatteryFraction = 1.0;
	return Context;
}

FTextChannelContext URunSceneSubsystem::MakeRunContext() const
{
	FTextChannelContext Context;
	if (const UHomeStateSubsystem* Home = GetGameInstance() ? GetGameInstance()->GetSubsystem<UHomeStateSubsystem>() : nullptr)
	{
		Context.HomeSecondsElapsed = Home->GetHomeSecondsElapsed();
		Context.BatteryFraction = Home->GetBatteryFraction();
	}
	return Context;
}

// --- Prologue ---

bool URunSceneSubsystem::OpenPrologue(UStdScreenWidget* Parent)
{
	const TArray<FRunSceneLine> Lines = BuildScene(PrologueScene(), MakeNewRunContext());
	if (Lines.IsEmpty() || !Parent)
	{
		UE_LOG(LogScenes, Log, TEXT("Prologue: no line, the new run starts at once."));
		return false;
	}
	URunTextSceneWidget* Screen = Cast<URunTextSceneWidget>(Parent->OpenChildScreen(URunTextSceneWidget::StaticClass()));
	if (!Screen)
	{
		return false;
	}
	Screen->Play(ERunSceneKind::Prologue, PrologueScene(), Lines);
	return true;
}

// --- Ending ---

FText URunSceneSubsystem::BuildEndingMessage(ETextDegradation* OutLevel) const
{
	const FTextChannelContext Context = MakeRunContext();
	if (OutLevel)
	{
		*OutLevel = UTheLastShiftSettings::Get()->GetTextLevel(Context.BatteryFraction);
	}
	TArray<FString> Texts;
	for (const FRunSceneLine& Line : BuildScene(EndingScene(), Context))
	{
		Texts.Add(Line.Text);
	}
	return FText::FromString(FString::Join(Texts, TEXT("\n\n")));
}

bool URunSceneSubsystem::IsOpenEndingOffered() const
{
	const URunSubsystem* Run = GetGameInstance() ? GetGameInstance()->GetSubsystem<URunSubsystem>() : nullptr;
	const int32 Required = UTheLastShiftSettings::Get()->GetOpenEndingTwinParts();
	return Run && Required > 0 && Run->GetArchive().TwinPartsPlayed >= Required;
}

URunEndingWidget* URunSceneSubsystem::OpenEnding(UStdScreenWidget* Parent)
{
	URunEndingWidget* Screen = Parent ? Cast<URunEndingWidget>(Parent->OpenChildScreen(URunEndingWidget::StaticClass())) : nullptr;
	if (Screen)
	{
		Screen->Configure();
	}
	return Screen;
}

TArray<FRunSceneLine> URunSceneSubsystem::BuildEndingText(FName Ending) const
{
	TArray<FRunSceneLine> Lines;
	const FString Text = UTheLastShiftSettings::Get()->GetEndingText(Ending).ToString();
	if (!Text.IsEmpty())
	{
		// The engineer's reply: in full at any battery, never degraded.
		FRunSceneLine& Line = Lines.AddDefaulted_GetRef();
		Line.RowName = Ending;
		Line.Voice = ESceneVoice::Engineer;
		Line.Text = Text;
	}
	return Lines;
}

void URunSceneSubsystem::SetEndingPending(bool bPending)
{
	if (bEndingPending != bPending)
	{
		UE_LOG(LogScenes, Log, TEXT("Ending %s."), bPending ? TEXT("pending: the main menu opens it on arrival") : TEXT("no longer pending"));
	}
	bEndingPending = bPending;
}

bool URunSceneSubsystem::ConsumePendingEnding()
{
	const bool bWas = bEndingPending;
	bEndingPending = false;
	return bWas;
}

void URunSceneSubsystem::NotifyLineStarted(FName Scene, int32 Index, const FRunSceneLine& Line)
{
	UE_LOG(LogScenes, Log, TEXT("Scene %s: line %d (%s, %s, %s%s) started: \"%s\""), *Scene.ToString(), Index + 1, *Line.RowName.ToString(), LexToString(Line.Voice),
		LexToString(Line.Level), Line.Cue.IsNone() ? TEXT(", no cue") : *FString::Printf(TEXT(", cue %s"), *Line.Cue.ToString()), *Line.Text);
	OnSceneLineStarted.Broadcast(Scene, Index, Line.Voice, Line.Cue);
}
