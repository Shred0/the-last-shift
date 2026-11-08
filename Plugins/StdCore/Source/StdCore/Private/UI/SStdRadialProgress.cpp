// Copyright 2026 <OWNER>. All rights reserved. See Plugins/StdCore/LICENSE.

#include "UI/SStdRadialProgress.h"

#include "Rendering/DrawElements.h"

namespace StdRadialProgress
{
	/** Target length of one polyline segment along the ring, in slate units: short enough to look round. */
	static constexpr float SegmentLength = 2.f;
	static constexpr int32 MinSegments = 32;
	static constexpr int32 MaxSegments = 256;

	/** Arc length, in slate units, by which a full circle overlaps its own start. */
	static constexpr float SeamOverlap = 1.f;

	/** Points of an arc from StartRadians over SweepRadians (signed), Segments steps for a full turn. */
	static void BuildArc(TArray<FVector2f>& OutPoints, const FVector2f& Center, float Radius, float StartRadians, float SweepRadians, int32 FullTurnSegments)
	{
		const float Fraction = FMath::Abs(SweepRadians) / UE_TWO_PI;
		const int32 Steps = FMath::Max(1, FMath::CeilToInt(FullTurnSegments * Fraction));
		OutPoints.Reset(Steps + 1);
		for (int32 Step = 0; Step <= Steps; ++Step)
		{
			// Slate's Y axis points down, so a positive angle turns clockwise on screen.
			const float Angle = StartRadians + SweepRadians * (static_cast<float>(Step) / Steps);
			OutPoints.Add(Center + FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
		}
	}
}

void SStdRadialProgress::Construct(const FArguments& InArgs)
{
	SetCanTick(false);
}

void SStdRadialProgress::SetParams(const FStdRadialProgressParams& InParams)
{
	const bool bSizeChanged = InParams.Radius != Params.Radius || InParams.Thickness != Params.Thickness;
	Params = InParams;
	Invalidate(bSizeChanged ? EInvalidateWidgetReason::Layout : EInvalidateWidgetReason::Paint);
}

FVector2D SStdRadialProgress::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	return FVector2D(2.f * Params.Radius + Params.Thickness);
}

int32 SStdRadialProgress::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace StdRadialProgress;

	const FVector2f Center = FVector2f(AllottedGeometry.GetLocalSize()) * 0.5f;
	const float Radius = Params.Radius;
	const int32 Segments = FMath::Clamp(FMath::CeilToInt(UE_TWO_PI * Radius / SegmentLength), MinSegments, MaxSegments);
	const float Start = FMath::DegreesToRadians(Params.StartAngle - 90.f); // 0 degrees = 12 o'clock
	const float Direction = Params.bClockwise ? 1.f : -1.f;

	const ESlateDrawEffect Effect = ShouldBeEnabled(bParentEnabled) ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
	const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint();
	const FPaintGeometry PaintGeometry = AllottedGeometry.ToPaintGeometry();

	TArray<FVector2f> Points;
	const bool bFull = Params.Progress >= 1.f;

	// A closed circle drawn as one polyline leaves a notch where the anti-aliased end caps meet: overlap them by ~1 px.
	const float FullTurn = UE_TWO_PI + SeamOverlap / FMath::Max(Radius, 1.f);

	// Track: the whole circle, under the fill. Skipped when the fill covers it.
	if (!bFull && Params.TrackColor.A > 0.f)
	{
		BuildArc(Points, Center, Radius, Start, FullTurn, Segments);
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, PaintGeometry, Points, Effect, Params.TrackColor * Tint, true, Params.Thickness);
	}

	if (Params.Progress > 0.f)
	{
		const FLinearColor Color = (bFull && Params.bUseReadyColor) ? Params.ReadyColor : Params.FillColor;
		const float Sweep = bFull ? FullTurn : UE_TWO_PI * Params.Progress;
		BuildArc(Points, Center, Radius, Start, Direction * Sweep, Segments);
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, PaintGeometry, Points, Effect, Color * Tint, true, Params.Thickness);
	}

	return LayerId + 1;
}
