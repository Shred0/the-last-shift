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
