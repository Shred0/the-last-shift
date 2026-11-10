// SPDX-License-Identifier: MIT

#include "Energy/LightGroupDevice.h"

#include "Components/LightComponent.h"
#include "Energy/EnergySubsystem.h"
#include "Engine/Light.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LightGroupDevice)

void ALightGroupDevice::BeginPlay()
{
	for (ALight* Light : Lights)
	{
		if (ULightComponent* Component = Light ? Light->GetLightComponent() : nullptr)
		{
			if (Component->Mobility != EComponentMobility::Movable)
			{
				UE_LOG(LogEnergy, Warning, TEXT("%s: light %s is not Movable and may not switch at runtime."), *GetName(), *Light->GetName());
			}
			Component->SetCastShadows(false);
		}
	}
	Super::BeginPlay();
}

void ALightGroupDevice::OnPowerStateChanged(bool bNowPowered)
{
	ApplyLights(bNowPowered && !IsTimeStopped());
}

void ALightGroupDevice::OnDeviceTimeStateChanged(ETimeState NewState)
{
	ApplyLights(IsPowered() && NewState == ETimeState::Normal);
}

void ALightGroupDevice::ApplyLights(bool bOn)
{
	for (ALight* Light : Lights)
	{
		if (ULightComponent* Component = Light ? Light->GetLightComponent() : nullptr)
		{
			Component->SetVisibility(bOn);
		}
	}
}

int32 ALightGroupDevice::GetLitCount() const
{
	int32 Count = 0;
	for (const ALight* Light : Lights)
	{
		const ULightComponent* Component = Light ? Light->GetLightComponent() : nullptr;
		Count += (Component && Component->IsVisible()) ? 1 : 0;
	}
	return Count;
}
