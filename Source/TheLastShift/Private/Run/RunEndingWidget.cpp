// SPDX-License-Identifier: MIT

#include "Run/RunEndingWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"
#include "Run/RunSceneSubsystem.h"
#include "Run/RunSubsystem.h"
#include "Run/RunTextSceneWidget.h"
#include "Run/TheLastShiftSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RunEndingWidget)

#define LOCTEXT_NAMESPACE "TheLastShift"

void URunEndingWidget::Configure()
{
	OnChoiceSelected.AddUniqueDynamic(this, &URunEndingWidget::HandleChoice);
	const URunSceneSubsystem* Scenes = URunSceneSubsystem::Get(this);
	const FText Message = Scenes ? Scenes->BuildEndingMessage(&MessageLevel) : FText::GetEmpty();

	TArray<FStdChoiceEntry> NewEntries;
	EntryEndings.Reset();
	FStdChoiceEntry Inject;
	Inject.Label = LOCTEXT("EndingInject", "Inject the harvest");
	NewEntries.Add(Inject);
	EntryEndings.Add(TEXT("Inject"));
	// Offered only to an engineer who has learnt what the entity is; nothing tells the others it exists.
	const bool bOpen = Scenes && Scenes->IsOpenEndingOffered();
	if (bOpen)
	{
		FStdChoiceEntry Open;
		Open.Label = LOCTEXT("EndingOpen", "Open the structure");
		NewEntries.Add(Open);
		EntryEndings.Add(TEXT("Open"));
	}

	SetTitle(FText::GetEmpty());
	SetBody(Message);
	SetAllowBack(false);
	SetBackLabel(FText::GetEmpty());
	SetEntries(NewEntries);

	const URunSubsystem* Run = URunSubsystem::Get(this);
	UE_LOG(LogScenes, Log, TEXT("%s: ending choice with %d entries (twin parts %d of %d required), message %s: \"%s\"."), *GetName(), NewEntries.Num(),
		Run ? Run->GetArchive().TwinPartsPlayed : -1, UTheLastShiftSettings::Get()->GetOpenEndingTwinParts(), LexToString(MessageLevel), *Message.ToString());
}

void URunEndingWidget::NativeConstruct()
{
	Super::NativeConstruct();
	NativeOnEntriesRebuilt();
}

void URunEndingWidget::NativeOnEntriesRebuilt()
{
	if (!WidgetTree)
	{
		return;
	}
	const FSuitTextColors& Colors = UTheLastShiftSettings::GetTextColors();
	// Over the scenes' background: nothing of the menu shows through. The theme's brush is translucent: replace it.
	if (UBorder* Background = Cast<UBorder>(WidgetTree->FindWidget(TEXT("Border_Background"))))
	{
		FSlateBrush Solid;
		Solid.DrawAs = ESlateBrushDrawType::Box;
		Solid.TintColor = FSlateColor(Colors.SceneBackground);
		Background->SetBrush(Solid);
		Background->SetBrushColor(FLinearColor::White);
	}
	// The home AI's last message, in its colour.
	if (Text_Body)
	{
		Text_Body->SetColorAndOpacity(FSlateColor(Colors.HomeAI));
	}
}

void URunEndingWidget::HandleChoice(UStdChoiceListWidget* Screen, int32 Index)
{
	const FName Ending = GetEndingOfEntry(Index);
	URunSceneSubsystem* Scenes = URunSceneSubsystem::Get(this);
	if (Ending.IsNone() || !Scenes)
	{
		return;
	}
	UE_LOG(LogScenes, Log, TEXT("%s: ending %s chosen."), *GetName(), *Ending.ToString());
	const TArray<FRunSceneLine> Lines = Scenes->BuildEndingText(Ending);
	if (Lines.IsEmpty())
	{
		UE_LOG(LogScenes, Warning, TEXT("%s: ending %s has no text; back to the main menu."), *GetName(), *Ending.ToString());
		CloseScreen();
		return;
	}
	if (URunTextSceneWidget* Text = Cast<URunTextSceneWidget>(OpenChildScreen(URunTextSceneWidget::StaticClass())))
	{
		Text->Play(ERunSceneKind::EndingText, Ending, Lines);
	}
}

#undef LOCTEXT_NAMESPACE
