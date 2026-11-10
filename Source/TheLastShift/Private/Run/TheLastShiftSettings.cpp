// SPDX-License-Identifier: MIT

#include "Run/TheLastShiftSettings.h"

#include "Engine/World.h"

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
