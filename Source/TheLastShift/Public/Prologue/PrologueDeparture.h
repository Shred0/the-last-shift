// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PrologueDeparture.generated.h"

class AAreaExit;
class APawn;
class APrologueEntryDoor;
class APrologueSlidingPanel;
class UBoxComponent;
class UTimeStateSubsystem;

/** Where the home map's departure scene stands. */
UENUM(BlueprintType)
enum class EPrologueDepartureState : uint8
{
	/** The player not yet wholly inside the hangar. */
	Waiting,
	/** The entry door closing behind the player over HomeDoorCloseSeconds. */
	DoorClosing,
	/** The hangar's hatch opening over HomeHatchOpenSeconds. */
	HatchOpening,
	/** The ship's hatch opening over ShipHatchOpenSeconds. */
	ShipHatchOpening,
	/** Everything open: walking into the ship starts the flight. */
	Boardable
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPrologueDepartureStateChanged, EPrologueDepartureState, NewState);

/**
 * The departure scene of the prologue's home map, and the one owner of its order and its times. When the player's pawn
 * is wholly inside the hangar (Hangar, the hangar's inside) and out of the entry door's doorway, in Normal time, the
 * entry door closes behind it over UTheLastShiftSettings::HomeDoorCloseSeconds and stays closed; then the hangar's hatch
 * opens over HomeHatchOpenSeconds; then the ship's hatch over ShipHatchOpenSeconds. Every phase runs on the run's clock
 * in Normal time: it stands still while time is Stopped and behind the pause menu. Nothing locks the camera or the
 * input, and nothing can be skipped.
 *
 * Until the ship's hatch is fully open the ship cannot be entered (its hatch blocks the way) and its exit (ShipExit)
 * does nothing: its volume has no collision; once open the exit is switched on and walking into the ship starts the
 * flight.
 */
UCLASS(Blueprintable)
class THELASTSHIFT_API APrologueDeparture : public AActor
{
	GENERATED_BODY()

public:
	APrologueDeparture();

	/** Half size of the hangar's inside, centred on the actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Departure", meta = (ClampMin = "10.0", ClampMax = "5000.0", Units = "cm"))
	FVector HangarExtent = FVector(1500.f, 2000.f, 700.f);

	/** The door from the corridor into the hangar; it closes behind the player. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Departure")
	TObjectPtr<APrologueEntryDoor> EntryDoor;

	/** The hangar's hatch; it opens once the entry door is closed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Departure")
	TObjectPtr<APrologueSlidingPanel> HangarHatch;

	/** The ship's hatch; it opens once the hangar's hatch is open. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Departure")
	TObjectPtr<APrologueSlidingPanel> ShipHatch;

	/** The exit inside the ship that starts the flight; off until the ship's hatch is fully open. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Departure")
	TObjectPtr<AAreaExit> ShipExit;

	UFUNCTION(BlueprintPure, Category = "Departure")
	EPrologueDepartureState GetDepartureState() const { return State; }

	/** Normal-time seconds of the current phase so far (the run's clock, Stopped and the pause menu left out). */
	UFUNCTION(BlueprintPure, Category = "Departure", meta = (Units = "s"))
	float GetPhaseSeconds() const { return static_cast<float>(PhaseSeconds); }

	/**
	 * Normal-time seconds a phase took (DoorClosing, HatchOpening, ShipHatchOpening), measured on the run's clock; -1
	 * until it has ended.
	 */
	UFUNCTION(BlueprintPure, Category = "Departure", meta = (Units = "s"))
	float GetPhaseTook(EPrologueDepartureState Phase) const;

	/** The run's clock when a phase began (-1: not yet). */
	UFUNCTION(BlueprintPure, Category = "Departure", meta = (Units = "s"))
	float GetPhaseStartRun(EPrologueDepartureState Phase) const;

	/** The hangar's inside in world space. */
	UFUNCTION(BlueprintPure, Category = "Departure")
	FBox GetHangarBox() const;

	/** The pawn's collision is wholly inside the hangar. */
	UFUNCTION(BlueprintPure, Category = "Departure")
	bool IsWhollyInside(const APawn* Pawn) const;

	/** The ship's exit is switched on. */
	UFUNCTION(BlueprintPure, Category = "Departure")
	bool IsBoardable() const { return State == EPrologueDepartureState::Boardable; }

	UPROPERTY(BlueprintAssignable, Category = "Departure")
	FOnPrologueDepartureStateChanged OnStateChanged;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void SetState(EPrologueDepartureState NewState, double Now);
	void SetExitEnabled(bool bEnabled);
	static int32 PhaseIndex(EPrologueDepartureState Phase);

	UPROPERTY(VisibleAnywhere, Category = "Departure")
	TObjectPtr<UBoxComponent> Hangar;

	UPROPERTY(Transient)
	TObjectPtr<UTimeStateSubsystem> TimeState;

	EPrologueDepartureState State = EPrologueDepartureState::Waiting;
	double PhaseSeconds = 0.0;
	double LastRunSeconds = -1.0;
	double PhaseTook[3] = { -1.0, -1.0, -1.0 };
	double PhaseStartRun[3] = { -1.0, -1.0, -1.0 };
};
