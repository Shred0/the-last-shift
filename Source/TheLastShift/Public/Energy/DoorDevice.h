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
 * and its end is announced by OnMotionEnded. Every mover of the game moves over time, so it is always true (kept for the
 * movers that share the event).
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnMotionStarted, AActor*, Mover, bool, bOpening, bool, bTimed);

/** A timed motion (OnMotionStarted) has ended: the opening is fully open or shut. A motion held by a Stop has not ended. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMotionEnded, AActor*, Mover);

/**
 * A door of the twin structure (and of the hand-built maps): an opening (Blocker's box, the door's size) and a panel that
 * rises straight up out of it while the door is powered, and comes down the same way when the power goes. Each travel,
 * opening or closing, takes UTheLastShiftSettings::DoorMoveSeconds of the run's clock (linearly, as the timed movers move;
 * from in between, the share of that time the travel left takes); it stands still while Stopped and behind the pause
 * menu and goes on from where it was. OnMotionStarted and OnMotionEnded announce each motion once.
 *
 * When the power goes (time ran out, a time jump, switched off) it starts to close, but never on someone: while a pawn
 * overlaps the Doorway volume it stays open, and closes as soon as the doorway is clear; a pawn that enters the doorway
 * while it closes opens it again from where it stands. Only a pawn holds it; the powered time ends when it ends.
 *
 * The panel's collision (PanelCollision) is where the panel is, and only while the panel is shown: a fully open door's
 * panel is hidden (risen out of the opening, into the wall above or past it) and blocks nothing. The door never affects
 * navigation: the navmesh runs through it whether open or shut (the entity walks through shut doors). While shown, the
 * Panel shows in scans in the interactable colour: it renders custom depth with the stencil value InteractableScanChannel
 * of the project settings, the lidar channel the scan director gives ScanColors.Interactable.
 */
UCLASS(Blueprintable)
class THELASTSHIFT_API ADoorDevice : public APoweredDevice
{
	GENERATED_BODY()

public:
	ADoorDevice();

	/** True from the moment the door starts to open until its panel is shut again. */
	UFUNCTION(BlueprintPure, Category = "Door")
	bool IsOpen() const { return OpenFraction > 0.f || MotionDirection > 0; }

	/** 0 shut, 1 fully open (the panel risen out of the opening). */
	UFUNCTION(BlueprintPure, Category = "Door")
	float GetOpenFraction() const { return OpenFraction; }

	UFUNCTION(BlueprintPure, Category = "Door")
	bool IsFullyOpen() const { return OpenFraction >= 1.f; }

	/** +1 opening, -1 closing, 0 at rest. A motion held by a Stop keeps its direction. */
	UFUNCTION(BlueprintPure, Category = "Door")
	int32 GetMotionDirection() const { return MotionDirection; }

	UFUNCTION(BlueprintPure, Category = "Door")
	bool IsMoving() const { return MotionDirection != 0; }

	/** How far the panel stands above its shut place now, along the door's up (cm, unscaled). */
	UFUNCTION(BlueprintPure, Category = "Door", meta = (Units = "cm"))
	float GetPanelRise() const;

	/** Full size of the doorway the door closes (Blocker's scaled box): X thickness along the passage, Y width, Z height. */
	UFUNCTION(BlueprintPure, Category = "Door", meta = (Units = "cm"))
	FVector GetDoorSize() const;

	/** The opening in world space (Blocker's box). */
	FBox GetOpeningBox() const;

	/** True when the door is due to close but a pawn in the doorway keeps it open. */
	UFUNCTION(BlueprintPure, Category = "Door")
	bool IsClosePending() const { return bClosePending; }

	/** True while a pawn overlaps the Doorway volume. */
	UFUNCTION(BlueprintPure, Category = "Door")
	bool IsDoorwayOccupied() const;

	/** The time a travel takes: DoorMoveSeconds, its one owner the project settings. */
	static float GetMoveSeconds();

	/** Tests only: ends the motion under way at once, with its end announced as at its natural end. */
	void FinishMotionAtOnce();

	UStaticMeshComponent* GetPanel() const { return Panel; }
	UBoxComponent* GetPanelCollision() const { return PanelCollision; }
	UBoxComponent* GetDoorway() const { return Doorway; }

	UPROPERTY(BlueprintAssignable, Category = "Door|Events")
	FOnDoorOpenChanged OnOpenChanged;

	/** The door starts to open or to close (timed, always): once for each motion, a reversal included. */
	UPROPERTY(BlueprintAssignable, Category = "Door|Events")
	FOnMotionStarted OnMotionStarted;

	/** The motion has ended: the door is fully open or shut. */
	UPROPERTY(BlueprintAssignable, Category = "Door|Events")
	FOnMotionEnded OnMotionEnded;

	/**
	 * Optional modelled panel, made at the panel's size with its pivot at its bottom centre: when set, Panel shows it at
	 * scale 1 instead of the engine's cube scaled to the door. Only the look changes: Panel keeps its visibility, its scan
	 * channel and its place, and the panel's collision blocks as before. Empty (the default): nothing changes.
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
	virtual void OnDeviceTimeStateChanged(ETimeState NewState) override;

	/** The cube panel's thickness as a share of the opening's (1: the whole opening). */
	virtual float GetPanelDepthShare() const { return 1.f; }

	/** The panel shows (and its collision blocks): while it is not fully open. */
	virtual bool IsPanelShown() const { return OpenFraction < 1.f; }

	/** Places, scales and shows the panel and its collision for the open fraction now. */
	void ApplyPanel();

	/** Sets the open fraction at once, with no motion announced (a mover that drives the panel itself: the entry door). */
	void SetPanelShareAtOnce(float InShare);

	/** The opening's box (the door's size). It blocks nothing by itself; a subclass may switch its collision on. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	TObjectPtr<UBoxComponent> Blocker;

	/** Visual panel; scaled to Blocker's extent (a 100 cm cube mesh), or PanelMesh at scale 1; tagged for the interactable scan channel at BeginPlay. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	TObjectPtr<UStaticMeshComponent> Panel;

	/** The panel's collision: Blocker's box moved with the panel, blocking while the panel is shown. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	TObjectPtr<UBoxComponent> PanelCollision;

	/** A pawn inside this volume keeps a door that is due to close open. Wider than Blocker along the passage. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Door")
	TObjectPtr<UBoxComponent> Doorway;

private:
	UFUNCTION()
	void HandleDoorwayEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	void TryClose();
	void StartMotion(int32 Direction, const TCHAR* Why);
	void AdvanceMotion(double Step);
	void UpdateOpenState();

	/** 0 shut, 1 fully open. */
	float OpenFraction = 0.f;

	/** +1 opening, -1 closing, 0 at rest. */
	int32 MotionDirection = 0;

	/** IsOpen() when OnOpenChanged last fired. */
	bool bOpen = false;

	bool bClosePending = false;

	/** The run's clock at the last tick (the motion's step) and at the start of the motion under way. */
	double LastRunSeconds = 0.0;
	double MotionStartRun = 0.0;
};
