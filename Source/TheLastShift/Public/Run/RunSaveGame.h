// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "RunSaveGame.generated.h"

/**
 * The suit's archive: which log entries the player has unlocked and how many parts of the twin AI they have played.
 * It stores which items, never their text: the text always comes from the narrative tables.
 */
USTRUCT(BlueprintType)
struct THELASTSHIFT_API FSuitArchive
{
	GENERATED_BODY()

	/** Orders (FLogRow::Order) of the stored log entries, in the order they were stored. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Run")
	TArray<int32> LogOrders;

	/** Parts of the twin AI played so far; they are played in order, so parts 1..TwinPartsPlayed are stored. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Run")
	int32 TwinPartsPlayed = 0;

	bool IsEmpty() const { return LogOrders.IsEmpty() && TwinPartsPlayed <= 0; }
};

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

	/** The suit's archive on entering the area. A save written before version 2 has none: it loads empty. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Run")
	FSuitArchive Archive;

	/** The entity had been found on entering the area (URunSubsystem::IsEntityFound). A save written before version 3 has none: it loads false. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Run")
	bool bEntityFound = false;

	/** The first contact with the entity had happened on entering the area (URunSubsystem::HasFirstContact). A save written before version 4 has none: it loads false. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "Run")
	bool bFirstContact = false;
};

/**
 * The saved run: one snapshot per act reached, in act order. One slot, written by URunSubsystem.
 * Version 1: battery, home time, banked energy. Version 2 (Brief 8): plus the suit's archive. Version 3 (Brief 17): plus
 * whether the entity had been found. Version 4 (Brief 20): plus whether the first contact with the entity had happened.
 * Properties are tagged, so an older file loads with the newer fields at their defaults (an empty archive, the entity
 * not found, the first contact not happened).
 */
UCLASS()
class THELASTSHIFT_API URunSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	static constexpr int32 CurrentVersion = 4;

	/**
	 * The version the file was written with. The default stays 1: a save skips values equal to the class default, so a
	 * file written before Brief 8 holds no Version and reads as 1; URunSubsystem writes CurrentVersion explicitly.
	 */
	UPROPERTY(SaveGame)
	int32 Version = 1;

	UPROPERTY(SaveGame)
	TArray<FAreaSnapshot> Snapshots;
};
