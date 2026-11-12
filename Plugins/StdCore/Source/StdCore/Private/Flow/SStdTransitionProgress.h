#pragma once

#include "CoreMinimal.h"
#include "Flow/StdTransitionProgress.h"
#include "Widgets/SLeafWidget.h"

struct FSlateBrush;

/** The covered screen's progress bar and marker: draws FStdTransitionLayout over all the space it is given. */
class SStdTransitionProgress : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SStdTransitionProgress) {}
		/** Progress of the current covered period. */
		SLATE_ATTRIBUTE(FStdTransitionProgress, Progress)
		/** Opacity of the overlay. */
		SLATE_ATTRIBUTE(float, Alpha)
		/** Brush of the marker image; null draws a plain rectangle. Must outlive the widget. */
		SLATE_ARGUMENT(const FSlateBrush*, MarkerBrush)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override { return FVector2D::ZeroVector; }

private:
	TAttribute<FStdTransitionProgress> Progress;
	TAttribute<float> Alpha;
	const FSlateBrush* MarkerBrush = nullptr;
};
