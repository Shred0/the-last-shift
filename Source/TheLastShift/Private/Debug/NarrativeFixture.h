// SPDX-License-Identifier: MIT

#pragma once

// Neutral fixture tables for the tests that need home AI messages or ending texts (Brief 7), so that no test depends on
// the wording, the number or the length of the rows in Narrative/. Development builds only.

#include "CoreMinimal.h"
#include "Narrative/NarrativeRows.h"

#if !UE_BUILD_SHIPPING

class UDataTable;
class UWorld;

namespace NarrativeFixture
{
	/**
	 * A new transient DT_AIMessages-shaped table: A1_AreaStart_1..3, A1_FirstEnergy_1, and for every act 1-4
	 * A<act>_JumpReturn_1..2 (all four tokens and the word "still"), A<act>_AreaEntry_1 ({home}, {battery}),
	 * A<act>_Spend_1 ({battery}) and A<act>_EntityFound_1 ({battery}). Every text is at least 60 characters long.
	 */
	UDataTable* BuildAIMessages();

	/** A new transient DT_Endings-shaped table with the rows Inject, Open and Loss. */
	UDataTable* BuildEndings();

	/**
	 * A new transient DT_Logs-shaped table with LogCount rows L_1..L_<LogCount> (Order = the number), each a neutral text
	 * of 120 to 160 characters that names its order ("Fixture log 7 ...").
	 */
	UDataTable* BuildLogs(int32 LogCount = 8);

	/** A new transient DT_TwinAI-shaped table: three parts of two rows each, T_<part>_<order>, neutral texts of about 70 characters. */
	UDataTable* BuildTwinAI();

	/**
	 * A new transient DT_Scenes-shaped table. With bPrologue: Prologue_1..6 (Paradise, Paradise, then HomeAI; line 3 is
	 * three sentences long, line 4 carries the cue FixtureCue). Always Ending_1 (HomeAI, with articles and long words so
	 * that every level differs).
	 */
	UDataTable* BuildScenes(bool bPrologue = true);

	/**
	 * A new transient DT_Prologue-shaped table: Wake_1..2, Hologram_1, Hangar_1, Flight_1..2, Light_1..2, Hatch_1..2 and Door_1..2,
	 * less the rows of the groups in Without (each text names its group and order, see GetPrologueLineText).
	 */
	UDataTable* BuildPrologue(const TArray<FName>& Without = TArray<FName>());

	/** The text of the fixture's prologue row <Group>_<Order>. */
	FString GetPrologueLineText(FName Group, int32 Order);

	/**
	 * Brief 31A: a new transient DT_Epilogue-shaped table (FPrologueRow): Return_1, Landing_1..2 (with {home} and {battery}),
	 * Passage_1 and Chamber_1, less the rows of the groups in Without (each text names its group and order).
	 */
	UDataTable* BuildEpilogue(const TArray<FName>& Without = TArray<FName>());

	/** The text of the fixture's epilogue row <Group>_<Order> (tokens not replaced). */
	FString GetEpilogueLineText(FName Group, int32 Order);

	/** Replaces the installed epilogue fixture with one without the rows of the groups in Without. No-op when not installed. */
	void SetEpilogueGroups(const TArray<FName>& Without);

	/** Replaces the installed prologue fixture with one without the rows of the groups in Without. No-op when not installed. */
	void SetPrologueGroups(const TArray<FName>& Without);

	/** The text of the fixture's Ending_1 row, in full. */
	FString GetEndingMessageText();

	/** Replaces the installed scenes fixture with one with or without its Prologue rows. No-op when not installed. */
	void SetPrologueRows(bool bPrologue);

	/** Replaces the installed logs fixture with one of LogCount rows (the settings point at it). No-op when not installed. */
	void SetLogCount(int32 LogCount);

	/** One engineer line of a fixture: said before the row Before, at Order among the lines before it. */
	struct FEngineerLine
	{
		FName Before;
		int32 Order = 1;
	};

	/** The text of the fixture's engineer line before Before at Order: it names its row and order, about 70 characters or more. */
	FString GetEngineerLineText(FName Before, int32 Order);

	/** The row name of that line: E_<Before>_<Order>. */
	FName GetEngineerLineName(FName Before, int32 Order);

	/**
	 * Replaces the installed engineer fixture (empty at Install) with one holding Lines, points the settings at it and gives
	 * it to every text channel of World. No-op when not installed.
	 */
	void SetEngineerLines(UWorld* World, const TArray<FEngineerLine>& Lines);

	/** Adds a row A<act>_<Trigger>_<order> to the installed messages fixture (a neutral text that names it). No-op when not installed. */
	void AddMessageRow(ETextTrigger Trigger, int32 Act, int32 Order);

	/** The text of a row added by AddMessageRow. */
	FString GetAddedMessageText(ETextTrigger Trigger, int32 Act, int32 Order);

	/**
	 * Points the project settings' AIMessagesTable, EndingsTable, LogsTable, TwinAITable, ScenesTable, PrologueTable,
	 * EpilogueTable and EngineerLinesTable (empty: no engineer line unless a test sets some) at fixture tables (kept alive
	 * until Restore) and gives every text channel already in World the fixture messages, twin rows and engineer lines. Everything that
	 * loads a narrative table after this (the home AI of a new map, the travel label, the loss text, the terminals, the
	 * archive) reads the fixtures. No-op when installed.
	 */
	void Install(UWorld* World);

	/** Puts the game's tables back in the project settings and in every text channel of World. No-op when not installed. */
	void Restore(UWorld* World);

	bool IsInstalled();
}

#endif // !UE_BUILD_SHIPPING
