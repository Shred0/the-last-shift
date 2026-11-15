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

/**
 * One log entry of the other humanity (DT_Logs, row name L_<order>). Text is the suit's translation, in plain English;
 * the barcodes are generated from it. Shown newest first (highest Order first), never shortened, no tokens.
 */
USTRUCT(BlueprintType)
struct THELASTSHIFT_API FLogRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Position in the logs' own chronology: the highest is the newest. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative", meta = (ClampMin = "1", ClampMax = "999"))
	int32 Order = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative", meta = (MultiLine = "true"))
	FText Text;
};

/**
 * One message of the twin AI (DT_TwinAI, row name T_<part>_<order>). Each payment at its terminal plays the rows of the
 * next Part, in ascending Order, on the text channel as the twin voice. Never shortened, no tokens.
 */
USTRUCT(BlueprintType)
struct THELASTSHIFT_API FTwinAIRow : public FTableRowBase
{
	GENERATED_BODY()

	/** The part the row belongs to (1 = the first payment). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative", meta = (ClampMin = "1", ClampMax = "99"))
	int32 Part = 1;

	/** Position among the rows of the same part (ascending). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative", meta = (ClampMin = "1", ClampMax = "999"))
	int32 Order = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative", meta = (MultiLine = "true"))
	FText Text;
};

/** One ending text (DT_Endings: Inject, Open, Loss). Shown in full, never degraded. */
USTRUCT(BlueprintType)
struct THELASTSHIFT_API FEndingRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative", meta = (MultiLine = "true"))
	FText Text;
};

/** Who speaks a line of a text scene: the voice sets the line's background and text colour. */
UENUM(BlueprintType)
enum class ESceneVoice : uint8
{
	/** The voices in the simulated world: dark text on a white background. */
	Paradise,
	/** The home AI: its colour on black; degraded by the run's battery. */
	HomeAI,
	/** The engineer's reply in an ending: its own colour on black. */
	Engineer
};

inline const TCHAR* LexToString(ESceneVoice Voice)
{
	switch (Voice)
	{
	case ESceneVoice::Paradise: return TEXT("Paradise");
	case ESceneVoice::HomeAI: return TEXT("HomeAI");
	case ESceneVoice::Engineer: return TEXT("Engineer");
	}
	return TEXT("Unknown");
}

/**
 * One line of a full-screen text scene (DT_Scenes, row name <Scene>_<order>): the prologue's lines (Scene Prologue) and
 * the home AI's last message before the ending's choice (Scene Ending). A scene shows its rows in ascending Order.
 * HomeAI lines are degraded by the run's battery and take the text channel's tokens; the other voices are shown as they
 * are. Cue names a sound or an effect for the line's start (empty: none); the game only passes it on.
 */
USTRUCT(BlueprintType)
struct THELASTSHIFT_API FSceneRow : public FTableRowBase
{
	GENERATED_BODY()

	/** The scene the line belongs to (Prologue, Ending). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName Scene;

	/** Position among the lines of the same scene (ascending). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative", meta = (ClampMin = "1", ClampMax = "999"))
	int32 Order = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	ESceneVoice Voice = ESceneVoice::HomeAI;

	/** A sound or an effect for the moment the line starts. None: nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName Cue;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative", meta = (MultiLine = "true"))
	FText Text;
};
