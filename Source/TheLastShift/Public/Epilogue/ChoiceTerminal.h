// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Energy/PowerTerminal.h"
#include "ChoiceTerminal.generated.h"

class AChoiceTerminal;
class UChoiceDialogWidget;
class UTimeStateSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnChoiceMade, AChoiceTerminal*, Terminal, FName, Ending);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnChoiceDialogChanged, AChoiceTerminal*, Terminal, bool, bOpen);

/**
 * The one terminal of the battery's chamber, on the power terminal's modelled body: the ending is chosen there. It takes
 * no payment and powers nothing. It offers (OffersChoice) as a terminal offers, by the Interact prompt within its reach,
 * with the Interact key and its label (URunSceneSubsystem::GetChooseLabel): only in Normal time, once the chamber has let
 * it (SetOffering: after the home AI's last message), before any choice is made and while its dialog is closed.
 *
 * Interact (a press, TryActivate) opens the choice's dialog (UChoiceDialogWidget) over the game's view, with one button
 * for each ending GetEndings gives: Inject always, Open only when the suit's archive holds the twin AI's parts the list
 * asks for (URunSceneSubsystem::IsOpenEndingOffered). A button held through calls Choose: OnChoiceMade with the ending,
 * and the terminal's OnActivated and OnAnyActivated with cost 0 (the terminal sound; no spending comment). Closed without
 * choosing, the terminal offers again.
 *
 * Brief 36B: its reach (InteractRadius) is UTheLastShiftSettings::ChoiceTerminalReachCm, its one owner, read every frame.
 */
UCLASS(Blueprintable)
class THELASTSHIFT_API AChoiceTerminal : public APowerTerminal
{
	GENERATED_BODY()

public:
	AChoiceTerminal();

	/** The chamber lets the terminal offer the choice (after the last message), or stops it (a choice was made). */
	UFUNCTION(BlueprintCallable, Category = "Choice")
	void SetOffering(bool bInOffering);

	/** The terminal offers now: let by the chamber, Normal time, the run not frozen, no choice made, its dialog closed. */
	UFUNCTION(BlueprintPure, Category = "Choice")
	bool OffersChoice() const;

	/** The endings the dialog offers now, in order: Inject, then Open when the twin AI's parts are in the archive (DT_Endings rows; never text). */
	UFUNCTION(BlueprintPure, Category = "Choice")
	TArray<FName> GetEndings() const;

	/** The label the prompt shows with the key: URunSceneSubsystem::GetChooseLabel. */
	UFUNCTION(BlueprintPure, Category = "Choice")
	FText GetPromptLabel() const;

	/** The dialog chose Ending (held through): false when the terminal does not offer it now. */
	UFUNCTION(BlueprintCallable, Category = "Choice")
	bool Choose(FName Ending);

	/** The choice is made. */
	UFUNCTION(BlueprintPure, Category = "Choice")
	bool HasChosen() const { return bChosen; }

	/** The dialog opened from this terminal, while it is open (null otherwise). */
	UFUNCTION(BlueprintPure, Category = "Choice")
	UChoiceDialogWidget* GetDialog() const { return Dialog; }

	UFUNCTION(BlueprintPure, Category = "Choice")
	bool IsDialogOpen() const { return Dialog != nullptr; }

	/** Called by the dialog when it closes, chosen or not. */
	void NotifyDialogClosed(UChoiceDialogWidget* InDialog);

	/** Dialogs opened so far (tests). */
	int32 GetDialogsOpened() const { return DialogsOpened; }

	UPROPERTY(BlueprintAssignable, Category = "Choice")
	FOnChoiceMade OnChoiceMade;

	UPROPERTY(BlueprintAssignable, Category = "Choice")
	FOnChoiceDialogChanged OnDialogChanged;

	/** Interact pressed within reach: opens the dialog when the terminal offers; otherwise nothing (no refusal, no sound). */
	virtual bool TryActivate(APawn* InstigatorPawn) override;

	// --- The power terminal: no payment, nothing powered ---
	virtual bool AcceptsPayment() const override { return false; }
	virtual bool OffersPayment() const override { return false; }
	virtual bool OffersRead() const override { return false; }
	virtual int32 GetCost() const override { return 0; }
	virtual EPaymentKind GetPaymentKind() const override { return EPaymentKind::Other; }

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(Transient)
	TObjectPtr<UTimeStateSubsystem> TimeState;

	UPROPERTY(Transient)
	TObjectPtr<UChoiceDialogWidget> Dialog;

	bool bOffering = false;
	bool bChosen = false;
	int32 DialogsOpened = 0;
};
