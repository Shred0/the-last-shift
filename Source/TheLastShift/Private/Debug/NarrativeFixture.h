// SPDX-License-Identifier: MIT

#pragma once

// Neutral fixture tables for the tests that need home AI messages or ending texts (Brief 7), so that no test depends on
// the wording, the number or the length of the rows in Narrative/. Development builds only.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

class UDataTable;
class UWorld;

namespace NarrativeFixture
{
	/**
	 * A new transient DT_AIMessages-shaped table: A1_AreaStart_1..3, A1_FirstEnergy_1, and for every act 1-4
	 * A<act>_JumpReturn_1..2 (all four tokens and the word "still"), A<act>_AreaEntry_1 ({home}, {battery}) and
	 * A<act>_Spend_1 ({battery}). Every text is at least 60 characters long.
	 */
	UDataTable* BuildAIMessages();

	/** A new transient DT_Endings-shaped table with the rows Inject, Open and Loss. */
	UDataTable* BuildEndings();

	/**
	 * Points the project settings' AIMessagesTable and EndingsTable at fixture tables (kept alive until Restore) and
	 * gives every text channel already in World the fixture messages. Everything that loads a narrative table after
	 * this (the home AI of a new map, the travel label, the loss text) reads the fixtures. No-op when installed.
	 */
	void Install(UWorld* World);

	/** Puts the game's tables back in the project settings and in every text channel of World. No-op when not installed. */
	void Restore(UWorld* World);

	bool IsInstalled();
}

#endif // !UE_BUILD_SHIPPING
