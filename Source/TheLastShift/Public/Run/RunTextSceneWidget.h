// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Run/RunSceneSubsystem.h"
#include "UI/StdTextSceneWidget.h"
#include "RunTextSceneWidget.generated.h"

/** What a text scene of the run is, and so what follows it. */
UENUM(BlueprintType)
enum class ERunSceneKind : uint8
{
	/** Before area 1: the new run starts after it. */
	Prologue,
	/** An ending's text: the main menu follows it. */
	EndingText
};

/**
 * A text scene of the run on StdCore's text scene screen: the lines of URunSceneSubsystem::BuildScene, each in the
 * colours of its voice, typed at the settings' SceneCharsPerSecond. Every line that starts is reported to the scene
 * subsystem (OnSceneLineStarted, with its cue). When the scene ends, finished or skipped: a prologue starts the new run
 * (the screen stays until the travel covers it), an ending's text closes the ending and the main menu shows again.
 */
UCLASS()
class THELASTSHIFT_API URunTextSceneWidget : public UStdTextSceneWidget
{
	GENERATED_BODY()

public:
	/** Fills the screen with Lines of scene SceneName and starts it. */
	void Play(ERunSceneKind InKind, FName InSceneName, const TArray<FRunSceneLine>& InLines);

	UFUNCTION(BlueprintPure, Category = "Scenes")
	ERunSceneKind GetKind() const { return Kind; }

	UFUNCTION(BlueprintPure, Category = "Scenes")
	FName GetSceneName() const { return SceneName; }

	const TArray<FRunSceneLine>& GetLines() const { return Lines; }

private:
	UFUNCTION()
	void HandleEntryStarted(UStdTextSceneWidget* Screen, int32 Index);

	UFUNCTION()
	void HandleFinished(UStdTextSceneWidget* Screen, bool bInSkipped);

	ERunSceneKind Kind = ERunSceneKind::Prologue;
	FName SceneName;
	TArray<FRunSceneLine> Lines;
};
