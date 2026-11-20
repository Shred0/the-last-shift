// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FlashlightHintWidget.generated.h"

class UBorder;
class UInputAction;
class UStdKeyPromptWidget;

/** Where the flashlight hint stands. */
UENUM(BlueprintType)
enum class EFlashlightHintState : uint8
{
	/** Not asked for in this map. */
	Idle,
	/** Asked for (Activate) and the flashlight still off: shown in Normal time. */
	Active,
	/** The flashlight was switched on: hidden for the rest of the map. */
	Done
};

/**
 * Suit UI: the flashlight hint, bottom-centre above the Interact prompt: "[key] Flashlight". Shows the key bound to the
 * Flashlight action through StdCore's key prompt, so a remap shows at once. Hidden until Activate (the arrival's hatch asks
 * for it once the home AI's Light lines are typed out); then shown until the owning pawn's flashlight is on, and never
 * again in that map. While time is Stopped (the flashlight is locked off then) and behind the pause menu it is hidden and
 * stays active. The label is a short UI label, never story text. Never the scan colour. The tree is built in C++; the pawn
 * creates it and gives it the Flashlight action.
 */
UCLASS()
class THELASTSHIFT_API UFlashlightHintWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Flashlight action whose key the hint shows (the pawn's own). */
	UFUNCTION(BlueprintCallable, Category = "Flashlight Hint")
	void SetFlashlightAction(const UInputAction* InAction);

	/** Starts the hint (no-op unless Idle). If the flashlight is already on, it is Done at once and never shows. */
	UFUNCTION(BlueprintCallable, Category = "Flashlight Hint")
	void Activate();

	UFUNCTION(BlueprintPure, Category = "Flashlight Hint")
	EFlashlightHintState GetHintState() const { return HintState; }

	/** The hint is on screen now. */
	UFUNCTION(BlueprintPure, Category = "Flashlight Hint")
	bool IsShown() const { return bShown; }

	/** The key the hint shows (invalid while hidden). */
	UFUNCTION(BlueprintPure, Category = "Flashlight Hint")
	FKey GetShownKey() const;

	/** The line as shown, the key in brackets ("[F] Flashlight"); empty while hidden. */
	UFUNCTION(BlueprintPure, Category = "Flashlight Hint")
	FString GetShownText() const;

	/** Real-time seconds of the first frame it was shown (-1 before). */
	double GetFirstShownRealSeconds() const { return FirstShownReal; }

	/** Distance of the hint's bottom edge from the bottom of the screen (above the Interact prompt). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flashlight Hint", meta = (ClampMin = "0.0", ClampMax = "600.0"))
	float BottomMargin = 190.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flashlight Hint", meta = (ClampMin = "8", ClampMax = "48"))
	int32 FontSize = 18;

	/** Colour of the key cap and label. Not the scan colour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flashlight Hint")
	FLinearColor TextColor = FLinearColor(0.85f, 0.85f, 0.85f, 0.9f);

	/** Panel behind the hint, so it stays readable over a lit surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flashlight Hint")
	FLinearColor BackdropColor = FLinearColor(0.f, 0.f, 0.f, 0.6f);

	/** After the key. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flashlight Hint|Labels")
	FText Label = NSLOCTEXT("TheLastShift", "HintFlashlight", "Flashlight");

	virtual bool Initialize() override;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Flashlight Hint")
	TObjectPtr<UBorder> Border_Hint;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Flashlight Hint")
	TObjectPtr<UStdKeyPromptWidget> KeyPrompt;

private:
	void ApplyStyle();
	void Refresh();
	void SetShown(bool bInShown);
	bool IsOwnerFlashlightOn() const;

	UPROPERTY(Transient)
	TObjectPtr<const UInputAction> FlashlightAction;

	EFlashlightHintState HintState = EFlashlightHintState::Idle;
	bool bShown = false;
	double FirstShownReal = -1.0;
};
