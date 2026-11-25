// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Energy/PoweredDevice.h"
#include "DoorDevice.generated.h"

class UBoxComponent;
class UStaticMesh;
class UStaticMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDoorOpenChanged, bool, bOpen);

/**
 * Something that closes an opening (a door, a hatch) starts to open (bOpening) or to close. bTimed: the motion takes time
 * and its end is announced by OnMotionEnded; false: it opens or closes at once, and nothing follows.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnMotionStarted, AActor*, Mover, bool, bOpening, bool, bTimed);

/** A timed motion (OnMotionStarted with bTimed) has ended: the opening is fully open or closed. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMotionEnded, AActor*, Mover);

/**
 * A door of the twin structure: a blocking volume and a panel mesh, open while powered. When the power goes (time ran
 * out, or a time jump) it closes, but never on someone: while a pawn overlaps the Doorway volume it stays open and
 * closes as soon as the doorway is clear.
 *
 * The door never affects navigation: the navmesh runs through it whether open or closed (the entity walks through
 * closed doors). Blocker's box extent is the door's size; the Panel is scaled from it, so it is set in one place.
 * While closed, the Panel shows in scans in the interactable colour: it renders custom depth with the stencil value
 * InteractableScanChannel of the project settings, the lidar channel the scan director gives ScanColors.Interactable.
 * An open door's panel is hidden and shows nothing.
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

	/**
	 * The door starts to open or to close: when it opens or closes (at once, bTimed false), except that a close announced
	 * earlier by AnnounceCloseStarted (the home's entry door, which lowers its panel over time: bTimed true) is not
	 * announced again.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Door|Events")
	FOnMotionStarted OnMotionStarted;

	/** A timed close announced by AnnounceCloseStarted has ended (AnnounceMotionEnded). Doors that move at once never fire it. */
	UPROPERTY(BlueprintAssignable, Category = "Door|Events")
	FOnMotionEnded OnMotionEnded;

	/**
	 * Optional modelled panel, made at the panel's size with its pivot at its bottom centre: when set, Panel shows it at
	 * scale 1 instead of the engine's cube scaled to the door. Only the look changes: Panel keeps its visibility, its scan
	 * channel and its place, and Blocker blocks as before. Empty (the default): nothing changes.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Door")
	TObjectPtr<UStaticMesh> PanelMesh;

	/**
	 * Gives InPanel the modelled mesh when Modelled is set, with no material override, so that the mesh's own material
	 * (its surface family) shows (and returns true: the caller then places it by its bottom centre at scale 1); when it is
	 * empty, gives the engine's cube SurfaceLook.PanelFallbackMaterial and returns false. Shared by the prologue's moving panels.
	 */
	static bool ApplyPanelMesh(UStaticMeshComponent* InPanel, UStaticMesh* Modelled);

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnPowerStateChanged(bool bNowPowered) override;

	/**
	 * A close that takes time starts now, before the door is closed: OnMotionStarted(false, timed) now, and not again when
	 * it closes; AnnounceMotionEnded ends it.
	 */
	void AnnounceCloseStarted();

	/** The timed close announced by AnnounceCloseStarted has ended: OnMotionEnded. */
	void AnnounceMotionEnded();

	/** Blocks pawns while the door is closed; its extent is the door's size. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	TObjectPtr<UBoxComponent> Blocker;

	/** Visual panel, shown while closed; scaled to Blocker's extent (a 100 cm cube mesh), or PanelMesh at scale 1; tagged for the interactable scan channel at BeginPlay. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	TObjectPtr<UStaticMeshComponent> Panel;

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

	/** OnMotionStarted(false) already fired for the coming close. */
	bool bCloseAnnounced = false;
};
