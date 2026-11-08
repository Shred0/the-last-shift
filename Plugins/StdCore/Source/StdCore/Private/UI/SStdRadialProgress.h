// Copyright 2026 <OWNER>. All rights reserved. See Plugins/StdCore/LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

/** Everything the ring needs to draw itself; values are expected already clamped. */
struct FStdRadialProgressParams
{
	float Progress = 0.f;
	float Radius = 24.f;
	float Thickness = 3.f;
	FLinearColor FillColor = FLinearColor::White;
	FLinearColor TrackColor = FLinearColor(1.f, 1.f, 1.f, 0.2f);
	FLinearColor ReadyColor = FLinearColor::White;
	bool bUseReadyColor = false;
	bool bClockwise = true;
	float StartAngle = 0.f;
};

/** Slate side of UStdRadialProgress: a track circle and a fill arc drawn as anti-aliased polylines. */
class SStdRadialProgress : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SStdRadialProgress) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Replaces all parameters; repaints, and re-lays out only when the size changed. */
	void SetParams(const FStdRadialProgressParams& InParams);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

protected:
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

private:
	FStdRadialProgressParams Params;
};
