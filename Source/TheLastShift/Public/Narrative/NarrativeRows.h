// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "NarrativeRows.generated.h"

/**
 * Row structs of the narrative DataTables. The code is MIT; the rows (the CSV files in Narrative/, imported into
 * Content/TheLastShift/Narrative/) are not. A row carries only its text and what selects it: no tuning numbers.
 */

/** When the home AI speaks. */
UENUM(BlueprintType)
enum class ETextTrigger : uint8
{
	/** A time jump (contact or graze). */
	JumpReturn,
	/** The label of the travel screen to an area: the act is the destination's. */
	AreaEntry,
	/** The player is released in an area that is ready. */
	AreaStart,
	/** Banked energy rises from zero for the first time in a map. */
	FirstEnergy,
	/** A successful payment at a terminal. */
	Spend
};

inline const TCHAR* LexToString(ETextTrigger Trigger)
{
	switch (Trigger)
	{
	case ETextTrigger::JumpReturn: return TEXT("JumpReturn");
	case ETextTrigger::AreaEntry: return TEXT("AreaEntry");
	case ETextTrigger::AreaStart: return TEXT("AreaStart");
	case ETextTrigger::FirstEnergy: return TEXT("FirstEnergy");
	case ETextTrigger::Spend: return TEXT("Spend");
	}
	return TEXT("Unknown");
}

/**
 * One message of the home AI (DT_AIMessages, row name A<act>_<Trigger>_<order>). Text is the full text; the
 * degraded forms are produced from it automatically, unless a variant column is set for that level. Tokens {still},
 * {jump}, {home} and {battery} are replaced when the message is shown.
 */
USTRUCT(BlueprintType)
struct THELASTSHIFT_API FAIMessageRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	ETextTrigger Trigger = ETextTrigger::JumpReturn;

	/** Act the message belongs to (1-4). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative", meta = (ClampMin = "1", ClampMax = "4"))
	int32 Act = 1;

	/** Position among the rows of the same trigger and act (ascending). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative", meta = (ClampMin = "0", ClampMax = "999"))
	int32 Order = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative", meta = (MultiLine = "true"))
	FText Text;

	/** Optional: the text at NoArticles. Empty = produced from Text. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative", meta = (MultiLine = "true"))
	FText TextNoArticles;

	/** Optional: the text at Abbreviated. Empty = produced from Text. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative", meta = (MultiLine = "true"))
	FText TextAbbreviated;

	/** Optional: the text at Scattered. Empty = produced from Text. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative", meta = (MultiLine = "true"))
	FText TextScattered;
};

/** One ending text (DT_Endings: Inject, Open, Loss). Shown in full, never degraded. */
USTRUCT(BlueprintType)
struct THELASTSHIFT_API FEndingRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative", meta = (MultiLine = "true"))
	FText Text;
};
