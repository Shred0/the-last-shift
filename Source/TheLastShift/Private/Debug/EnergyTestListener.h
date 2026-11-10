// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "EnergyTestListener.generated.h"

class ADoorDevice;
class APowerTerminal;
class UEnergySubsystem;

/**
 * tls.ResidueTest / tls.DeviceTest / tls.MapChangeTest helper: records energy, terminal and door events with their
 * world time. Debug only, never referenced by game code.
 */
UCLASS(Transient)
class UTlsEnergyTestListener : public UObject
{
	GENERATED_BODY()

public:
	void BindEnergy(UEnergySubsystem* Energy);
	void BindTerminal(APowerTerminal* Terminal);
	void BindDoor(ADoorDevice* Door);
	void UnbindAll();

	int32 EnergyEvents = 0;
	int32 LastBanked = -1;
	int32 LastSession = -1;

	int32 Activations = 0;
	/** World time of the last OnActivated (needs a bound door for the clock); -1 before. */
	double LastActivationTime = -1.0;
	int32 Refusals = 0;
	bool bLastRefusalStopped = false;

	/** World time of the door's last OnUnpowered and last close; -1 before. */
	double DoorUnpoweredTime = -1.0;
	double DoorClosedTime = -1.0;

private:
	UFUNCTION()
	void HandleEnergyChanged(int32 BankedEnergy, int32 SessionEnergy);

	UFUNCTION()
	void HandleActivated(int32 Cost);

	UFUNCTION()
	void HandleInsufficient(int32 Cost, int32 BankedEnergy, bool bTimeStopped);

	UFUNCTION()
	void HandleDoorUnpowered();

	UFUNCTION()
	void HandleDoorOpenChanged(bool bOpen);

	double GetWorldTime() const;

	TWeakObjectPtr<UEnergySubsystem> BoundEnergy;
	TArray<TWeakObjectPtr<APowerTerminal>> BoundTerminals;
	TWeakObjectPtr<ADoorDevice> BoundDoor;
};
