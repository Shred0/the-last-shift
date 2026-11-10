// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Area/AreaTypes.h"
#include "Engine/DeveloperSettings.h"
#include "TheLastShiftSettings.generated.h"

class UWorld;

/**
 * Project settings of the game (Project Settings > Game > The Last Shift), stored as text in Config/DefaultGame.ini.
 * Sole owner of the run's map order and of every area's recipe and seed: the area generator and the run subsystem
 * read them from here and copy nothing.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "The Last Shift"))
class THELASTSHIFT_API UTheLastShiftSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const UTheLastShiftSettings* Get() { return GetDefault<UTheLastShiftSettings>(); }

	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	/** The four area maps, in the order they are travelled. A map's act is its position in this list (1-4). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Run")
	TArray<TSoftObjectPtr<UWorld>> AreaMaps;

	/** One recipe per act, same order as AreaMaps. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Run")
	TArray<FAreaRecipe> AreaRecipes;

	/** One seed per act, same order as AreaMaps. Used in the game only if tls.AreaValidate passes for it. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Run", meta = (ClampMin = "0", ClampMax = "999999"))
	TArray<int32> AreaSeeds;

	/** Recipe of any area map outside AreaMaps (AreaTest). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Test")
	FAreaRecipe TestRecipe;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Test", meta = (ClampMin = "0", ClampMax = "999999"))
	int32 TestSeed = 1;

	/** The travel screen stays black at least this long, even when the next area is ready sooner. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Travel", meta = (ClampMin = "0.0", ClampMax = "30.0", Units = "s"))
	float TravelHoldSeconds = 4.f;

	/** Short label shown on the black travel screen. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Travel")
	FText TravelLabel;

	/** Act of the map named PackageName (1-based position in AreaMaps, PIE prefix ignored); 0 when it is not an area map. */
	int32 GetActOfMap(const FString& PackageName) const;

	/** Recipe of Act (1-4), or the test recipe for 0; null when the act has none. */
	const FAreaRecipe* FindRecipe(int32 Act) const;

	/** Seed of Act (1-4), or the test seed for 0; -1 when the act has none. */
	int32 GetSeed(int32 Act) const;
};
