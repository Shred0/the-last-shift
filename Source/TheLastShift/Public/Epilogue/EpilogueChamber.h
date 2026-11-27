// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EpilogueChamber.generated.h"

class AChoiceTerminal;
class ADoorDevice;
class UTimeStateSubsystem;

/** Where the battery's chamber stands. */
UENUM(BlueprintType)
enum class EEpilogueChamberState : uint8
{
	/** Before the player's pawn and its channel are there. */
	Waiting,
	/** The home AI's Chamber rows and last message are being said: the terminal offers nothing. */
	Lines,
	/** The last message typed out (or none): the terminal offers the choice. */
	Offering,
	/** A choice was made and its ending's travel asked for: nothing in the room answers any more. */
	Ended
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEpilogueChamberStateChanged, EEpilogueChamberState, NewState);

/**
 * The battery's chamber (its map, UTheLastShiftSettings::ChamberMap), one placed in it: the one place that knows the
 * room's order. When the player's pawn is there, the home AI is asked for the chamber's lines (the Chamber rows of the
 * epilogue table, then its last message, UHomeAIComponent::SayChamberLines); once the last of them is typed out (or has
 * left the queue without being shown, dropped at Silent or refused by a full queue, once the channel is typed out; at
 * once when there is no row: the way home's door's rule, the engineer's lines counting as rows) the terminal is let offer
 * the choice. The choice made in its dialog asks at once for its ending (URunSubsystem::TravelToChosenEnding); from the
 * choice on nothing in the room answers. Every wait stands still while Stopped and behind the pause menu; an ending's
 * travel is asked in Normal time only. Nothing is carried across maps.
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

	/** The doorway's door, shut behind the player (nothing powers it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chamber")
	TObjectPtr<ADoorDevice> EntryDoor;

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
};
