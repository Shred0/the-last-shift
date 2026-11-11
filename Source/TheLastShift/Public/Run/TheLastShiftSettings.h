// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Area/AreaTypes.h"
#include "Engine/DeveloperSettings.h"
#include "TheLastShiftSettings.generated.h"

class UWorld;

/** Per-act size of a full time jump (a contact with response Jump or RewindAndJump). A graze is GrazeJumpFraction of it. */
USTRUCT(BlueprintType)
struct THELASTSHIFT_API FActTuning
{
	GENERATED_BODY()

	/** Home time added by a full jump (600 s = ten minutes; 31 557 600 s = one year of 365.25 days). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Act", meta = (ClampMin = "0.0", ClampMax = "1000000000000.0", Units = "s"))
	double JumpHomeSeconds = 0.0;

	/** Home battery fraction lost by a full jump. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Act", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	double JumpBatteryDrop = 0.0;
};

/**
 * Every colour a scan can show, in one place. A scan's own points, the scan-ready cue, residues, the entity and the
 * interactables (terminals, doors) read their colour from here when they draw; nothing copies it. The values live in
 * Config/DefaultGame.ini; the C++ defaults are only a neutral fallback for a missing ini entry.
 */
USTRUCT(BlueprintType)
struct THELASTSHIFT_API FScanColors
{
	GENERATED_BODY()

	/** Captured points of a scan (the lidar's own colour). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scan Colors")
	FLinearColor Scan = FLinearColor::White;

	/** The scan-ready ring once a scan can be requested. Never the scan colour. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scan Colors")
	FLinearColor ReadyCue = FLinearColor::White;

	/** Residue points and the residue's collect ring. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scan Colors")
	FLinearColor Residue = FLinearColor::White;

	/** Every point of the entity (figure and trail). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scan Colors")
	FLinearColor Entity = FLinearColor::White;

	/** Surfaces of what the player can use (terminals, closed doors). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scan Colors")
	FLinearColor Interactable = FLinearColor::White;
};

/**
 * Project settings of the game (Project Settings > Game > The Last Shift), stored as text in Config/DefaultGame.ini.
 * Sole owner of the run's map order, of every area's recipe and seed, of the per-act jump tuning and of the scan colours: the area
 * generator, the run subsystem and the home state subsystem read them from here and copy nothing.
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

	/** One entry per act, same order as AreaMaps: what a full time jump costs in that act. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Home")
	TArray<FActTuning> ActTuning;

	/** Jump tuning of any map outside AreaMaps (TestLidar, AreaTest). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Test")
	FActTuning TestActTuning;

	/** A graze (a voluntary Resume with the figure within the entity's GrazeRadius) jumps by this fraction of the act's values. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Home", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GrazeJumpFraction = 0.25f;

	/** Every colour a scan can show: the one owner of those colours (see FScanColors). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Readability")
	FScanColors ScanColors;

	/**
	 * Custom depth stencil value of the meshes shown as interactables (terminals, closed doors) and the lidar colour
	 * channel the scan director gives ScanColors.Interactable. The one owner of that number.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Readability", meta = (ClampMin = "1", ClampMax = "4"))
	int32 InteractableScanChannel = 1;

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

	/** Jump tuning of Act (1-4), or the test tuning for 0, with every value clamped; false when the act has none. */
	bool GetActTuning(int32 Act, FActTuning& OutTuning) const;

	/** GrazeJumpFraction, clamped to [0, 1]. */
	float GetGrazeJumpFraction() const { return FMath::Clamp(GrazeJumpFraction, 0.f, 1.f); }

	/** ScanColors of the project settings. */
	static const FScanColors& GetScanColors() { return Get()->ScanColors; }

	/** InteractableScanChannel of the project settings, clamped to [1, 4]. */
	static int32 GetInteractableScanChannel() { return FMath::Clamp(Get()->InteractableScanChannel, 1, 4); }

	/** The map a run starts in: the first entry of AreaMaps (null when the list is empty). */
	TSoftObjectPtr<UWorld> GetFirstAreaMap() const { return AreaMaps.Num() > 0 ? AreaMaps[0] : TSoftObjectPtr<UWorld>(); }
};
