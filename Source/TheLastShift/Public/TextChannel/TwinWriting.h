// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

class UDataTable;

/**
 * The other humanity's writing, as the game reads it from the narrative tables: the logs (FLogRow) and the twin AI's
 * parts (FTwinAIRow). Plain functions on a table: the text always comes from the table, never from a copy.
 */
namespace TwinWriting
{
	/** Orders of every log entry of Table, newest (highest Order) first. Empty for a null table or one of another row struct. */
	THELASTSHIFT_API TArray<int32> GetLogOrdersNewestFirst(const UDataTable* Table);

	/** The text of the log entry with Order; empty when there is none. */
	THELASTSHIFT_API FText FindLogText(const UDataTable* Table, int32 Order);

	/**
	 * The entries of group GroupIndex when OrdersNewestFirst is dealt in equal consecutive groups to TerminalCount
	 * terminals: ceil(E / T) entries per group, the last group smaller. Group 0 holds the newest. Empty past the last group.
	 */
	THELASTSHIFT_API TArray<int32> DealLogGroup(const TArray<int32>& OrdersNewestFirst, int32 TerminalCount, int32 GroupIndex);

	/** How many parts the twin AI has: the highest Part of Table's rows (0 without rows). */
	THELASTSHIFT_API int32 GetTwinPartCount(const UDataTable* Table);

	/** Row names of Part in Table, in ascending Order (ties by row name). */
	THELASTSHIFT_API TArray<FName> GetTwinPartRows(const UDataTable* Table, int32 Part);

	/** The texts of Part's rows, in order. */
	THELASTSHIFT_API TArray<FText> GetTwinPartTexts(const UDataTable* Table, int32 Part);

	/**
	 * Cells of a twin message of Length characters after Units characters' worth of typing: the barcode cells shown
	 * (Units, up to Length) and the cells translated (LagChars behind, never ahead, catching up after the barcodes end).
	 */
	THELASTSHIFT_API void GetTwinCounts(int32 Length, double Units, int32 LagChars, int32& OutShown, int32& OutTranslated);
}
