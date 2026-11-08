// Copyright 2026 <OWNER>. All rights reserved. See Plugins/StdCore/LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "StdRadialProgress.generated.h"

class SStdRadialProgress;

/**
 * A ring that fills with Progress: a track circle and a fill arc on top of it, drawn with anti-aliased Slate lines
 * (no textures, no material). Place it from the UMG palette (Std Core) or create it in C++.
 *
 * The widget's desired size is the ring's outer diameter, 2 * Radius + Thickness. At Progress >= 1 the ring is drawn
 * full, in ReadyColor when bUseReadyColor is set.
 */
UCLASS(meta = (DisplayName = "Radial Progress"))
class STDCORE_API UStdRadialProgress : public UWidget
{
	GENERATED_BODY()

public:
	UStdRadialProgress();

	/** 0 = empty, 1 = full. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetProgress, Category = "Std Core|Radial Progress", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float Progress = 0.f;

	/** Radius of the ring's centre line, in slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetRadius, Category = "Std Core|Radial Progress", meta = (ClampMin = "1.0", ClampMax = "512.0"))
	float Radius = 24.f;

	/** Line width of both the track and the fill, in slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetThickness, Category = "Std Core|Radial Progress", meta = (ClampMin = "0.5", ClampMax = "64.0"))
	float Thickness = 3.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetFillColor, Category = "Std Core|Radial Progress")
	FLinearColor FillColor = FLinearColor::White;

	/** Colour of the unfilled part of the ring. Alpha 0 hides the track. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetTrackColor, Category = "Std Core|Radial Progress")
	FLinearColor TrackColor = FLinearColor(1.f, 1.f, 1.f, 0.2f);

	/** When set, a full ring (Progress >= 1) is drawn in ReadyColor instead of FillColor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetUseReadyColor, Category = "Std Core|Radial Progress")
	bool bUseReadyColor = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetReadyColor, Category = "Std Core|Radial Progress", meta = (EditCondition = "bUseReadyColor"))
	FLinearColor ReadyColor = FLinearColor::White;

	/** Fill direction on screen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetClockwise, Category = "Std Core|Radial Progress")
	bool bClockwise = true;

	/** Where the fill starts, in degrees clockwise from 12 o'clock. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetStartAngle, Category = "Std Core|Radial Progress", meta = (ClampMin = "-360.0", ClampMax = "360.0", Units = "deg"))
	float StartAngle = 0.f;

	UFUNCTION(BlueprintCallable, Category = "Std Core|Radial Progress")
	void SetProgress(float InProgress);

	UFUNCTION(BlueprintCallable, Category = "Std Core|Radial Progress")
	void SetRadius(float InRadius);

	UFUNCTION(BlueprintCallable, Category = "Std Core|Radial Progress")
	void SetThickness(float InThickness);

	UFUNCTION(BlueprintCallable, Category = "Std Core|Radial Progress")
	void SetFillColor(FLinearColor InColor);

	UFUNCTION(BlueprintCallable, Category = "Std Core|Radial Progress")
	void SetTrackColor(FLinearColor InColor);

	UFUNCTION(BlueprintCallable, Category = "Std Core|Radial Progress")
	void SetUseReadyColor(bool bInUseReadyColor);

	UFUNCTION(BlueprintCallable, Category = "Std Core|Radial Progress")
	void SetReadyColor(FLinearColor InColor);

	UFUNCTION(BlueprintCallable, Category = "Std Core|Radial Progress")
	void SetClockwise(bool bInClockwise);

	UFUNCTION(BlueprintCallable, Category = "Std Core|Radial Progress")
	void SetStartAngle(float InDegrees);

	/** True when the ring is drawn full (Progress >= 1). */
	UFUNCTION(BlueprintPure, Category = "Std Core|Radial Progress")
	bool IsFull() const { return Progress >= 1.f; }

	/** Outer diameter of the ring, the widget's desired width and height. */
	UFUNCTION(BlueprintPure, Category = "Std Core|Radial Progress")
	float GetOuterDiameter() const;

	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	/** Clamps every property (Blueprint and C++ writes bypass ClampMin/ClampMax) and pushes them to the Slate ring. */
	void PushToSlate();

	TSharedPtr<SStdRadialProgress> MyRing;
};
