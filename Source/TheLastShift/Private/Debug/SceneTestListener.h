// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Narrative/NarrativeRows.h"
#include "Run/RunSceneSubsystem.h"
#include "UObject/Object.h"
#include "SceneTestListener.generated.h"

/**
 * Records URunSceneSubsystem::OnSceneLineStarted for tls.SceneTest and tls.EndingTest (reflected, so it exists in every
 * configuration; used in development builds only).
 */
UCLASS(Transient)
class UTlsSceneTestListener : public UObject
{
	GENERATED_BODY()

public:
	struct FLine
	{
		FName Scene;
		int32 Index = 0;
		ESceneVoice Voice = ESceneVoice::HomeAI;
		FName Cue;
		double RealTime = 0.0;
	};

	TArray<FLine> Lines;

	void Bind(URunSceneSubsystem* InScenes)
	{
		Unbind();
		Lines.Reset();
		if (InScenes)
		{
			Scenes = InScenes;
			InScenes->OnSceneLineStarted.AddUniqueDynamic(this, &UTlsSceneTestListener::HandleLine);
		}
	}

	void Unbind()
	{
		if (URunSceneSubsystem* Old = Scenes.Get())
		{
			Old->OnSceneLineStarted.RemoveDynamic(this, &UTlsSceneTestListener::HandleLine);
		}
		Scenes.Reset();
	}

	UFUNCTION()
	void HandleLine(FName Scene, int32 Index, ESceneVoice Voice, FName Cue)
	{
		Lines.Add({ Scene, Index, Voice, Cue, FPlatformTime::Seconds() });
	}

private:
	TWeakObjectPtr<URunSceneSubsystem> Scenes;
};
