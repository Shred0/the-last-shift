// SPDX-License-Identifier: MIT

#include "Energy/TwinAITerminal.h"

#include "Energy/EnergySubsystem.h"
#include "Run/RunSubsystem.h"
#include "Run/TheLastShiftSettings.h"
#include "TextChannel/TwinWriting.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TwinAITerminal)

ATwinAITerminal::ATwinAITerminal()
{
	Cost = 1;
}

int32 ATwinAITerminal::GetPartCount() const
{
	return TwinWriting::GetTwinPartCount(UTheLastShiftSettings::Get()->LoadTwinAITable());
}

int32 ATwinAITerminal::GetPartsPlayed() const
{
	const URunSubsystem* Run = URunSubsystem::Get(this);
	return Run ? Run->GetArchive().TwinPartsPlayed : 0;
}

int32 ATwinAITerminal::GetCost() const
{
	// The first payment wakes the twin AI; every later one plays a part (Brief 26; one owner: the settings).
	return GetPartsPlayed() == 0 ? UTheLastShiftSettings::GetTwinAIWakeCost() : UTheLastShiftSettings::GetTwinAIPartCost();
}

UStaticMesh* ATwinAITerminal::GetModelledBodyMesh() const
{
	return UTheLastShiftSettings::GetTwinTerminalBodyMesh();
}

bool ATwinAITerminal::GetScreenFace(FVector& OutCentre, FVector& OutNormal, FVector& OutUp, FVector2D& OutSize) const
{
	const UStaticMesh* Shown = Mesh ? Mesh->GetStaticMesh() : nullptr;
	if (!bModelledBody || !Shown || Shown != UTheLastShiftSettings::GetTwinTerminalBodyMesh())
	{
		return false;
	}
	// SM_TwinTerminal's slanted desk in its own frame (pivot at its base centre, front +X): 120 cm wide, from X 5 at 128 cm
	// up to X 35 at 110 cm, facing up and forward.
	OutCentre = FVector(20.f, 0.f, 119.f);
	OutNormal = FVector(0.514f, 0.f, 0.857f).GetSafeNormal();
	OutUp = FVector(-30.f, 0.f, 18.f).GetSafeNormal();
	OutSize = FVector2D(116.f, 33.f);
	return true;
}

bool ATwinAITerminal::AcceptsPayment() const
{
	return GetPartsPlayed() < GetPartCount();
}

void ATwinAITerminal::HandlePaid(APawn* InstigatorPawn)
{
	const int32 Part = GetPartsPlayed() + 1;
	URunSubsystem* Run = URunSubsystem::Get(this);
	LastPlayedPart = Run && Run->StoreTwinPart(Part) ? Part : 0;
	UE_LOG(LogEnergy, Log, TEXT("%s: twin AI part %d of %d played."), *GetName(), LastPlayedPart, GetPartCount());
}
