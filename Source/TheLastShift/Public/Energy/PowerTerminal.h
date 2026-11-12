// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PowerTerminal.generated.h"

class APawn;
class APoweredDevice;
class UStaticMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTerminalActivated, int32, Cost);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnTerminalInsufficientEnergy, int32, Cost, int32, BankedEnergy, bool, bTimeStopped);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAnyTerminalActivated, APowerTerminal* /*Terminal*/, int32 /*Cost*/);

/**
 * Where energy is spent: Interact within InteractRadius, in Normal time, spends Cost banked energy and powers every
 * device in Targets (which can be far away). With too little energy, or while time is Stopped (terminals work only in
 * Normal time), nothing changes and OnInsufficientEnergy fires. Shows in scans in the interactable colour: Mesh renders
 * custom depth with the stencil value InteractableScanChannel of the project settings, the lidar channel the scan
 * director gives ScanColors.Interactable.
 */
UCLASS(Blueprintable)
class THELASTSHIFT_API APowerTerminal : public AActor
{
	GENERATED_BODY()

public:
	APowerTerminal();

	/** Banked energy spent per activation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal", meta = (ClampMin = "0", ClampMax = "99"))
	int32 Cost = 1;

	/** Distance from the pawn to the terminal within which Interact reaches it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal", meta = (ClampMin = "50.0", ClampMax = "1000.0", Units = "cm"))
	float InteractRadius = 150.f;

	/** Devices powered on activation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	TArray<TObjectPtr<APoweredDevice>> Targets;

	/**
	 * Interact from Instigator. Out of range, or while the pause menu freezes the run: false, nothing happens. In range: in Normal time with enough banked
	 * energy, spends Cost, powers Targets, fires OnActivated and returns true; otherwise fires OnInsufficientEnergy and
	 * returns false.
	 */
	UFUNCTION(BlueprintCallable, Category = "Terminal")
	bool TryActivate(APawn* InstigatorPawn);

	UFUNCTION(BlueprintPure, Category = "Terminal")
	bool IsInRange(const APawn* InstigatorPawn) const;

	/** True when at least one device in Targets is not powered: activating would switch something on. */
	UFUNCTION(BlueprintPure, Category = "Terminal")
	bool HasUnpoweredTarget() const;

	/** Cost, clamped as TryActivate spends it. */
	UFUNCTION(BlueprintPure, Category = "Terminal")
	int32 GetCost() const;

	/** The terminal nearest to Pawn within its InteractRadius, or null. */
	static APowerTerminal* FindNearestInRange(const APawn* Pawn);

	UPROPERTY(BlueprintAssignable, Category = "Terminal|Events")
	FOnTerminalActivated OnActivated;

	/** Every successful payment at any terminal, in any world, right after that terminal's OnActivated (listeners filter by world). */
	static FOnAnyTerminalActivated OnAnyActivated;

	UPROPERTY(BlueprintAssignable, Category = "Terminal|Events")
	FOnTerminalInsufficientEnergy OnInsufficientEnergy;

protected:
	virtual void BeginPlay() override;

	/** The terminal's body; tagged for the interactable scan channel at BeginPlay. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Terminal")
	TObjectPtr<UStaticMeshComponent> Mesh;
};
