// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Energy/PowerTerminal.h"
#include "ChoiceTerminal.generated.h"

class AChoiceTerminal;
class UTimeStateSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChoiceHoldProgress, float, Fraction);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChoiceMade, AChoiceTerminal*, Terminal);

/**
 * One of the two places of the battery's chamber, on the power terminal's modelled body: acting at it chooses one of the
 * two endings (Ending: Inject or Open). It takes no payment and powers nothing: the Interact prompt shows the key and the
 * ending's label (URunSceneSubsystem::GetEndingLabel, the list's own) while it offers the choice.
 *
 * It offers the choice (OffersChoice) only in Normal time, once the chamber has let it (SetOffering: after the home AI's
 * last message), before any choice is made, and for Open only when the suit's archive holds the twin AI's parts the list
 * asks for (URunSceneSubsystem::IsOpenEndingOffered). Without them it offers nothing: no prompt, no refusal, no sound.
 *
 * The choice is a hold: BeginHold starts it (the pawn's Interact pressed), EndHold before the end cancels it (released).
 * The hold lasts UTheLastShiftSettings::ChoiceHoldSeconds of the run's clock, reported by OnHoldProgress (the suit's
 * collection ring follows it); it stands still behind the pause menu and is cancelled by a Stop or by leaving the
 * terminal's reach. Held through: OnChoiceMade, and the terminal's OnActivated and OnAnyActivated with cost 0 (the
 * terminal sound; no spending comment).
 */
UCLASS(Blueprintable)
class THELASTSHIFT_API AChoiceTerminal : public APowerTerminal
{
	GENERATED_BODY()

public:
	AChoiceTerminal();

	/** The ending this place chooses: Inject or Open (the DT_Endings row; never text). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Choice")
	FName Ending = TEXT("Inject");

	/** The chamber lets the place offer its choice (after the last message), or stops it (a choice was made). */
	UFUNCTION(BlueprintCallable, Category = "Choice")
	void SetOffering(bool bInOffering);

	/** The place offers its choice now: let by the chamber, Normal time, no choice made, and for Open the twin AI's parts. */
	UFUNCTION(BlueprintPure, Category = "Choice")
	bool OffersChoice() const;

	/** The label the prompt shows with the key: the ending list's label for Ending. */
	UFUNCTION(BlueprintPure, Category = "Choice")
	FText GetChoiceLabel() const;

	/** Starts the hold for Pawn: false when the place offers nothing or Pawn is out of its reach. */
	UFUNCTION(BlueprintCallable, Category = "Choice")
	bool BeginHold(APawn* Pawn);

	/** Interact released: cancels a hold that has not ended. */
	UFUNCTION(BlueprintCallable, Category = "Choice")
	void EndHold();

	UFUNCTION(BlueprintPure, Category = "Choice")
	bool IsHolding() const { return bHolding; }

	/** 0..1 of the hold now (0 when none). */
	UFUNCTION(BlueprintPure, Category = "Choice")
	float GetHoldFraction() const;

	/** The hold was held through: the choice is made. */
	UFUNCTION(BlueprintPure, Category = "Choice")
	bool HasChosen() const { return bChosen; }

	/** Holds started and cancelled so far (tests). */
	int32 GetHoldsStarted() const { return HoldsStarted; }
	int32 GetHoldsCancelled() const { return HoldsCancelled; }

	UPROPERTY(BlueprintAssignable, Category = "Choice")
	FOnChoiceHoldProgress OnHoldProgress;

	UPROPERTY(BlueprintAssignable, Category = "Choice")
	FOnChoiceMade OnChoiceMade;

	// --- The power terminal: no payment, nothing powered ---
	virtual bool TryActivate(APawn* InstigatorPawn) override { return false; }
	virtual bool AcceptsPayment() const override { return false; }
	virtual bool OffersPayment() const override { return false; }
	virtual bool OffersRead() const override { return false; }
	virtual int32 GetCost() const override { return 0; }
	virtual EPaymentKind GetPaymentKind() const override { return EPaymentKind::Other; }

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void CancelHold(const TCHAR* Why);

	UPROPERTY(Transient)
	TObjectPtr<UTimeStateSubsystem> TimeState;

	TWeakObjectPtr<APawn> HoldPawn;
	bool bOffering = false;
	bool bHolding = false;
	bool bChosen = false;
	double HoldSeconds = 0.0;
	double LastRunSeconds = -1.0;
	int32 HoldsStarted = 0;
	int32 HoldsCancelled = 0;
};
