// SPDX-License-Identifier: MIT

#include "Run/TheLastShiftSettings.h"

#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Narrative/NarrativeRows.h"
#include "TextChannel/TwinWriting.h"

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

int32 UTheLastShiftSettings::GetPrologueStageOfMap(const FString& PackageName) const
{
	const FString Clean = UWorld::RemovePIEPrefix(PackageName);
	if (!PrologueHomeMap.IsNull() && PrologueHomeMap.ToSoftObjectPath().GetLongPackageName() == Clean)
	{
		return 1;
	}
	if (!PrologueArrivalMap.IsNull() && PrologueArrivalMap.ToSoftObjectPath().GetLongPackageName() == Clean)
	{
		return 2;
	}
	return 0;
}

bool UTheLastShiftSettings::GetMapFlashlight(int32 PrologueStage, bool& bInOutOnAtStart) const
{
	switch (PrologueStage)
	{
	case 1:
		bInOutOnAtStart = bFlashlightAtHome && bInOutOnAtStart;
		return bFlashlightAtHome;
	case 2:
		bInOutOnAtStart = bFlashlightOnAtArrival;
		return true;
	default:
		return true;
	}
}

FHologramTuning UTheLastShiftSettings::GetHologramTuning() const
{
	FHologramTuning T;
	T.Size = FMath::Clamp(Hologram.Size, 50.f, 1000.f);
	T.Points = FMath::Clamp(Hologram.Points, 50, 10000);
	T.PointSize = FMath::Clamp(Hologram.PointSize, 0.5f, 20.f);
	T.EdgeShare = FMath::Clamp(Hologram.EdgeShare, 0.f, 1.f);
	return T;
}

UDataTable* UTheLastShiftSettings::LoadPrologueTable() const
{
	return PrologueTable.IsNull() ? nullptr : PrologueTable.LoadSynchronous();
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

UDataTable* UTheLastShiftSettings::LoadLogsTable() const
{
	return LogsTable.IsNull() ? nullptr : LogsTable.LoadSynchronous();
}

UDataTable* UTheLastShiftSettings::LoadTwinAITable() const
{
	return TwinAITable.IsNull() ? nullptr : TwinAITable.LoadSynchronous();
}

FText UTheLastShiftSettings::GetEndingText(FName RowName) const
{
	const UDataTable* Table = LoadEndingsTable();
	const FEndingRow* Row = Table ? Table->FindRow<FEndingRow>(RowName, TEXT("Ending"), false) : nullptr;
	return Row ? Row->Text : FText::GetEmpty();
}

UDataTable* UTheLastShiftSettings::LoadScenesTable() const
{
	return ScenesTable.IsNull() ? nullptr : ScenesTable.LoadSynchronous();
}

ETextDegradation UTheLastShiftSettings::GetTextLevel(double BatteryFraction) const
{
	const FTextDegradationTuning& T = TextDegradation;
	return UTextDegradationLibrary::LevelFromBattery(BatteryFraction, FMath::Clamp(T.NoArticlesBelow, 0.f, 1.f), FMath::Clamp(T.AbbreviatedBelow, 0.f, 1.f),
		FMath::Clamp(T.ScatteredBelow, 0.f, 1.f), FMath::Clamp(T.SilentBelow, 0.f, 1.f));
}

float UTheLastShiftSettings::GetTextCharsPerSecond(double BatteryFraction) const
{
	const float Empty = FMath::Clamp(TextDegradation.CharsPerSecondEmpty, 1.f, 500.f);
	const float Full = FMath::Clamp(TextDegradation.CharsPerSecondFull, 1.f, 500.f);
	return FMath::Lerp(Empty, Full, static_cast<float>(FMath::Clamp(BatteryFraction, 0.0, 1.0)));
}

int32 UTheLastShiftSettings::GetOpenEndingTwinParts() const
{
	const int32 Setting = FMath::Clamp(OpenEndingTwinParts, 0, 99);
	return Setting > 0 ? Setting : TwinWriting::GetTwinPartCount(LoadTwinAITable());
}

void UTheLastShiftSettings::GetSceneVoiceColors(ESceneVoice Voice, FLinearColor& OutText, FLinearColor& OutBackground) const
{
	switch (Voice)
	{
	case ESceneVoice::Paradise:
		OutText = TextColors.ParadiseText;
		OutBackground = TextColors.ParadiseBackground;
		return;
	case ESceneVoice::Engineer:
		OutText = TextColors.Engineer;
		OutBackground = TextColors.SceneBackground;
		return;
	case ESceneVoice::HomeAI:
	default:
		OutText = TextColors.HomeAI;
		OutBackground = TextColors.SceneBackground;
		return;
	}
}
