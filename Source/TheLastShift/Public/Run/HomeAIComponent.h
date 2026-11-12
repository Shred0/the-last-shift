// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TextChannel/TextTokens.h"
#include "Time/TimeState.h"
#include "HomeAIComponent.generated.h"

class APowerTerminal;
class UTextChannelComponent;
struct FHomeJump;

/**
 * The one place where the home AI's text channel is driven: it gives the channel its table, pushes every message and
 * suspends the channel. Nothing else pushes to it. Sits next to a UTextChannelComponent on the player's pawn.
 *
 * - Suspended while time is Stopped, while StdCore's pause menu is open, while a map transition covers the screen and
 *   once the run is lost.
 * - A time jump (contact or graze, UHomeStateSubsystem::OnHomeJump): one JumpReturn row of the jump's act, with the
 *   numbers of that jump. A plain Resume or a rewind without a jump says nothing.
 * - The player released in an area that is ready (URunSubsystem::OnAreaEntered): every AreaStart row of the act.
 * - Banked energy rising from zero through a voluntary Resume, once per map load: every FirstEnergy row of the act.
 * - A successful payment at a terminal: one Spend row of the act.
 * - The loss: the channel is cleared and hidden (the loss screen shows the Loss ending, not an AI message).
 * The travel screen's label (an AreaEntry row) is built here too, for the run subsystem: BuildTravelLabel.
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

	/** True once the FirstEnergy rows of this map were pushed. */
	UFUNCTION(BlueprintPure, Category = "HomeAI")
	bool HasSpokenFirstEnergy() const { return bFirstEnergyDone; }

	/**
	 * The label of the travel screen to area Act: its AreaEntry row at the current battery's level (the thresholds of
	 * the world's channel, else the channel's defaults), tokens replaced, never typed out. False and an empty label when
	 * the act has no such row or the level is Silent. OutReadingSeconds is the channel's hold time for that text.
	 */
	static bool BuildTravelLabel(const UObject* WorldContext, int32 Act, FText& OutLabel, float& OutReadingSeconds);

private:
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

	void HandleTerminalActivated(APowerTerminal* Terminal, int32 Cost);

	void UpdateSuspension();
	void PushAreaStart(int32 Act);
	int32 GetAct() const;

	TWeakObjectPtr<UTextChannelComponent> Channel;
	FDelegateHandle TerminalHandle;
	bool bAreaStartDone = false;
	bool bFirstEnergyDone = false;
};
