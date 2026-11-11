// SPDX-License-Identifier: MIT

#include "Scan/ScanCueWidget.h"

#include "Components/CanvasPanelSlot.h"
#include "Run/TheLastShiftSettings.h"
#include "Scan/ScanDirectorComponent.h"
#include "UI/StdRadialProgress.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ScanCueWidget)

namespace ScanCue
{
	/** Highest fill shown while the cooldown runs: a full ring always means IsScanReady(). */
	static constexpr float MaxCooldownProgress = 0.999f;
}

void UScanCueWidget::SetScanDirector(UScanDirectorComponent* InDirector)
{
	Director = InDirector;
	Refresh();
}

void UScanCueWidget::NativePreConstruct()
{
	Super::NativePreConstruct();
	ApplyRingStyle();
}

void UScanCueWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Purely visual: never takes clicks or focus away from the game.
	SetVisibility(ESlateVisibility::HitTestInvisible);
	ApplyRingStyle();
	bLastShown = true;
	Refresh();
}

void UScanCueWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	Refresh();
}

void UScanCueWidget::ApplyRingStyle()
{
	if (!Ring)
	{
		return;
	}

	Ring->SetRadius(FMath::Clamp(RingRadius, 8.f, 64.f));
	Ring->SetThickness(FMath::Clamp(RingThickness, 1.f, 12.f));
	Ring->SetFillColor(FillColor);
	Ring->SetTrackColor(TrackColor);
	Ring->SetUseReadyColor(true);
	Ring->SetReadyColor(UTheLastShiftSettings::GetScanColors().ReadyCue);
	Ring->SetClockwise(true);
	Ring->SetStartAngle(0.f);
	Ring->SetVisibility(ESlateVisibility::HitTestInvisible);

	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Ring->Slot))
	{
		const float Diameter = Ring->GetOuterDiameter();
		CanvasSlot->SetAnchors(FAnchors(0.5f, 1.f));
		CanvasSlot->SetAlignment(FVector2D(0.5f, 1.f));
		CanvasSlot->SetAutoSize(false);
		CanvasSlot->SetSize(FVector2D(Diameter));
		CanvasSlot->SetPosition(FVector2D(0.f, -FMath::Clamp(BottomMargin, 0.f, 400.f)));
	}

	// In the designer there is no director: preview the cooldown half-way.
	if (IsDesignTime())
	{
		Ring->SetProgress(0.5f);
	}
}

void UScanCueWidget::Refresh()
{
	if (!Ring || IsDesignTime())
	{
		return;
	}

	const UScanDirectorComponent* Current = Director.Get();
	const bool bShow = Current && Current->IsScanCycleActive();
	if (bShow != bLastShown)
	{
		bLastShown = bShow;
		Ring->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
	if (!bShow)
	{
		return;
	}

	const FLinearColor& Ready = UTheLastShiftSettings::GetScanColors().ReadyCue;
	if (!Ring->ReadyColor.Equals(Ready, 0.f))
	{
		Ring->SetReadyColor(Ready);
	}

	// Before the first scan of a stop the fraction is 0: an empty ring until the automatic scan starts the cooldown.
	const float Progress = Current->IsScanReady()
		? 1.f
		: FMath::Min(Current->GetCooldownFraction(), ScanCue::MaxCooldownProgress);
	Ring->SetProgress(Progress);
}
