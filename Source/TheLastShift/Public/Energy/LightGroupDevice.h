// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Energy/PoweredDevice.h"
#include "LightGroupDevice.generated.h"

class ALight;

/**
 * A group of lights of the twin structure, lit while powered and time is Normal: pay to see the way, then walk it in
 * the dark on the scans. While time is Stopped the lights are dark (the screen is black but for the scans) and the
 * device stays powered with its timer frozen; they light again on Resume if still powered.
 * The lights are level actors (movable, so they can switch at runtime); their colour, intensity and radius stay on
 * them. They never cast shadows: the device turns shadow casting off on BeginPlay.
 */
UCLASS(Blueprintable)
class THELASTSHIFT_API ALightGroupDevice : public APoweredDevice
{
	GENERATED_BODY()

public:
	/** Lights switched by this device. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Lights")
	TArray<TObjectPtr<ALight>> Lights;

	/** Lights currently lit (of those in Lights that exist). */
	UFUNCTION(BlueprintPure, Category = "Lights")
	int32 GetLitCount() const;

protected:
	virtual void BeginPlay() override;
	virtual void OnPowerStateChanged(bool bNowPowered) override;
	virtual void OnDeviceTimeStateChanged(ETimeState NewState) override;

private:
	void ApplyLights(bool bOn);
};
