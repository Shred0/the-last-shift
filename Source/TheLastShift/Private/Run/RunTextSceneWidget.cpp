// SPDX-License-Identifier: MIT

#include "Run/RunTextSceneWidget.h"

#include "Run/RunEndingWidget.h"
#include "Run/RunSubsystem.h"
#include "Run/TheLastShiftSettings.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RunTextSceneWidget)

#define LOCTEXT_NAMESPACE "TheLastShift"

void URunTextSceneWidget::Play(ERunSceneKind InKind, FName InSceneName, const TArray<FRunSceneLine>& InLines)
{
	Kind = InKind;
	SceneName = InSceneName;
	Lines = InLines;
	const UTheLastShiftSettings* Settings = UTheLastShiftSettings::Get();
	CharsPerSecond = Settings->GetSceneCharsPerSecond();
	// The on-screen controls for going on and skipping, with the game's short UI labels.
	ContinueLabel = LOCTEXT("SceneContinue", "Continue");
	SkipLabel = LOCTEXT("SceneSkip", "Skip");
	SetShowControls(true);
	TArray<FStdTextSceneEntry> NewEntries;
	for (const FRunSceneLine& Line : Lines)
	{
		FStdTextSceneEntry& Entry = NewEntries.AddDefaulted_GetRef();
		Entry.Text = FText::FromString(Line.Text);
		Settings->GetSceneVoiceColors(Line.Voice, Entry.TextColor, Entry.BackgroundColor);
	}
	OnEntryStarted.AddUniqueDynamic(this, &URunTextSceneWidget::HandleEntryStarted);
	OnSceneFinished.AddUniqueDynamic(this, &URunTextSceneWidget::HandleFinished);
	SetEntries(NewEntries);
	UE_LOG(LogScenes, Log, TEXT("%s: scene %s, %d lines at %.0f characters per second."), *GetName(), *SceneName.ToString(), Lines.Num(), CharsPerSecond);
	StartScene();
}

void URunTextSceneWidget::HandleEntryStarted(UStdTextSceneWidget* Screen, int32 Index)
{
	if (URunSceneSubsystem* Scenes = URunSceneSubsystem::Get(this); Scenes && Lines.IsValidIndex(Index))
	{
		Scenes->NotifyLineStarted(SceneName, Index, Lines[Index]);
	}
}

void URunTextSceneWidget::HandleFinished(UStdTextSceneWidget* Screen, bool bInSkipped)
{
	UE_LOG(LogScenes, Log, TEXT("%s: scene %s %s at line %d of %d."), *GetName(), *SceneName.ToString(), bInSkipped ? TEXT("skipped") : TEXT("read through"),
		GetCurrentIndex() + 1, Lines.Num());
	if (Kind == ERunSceneKind::Prologue)
	{
		// The new run starts exactly as "New game" did without a prologue; the screen stays until the travel covers it.
		URunSubsystem* Run = URunSubsystem::Get(this);
		if (!Run || !Run->StartNewRun())
		{
			UE_LOG(LogScenes, Warning, TEXT("%s: the new run was refused after the prologue; back to the main menu."), *GetName());
			CloseScreen();
		}
		return;
	}
	// An ending's text: the ending closes with it, and the main menu shows again.
	if (URunEndingWidget* Ending = Cast<URunEndingWidget>(GetParentScreen()))
	{
		Ending->CloseScreen();
	}
	else
	{
		CloseScreen();
	}
}

#undef LOCTEXT_NAMESPACE
