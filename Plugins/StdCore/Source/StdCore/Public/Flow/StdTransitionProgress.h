#pragma once

#include "CoreMinimal.h"

class UStdProjectSettings;

/**
 * What the covered screen's progress bar and marker show during one covered period, without Slate.
 * UStdFlowSubsystem owns one and advances it; the overlay only draws it.
 *
 * Bar: the largest fraction reported so far (0..1); it never goes back and no timer moves it.
 * Marker: the fraction of the covered time that has passed (0..1), never going back, waiting at 1 once that time has
 * passed. It exists only when the covered time is greater than zero. It crosses the screen from the left edge to the
 * right, or from the right edge to the left when the covered period asks for it (Reset).
 */
struct STDCORE_API FStdTransitionProgress
{
	/**
	 * Starts a covered period: bar and marker at 0. CoveredTargetSeconds <= 0 means no marker. bInMarkerRightToLeft: the
	 * marker starts at the right edge and crosses to the left.
	 */
	void Reset(float CoveredTargetSeconds, bool bInMarkerRightToLeft = false);

	/** Reports progress; the bar shows the largest fraction reported since Reset. */
	void Report(float Fraction);

	/** Advances the marker to CoveredSeconds of the covered time (it never moves back). */
	void UpdateCovered(float CoveredSeconds);

	/** The covered time is over: the marker is at the right edge. */
	void FinishCovered();

	float GetBarFraction() const { return Bar; }
	float GetMarkerFraction() const { return Marker; }
	/** Where the marker stands across the screen, 0 (left edge) to 1 (right edge): the fraction, or 1 minus it from right to left. */
	float GetMarkerPosition() const { return bMarkerRightToLeft ? 1.f - Marker : Marker; }
	bool IsMarkerRightToLeft() const { return bMarkerRightToLeft; }
	float GetCoveredTargetSeconds() const { return CoveredTarget; }
	bool HasMarker() const { return CoveredTarget > 0.f; }

private:
	float Bar = 0.f;
	float Marker = 0.f;
	float CoveredTarget = 0.f;
	bool bMarkerRightToLeft = false;
};

/** Where and how the bar and the marker are drawn on a covered screen of a given size (Slate units, origin top left). */
struct STDCORE_API FStdTransitionLayout
{
	bool bShowBar = false;
	/** The filled part of the bar: from the left edge, BarFraction of the width, on the bottom edge. */
	FVector2f BarPosition = FVector2f::ZeroVector;
	FVector2f BarSize = FVector2f::ZeroVector;
	FLinearColor BarColor = FLinearColor::Transparent;

	bool bShowMarker = false;
	/** Top left corner: x from 0 (left edge) to width - marker width (right edge), by the marker's position; its bottom on the bar's top edge. */
	FVector2f MarkerPosition = FVector2f::ZeroVector;
	FVector2f MarkerSize = FVector2f::ZeroVector;
	FLinearColor MarkerColor = FLinearColor::Transparent;

	/**
	 * The layout for Settings (switches, sizes, colours) on a screen of ScreenSize, with Progress, at overlay opacity Alpha
	 * (colours are multiplied by it; nothing is shown at 0).
	 */
	static FStdTransitionLayout Compute(const UStdProjectSettings& Settings, const FVector2f& ScreenSize, const FStdTransitionProgress& Progress, float Alpha);
};
