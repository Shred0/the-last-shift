// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Time/TimeState.h"
#include "EnergySubsystem.generated.h"

class UScanDirectorComponent;

THELASTSHIFT_API DECLARE_LOG_CATEGORY_EXTERN(LogEnergy, Log, All);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnEnergyChanged, int32, BankedEnergy, int32, SessionEnergy);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSessionEnergyLost, int32, LostEnergy, EStopExitReason, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSessionEnergyBanked, int32, BankedAmount);

/**
 * Sole owner of the run's energy. One currency, two pools:
 *
 * - Session energy: collected during the current Stop. It becomes banked energy only when the Stop ends with a
 *   voluntary Resume; on any other exit (Rewound, Jumped, RewoundAndJumped) it is lost (OnSessionLost), and whoever
 *   provided it (residues) returns to its place.
 * - Banked energy: what can be spent (terminals, in Normal time). Survives map changes, because this is a
 *   game-instance subsystem; session energy is zero after one.
 *
 * Follows the scan director of the current world: every UScanDirectorComponent announces its BeginPlay / EndPlay on
 * UScanDirectorComponent's global native lifecycle delegates; this subsystem binds OnStopExited of the director whose
 * world belongs to its game instance and unbinds when that director ends play, so nothing stays bound to an old world.
 */
UCLASS()
class THELASTSHIFT_API UEnergySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** The energy subsystem of WorldContext's game instance, or null. */
	static UEnergySubsystem* Get(const UObject* WorldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure, Category = "Energy")
	int32 GetBankedEnergy() const { return BankedEnergy; }

	UFUNCTION(BlueprintPure, Category = "Energy")
	int32 GetSessionEnergy() const { return SessionEnergy; }

	/** Banked plus session energy. */
	UFUNCTION(BlueprintPure, Category = "Energy")
	int32 GetTotalEnergy() const { return BankedEnergy + SessionEnergy; }

	/** True when Cost can be spent now (banked energy only; session energy is not spendable). */
	UFUNCTION(BlueprintPure, Category = "Energy")
	bool CanSpend(int32 Cost) const { return Cost >= 0 && BankedEnergy >= Cost; }

	/** Removes Cost from banked energy. Returns false and changes nothing when there is not enough. Spent energy never comes back. */
	UFUNCTION(BlueprintCallable, Category = "Energy")
	bool Spend(int32 Cost);

	/** Adds energy collected during the current Stop. */
	UFUNCTION(BlueprintCallable, Category = "Energy")
	void AddSessionEnergy(int32 Amount);

	/** Adds banked energy directly (debug: tls.Energy.Give). */
	UFUNCTION(BlueprintCallable, Category = "Energy")
	void AddBankedEnergy(int32 Amount);

	/** Sets the banked energy (clamped to >= 0) and drops any session energy: a run snapshot being restored. */
	UFUNCTION(BlueprintCallable, Category = "Energy")
	void RestoreBankedEnergy(int32 Banked);

	/** True while bound to a scan director's OnStopExited. */
	UFUNCTION(BlueprintPure, Category = "Energy")
	bool HasBoundDirector() const { return BoundDirector.IsValid(); }

	/** Every change of either pool. */
	UPROPERTY(BlueprintAssignable, Category = "Energy")
	FOnEnergyChanged OnEnergyChanged;

	/** A Stop ended without a voluntary Resume while session energy was held: that energy is gone. */
	UPROPERTY(BlueprintAssignable, Category = "Energy")
	FOnSessionEnergyLost OnSessionLost;

	/** A voluntary Resume moved the session energy into the bank (also fires with 0). */
	UPROPERTY(BlueprintAssignable, Category = "Energy")
	FOnSessionEnergyBanked OnSessionBanked;

private:
	void HandleDirectorBeganPlay(UScanDirectorComponent* Director);
	void HandleDirectorEndingPlay(UScanDirectorComponent* Director);

	UFUNCTION()
	void HandleStopExited(EStopExitReason Reason, float StoppedSeconds);

	bool IsOurWorld(const UScanDirectorComponent* Director) const;
	void Bind(UScanDirectorComponent* Director);
	void Unbind();
	void BroadcastChanged();

	int32 BankedEnergy = 0;
	int32 SessionEnergy = 0;

	TWeakObjectPtr<UScanDirectorComponent> BoundDirector;
	FDelegateHandle BeganPlayHandle;
	FDelegateHandle EndingPlayHandle;
};
