// SPDX-License-Identifier: MIT

#include "Run/RunTextSceneWidget.h"

#include "Run/RunEndingWidget.h"
#include "Run/RunSceneSubsystem.h"
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
	TArray<ESceneVoice> Voices;
	for (const FRunSceneLine& Line : Lines)
	{
		Voices.Add(Line.Voice);
	}
	TArray<FStdTextSceneEntry> NewEntries;
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		const FRunSceneLine& Line = Lines[Index];
		FStdTextSceneEntry& Entry = NewEntries.AddDefaulted_GetRef();
		Entry.Text = FText::FromString(Line.Text);
		Settings->GetSceneVoiceColors(Line.Voice, Entry.TextColor, Entry.BackgroundColor);
		// Brief 27: the cut from the paradise's white to black (where the interruption's noise plays) fills with static.
		if (RunScenes::IsInterruptionLine(Voices, Index))
		{
			Entry.BackgroundEffect = EStdTextSceneEffect::Noise;
			Entry.EffectSeconds = Settings->GetStaticSeconds();
			Entry.EffectFadeSeconds = Settings->GetStaticFadeSeconds();
		}
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
		Scenes->NotifyLineStarted(SceneName, Index, Lines[Index], this);
	}
}

void URunTextSceneWidget::HandleFinished(UStdTextSceneWidget* Screen, bool bInSkipped)
{
	UE_LOG(LogScenes, Log, TEXT("%s: scene %s %s at line %d of %d."), *GetName(), *SceneName.ToString(), bInSkipped ? TEXT("skipped") : TEXT("read through"),
		GetCurrentIndex() + 1, Lines.Num());
	if (Kind == ERunSceneKind::Prologue)
	{
		// Read or skipped, the same: the prologue's maps when they are on, else the new run at once; the screen stays until
		// the travel covers it.
		URunSubsystem* Run = URunSubsystem::Get(this);
		if (!Run || !Run->StartNewGame())
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
