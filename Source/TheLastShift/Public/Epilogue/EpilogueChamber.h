// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EpilogueChamber.generated.h"

class AChamberBattery;
class AChoiceTerminal;
class ADoorDevice;
class APrologueSlidingPanel;
class ULightComponent;
class UTimeStateSubsystem;

/** Where the battery's chamber stands. */
UENUM(BlueprintType)
enum class EEpilogueChamberState : uint8
{
	/** Before the player's pawn and its channel are there, and while the travel screen covers the hall. */
	Waiting,
	/** Brief 36B: the player in the lock; its panel opens by itself (LockOpenDelaySeconds, then over LockOpenSeconds). */
	Lock,
	/** The home AI's Chamber rows and last message are being said: the terminal offers nothing. */
	Lines,
	/** The last message typed out (or none): the terminal offers the choice. */
	Offering,
	/** Brief 36B: a choice was made: the hall answers it for ChoiceAftermathSeconds; nothing answers the player. */
	Aftermath,
	/** The aftermath over and the ending's travel asked for: nothing in the room answers any more. */
	Ended
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEpilogueChamberStateChanged, EEpilogueChamberState, NewState);

/**
 * The battery's chamber (its map, UTheLastShiftSettings::ChamberMap; since Brief 36B the battery's access hall), one
 * placed in it: the one place that knows the hall's order. When the player's pawn is there and the travel screen is
 * gone, the lock's panel (LockPanel) opens by itself, LockOpenDelaySeconds later, over LockOpenSeconds, and stays open.
 * Once it is open the home AI is asked for the chamber's lines (the Chamber rows of the epilogue table, then its last
 * message, UHomeAIComponent::SayChamberLines); once the last of them is typed out (or has left the queue without being
 * shown, dropped at Silent or refused by a full queue, once the channel is typed out; at once when there is no row: the
 * way home's door's rule, the engineer's lines counting as rows) the terminal is let offer the choice. The choice made
 * in its dialog starts the aftermath: for ChoiceAftermathSeconds the hall answers it (Inject: the battery is fed, its
 * glow and the monitors rising, AChamberBattery; Open: the structure lets go, the glow, the rings and the monitors going
 * out, the hall's lights falling to AftermathLightShare of their level, here, and the lock's door opening), then its
 * ending is asked for (URunSubsystem::TravelToChosenEnding). From the choice on nothing in the hall answers the player.
 * Every wait stands still while Stopped and behind the pause menu; an ending's travel is asked in Normal time only.
 * Nothing is carried across maps.
 */
UCLASS(Blueprintable)
class THELASTSHIFT_API AEpilogueChamber : public AActor
{
	GENERATED_BODY()

public:
	AEpilogueChamber();

	/** The chamber's one terminal, in front of the battery: its dialog chooses the ending. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chamber")
	TObjectPtr<AChoiceTerminal> Terminal;

	/** The lock's door, shut behind the player (nothing powers it until Open's aftermath opens it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chamber")
	TObjectPtr<ADoorDevice> EntryDoor;

	/** Brief 36B: the lock's sliding panel onto the gallery: it opens by itself after the travel, then stays open. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chamber")
	TObjectPtr<APrologueSlidingPanel> LockPanel;

	/** Brief 36B: the battery (its glow, rings and level), which answers the aftermath with its own values. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chamber")
	TObjectPtr<AChamberBattery> Battery;

	/** Brief 36B: the aftermath's ending (None before the choice), and how far it has gone: 0 at the choice, 1 at its end. */
	UFUNCTION(BlueprintPure, Category = "Chamber")
	FName GetAftermathEnding() const { return State == EEpilogueChamberState::Aftermath || State == EEpilogueChamberState::Ended ? Chosen : NAME_None; }

	UFUNCTION(BlueprintPure, Category = "Chamber")
	float GetAftermathAlpha() const;

	/** Brief 36B: the run's clock when the lock's panel began to open and when it was open (-1: not yet). */
	UFUNCTION(BlueprintPure, Category = "Chamber", meta = (Units = "s"))
	float GetLockOpeningRunSeconds() const { return static_cast<float>(LockOpeningRun); }

	UFUNCTION(BlueprintPure, Category = "Chamber", meta = (Units = "s"))
	float GetLockOpenRunSeconds() const { return static_cast<float>(LockOpenRun); }

	/** Brief 36B: the run's clock when the travel screen was gone with the pawn in the lock (-1: not yet). */
	UFUNCTION(BlueprintPure, Category = "Chamber", meta = (Units = "s"))
	float GetLockStartRunSeconds() const { return static_cast<float>(LockStartRun); }

	/** Brief 36B: the share of their level the hall's lights show now (1 but in Open's aftermath). */
	UFUNCTION(BlueprintPure, Category = "Chamber")
	float GetLightShare() const { return LightShare; }

	/** Brief 36B: the hall's lights (every light of the map), at their level when play began. */
	int32 GetLightCount() const { return Lights.Num(); }

	UFUNCTION(BlueprintPure, Category = "Chamber")
	EEpilogueChamberState GetChamberState() const { return State; }

	/** The ending chosen (None before the choice). */
	UFUNCTION(BlueprintPure, Category = "Chamber")
	FName GetChosenEnding() const { return Chosen; }

	/** The last row of the chamber's lines (None: none). */
	UFUNCTION(BlueprintPure, Category = "Chamber")
	FName GetLastRow() const { return LastRow; }

	/** The run's clock when the lines were asked for, when the terminal began to offer, at the choice and at the ending's travel (-1: not yet). */
	UFUNCTION(BlueprintPure, Category = "Chamber", meta = (Units = "s"))
	float GetLinesStartRunSeconds() const { return static_cast<float>(LinesStartRun); }

	UFUNCTION(BlueprintPure, Category = "Chamber", meta = (Units = "s"))
	float GetOfferedRunSeconds() const { return static_cast<float>(OfferedRun); }

	UFUNCTION(BlueprintPure, Category = "Chamber", meta = (Units = "s"))
	float GetChosenRunSeconds() const { return static_cast<float>(ChosenRun); }

	UFUNCTION(BlueprintPure, Category = "Chamber", meta = (Units = "s"))
	float GetEndingRunSeconds() const { return static_cast<float>(EndingRun); }

	/** The chamber of World, or null. */
	static AEpilogueChamber* Find(const UWorld* World);

	UPROPERTY(BlueprintAssignable, Category = "Chamber")
	FOnEpilogueChamberStateChanged OnStateChanged;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	UFUNCTION()
	void HandleChoiceMade(AChoiceTerminal* InTerminal, FName Ending);

	void SetState(EEpilogueChamberState NewState);
	void StartLines(double Now);
	void Offer(double Now, const TCHAR* Why);
	void TickLock(double Now, double Delta, bool bStill);
	void TickAftermath(double Now, double Delta, bool bStill);
	void ApplyLightShare(float Share);

	UPROPERTY(Transient)
	TObjectPtr<UTimeStateSubsystem> TimeState;

	EEpilogueChamberState State = EEpilogueChamberState::Waiting;
	FName LastRow;
	FName Chosen;
	bool bLastSeen = false;
	bool bTravelAsked = false;
	double LastRunSeconds = -1.0;
	double LinesStartRun = -1.0;
	double OfferedRun = -1.0;
	double ChosenRun = -1.0;
	double EndingRun = -1.0;
	double LockStartRun = -1.0;
	double LockOpeningRun = -1.0;
	double LockOpenRun = -1.0;
	/** Normal time (run's clock) since the lock's start and since the choice. */
	double LockElapsed = 0.0;
	double AftermathElapsed = 0.0;
	/** The run's clock that passed Stopped since the choice (not counted in the aftermath). */
	double AftermathStill = 0.0;
	float LightShare = 1.f;
	bool bDoorOpened = false;

	struct FHallLight
	{
		TWeakObjectPtr<ULightComponent> Light;
		float Intensity = 0.f;
	};
	TArray<FHallLight> Lights;
};
