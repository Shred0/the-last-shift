// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Energy/PowerTerminal.h"
#include "Narrative/NarrativeRows.h"
#include "Run/RunSubsystem.h"
#include "TextChannel/TextTokens.h"
#include "Time/TimeState.h"
#include "HomeAIComponent.generated.h"

class APowerTerminal;
class UDataTable;
class UTextChannelComponent;
struct FHomeJump;

/**
 * The one place where the suit's text channel is driven, for every voice (the home AI, the twin AI, the engineer): it
 * gives the channel its tables (the home AI's messages, with the engineer's rows among them, and the twin AI's), pushes
 * every message and suspends the channel. Nothing else pushes to it. Sits next to a
 * UTextChannelComponent on the player's pawn.
 *
 * - Suspended while time is Stopped, while StdCore's pause menu is open, while a map transition covers the screen and
 *   once the run is lost.
 * - A time jump (contact or graze, UHomeStateSubsystem::OnHomeJump): one JumpReturn row of the jump's act, with the
 *   numbers of that jump. A plain Resume or a rewind without a jump says nothing.
 * - The player released in an area that is ready (URunSubsystem::OnAreaEntered): every AreaStart row of the act.
 * - Banked energy rising from zero through a voluntary Resume, once per map load: every FirstEnergy row of the act.
 * - A successful payment at a terminal: one row of the trigger of its kind (APowerTerminal::GetPaymentKind: SpendDoor,
 *   SpendLights, SpendBeacon, SpendLog, SpendTwin) when the act has rows of it, each kind counting on its own; else one
 *   Spend row of the act, as before. At the twin AI's terminal, then every row of the part that payment played
 *   (ATwinAITerminal::GetLastPlayedPart), as the twin voice, after the spending comment. A terminal that costs nothing
 *   takes no payment: it says nothing.
 * - Before any row it pushes, the channel says the engineer's lines before that row (the engineer table it gives the
 *   channel, DT_EngineerLines), the first time that row is said in the map: the first contact's sequence is the return
 *   message, the engineer's lines before the first EntityContact row, the EntityContact rows, then the EntityFound rows.
 * - The entity (URunSubsystem::OnEntityFound and OnFirstContact, each once in a run, at the end of a Stop), pushed on
 *   the next frame, so after the return message of that Stop's jump when there is one. Found by a clear sighting: every
 *   EntitySeen row of the act (the engineer's voice), then every EntityFound row of the act. The first contact: every
 *   EntityContact row of the act, then every EntityFound row of the act if the contact found it (it was not found before
 *   that Stop); a sighting in the same Stop adds nothing. Not said once the run is lost.
 * - The loss: the channel is cleared and hidden (the loss screen shows the Loss ending, not an AI message).
 * - The prologue's maps (outside the run): every row of a group of the prologue table (FPrologueRow), once per map, in
 *   order: Wake when the home map begins, the others when the map's triggers fire (APrologueLineTrigger::OnAnyLine:
 *   Hologram, Hangar on entering their rooms, Light when the player steps out of the ship in the arrival, Hatch when the
 *   arrival's hatch has closed and the Light lines are typed out, Door once the Hatch lines are typed out). Brief 31A: the
 *   epilogue's groups come from the epilogue table (Landing when the player steps out of the ship on the way home); on
 *   the way home the home map's Wake lines are not said.
 * The travel screen's labels are built here too, for the run subsystem: BuildTravelLabel (an AreaEntry row) and
 * BuildPrologueTravelLabel (the first Flight row).
 *
 * The numbers come from their owners (home state, time state) when the event happens; nothing is copied.
 */
UCLASS(ClassGroup = (TheLastShift), meta = (BlueprintSpawnableComponent))
class THELASTSHIFT_API UHomeAIComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHomeAIComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** The channel this component drives (the owner's UTextChannelComponent). */
	UFUNCTION(BlueprintPure, Category = "HomeAI")
	UTextChannelComponent* GetChannel() const;

	/** The context of a message about nothing in particular now: home time, battery and the last Stop's length. */
	UFUNCTION(BlueprintPure, Category = "HomeAI")
	FTextChannelContext MakeContext() const;

	/** True between the entity being found or its first contact and their rows being pushed (the next frame). */
	UFUNCTION(BlueprintPure, Category = "HomeAI")
	bool IsEntityFoundMessagePending() const { return bEntityFoundPending || bFirstContactPending; }

	/** True once the FirstEnergy rows of this map were pushed. */
	UFUNCTION(BlueprintPure, Category = "HomeAI")
	bool HasSpokenFirstEnergy() const { return bFirstEnergyDone; }

	/**
	 * The label of the travel screen to area Act: its AreaEntry row at the current battery's level (the thresholds of
	 * the project settings, the same with or without a pawn), tokens replaced, never typed out. False and an empty label when
	 * the act has no such row or the level is Silent. OutReadingSeconds is the channel's hold time for that text.
	 */
	static bool BuildTravelLabel(const UObject* WorldContext, int32 Act, FText& OutLabel, float& OutReadingSeconds);

	/** The label of the travel screen between the prologue's two maps: the first Flight row, as BuildTravelLabel builds a line. */
	static bool BuildPrologueTravelLabel(const UObject* WorldContext, FText& OutLabel, float& OutReadingSeconds);

	/** Brief 31A: the label of the flight back: the first Return row of the epilogue table, as BuildTravelLabel builds a line. */
	static bool BuildReturnTravelLabel(const UObject* WorldContext, FText& OutLabel, float& OutReadingSeconds);

	/** Rows of Group in a prologue table (FPrologueRow), in ascending Order (ties by row name). */
	static TArray<FName> FindPrologueRows(const UDataTable* Table, FName Group);

	/** The trigger of a payment's kind: SpendDoor, SpendLights, SpendBeacon, SpendLog, SpendTwin; Spend for Other. */
	UFUNCTION(BlueprintPure, Category = "HomeAI")
	static ETextTrigger GetSpendTrigger(EPaymentKind Kind);

	/**
	 * Queues every row of Group of the prologue table, in order, the first time it is asked in this map; later asks say
	 * nothing. Returns how many were queued (0 for a group with no rows).
	 */
	UFUNCTION(BlueprintCallable, Category = "HomeAI")
	int32 SayPrologueGroup(FName Group);

	/** True once Group was asked for in this map (with or without rows). */
	UFUNCTION(BlueprintPure, Category = "HomeAI")
	bool HasSaidPrologueGroup(FName Group) const { return SaidPrologueGroups.Contains(Group); }

private:
	void HandlePrologueLine(UWorld* World, FName Group);
	UFUNCTION()
	void HandleTimeStateChanged(ETimeState NewState, ETimeState OldState, EStopExitReason Reason);

	UFUNCTION()
	void HandleHomeJump(const FHomeJump& Jump);

	UFUNCTION()
	void HandleRunLost();

	UFUNCTION()
	void HandleAreaEntered(int32 ActIndex);

	UFUNCTION()
	void HandleSessionBanked(int32 BankedAmount);

	UFUNCTION()
	void HandleEntityFound(EEntityFoundBy By);

	UFUNCTION()
	void HandleFirstContact();

	/** Pushes the entity's rows when some are pending (from the tick: after the frame's return message). */
	void PushEntityLines();

	void HandleTerminalActivated(APowerTerminal* Terminal, int32 Cost);

	void UpdateSuspension();
	void PushAreaStart(int32 Act);
	int32 GetAct() const;

	TWeakObjectPtr<UTextChannelComponent> Channel;
	FDelegateHandle TerminalHandle;
	FDelegateHandle PrologueHandle;
	TSet<FName> SaidPrologueGroups;
	bool bAreaStartDone = false;
	bool bFirstEnergyDone = false;
	bool bEntityFoundPending = false;
	bool bFirstContactPending = false;
	bool bFoundBySighting = false;
};
