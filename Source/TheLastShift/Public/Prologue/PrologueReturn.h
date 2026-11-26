// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PrologueReturn.generated.h"

class AAreaExit;
class ADoorDevice;
class APawn;
class APrologueSlidingPanel;
class UTimeStateSubsystem;

/** Where the home map's return stands. */
UENUM(BlueprintType)
enum class EPrologueReturnState : uint8
{
	/** The map is in its departure state: nothing of the return runs, the way home's door stays shut and its exit off. */
	Departure,
	/** The return: the player not yet out of the ship. */
	InShip,
	/** Stepped out: the home AI's Landing lines are being said. */
	Landing,
	/** The Landing lines typed out (or none): ExitDoorDelaySeconds of Normal time before the way home's door opens. */
	DoorDelay,
	/** The way home's door is open. */
	DoorOpened
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPrologueReturnStateChanged, EPrologueReturnState, NewState);

/**
 * Brief 31A: the home map's return, and the one place that knows which of its two states the map is in. The state comes
 * from the run (URunSubsystem::IsReturnStateOf: the run is on its way home), read when the map begins; nothing is
 * carried across the travel.
 *
 * Departure state: nothing changes from the departure, except that the way home's door (HomeDoor) stands shut and the
 * exit of the corridor behind it (CorridorExit) is switched off.
 *
 * Return state, set up when the map begins (behind the travel screen): the ship, its hatch and its exit are turned to
 * the return pose (ReturnShipLocation, ReturnShipYaw: the ship has come in through the hangar's hatch, its opening away
 * from it), the ship's hatch open and its exit switched off; the hangar's hatch open; the player's pawn inside the ship,
 * facing its opening; the return's snapshot taken (URunSubsystem::NotifyReturnEntered); the corridor's exit switched on.
 * The departure scene, the hangar's terminal and the line triggers do nothing in this state (each asks the run).
 * When the pawn first stands outside the ship, the hangar's hatch starts to close over HatchCloseSeconds of Normal time
 * and the home AI is asked for the Landing lines (DT_Epilogue); once their last row is typed out (or has left the queue
 * without being shown, dropped at Silent or refused by a full queue; at once when the group has no row) the door waits
 * ExitDoorDelaySeconds of Normal time and opens (powered, untimed). Every wait stands still while Stopped and behind the
 * pause menu. The times are the arrival's, owned by UTheLastShiftSettings.
 */
UCLASS(Blueprintable)
class THELASTSHIFT_API APrologueReturn : public AActor
{
	GENERATED_BODY()

public:
	APrologueReturn();

	/** The ship (its mesh actor, movable): turned to the return pose in the return state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Return")
	TObjectPtr<AActor> Ship;

	/** The ship's hatch: moved with the ship and open in the return state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Return")
	TObjectPtr<APrologueSlidingPanel> ShipHatch;

	/** The ship's exit (the departure's flight): moved with the ship and switched off in the return state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Return")
	TObjectPtr<AAreaExit> ShipExit;

	/** The hangar's hatch: open when the return begins, closing from the step out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Return")
	TObjectPtr<APrologueSlidingPanel> HangarHatch;

	/** The door of the way home, in the hangar's wall opposite its hatch: shut in the departure, opened by the return. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Return")
	TObjectPtr<ADoorDevice> HomeDoor;

	/** The exit in the corridor behind HomeDoor: on in the return state only (it opens the ending). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Return")
	TObjectPtr<AAreaExit> CorridorExit;

	/** The ship's pose on the return: its pivot on the hangar's floor, and its yaw. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Return")
	FVector ReturnShipLocation = FVector(4230.f, -800.f, 0.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Return", meta = (ClampMin = "-180.0", ClampMax = "180.0", Units = "deg"))
	float ReturnShipYaw = -90.f;

	/** The ship's inside, relative to the ship: the pawn starts in it; leaving it is the step out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Return")
	FVector ShipInsideOffset = FVector(0.f, 0.f, 250.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Return", meta = (ClampMin = "10.0", ClampMax = "5000.0", Units = "cm"))
	FVector ShipInsideExtent = FVector(300.f, 600.f, 250.f);

	/** Where the pawn starts, relative to the ship (on its floor), and its yaw relative to the ship's (180: facing the opening). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Return")
	FVector PawnStartInShip = FVector(50.f, 0.f, 0.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Return", meta = (ClampMin = "-180.0", ClampMax = "180.0", Units = "deg"))
	float PawnStartYawInShip = 180.f;

	UFUNCTION(BlueprintPure, Category = "Return")
	EPrologueReturnState GetReturnState() const { return State; }

	/** The map is in its return state (decided when it began). */
	UFUNCTION(BlueprintPure, Category = "Return")
	bool IsReturn() const { return State != EPrologueReturnState::Departure; }

	/** The ship's inside in world space (in the pose the ship stands in now). */
	UFUNCTION(BlueprintPure, Category = "Return")
	FBox GetShipInsideBox() const;

	UFUNCTION(BlueprintPure, Category = "Return")
	bool IsInsideShip(const AActor* Actor) const;

	/** The pawn was placed inside the ship when the return began. */
	UFUNCTION(BlueprintPure, Category = "Return")
	bool HasPlacedPawn() const { return bPawnPlaced; }

	/** 0 open, 1 closed: the hangar's hatch on the return (0 before the step out). */
	UFUNCTION(BlueprintPure, Category = "Return")
	float GetHatchClosedFraction() const;

	/** Normal-time seconds of the door's delay so far (the run's clock, Stopped and the pause menu left out). */
	UFUNCTION(BlueprintPure, Category = "Return", meta = (Units = "s"))
	float GetDoorDelaySeconds() const { return static_cast<float>(DoorDelaySeconds); }

	/** The run's clock at the step out, when the Landing lines were done and when the door opened (-1: not yet). */
	UFUNCTION(BlueprintPure, Category = "Return", meta = (Units = "s"))
	float GetSteppedOutRunSeconds() const { return static_cast<float>(SteppedOutRun); }

	UFUNCTION(BlueprintPure, Category = "Return", meta = (Units = "s"))
	float GetLandingDoneRunSeconds() const { return static_cast<float>(LandingDoneRun); }

	UFUNCTION(BlueprintPure, Category = "Return", meta = (Units = "s"))
	float GetDoorOpenedRunSeconds() const { return static_cast<float>(DoorOpenedRun); }

	UPROPERTY(BlueprintAssignable, Category = "Return")
	FOnPrologueReturnStateChanged OnStateChanged;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void SetState(EPrologueReturnState NewState);
	/** Turns the ship, its hatch and its exit to the return pose. */
	void ApplyReturnPose();
	/** Puts the pawn inside the ship, facing its opening; false while there is no pawn yet. */
	bool PlacePawn(APawn* Pawn);
	void StepOut(double Now);
	void UpdateLanding(double Now);
	void FinishLanding(double Now, const TCHAR* Why);

	UPROPERTY(Transient)
	TObjectPtr<UTimeStateSubsystem> TimeState;

	EPrologueReturnState State = EPrologueReturnState::Departure;
	bool bStarted = false;
	bool bPawnPlaced = false;
	bool bPawnWasInside = false;
	bool bHatchClosing = false;
	double HatchClosingSeconds = 0.0;
	double DoorDelaySeconds = 0.0;
	double LastRunSeconds = -1.0;
	FName LastLandingRow;
	bool bLastLandingSeen = false;
	double SteppedOutRun = -1.0;
	double LandingDoneRun = -1.0;
	double DoorOpenedRun = -1.0;
};
