// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InteractPromptWidget.generated.h"

class APawn;
class APowerTerminal;
class UBorder;
class UHorizontalBox;
class UInputAction;
class UStdKeyPromptWidget;
class UTextBlock;

/** What the Interact prompt shows. */
UENUM(BlueprintType)
enum class EInteractPromptMode : uint8
{
	Hidden,
	/** Stopped, a collectable residue within reach: "Hold [key] Collect". */
	Collect,
	/** Normal time, a terminal within reach with something to power: "[key] Power — cost · energy banked". */
	Power,
	/** A terminal refused for lack of energy: "Not enough energy" for RefusalSeconds. */
	Refusal
};

/**
 * Suit UI: the Interact prompt, bottom-centre above the scan cue. Shows the key bound to Interact through StdCore's key
 * prompt, so a remap shows at once.
 *
 * - While Stopped, with an Available residue within its CollectRadius (AResidue::FindCollectable), whether or not it
 *   was scanned: "Hold [key] Collect". Hidden while the collect ring fills and once the residue is taken.
 * - In Normal time, within a terminal's InteractRadius, when one of its targets is not powered:
 *   "[key] Power — cost · energy banked", the numbers read every frame.
 * - A press refused for lack of energy (the terminal's OnInsufficientEnergy in Normal time): "Not enough energy" for
 *   RefusalSeconds of real time.
 *
 * The labels are short UI labels, never story text. Never the scan colour, never at the screen centre. The tree is built
 * in C++, so the class is used as is. Created by whoever owns the HUD (the pawn) and given the Interact action.
 */
UCLASS()
class THELASTSHIFT_API UInteractPromptWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Interact action whose key the prompt shows (the pawn's own). */
	UFUNCTION(BlueprintCallable, Category = "Interact Prompt")
	void SetInteractAction(const UInputAction* InAction);

	/** Distance of the prompt's bottom edge from the bottom of the screen (the scan cue sits below it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interact Prompt", meta = (ClampMin = "0.0", ClampMax = "600.0"))
	float BottomMargin = 140.f;

	/** How long "Not enough energy" stays after a refused press. Real time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interact Prompt", meta = (ClampMin = "0.5", ClampMax = "10.0", Units = "s"))
	float RefusalSeconds = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interact Prompt", meta = (ClampMin = "8", ClampMax = "48"))
	int32 FontSize = 18;

	/** Colour of the prompt's text and key cap. Not the scan colour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interact Prompt")
	FLinearColor TextColor = FLinearColor(0.85f, 0.85f, 0.85f, 0.9f);

	/** Panel behind the prompt, so it stays readable over a lit surface (the flashlight's spot); invisible on the black screen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interact Prompt")
	FLinearColor BackdropColor = FLinearColor(0.f, 0.f, 0.f, 0.6f);

	/** Before the key, while Stopped near a residue. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interact Prompt|Labels")
	FText HoldLabel = NSLOCTEXT("TheLastShift", "PromptHold", "Hold");

	/** After the key, while Stopped near a residue. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interact Prompt|Labels")
	FText CollectLabel = NSLOCTEXT("TheLastShift", "PromptCollect", "Collect");

	/** After the key near a terminal: {Cost} and {Energy} (banked) are replaced. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interact Prompt|Labels")
	FText PowerLabel = NSLOCTEXT("TheLastShift", "PromptPower", "Power — {Cost} · energy {Energy}");

	/** Shown alone after a refused press. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interact Prompt|Labels")
	FText RefusalLabel = NSLOCTEXT("TheLastShift", "PromptRefusal", "Not enough energy");

	UFUNCTION(BlueprintPure, Category = "Interact Prompt")
	EInteractPromptMode GetMode() const { return Mode; }

	/** The whole line as shown, the key in brackets ("Hold [E] Collect"); empty when hidden. */
	UFUNCTION(BlueprintPure, Category = "Interact Prompt")
	FString GetShownText() const;

	/** The key the prompt shows (invalid when hidden or for the refusal). */
	UFUNCTION(BlueprintPure, Category = "Interact Prompt")
	FKey GetShownKey() const;

	virtual bool Initialize() override;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Interact Prompt")
	TObjectPtr<UBorder> Border_Prompt;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Interact Prompt")
	TObjectPtr<UHorizontalBox> Box_Prompt;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Interact Prompt")
	TObjectPtr<UTextBlock> Text_Prefix;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Interact Prompt")
	TObjectPtr<UStdKeyPromptWidget> KeyPrompt;

private:
	UFUNCTION()
	void HandleInsufficientEnergy(int32 Cost, int32 BankedEnergy, bool bTimeStopped);

	void ApplyStyle();
	void Refresh();
	void ShowMode(EInteractPromptMode NewMode, const FText& Prefix, const FText& Label);
	void BindTerminal(APowerTerminal* Terminal);
	double GetRealNow() const;

	UPROPERTY(Transient)
	TObjectPtr<const UInputAction> InteractAction;

	/** Terminal in reach whose refusals the prompt listens to. */
	TWeakObjectPtr<APowerTerminal> BoundTerminal;

	EInteractPromptMode Mode = EInteractPromptMode::Hidden;
	FText ShownPrefix;
	FText ShownLabel;

	/** Real time until which the refusal is shown; negative when none. */
	double RefusalUntil = -1.0;
};
