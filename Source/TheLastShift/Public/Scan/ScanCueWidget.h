// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ScanCueWidget.generated.h"

class UScanDirectorComponent;
class UStdRadialProgress;

/**
 * Minimal "scan ready" cue: a thin ring, bottom-centre, that fills over the scan cooldown and turns ReadyColor when a
 * scan can be requested. Hidden in Normal time. No text, no bars, no sound. Never the scan colour.
 *
 * Reads the director's pures every frame: the ring is full and recoloured in the same frame IsScanReady() turns true,
 * never before (the fill stops just short of full until then). Created by whoever owns the HUD (the test character for
 * now), never by the director, which stays UI-free. WBP_ScanCue derives from this and provides the Ring inside a Canvas
 * Panel; size, colours and position are applied from the tunables below.
 */
UCLASS(Abstract)
class THELASTSHIFT_API UScanCueWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Director whose cooldown the cue follows. Null hides the cue. */
	UFUNCTION(BlueprintCallable, Category = "Scan Cue")
	void SetScanDirector(UScanDirectorComponent* InDirector);

	UFUNCTION(BlueprintPure, Category = "Scan Cue")
	UScanDirectorComponent* GetScanDirector() const { return Director.Get(); }

	/** Ring radius (centre line), in slate units (px at DPI scale 1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scan Cue", meta = (ClampMin = "8.0", ClampMax = "64.0"))
	float RingRadius = 24.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scan Cue", meta = (ClampMin = "1.0", ClampMax = "12.0"))
	float RingThickness = 2.5f;

	/** Distance of the ring's bottom edge from the bottom of the screen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scan Cue", meta = (ClampMin = "0.0", ClampMax = "400.0"))
	float BottomMargin = 48.f;

	/** Filled part of the ring while the cooldown runs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scan Cue")
	FLinearColor FillColor = FLinearColor(1.f, 1.f, 1.f, 0.35f);

	/** Unfilled part of the ring. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scan Cue")
	FLinearColor TrackColor = FLinearColor(1.f, 1.f, 1.f, 0.08f);

	/** The whole ring once a scan is ready. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scan Cue")
	FLinearColor ReadyColor = FLinearColor(1.f, 1.f, 1.f, 0.9f);

	/** Pushes the tunables above to the ring (called on construct; call after changing them at runtime). */
	UFUNCTION(BlueprintCallable, Category = "Scan Cue")
	void ApplyRingStyle();

protected:
	virtual void NativePreConstruct() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(BlueprintReadOnly, Category = "Scan Cue", meta = (BindWidget))
	TObjectPtr<UStdRadialProgress> Ring;

private:
	void Refresh();

	TWeakObjectPtr<UScanDirectorComponent> Director;

	/** Last visibility pushed to the ring, to skip redundant updates. */
	bool bLastShown = true;
};
