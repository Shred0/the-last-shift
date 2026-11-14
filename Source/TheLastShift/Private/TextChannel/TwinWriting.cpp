// SPDX-License-Identifier: MIT

#include "TextChannel/TwinWriting.h"

#include "Engine/DataTable.h"
#include "Narrative/NarrativeRows.h"

namespace TwinWriting
{
	template <class TRow>
	static bool Holds(const UDataTable* Table)
	{
		return Table && Table->GetRowStruct() && Table->GetRowStruct()->IsChildOf(TRow::StaticStruct());
	}

	TArray<int32> GetLogOrdersNewestFirst(const UDataTable* Table)
	{
		TArray<int32> Orders;
		if (Holds<FLogRow>(Table))
		{
			for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
			{
				Orders.AddUnique(reinterpret_cast<const FLogRow*>(Pair.Value)->Order);
			}
		}
		Orders.Sort([](int32 A, int32 B) { return A > B; });
		return Orders;
	}

	FText FindLogText(const UDataTable* Table, int32 Order)
	{
		if (Holds<FLogRow>(Table))
		{
			for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
			{
				const FLogRow* Row = reinterpret_cast<const FLogRow*>(Pair.Value);
				if (Row->Order == Order)
				{
					return Row->Text;
				}
			}
		}
		return FText::GetEmpty();
	}

	TArray<int32> DealLogGroup(const TArray<int32>& OrdersNewestFirst, int32 TerminalCount, int32 GroupIndex)
	{
		TArray<int32> Group;
		const int32 Entries = OrdersNewestFirst.Num();
		if (Entries == 0 || TerminalCount <= 0 || GroupIndex < 0)
		{
			return Group;
		}
		const int32 Size = FMath::DivideAndRoundUp(Entries, TerminalCount);
		for (int32 Index = GroupIndex * Size; Index < FMath::Min((GroupIndex + 1) * Size, Entries); ++Index)
		{
			Group.Add(OrdersNewestFirst[Index]);
		}
		return Group;
	}

	int32 GetTwinPartCount(const UDataTable* Table)
	{
		int32 Parts = 0;
		if (Holds<FTwinAIRow>(Table))
		{
			for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
			{
				Parts = FMath::Max(Parts, reinterpret_cast<const FTwinAIRow*>(Pair.Value)->Part);
			}
		}
		return Parts;
	}

	TArray<FName> GetTwinPartRows(const UDataTable* Table, int32 Part)
	{
		TArray<TPair<int32, FName>> Found;
		if (Holds<FTwinAIRow>(Table))
		{
			for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
			{
				const FTwinAIRow* Row = reinterpret_cast<const FTwinAIRow*>(Pair.Value);
				if (Row->Part == Part)
				{
					Found.Emplace(Row->Order, Pair.Key);
				}
			}
		}
		Found.Sort([](const TPair<int32, FName>& A, const TPair<int32, FName>& B)
		{
			return A.Key != B.Key ? A.Key < B.Key : A.Value.LexicalLess(B.Value);
		});
		TArray<FName> Names;
		for (const TPair<int32, FName>& Pair : Found)
		{
			Names.Add(Pair.Value);
		}
		return Names;
	}

	TArray<FText> GetTwinPartTexts(const UDataTable* Table, int32 Part)
	{
		TArray<FText> Texts;
		for (const FName& Name : GetTwinPartRows(Table, Part))
		{
			if (const FTwinAIRow* Row = Table->FindRow<FTwinAIRow>(Name, TEXT("TwinWriting"), false))
			{
				Texts.Add(Row->Text);
			}
		}
		return Texts;
	}

	void GetTwinCounts(int32 Length, double Units, int32 LagChars, int32& OutShown, int32& OutTranslated)
	{
		// The same tolerance as the typewriter: frame times summing to a whole second reach the last character.
		const int32 Typed = FMath::Max(0, static_cast<int32>(FMath::FloorToDouble(FMath::Max(Units, 0.0) + 1.e-4)));
		OutShown = FMath::Clamp(Typed, 0, FMath::Max(Length, 0));
		OutTranslated = FMath::Clamp(Typed - FMath::Max(LagChars, 0), 0, OutShown);
	}
}
