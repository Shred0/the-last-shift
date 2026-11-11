// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Energy/PoweredDevice.h"
#include "DoorDevice.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UInteractableScanComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDoorOpenChanged, bool, bOpen);

/**
 * A door of the twin structure: a blocking volume and a panel mesh, open while powered. When the power goes (time ran
 * out, or a time jump) it closes, but never on someone: while a pawn overlaps the Doorway volume it stays open and
 * closes as soon as the doorway is clear.
 *
 * The door never affects navigation: the navmesh runs through it whether open or closed (the entity walks through
 * closed doors). Blocker's box extent is the door's size; the Panel is scaled from it, so it is set in one place.
 * While closed, the Panel shows in scans in the interactable colour (InteractableScan); an open door shows nothing.
 */
UCLASS(Blueprintable)
class THELASTSHIFT_API ADoorDevice : public APoweredDevice
{
	GENERATED_BODY()

public:
	ADoorDevice();

	UFUNCTION(BlueprintPure, Category = "Door")
	bool IsOpen() const { return bOpen; }

	/** Full size of the doorway the door closes (Blocker's scaled box): X thickness along the passage, Y width, Z height. */
	UFUNCTION(BlueprintPure, Category = "Door", meta = (Units = "cm"))
	FVector GetDoorSize() const;


	/** True when the power is gone but a pawn in the doorway keeps the door open. */
	UFUNCTION(BlueprintPure, Category = "Door")
	bool IsClosePending() const { return bClosePending; }

	/** True while a pawn overlaps the Doorway volume. */
	UFUNCTION(BlueprintPure, Category = "Door")
	bool IsDoorwayOccupied() const;

	UPROPERTY(BlueprintAssignable, Category = "Door|Events")
	FOnDoorOpenChanged OnOpenChanged;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnPowerStateChanged(bool bNowPowered) override;

	/** Blocks pawns while the door is closed; its extent is the door's size. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	TObjectPtr<UBoxComponent> Blocker;

	/** Visual panel, shown while closed; scaled to Blocker's extent (a 100 cm cube mesh). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	TObjectPtr<UStaticMeshComponent> Panel;

	/** Shows the Panel in scans in the interactable colour (nothing while the panel is hidden, i.e. open). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	TObjectPtr<UInteractableScanComponent> InteractableScan;

	/** A pawn inside this volume keeps an unpowered door open. Set its extent wider than Blocker along the passage. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	TObjectPtr<UBoxComponent> Doorway;

private:
	UFUNCTION()
	void HandleDoorwayEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	void SetOpen(bool bNewOpen);
	void TryClose();
	void FitPanelToBlocker();

	bool bOpen = false;
	bool bClosePending = false;
};
