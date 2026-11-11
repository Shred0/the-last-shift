// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ResidueCollectCueWidget.generated.h"

class AResidue;
class UStdRadialProgress;

/**
 * Hold-to-collect feedback: a ring at the centre of the screen, filled by the residue's OnCollectProgress, in that
 * residue colour (AResidue::GetResidueColor, read when a hold starts, never copied). Hidden unless a hold is in progress. No text.
 *
 * A second instance of the StdCore radial ring that the scan cue uses; the widget tree (a canvas and the ring) is built
 * in C++, so the class is used as is, without a widget Blueprint. Created by whoever owns the HUD.
 */
UCLASS()
class THELASTSHIFT_API UResidueCollectCueWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Residue whose hold the ring follows. Null hides it. */
	UFUNCTION(BlueprintCallable, Category = "Collect Cue")
	void SetResidue(AResidue* InResidue);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collect Cue", meta = (ClampMin = "8.0", ClampMax = "64.0"))
	float RingRadius = 18.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collect Cue", meta = (ClampMin = "1.0", ClampMax = "12.0"))
	float RingThickness = 3.f;

	/** Unfilled part of the ring. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collect Cue")
	FLinearColor TrackColor = FLinearColor(1.f, 1.f, 1.f, 0.08f);

	virtual bool Initialize() override;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Collect Cue")
	TObjectPtr<UStdRadialProgress> Ring;

private:
	UFUNCTION()
	void HandleProgress(float Fraction);

	UFUNCTION()
	void HandleCollected();

	void ApplyStyle();
	void Show(bool bShow);
	void UnbindResidue();

	TWeakObjectPtr<AResidue> Residue;
};
