#include "Flow/StdTransitionProgress.h"

#include "Settings/StdProjectSettings.h"

void FStdTransitionProgress::Reset(float CoveredTargetSeconds)
{
	Bar = 0.f;
	Marker = 0.f;
	CoveredTarget = FMath::Max(CoveredTargetSeconds, 0.f);
}

void FStdTransitionProgress::Report(float Fraction)
{
	Bar = FMath::Max(Bar, FMath::Clamp(Fraction, 0.f, 1.f));
}

void FStdTransitionProgress::UpdateCovered(float CoveredSeconds)
{
	if (HasMarker())
	{
		Marker = FMath::Max(Marker, FMath::Clamp(CoveredSeconds / CoveredTarget, 0.f, 1.f));
	}
}

void FStdTransitionProgress::FinishCovered()
{
	if (HasMarker())
	{
		Marker = 1.f;
	}
}

FStdTransitionLayout FStdTransitionLayout::Compute(const UStdProjectSettings& Settings, const FVector2f& ScreenSize, const FStdTransitionProgress& Progress, float Alpha)
{
	FStdTransitionLayout Layout;
	const float Opacity = FMath::Clamp(Alpha, 0.f, 1.f);
	if (Opacity <= 0.f || ScreenSize.X <= 0.f || ScreenSize.Y <= 0.f)
	{
		return Layout;
	}

	const float BarHeight = FMath::Clamp(Settings.TransitionProgressBarHeight, 1.f, 64.f);
	Layout.bShowBar = Settings.bTransitionProgressBar;
	if (Layout.bShowBar)
	{
		Layout.BarPosition = FVector2f(0.f, ScreenSize.Y - BarHeight);
		Layout.BarSize = FVector2f(ScreenSize.X * Progress.GetBarFraction(), BarHeight);
		Layout.BarColor = Settings.TransitionProgressBarColor;
		Layout.BarColor.A *= Opacity;
	}

	Layout.bShowMarker = Settings.bTransitionMarker && Progress.HasMarker();
	if (Layout.bShowMarker)
	{
		const FVector2f Size(
			FMath::Clamp(static_cast<float>(Settings.TransitionMarkerSize.X), 1.f, FMath::Min(512.f, ScreenSize.X)),
			FMath::Clamp(static_cast<float>(Settings.TransitionMarkerSize.Y), 1.f, 512.f));
		const float Bottom = ScreenSize.Y - (Layout.bShowBar ? BarHeight : 0.f);
		Layout.MarkerSize = Size;
		Layout.MarkerPosition = FVector2f((ScreenSize.X - Size.X) * Progress.GetMarkerFraction(), Bottom - Size.Y);
		Layout.MarkerColor = Settings.TransitionMarkerColor;
		Layout.MarkerColor.A *= Opacity;
	}
	return Layout;
}
