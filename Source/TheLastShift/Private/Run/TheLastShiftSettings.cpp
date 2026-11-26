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

bool UTheLastShiftSettings::IsChamberMap(const FString& PackageName) const
{
	return !ChamberMap.IsNull() && ChamberMap.ToSoftObjectPath().GetLongPackageName() == UWorld::RemovePIEPrefix(PackageName);
}

bool UTheLastShiftSettings::IsHomeLookMap(const FString& PackageName) const
{
	const FString Clean = UWorld::RemovePIEPrefix(PackageName);
	for (const TSoftObjectPtr<UWorld>& Map : SurfaceLook.HomeMaps)
	{
		if (!Map.IsNull() && Map.ToSoftObjectPath().GetLongPackageName() == Clean)
		{
			return true;
		}
	}
	return false;
}

FFlashlightTuning UTheLastShiftSettings::GetFlashlightTuning() const
{
	FFlashlightTuning F = Flashlight;
	F.Intensity = FMath::Clamp(F.Intensity, 0.f, 1000000.f);
	F.FalloffExponent = FMath::Clamp(F.FalloffExponent, 0.5f, 16.f);
	F.AttenuationRadius = FMath::Clamp(F.AttenuationRadius, 100.f, 20000.f);
	F.InnerConeAngle = FMath::Clamp(F.InnerConeAngle, 0.f, 80.f);
	F.OuterConeAngle = FMath::Clamp(F.OuterConeAngle, FMath::Max(1.f, F.InnerConeAngle), 80.f);
	return F;
}

FSurfaceVariant UTheLastShiftSettings::GetSurfaceVariant(bool bHome) const
{
	FSurfaceVariant V = bHome ? SurfaceLook.Home : SurfaceLook.Twin;
	V.AccentStrength = FMath::Clamp(V.AccentStrength, 0.f, 1.f);
	V.AccentLevel = FMath::Clamp(V.AccentLevel, 0.f, 8.f);
	V.Tone = FMath::Clamp(V.Tone, 0.1f, 4.f);
	V.Wear = FMath::Clamp(V.Wear, 0.f, 1.f);
	V.Dirt = FMath::Clamp(V.Dirt, 0.f, 1.f);
	V.PanelVariation = FMath::Clamp(V.PanelVariation, 0.f, 0.5f);
	return V;
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

FRackLightsTuning UTheLastShiftSettings::GetRackLightsTuning() const
{
	FRackLightsTuning T;
	T.PerServer = FMath::Clamp(RackLights.PerServer, 0, 6);
	T.PeriodMinSeconds = FMath::Clamp(RackLights.PeriodMinSeconds, 0.1f, 30.f);
	T.PeriodMaxSeconds = FMath::Clamp(RackLights.PeriodMaxSeconds, T.PeriodMinSeconds, 30.f);
	T.OnShare = FMath::Clamp(RackLights.OnShare, 0.05f, 0.95f);
	T.Level = FMath::Clamp(RackLights.Level, 0.f, 50.f);
	T.SecondColor = RackLights.SecondColor;
	T.SecondShare = FMath::Clamp(RackLights.SecondShare, 0.f, 1.f);
	T.SizeCm = FMath::Clamp(RackLights.SizeCm, 0.2f, 5.f);
	return T;
}

FHologramTuning UTheLastShiftSettings::GetHologramTuning() const
{
	FHologramTuning T;
	T.Size = FMath::Clamp(Hologram.Size, 50.f, 1000.f);
	T.Points = FMath::Clamp(Hologram.Points, 50, 10000);
	T.PointSize = FMath::Clamp(Hologram.PointSize, 0.5f, 20.f);
	T.EdgeShare = FMath::Clamp(Hologram.EdgeShare, 0.f, 1.f);
	T.TurnSeconds = FMath::Clamp(Hologram.TurnSeconds, 5.f, 600.f);
	T.DriftDistance = FMath::Clamp(Hologram.DriftDistance, 0.f, 20.f);
	T.DriftSeconds = FMath::Clamp(Hologram.DriftSeconds, 0.5f, 30.f);
	T.DriftPeriodSpread = FMath::Clamp(Hologram.DriftPeriodSpread, 0.f, 0.9f);
	// Each node inside the octahedron: components in [-1, 1], |x| + |y| + |z| at most 0.95.
	const auto Node = [](const FVector& In)
	{
		FVector N(FMath::Clamp(In.X, -1.0, 1.0), FMath::Clamp(In.Y, -1.0, 1.0), FMath::Clamp(In.Z, -1.0, 1.0));
		const double L1 = FMath::Abs(N.X) + FMath::Abs(N.Y) + FMath::Abs(N.Z);
		return L1 > 0.95 ? N * (0.95 / L1) : N;
	};
	T.RouteNode1 = Node(Hologram.RouteNode1);
	T.RouteNode2 = Node(Hologram.RouteNode2);
	T.RouteNode3 = Node(Hologram.RouteNode3);
	T.RouteNode4 = Node(Hologram.RouteNode4);
	T.RouteNodeSize = FMath::Clamp(Hologram.RouteNodeSize, 2.f, 40.f);
	T.RouteLineWidth = FMath::Clamp(Hologram.RouteLineWidth, 0.5f, 20.f);
	T.BeamRods = FMath::Clamp(Hologram.BeamRods, 0, 32);
	T.BeamRodRadius = FMath::Clamp(Hologram.BeamRodRadius, 0.f, 500.f);
	T.BeamRodTop = FMath::Clamp(Hologram.BeamRodTop, 0.f, 300.f);
	T.BeamRodAngle = FMath::Clamp(Hologram.BeamRodAngle, -180.f, 180.f);
	T.BeamLength = FMath::Clamp(Hologram.BeamLength, 0.1f, 1.f);
	T.BeamMargin = FMath::Clamp(Hologram.BeamMargin, 0.f, 100.f);
	T.BeamFadePower = FMath::Clamp(Hologram.BeamFadePower, 1.f, 8.f);
	T.BeamSpreadShare = FMath::Clamp(Hologram.BeamSpreadShare, 0.2f, 1.f);
	T.BeamLevel = FMath::Clamp(Hologram.BeamLevel, 0.f, 1.f);
	T.BeamFlicker = FMath::Clamp(Hologram.BeamFlicker, 0.f, 1.f);
	T.BeamFlickerHz = FMath::Clamp(Hologram.BeamFlickerHz, 0.1f, 30.f);
	return T;
}

UDataTable* UTheLastShiftSettings::LoadPrologueTable() const
{
	return PrologueTable.IsNull() ? nullptr : PrologueTable.LoadSynchronous();
}

UDataTable* UTheLastShiftSettings::LoadEpilogueTable() const
{
	return EpilogueTable.IsNull() ? nullptr : EpilogueTable.LoadSynchronous();
}

UDataTable* UTheLastShiftSettings::LoadGroupTable(FName Group) const
{
	return EpilogueGroups::IsEpilogueGroup(Group) ? LoadEpilogueTable() : LoadPrologueTable();
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
	OutTuning.EntityForm = GetEntityFormOfAct(Act);
	return true;
}

EEntityForm UTheLastShiftSettings::GetEntityFormOfAct(int32 Act) const
{
	// A map outside AreaMaps, and an act with no entry, take act 1's form: the one owner stays ActTuning's first entry.
	if (ActTuning.IsValidIndex(Act - 1))
	{
		return ActTuning[Act - 1].EntityForm;
	}
	return ActTuning.Num() > 0 ? ActTuning[0].EntityForm : EEntityForm::Procedural;
}

const FEntityFormTuning* UTheLastShiftSettings::FindEntityForm(EEntityForm Form) const
{
	if (Form == EEntityForm::Procedural)
	{
		return nullptr;
	}
	return EntityForms.Forms.FindByPredicate([Form](const FEntityFormTuning& Entry) { return Entry.Form == Form; });
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

UDataTable* UTheLastShiftSettings::LoadEngineerLinesTable() const
{
	return EngineerLinesTable.IsNull() ? nullptr : EngineerLinesTable.LoadSynchronous();
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
	case ESceneVoice::EngineerParadise:
		// The engineer inside the paradise: the paradise's background, no cut to black.
		OutText = TextColors.EngineerParadise;
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
