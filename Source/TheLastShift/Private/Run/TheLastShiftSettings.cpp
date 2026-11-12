// SPDX-License-Identifier: MIT

#include "Run/TheLastShiftSettings.h"

#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Narrative/NarrativeRows.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TheLastShiftSettings)

int32 UTheLastShiftSettings::GetActOfMap(const FString& PackageName) const
{
	const FString Clean = UWorld::RemovePIEPrefix(PackageName);
	for (int32 Index = 0; Index < AreaMaps.Num(); ++Index)
	{
		if (!AreaMaps[Index].IsNull() && AreaMaps[Index].ToSoftObjectPath().GetLongPackageName() == Clean)
		{
			return Index + 1;
		}
	}
	return 0;
}

const FAreaRecipe* UTheLastShiftSettings::FindRecipe(int32 Act) const
{
	if (Act <= 0)
	{
		return &TestRecipe;
	}
	return AreaRecipes.IsValidIndex(Act - 1) ? &AreaRecipes[Act - 1] : nullptr;
}

int32 UTheLastShiftSettings::GetSeed(int32 Act) const
{
	if (Act <= 0)
	{
		return TestSeed;
	}
	return AreaSeeds.IsValidIndex(Act - 1) ? AreaSeeds[Act - 1] : -1;
}

bool UTheLastShiftSettings::GetActTuning(int32 Act, FActTuning& OutTuning) const
{
	const FActTuning* Tuning = Act <= 0 ? &TestActTuning : (ActTuning.IsValidIndex(Act - 1) ? &ActTuning[Act - 1] : nullptr);
	if (!Tuning)
	{
		return false;
	}
	OutTuning.JumpHomeSeconds = FMath::Clamp(Tuning->JumpHomeSeconds, 0.0, 1.0e12);
	OutTuning.JumpBatteryDrop = FMath::Clamp(Tuning->JumpBatteryDrop, 0.0, 1.0);
	return true;
}

UDataTable* UTheLastShiftSettings::LoadAIMessagesTable() const
{
	return AIMessagesTable.IsNull() ? nullptr : AIMessagesTable.LoadSynchronous();
}

UDataTable* UTheLastShiftSettings::LoadEndingsTable() const
{
	return EndingsTable.IsNull() ? nullptr : EndingsTable.LoadSynchronous();
}

FText UTheLastShiftSettings::GetEndingText(FName RowName) const
{
	const UDataTable* Table = LoadEndingsTable();
	const FEndingRow* Row = Table ? Table->FindRow<FEndingRow>(RowName, TEXT("Ending"), false) : nullptr;
	return Row ? Row->Text : FText::GetEmpty();
}
