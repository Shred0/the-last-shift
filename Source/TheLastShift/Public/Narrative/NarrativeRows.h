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
	/**
	 * A successful payment at a terminal: the spending comment when the act has no row for the payment's kind (SpendDoor,
	 * SpendLights, SpendBeacon, SpendLog, SpendTwin), or the terminal powers nothing of those kinds.
	 */
	Spend,
	/**
	 * Once in a run, on the return to Normal time after the Stop in which the entity was found (after the return message,
	 * the EntitySeen rows or the EntityContact rows): the home AI switches the entity's own colour on.
	 */
	EntityFound,
	/**
	 * Once in a run, on the return to Normal time after the Stop in which a clear sighting found the entity (a Stop that
	 * did not end with a contact), before the EntityFound rows. The engineer's voice: the trigger decides it.
	 */
	EntitySeen,
	/** Once in a run, on the return to Normal time after the first contact with the entity (after the return message): the home AI's lines about it. */
	EntityContact,
	/** A payment at a gate's door terminal (said instead of Spend when the act has a row of it). */
	SpendDoor,
	/** A payment at a terminal of a group of lights (said instead of Spend when the act has a row of it). */
	SpendLights,
	/** A payment at a beacon's terminal (said instead of Spend when the act has a row of it). */
	SpendBeacon,
	/** A payment at a log terminal (said instead of Spend when the act has a row of it). */
	SpendLog,
	/** A payment at the twin AI's terminal, before the part it plays (said instead of Spend when the act has a row of it). */
	SpendTwin
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
	case ETextTrigger::EntityFound: return TEXT("EntityFound");
	case ETextTrigger::EntitySeen: return TEXT("EntitySeen");
	case ETextTrigger::EntityContact: return TEXT("EntityContact");
	case ETextTrigger::SpendDoor: return TEXT("SpendDoor");
	case ETextTrigger::SpendLights: return TEXT("SpendLights");
	case ETextTrigger::SpendBeacon: return TEXT("SpendBeacon");
	case ETextTrigger::SpendLog: return TEXT("SpendLog");
	case ETextTrigger::SpendTwin: return TEXT("SpendTwin");
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
	Engineer,
	/**
	 * The engineer speaking inside the paradise: the Paradise voice's background (no cut to black), its text in a dark tone
	 * of home's tint, so that it reads as another speaker than the paradise. Never degraded.
	 */
	EngineerParadise
};

inline const TCHAR* LexToString(ESceneVoice Voice)
{
	switch (Voice)
	{
	case ESceneVoice::Paradise: return TEXT("Paradise");
	case ESceneVoice::HomeAI: return TEXT("HomeAI");
	case ESceneVoice::Engineer: return TEXT("Engineer");
	case ESceneVoice::EngineerParadise: return TEXT("EngineerParadise");
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

/**
 * One line of the home AI in the prologue's maps (DT_Prologue, row name <Group>_<order>). A group is the moment the
 * lines belong to: Wake, Hologram, Hangar, Flight, Light, Hatch, Door. The text channel says a group's rows once, in ascending
 * Order, degraded and with tokens like any home AI message; Flight is the travel screen's line between the two maps and
 * uses its first row only. A group with no rows says nothing. Brief 31A: the same row in DT_Epilogue, the way home
 * (groups Return, Landing, Passage, Chamber: EpilogueGroups); Return and Passage are labels, never typed on the channel.
 */
USTRUCT(BlueprintType)
struct THELASTSHIFT_API FPrologueRow : public FTableRowBase
{
	GENERATED_BODY()

	/** The moment of the prologue the line belongs to (Wake, Hologram, Hangar, Flight, Light, Hatch, Door). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName Group;

	/** Position among the lines of the same group (ascending). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative", meta = (ClampMin = "1", ClampMax = "999"))
	int32 Order = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative", meta = (MultiLine = "true"))
	FText Text;
};

/**
 * One line of the engineer on the text channel (DT_EngineerLines, row name E_<Before>_<Order>): said before the row
 * Before of DT_AIMessages, DT_Prologue or DT_Epilogue (a Landing or a Chamber row), the first time that row is said in
 * the act (in a prologue map, on the way home: in that map), then the row. Several lines before the same row are said in
 * ascending Order. The engineer's voice: never shortened, typed at the full battery's speed, said at Silent too. Never
 * before a row shown as a label (AreaEntry, Flight, Return, Passage). Tokens as the home AI's messages, with the context
 * of the row it comes before.
 */
USTRUCT(BlueprintType)
struct THELASTSHIFT_API FEngineerLineRow : public FTableRowBase
{
	GENERATED_BODY()

	/** The key of the row of DT_AIMessages, DT_Prologue or DT_Epilogue the line comes before. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative")
	FName Before;

	/** Position among the lines before the same row (ascending). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative", meta = (ClampMin = "1", ClampMax = "999"))
	int32 Order = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Narrative", meta = (MultiLine = "true"))
	FText Text;
};

/** The groups of DT_Prologue: the moments at which the home AI speaks in the prologue's maps. */
namespace PrologueGroups
{
	inline const FName Wake(TEXT("Wake"));
	inline const FName Hologram(TEXT("Hologram"));
	inline const FName Hangar(TEXT("Hangar"));
	inline const FName Flight(TEXT("Flight"));
	/** The arrival, when the player steps out of the ship, before Hatch: the flashlight (its hint follows the last row). */
	inline const FName Light(TEXT("Light"));
	inline const FName Hatch(TEXT("Hatch"));
	inline const FName Door(TEXT("Door"));
}

/** Brief 31A: the groups of DT_Epilogue, the way home (rows of the prologue's row struct, FPrologueRow). */
namespace EpilogueGroups
{
	/** The travel screen's label of the flight back (its first row): a label, never typed on the channel. */
	inline const FName Return(TEXT("Return"));
	/** Said on the channel in home's hangar on the return, when the player steps out of the ship. */
	inline const FName Landing(TEXT("Landing"));
	/** A label (Brief 31B). */
	inline const FName Passage(TEXT("Passage"));
	/** Said on the channel in the battery's chamber (Brief 31B). */
	inline const FName Chamber(TEXT("Chamber"));

	/** Group is one of the four. */
	inline bool IsEpilogueGroup(FName Group) { return Group == Return || Group == Landing || Group == Passage || Group == Chamber; }

	/** Group's rows are labels, never typed on the channel (Return, Passage): no engineer line comes before them. */
	inline bool IsLabelGroup(FName Group) { return Group == Return || Group == Passage; }
}
