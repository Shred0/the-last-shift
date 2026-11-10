// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Time/TimeState.h"
#include "PoweredDevice.generated.h"

class UTimeStateSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPoweredDeviceChanged);

/**
 * A system of the twin structure that energy switches on (paid at an APowerTerminal, in Normal time). It may stay
 * powered only for PoweredSeconds of Normal time: the timer stands still while time is Stopped, because the device is
 * part of the world that stopped. A Stop that ends with a time jump (Jumped, RewoundAndJumped) switches it off when
 * bResetOnJump; a rewind (Rewound) leaves it as it was, remaining time included.
 *
 * Subclasses react in OnPowerStateChanged (C++) or ReceivePowerStateChanged (Blueprint).
 */
UCLASS(Abstract, Blueprintable)
class THELASTSHIFT_API APoweredDevice : public AActor
{
	GENERATED_BODY()

public:
	APoweredDevice();

	/** Normal-time seconds the device stays powered once paid. 0 = stays on until a time jump. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Device", meta = (ClampMin = "0.0", ClampMax = "600.0", Units = "s"))
	float PoweredSeconds = 0.f;

	/** A Stop that ends with a time jump switches the device off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Device")
	bool bResetOnJump = true;

	/** Switches on with a full PoweredSeconds (also when already on: paying again refills the timer). */
	UFUNCTION(BlueprintCallable, Category = "Device")
	void PowerOn();

	/** Switches off now. */
	UFUNCTION(BlueprintCallable, Category = "Device")
	void PowerOff();

	UFUNCTION(BlueprintPure, Category = "Device")
	bool IsPowered() const { return bPowered; }

	/** True when powered with a finite PoweredSeconds. */
	UFUNCTION(BlueprintPure, Category = "Device")
	bool IsTimed() const;

	/** Normal-time seconds left before the device switches off; 0 when off or untimed. */
	UFUNCTION(BlueprintPure, Category = "Device", meta = (Units = "s"))
	float GetRemainingSeconds() const;

	/** Remaining / PoweredSeconds; 1 when powered and untimed, 0 when off. */
	UFUNCTION(BlueprintPure, Category = "Device")
	float GetPoweredFraction() const;

	UPROPERTY(BlueprintAssignable, Category = "Device|Events")
	FOnPoweredDeviceChanged OnPowered;

	UPROPERTY(BlueprintAssignable, Category = "Device|Events")
	FOnPoweredDeviceChanged OnUnpowered;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/** Called after every power change, before OnPowered / OnUnpowered. */
	virtual void OnPowerStateChanged(bool bNowPowered) {}

	/** Called after the device has handled a time state change (timer frozen or resumed, jump reset applied). */
	virtual void OnDeviceTimeStateChanged(ETimeState NewState) {}

	UFUNCTION(BlueprintImplementableEvent, Category = "Device", meta = (DisplayName = "Power State Changed"))
	void ReceivePowerStateChanged(bool bNowPowered);

	bool IsTimeStopped() const;
	double GetNow() const;

private:
	UFUNCTION()
	void HandleTimeStateChanged(ETimeState NewState, ETimeState OldState, EStopExitReason Reason);

	void SetPowered(bool bNewPowered, const TCHAR* Why);

	UPROPERTY(Transient)
	TObjectPtr<UTimeStateSubsystem> TimeState;

	bool bPowered = false;

	/** Remaining seconds at MarkTime; the timer runs from MarkTime while time is Normal. */
	double RemainingAtMark = 0.0;
	double MarkTime = 0.0;

	/** PoweredSeconds when the device was last powered (the fraction's denominator). */
	float PoweredDuration = 0.f;
};
