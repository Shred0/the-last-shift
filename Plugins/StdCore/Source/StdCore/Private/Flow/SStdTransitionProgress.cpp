#include "Flow/SStdTransitionProgress.h"

#include "Rendering/DrawElements.h"
#include "Settings/StdProjectSettings.h"
#include "Styling/CoreStyle.h"

void SStdTransitionProgress::Construct(const FArguments& InArgs)
{
	Progress = InArgs._Progress;
	Alpha = InArgs._Alpha;
	MarkerBrush = InArgs._MarkerBrush;
	SetCanTick(false);
}

int32 SStdTransitionProgress::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	const FStdTransitionLayout Layout = FStdTransitionLayout::Compute(*UStdProjectSettings::Get(), FVector2f(static_cast<float>(LocalSize.X), static_cast<float>(LocalSize.Y)),
		Progress.Get(), Alpha.Get());
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");

	if (Layout.bShowBar && Layout.BarSize.X > 0.f)
	{
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(Layout.BarSize, FSlateLayoutTransform(Layout.BarPosition)),
			White, ESlateDrawEffect::None, Layout.BarColor);
	}
	if (Layout.bShowMarker)
	{
		const FSlateBrush* Brush = MarkerBrush && MarkerBrush->GetResourceObject() ? MarkerBrush : White;
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(Layout.MarkerSize, FSlateLayoutTransform(Layout.MarkerPosition)),
			Brush, ESlateDrawEffect::None, Layout.MarkerColor);
	}
	return LayerId + 1;
}
