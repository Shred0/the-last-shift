// SPDX-License-Identifier: MIT

#include "Run/RunActListWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Run/RunSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RunActListWidget)

#define LOCTEXT_NAMESPACE "TheLastShift"

void URunActListWidget::Configure(ERunActListMode InMode)
{
	Mode = InMode;
	OnChoiceSelected.AddUniqueDynamic(this, &URunActListWidget::HandleChoice);

	TArray<FStdChoiceEntry> NewEntries;
	EntryActs.Reset();
	if (const URunSubsystem* Run = URunSubsystem::Get(this))
	{
		for (const FAreaSnapshot& Snapshot : Run->GetSnapshots())
		{
			FStdChoiceEntry Entry;
			Entry.Label = FText::Format(LOCTEXT("ActEntry", "Area {0} — {1}%"), FText::AsNumber(Snapshot.ActIndex),
				FText::AsNumber(FMath::RoundToInt(Snapshot.BatteryFraction * 100.0)));
			NewEntries.Add(Entry);
			EntryActs.Add(Snapshot.ActIndex);
		}
	}
	if (Mode == ERunActListMode::RunLost)
	{
		FStdChoiceEntry MainMenu;
		MainMenu.Label = LOCTEXT("MainMenuEntry", "Main menu");
		NewEntries.Add(MainMenu);
		EntryActs.Add(0);
	}

	SetTitle(Mode == ERunActListMode::RunLost ? LOCTEXT("PowerLostTitle", "Power lost") : LOCTEXT("SelectActTitle", "Select act"));
	SetAllowBack(Mode == ERunActListMode::SelectAct);
	SetBackLabel(Mode == ERunActListMode::SelectAct ? LOCTEXT("BackEntry", "Back") : FText::GetEmpty());
	SetEntries(NewEntries);

	FadeElapsed = 0.f;
	SetRenderOpacity(Mode == ERunActListMode::RunLost && LossFadeSeconds > 0.f ? 0.f : 1.f);
}

void URunActListWidget::NativeConstruct()
{
	Super::NativeConstruct();
	NativeOnEntriesRebuilt();
}

void URunActListWidget::NativeOnEntriesRebuilt()
{
	// Over black: nothing of the game shows through the loss screen.
	if (Mode == ERunActListMode::RunLost && WidgetTree)
	{
		if (UBorder* Background = Cast<UBorder>(WidgetTree->FindWidget(TEXT("Border_Background"))))
		{
			// The theme's backdrop brush is translucent and the brush colour only tints it: replace the brush.
			FSlateBrush Black;
			Black.DrawAs = ESlateBrushDrawType::Box;
			Black.TintColor = FSlateColor(FLinearColor::Black);
			Background->SetBrush(Black);
			Background->SetBrushColor(FLinearColor::White);
		}
	}
}

void URunActListWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (Mode == ERunActListMode::RunLost && GetRenderOpacity() < 1.f)
	{
		FadeElapsed += InDeltaTime;
		const float Fade = FMath::Clamp(LossFadeSeconds, 0.f, 10.f);
		SetRenderOpacity(Fade > 0.f ? FMath::Clamp(FadeElapsed / Fade, 0.f, 1.f) : 1.f);
	}
}

void URunActListWidget::HandleChoice(UStdChoiceListWidget* Screen, int32 Index)
{
	URunSubsystem* Run = URunSubsystem::Get(this);
	const int32 Act = GetActOfEntry(Index);
	if (!Run || Act < 0)
	{
		return;
	}
	if (Act == 0)
	{
		Run->ReturnToMainMenu();
	}
	else
	{
		Run->RestartFromAct(Act);
	}
}

#undef LOCTEXT_NAMESPACE
