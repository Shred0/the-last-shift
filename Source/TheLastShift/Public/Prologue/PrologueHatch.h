// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PrologueHatch.generated.h"

class ADoorDevice;
class UBoxComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UTimeStateSubsystem;

/** Where the arrival hangar's hatch stands. */
UENUM(BlueprintType)
enum class EPrologueHatchState : uint8
{
	/** Open, the player not yet out of the ship. */
	Open,
	/** Closing over HatchCloseSeconds of Normal time. */
	Closing,
	/** Closed; the home AI's Hatch lines are being said. */
	Closed,
	/** The Hatch lines typed out; the home AI's Door lines, which announce the exit door, are being said. */
	DoorLines,
	/** The Door lines typed out; ExitDoorDelaySeconds of Normal time before the exit door opens. */
	DoorDelay,
	/** Closed and the exit door opened. */
	DoorOpened
};

/**
 * The hatch of the twin structure's arrival hangar, in the hatch wall behind the ship, and what follows it. It starts to
 * close when the player's pawn, which begins inside the ship (ShipVolume), first stands outside it, and closes with one
 * movement (the panel slides down in the wall's opening) over UTheLastShiftSettings::HatchCloseSeconds of the run's
 * clock in Normal time: it stands still while time is Stopped and behind the pause menu. Once closed it asks the home AI
 * for the Hatch lines; once they are said it asks for the Door lines; once those are said it waits
 * UTheLastShiftSettings::ExitDoorDelaySeconds of Normal time (still while Stopped and behind the pause menu, like the
 * closing) and opens ExitDoor (powered, untimed: it stays open). A group is said when the pawn's text channel has typed
 * out everything pushed to it (UTextChannelComponent::IsTypedOut: the last row shows its last character), not when it
 * has left the channel. A group with no rows blocks nothing.
 *
 * The opening never lets the player out: Blocker fills it at all times, and the panel itself has no collision, so it
 * never moves into the pawn.
 */
UCLASS(Blueprintable)
class THELASTSHIFT_API APrologueHatch : public AActor
{
	GENERATED_BODY()

public:
	APrologueHatch();

	/** Size of the opening the panel closes (X thickness, Y width, Z height); the actor's origin is the opening's bottom centre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hatch", meta = (ClampMin = "10.0", ClampMax = "5000.0", Units = "cm"))
	FVector OpeningSize = FVector(30.f, 1600.f, 700.f);

	/**
	 * Optional modelled panel, made at the panel's size (the opening's width plus 20 cm on each side, its height plus
	 * 20 cm, 75% of its depth) with its pivot at its bottom centre: when set, the panel shows it at scale 1 instead of the
	 * engine's cube scaled to that size. Only the look changes: it moves and blocks as before. Empty (the default):
	 * nothing changes.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hatch")
	TObjectPtr<UStaticMesh> PanelMesh;

	/** The ship's inside, relative to the actor: the player starts in it; leaving it starts the hatch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hatch")
	FVector ShipVolumeOffset = FVector(-900.f, 0.f, 250.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hatch", meta = (ClampMin = "10.0", ClampMax = "5000.0", Units = "cm"))
	FVector ShipVolumeExtent = FVector(600.f, 300.f, 250.f);

	/** The door to area 1: it opens by itself after the hatch has closed, the Hatch and Door lines are said and the delay has passed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Hatch")
	TObjectPtr<ADoorDevice> ExitDoor;

	UFUNCTION(BlueprintPure, Category = "Hatch")
	EPrologueHatchState GetHatchState() const { return State; }

	/** 0 open, 1 closed. */
	UFUNCTION(BlueprintPure, Category = "Hatch")
	float GetClosedFraction() const;

	/** Normal-time seconds the hatch has been closing (the run's clock, Stopped and the pause menu left out). */
	UFUNCTION(BlueprintPure, Category = "Hatch", meta = (Units = "s"))
	float GetClosingSeconds() const { return static_cast<float>(ClosingSeconds); }

	/** Normal-time seconds of the exit door's delay so far (0 before DoorDelay; the run's clock, Stopped and the pause menu left out). */
	UFUNCTION(BlueprintPure, Category = "Hatch", meta = (Units = "s"))
	float GetDoorDelaySeconds() const { return static_cast<float>(DoorDelaySeconds); }

	/** The pawn's location is inside ShipVolume. */
	UFUNCTION(BlueprintPure, Category = "Hatch")
	bool IsInsideShip(const AActor* Actor) const;

	/** The ship's inside in world space. */
	UFUNCTION(BlueprintPure, Category = "Hatch")
	FBox GetShipBox() const;

	UFUNCTION(BlueprintImplementableEvent, Category = "Hatch")
	void ReceiveHatchStateChanged(EPrologueHatchState NewState);

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void SetState(EPrologueHatchState NewState);
	void ApplyPanel();
	/** The pawn's text channel has typed out everything pushed to it (no channel: true). */
	bool IsChannelTypedOut() const;

	UPROPERTY(VisibleAnywhere, Category = "Hatch")
	TObjectPtr<UStaticMeshComponent> Panel;

	/** Fills the opening at all times (the void beyond is never reachable). */
	UPROPERTY(VisibleAnywhere, Category = "Hatch")
	TObjectPtr<UBoxComponent> Blocker;

	UPROPERTY(VisibleAnywhere, Category = "Hatch")
	TObjectPtr<UBoxComponent> ShipVolume;

	UPROPERTY(Transient)
	TObjectPtr<UTimeStateSubsystem> TimeState;

	EPrologueHatchState State = EPrologueHatchState::Open;
	bool bPawnWasInside = false;
	double ClosingSeconds = 0.0;
	double DoorDelaySeconds = 0.0;
	double LastRunSeconds = -1.0;
};
