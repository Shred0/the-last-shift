// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "RunSaveGame.generated.h"

/** The run state at the moment an area was entered, before the player could act. */
USTRUCT(BlueprintType)
struct THELASTSHIFT_API FAreaSnapshot
{
	GENERATED_BODY()

	/** Act of the area (1-4). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Run")
	int32 ActIndex = 0;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Run")
	double BatteryFraction = 1.0;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Run", meta = (Units = "s"))
	double HomeSecondsElapsed = 0.0;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Run")
	int32 BankedEnergy = 0;
};

/** The saved run: one snapshot per act reached, in act order. One slot, written by URunSubsystem. */
UCLASS()
class THELASTSHIFT_API URunSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	UPROPERTY(SaveGame)
	int32 Version = 1;

	UPROPERTY(SaveGame)
	TArray<FAreaSnapshot> Snapshots;
};
