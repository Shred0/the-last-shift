// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "TextChannel/TextDegradation.h"
#include "TextTokens.generated.h"

/**
 * The numbers a message may show, given with every push. The text channel owns none of them: whoever pushes fills
 * the context from its owners.
 */
USTRUCT(BlueprintType)
struct THELASTSHIFT_API FTextChannelContext
{
	GENERATED_BODY()

	/** {still}: seconds of the Stop this message is about. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextChannel", meta = (Units = "s"))
	double StoppedSeconds = 0.0;

	/** {jump}: home seconds added by the jump this message is about. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextChannel", meta = (Units = "s"))
	double JumpSeconds = 0.0;

	/** {home}: home seconds elapsed in the run. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextChannel", meta = (Units = "s"))
	double HomeSecondsElapsed = 0.0;

	/** {battery}, and the degradation level and typing speed of the message: 1 = full. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextChannel")
	double BatteryFraction = 1.0;
};

/**
 * Token replacement, applied after degradation: {still}, {jump} and {home} become durations, {battery} a whole percent.
 * Each is written for the message's level. Unknown tokens are left as they are.
 */
UCLASS()
class THELASTSHIFT_API UTextTokensLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Replaces every known token of Text with the context's numbers written for Level. */
	UFUNCTION(BlueprintPure, Category = "TextChannel")
	static FString ReplaceTokens(const FString& Text, const FTextChannelContext& Context, ETextDegradation Level);

	/**
	 * Seconds as words. Full and NoArticles: the two largest non-zero units below one year ("2 minutes and 11 seconds"),
	 * a single unit from one year up ("212 years"). Abbreviated: the largest unit with a short label ("212 yrs").
	 * Scattered: the largest unit with the shortest label, thousands as k ("1k y"). Silent: empty.
	 * One year is 365.25 days. Values are rounded down.
	 */
	UFUNCTION(BlueprintPure, Category = "TextChannel")
	static FString FormatDuration(double Seconds, ETextDegradation Level);

	/** The battery as a whole percent: "31%" down to Abbreviated, "31" at Scattered, empty at Silent. */
	UFUNCTION(BlueprintPure, Category = "TextChannel")
	static FString FormatBattery(double Fraction, ETextDegradation Level);
};
