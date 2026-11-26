// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Energy/PowerTerminal.h"
#include "PrologueBriefingTerminal.generated.h"

class UWorld;

/**
 * The home map's terminal by the hangar's entry door: a free terminal ("Power", no cost) that works only after the home
 * AI's briefing, the rows of BriefingGroup (Hologram, said on entering the monitor room), has been said: its last row
 * typed out to its last character in the pawn's text channel (UTextChannelComponent::IsTypedOut while that row is the
 * channel's message, or once it has left it, shown or dropped). The engineer's lines before the group's rows are rows
 * of the briefing: they come before its last row, so it waits for them. With no row in the group it works as soon as the group is asked for,
 * that is as soon as the player has entered the monitor room.
 *
 * Before that it takes no payment and offers none: no Interact prompt, and an Interact does nothing. It shows in scans
 * in the interactable colour in every state, like any terminal. Once paid it offers nothing again (its door closes
 * behind the player and stays closed).
 */
UCLASS(Blueprintable)
class THELASTSHIFT_API APrologueBriefingTerminal : public APowerTerminal
{
	GENERATED_BODY()

public:
	APrologueBriefingTerminal();

	/** The group of DT_Prologue that is the briefing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Terminal")
	FName BriefingGroup;

	/** The briefing has been said: the terminal works. */
	UFUNCTION(BlueprintPure, Category = "Terminal")
	bool IsBriefed() const { return bBriefed; }

	/** The briefing group has been asked for (the player has entered the monitor room). */
	UFUNCTION(BlueprintPure, Category = "Terminal")
	bool HasBriefingStarted() const { return bAsked; }

	/**
	 * The share of the briefing typed, 0 to 1: 0 until its first row starts to be typed, then the share of its text typed
	 * out, each row weighted by its length, whatever the number or the length of the rows, the engineer's lines before its
	 * rows (DT_EngineerLines) counted as rows of the briefing, by their length too; 1 once the briefing has been
	 * said (at once when the group has no row). It never goes back (a row restarted by a Stop does not undo it). The
	 * hologram draws its route by it.
	 */
	UFUNCTION(BlueprintPure, Category = "Terminal")
	float GetBriefingProgress01() const { return Progress; }

	/** The run's clock when the terminal became usable (-1: not yet). */
	UFUNCTION(BlueprintPure, Category = "Terminal", meta = (Units = "s"))
	float GetBriefedRunSeconds() const { return static_cast<float>(BriefedRun); }

	/** The terminal has been paid (its door opened). */
	UFUNCTION(BlueprintPure, Category = "Terminal")
	bool HasBeenPaid() const { return bPaid; }

	virtual bool AcceptsPayment() const override { return bBriefed && !bPaid; }
	virtual bool OffersPayment() const override { return bBriefed && !bPaid && HasUnpoweredTarget(); }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void HandlePaid(APawn* InstigatorPawn) override;

private:
	void HandleLine(UWorld* World, FName Group);
	void SetBriefed(const TCHAR* Why);

	void UpdateProgress(const class UTextChannelComponent* Channel);

	FDelegateHandle LineHandle;
	FName LastRow;
	/** The briefing's rows in order and their weights (their texts' lengths). */
	TArray<FName> Rows;
	TArray<float> Weights;
	float TotalWeight = 0.f;
	float Progress = 0.f;
	bool bAsked = false;
	bool bLastRowSeen = false;
	bool bBriefed = false;
	bool bPaid = false;
	double BriefedRun = -1.0;
};
