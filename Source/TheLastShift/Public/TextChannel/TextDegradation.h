// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "TextDegradation.generated.h"

/** How much of a message survives, from the battery fraction. */
UENUM(BlueprintType)
enum class ETextDegradation : uint8
{
	/** The full text. */
	Full,
	/** The whole words a, an and the are gone. */
	NoArticles,
	/** NoArticles, lowercase, and the inner vowels of words longer than three letters are gone. */
	Abbreviated,
	/** A fragment of some words only. */
	Scattered,
	/** Nothing: the message is discarded. */
	Silent
};

inline const TCHAR* LexToString(ETextDegradation Level)
{
	switch (Level)
	{
	case ETextDegradation::Full: return TEXT("Full");
	case ETextDegradation::NoArticles: return TEXT("NoArticles");
	case ETextDegradation::Abbreviated: return TEXT("Abbreviated");
	case ETextDegradation::Scattered: return TEXT("Scattered");
	case ETextDegradation::Silent: return TEXT("Silent");
	}
	return TEXT("Unknown");
}

/**
 * The automatic degradation of a message: a deterministic function of the full text and the level, with no
 * randomness. Digits and {tokens} are never altered at any level. Pure functions; the thresholds are given by the
 * caller (their owner is UTextChannelComponent).
 */
UCLASS()
class THELASTSHIFT_API UTextDegradationLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Level of a battery fraction: Silent below SilentBelow, Scattered below ScatteredBelow, Abbreviated below
	 * AbbreviatedBelow, NoArticles below NoArticlesBelow, Full otherwise. A fraction equal to a threshold is not below it.
	 */
	UFUNCTION(BlueprintPure, Category = "TextChannel")
	static ETextDegradation LevelFromBattery(double Fraction, float NoArticlesBelow, float AbbreviatedBelow, float ScatteredBelow, float SilentBelow);

	/** Text at Level: Full unchanged, Silent empty. */
	UFUNCTION(BlueprintPure, Category = "TextChannel")
	static FString Degrade(const FString& Text, ETextDegradation Level);
};
