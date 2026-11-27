// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Time/TimeState.h"
#include "ChamberBattery.generated.h"

class AEpilogueChamber;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;
class UTimeStateSubsystem;

/**
 * Brief 36B: the battery of the battery's access hall (Epilogue_Chamber), one placed at the column's axis on the hall's
 * floor: its place is the battery's (the hum sounds from there). When play begins it finds the column's pieces in its map
 * by their meshes: the glow strips (SM_BatteryTierGlow, SM_BatteryTierBGlow, SM_BatterySpineGlow) and the rings
 * (SM_BatteryRing). Its values are BatteryLook's in UTheLastShiftSettings, their one owner.
 *
 * - The glow: MI_BatteryGlow (unlit, emissive), in home's tint lightened by GlowTintShare, its level and the speed of its
 *   pulses following the battery the hall shows (the run's, UHomeStateSubsystem; tls.Chamber.Battery sets one for the
 *   session). The pulses travel along each strip, upward on the spines (in world height, so the stacked spines read as
 *   one strip). Light, not a surface: shown in Normal time only (hidden while Stopped), never in a scan's capture, casting
 *   nothing and lighting nothing; the pulses stand still behind the pause menu (the run's clock).
 * - The rings turn about the column's axis at RingDegreesPerSecond in Normal time, each the other way from the one below
 *   it; they stand still while Stopped and behind the pause menu. A scan shows a ring where it stands.
 * - The aftermath of the choice (AEpilogueChamber): Inject, the pulses run faster and faster, the glow rises to
 *   GlowLevelFull and past it by AftermathSurge, the gauges climb to full; Open, the glow sinks to nothing, the pulses stop,
 *   the rings slow to a stand and the monitors go out one after another across the wall.
 *
 * It knows nothing of sound: the audio reads what it announces (GetBatteryShown, GetAftermathEnding, GetAftermathAlpha).
 */
UCLASS()
class THELASTSHIFT_API AChamberBattery : public AActor
{
	GENERATED_BODY()

public:
	AChamberBattery();

	virtual void Tick(float DeltaSeconds) override;

	/** The glow's look (MI_BatteryGlow). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Battery")
	TObjectPtr<UMaterialInterface> GlowMaterial;

	/** The battery the hall shows now: the session's (tls.Chamber.Battery) or the run's, 0 to 1. */
	UFUNCTION(BlueprintPure, Category = "Battery")
	float GetBatteryShown() const;

	/** The glow's emissive level now (BatteryLook's, the aftermath included; 0 while hidden counts as shown level). */
	UFUNCTION(BlueprintPure, Category = "Battery")
	float GetGlowLevel() const { return GlowLevel; }

	/** The pulses' speed now (metres a second) and their phase (metres travelled). */
	UFUNCTION(BlueprintPure, Category = "Battery")
	float GetPulseSpeed() const { return PulseSpeed; }

	UFUNCTION(BlueprintPure, Category = "Battery")
	float GetPulsePhase() const { return static_cast<float>(PulsePhase); }

	/** The glow's colour (home's tint lightened toward white by GlowTintShare). */
	UFUNCTION(BlueprintPure, Category = "Battery")
	FLinearColor GetGlowColor() const;

	/** The glow strips are shown now (Normal time). */
	UFUNCTION(BlueprintPure, Category = "Battery")
	bool AreGlowsShown() const { return bGlowsShown; }

	/** The level the monitors' gauges show (0 to 1): the battery, climbing to full in Inject's aftermath. */
	UFUNCTION(BlueprintPure, Category = "Battery")
	float GetGaugeLevel() const;

	/** The share of the monitors gone out across the wall (0; Open's aftermath: rising to 1 before its end). */
	UFUNCTION(BlueprintPure, Category = "Battery")
	float GetScreenOutShare() const;

	/** The aftermath the hall shows: its ending (None before the choice) and how far it has gone (0 to 1). */
	UFUNCTION(BlueprintPure, Category = "Battery")
	FName GetAftermathEnding() const;

	UFUNCTION(BlueprintPure, Category = "Battery")
	float GetAftermathAlpha() const;

	/** The rings' speed now, degrees a second (RingDegreesPerSecond; slowing to 0 in Open's aftermath). */
	UFUNCTION(BlueprintPure, Category = "Battery")
	float GetRingSpeed() const { return RingSpeed; }

	const TArray<TObjectPtr<UStaticMeshComponent>>& GetGlows() const { return Glows; }
	const TArray<TObjectPtr<UStaticMeshComponent>>& GetRings() const { return Rings; }

	/** Ring Index's turn since play began (degrees, signed: + one way, - the other), lowest ring first. */
	float GetRingTurn(int32 Index) const { return RingTurns.IsValidIndex(Index) ? static_cast<float>(RingTurns[Index]) : 0.f; }

	/** The two glow materials (the tiers' and the spines'). */
	UMaterialInstanceDynamic* GetGlowMaterial(bool bSpines) const { return bSpines ? SpineMid : TierMid; }

	/** The battery of World, or null. */
	static AChamberBattery* Find(const UWorld* World);

	/** tls.Chamber.Battery: the hall shows Fraction for the session (a value below 0 clears it: the run's again). */
	static void SetSessionBattery(float Fraction);
	static float GetSessionBattery();

	/** Test only: the glow shown while Stopped too (to prove a scan does not capture it), or hidden in every state. */
	void SetShownWhileStoppedForTest(bool bShown);
	void SetHiddenForTest(bool bHidden);

protected:
	virtual void BeginPlay() override;

private:
	UFUNCTION()
	void HandleTimeStateChanged(ETimeState NewState, ETimeState OldState, EStopExitReason Reason);

	void Collect();
	void UpdateVisibility();
	void Apply();

	UPROPERTY(VisibleAnywhere, Category = "Battery")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Glows;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Rings;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TierMid;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> SpineMid;

	UPROPERTY(Transient)
	TObjectPtr<UTimeStateSubsystem> TimeState;

	TWeakObjectPtr<AEpilogueChamber> Chamber;
	TArray<FQuat> RingBase;
	TArray<double> RingTurns;
	double PulsePhase = 0.0;
	double LastRun = -1.0;
	float GlowLevel = 0.f;
	float PulseSpeed = 0.f;
	float RingSpeed = 0.f;
	bool bGlowsShown = true;
	bool bShownWhileStoppedForTest = false;
	bool bHiddenForTest = false;
};
