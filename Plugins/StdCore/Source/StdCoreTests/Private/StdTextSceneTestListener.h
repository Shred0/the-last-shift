// Copyright 2026 <OWNER>. All rights reserved. See Plugins/StdCore/LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "StdTextSceneTestListener.generated.h"

class UStdTextSceneWidget;

/** Records what a text scene reports, for StdCore.UI.TextScene. Editor tests only. */
UCLASS(Transient)
class UStdTextSceneTestListener : public UObject
{
	GENERATED_BODY()

public:
	TArray<int32> Started;
	TArray<bool> Finished;

	UFUNCTION()
	void HandleEntryStarted(UStdTextSceneWidget* Screen, int32 Index) { Started.Add(Index); }

	UFUNCTION()
	void HandleFinished(UStdTextSceneWidget* Screen, bool bSkipped) { Finished.Add(bSkipped); }
};
