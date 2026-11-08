// Copyright 2026 <OWNER>. All rights reserved. See Plugins/StdCore/LICENSE.

#include "UI/StdRadialProgress.h"

#include "UI/SStdRadialProgress.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(StdRadialProgress)

#define LOCTEXT_NAMESPACE "StdCore"

namespace StdRadialProgressDetail
{
	// Mirrors of the UPROPERTY clamps.
	static float ClampProgress(float Value) { return FMath::Clamp(Value, 0.f, 1.f); }
	static float ClampRadius(float Value) { return FMath::Clamp(Value, 1.f, 512.f); }
	static float ClampThickness(float Value) { return FMath::Clamp(Value, 0.5f, 64.f); }
	static float ClampStartAngle(float Value) { return FMath::Clamp(Value, -360.f, 360.f); }
}

UStdRadialProgress::UStdRadialProgress()
{
	// Purely visual by default; a parent can still make it hit-testable.
	SetVisibilityInternal(ESlateVisibility::HitTestInvisible);
}

TSharedRef<SWidget> UStdRadialProgress::RebuildWidget()
{
	MyRing = SNew(SStdRadialProgress);
	return MyRing.ToSharedRef();
}

void UStdRadialProgress::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MyRing.Reset();
}

void UStdRadialProgress::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	PushToSlate();
}

void UStdRadialProgress::PushToSlate()
{
	if (!MyRing.IsValid())
	{
		return;
	}

	using namespace StdRadialProgressDetail;
	FStdRadialProgressParams Params;
	Params.Progress = ClampProgress(Progress);
	Params.Radius = ClampRadius(Radius);
	Params.Thickness = ClampThickness(Thickness);
	Params.FillColor = FillColor;
	Params.TrackColor = TrackColor;
	Params.ReadyColor = ReadyColor;
	Params.bUseReadyColor = bUseReadyColor;
	Params.bClockwise = bClockwise;
	Params.StartAngle = ClampStartAngle(StartAngle);
	MyRing->SetParams(Params);
}

float UStdRadialProgress::GetOuterDiameter() const
{
	return 2.f * StdRadialProgressDetail::ClampRadius(Radius) + StdRadialProgressDetail::ClampThickness(Thickness);
}

void UStdRadialProgress::SetProgress(float InProgress)
{
	InProgress = StdRadialProgressDetail::ClampProgress(InProgress);
	if (InProgress != Progress)
	{
		Progress = InProgress;
		PushToSlate();
	}
}

void UStdRadialProgress::SetRadius(float InRadius)
{
	Radius = StdRadialProgressDetail::ClampRadius(InRadius);
	PushToSlate();
}

void UStdRadialProgress::SetThickness(float InThickness)
{
	Thickness = StdRadialProgressDetail::ClampThickness(InThickness);
	PushToSlate();
}

void UStdRadialProgress::SetFillColor(FLinearColor InColor)
{
	FillColor = InColor;
	PushToSlate();
}

void UStdRadialProgress::SetTrackColor(FLinearColor InColor)
{
	TrackColor = InColor;
	PushToSlate();
}

void UStdRadialProgress::SetUseReadyColor(bool bInUseReadyColor)
{
	bUseReadyColor = bInUseReadyColor;
	PushToSlate();
}

void UStdRadialProgress::SetReadyColor(FLinearColor InColor)
{
	ReadyColor = InColor;
	PushToSlate();
}

void UStdRadialProgress::SetClockwise(bool bInClockwise)
{
	bClockwise = bInClockwise;
	PushToSlate();
}

void UStdRadialProgress::SetStartAngle(float InDegrees)
{
	StartAngle = StdRadialProgressDetail::ClampStartAngle(InDegrees);
	PushToSlate();
}

#if WITH_EDITOR
const FText UStdRadialProgress::GetPaletteCategory()
{
	return LOCTEXT("PaletteCategory", "Std Core");
}
#endif

#undef LOCTEXT_NAMESPACE
