// SPDX-License-Identifier: MIT

#include "Residue/ResidueCollectCueWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Epilogue/ChoiceTerminal.h"
#include "Residue/Residue.h"
#include "UI/StdRadialProgress.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ResidueCollectCueWidget)

bool UResidueCollectCueWidget::Initialize()
{
	const bool bSuperInitialized = Super::Initialize();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Canvas"));
		WidgetTree->RootWidget = Canvas;
		Ring = WidgetTree->ConstructWidget<UStdRadialProgress>(UStdRadialProgress::StaticClass(), TEXT("Ring"));
		Canvas->AddChildToCanvas(Ring);
	}
	return bSuperInitialized;
}

void UResidueCollectCueWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::HitTestInvisible);
	ApplyStyle();
	Show(false);
}

void UResidueCollectCueWidget::NativeDestruct()
{
	UnbindResidue();
	UnbindChoice();
	Super::NativeDestruct();
}

void UResidueCollectCueWidget::ApplyStyle()
{
	if (!Ring)
	{
		return;
	}
	Ring->SetRadius(FMath::Clamp(RingRadius, 8.f, 64.f));
	Ring->SetThickness(FMath::Clamp(RingThickness, 1.f, 12.f));
	Ring->SetTrackColor(TrackColor);
	Ring->SetClockwise(true);
	Ring->SetStartAngle(0.f);
	if (Residue.IsValid() || Choice.IsValid())
	{
		Ring->SetFillColor(AResidue::GetResidueColor());
		Ring->SetUseReadyColor(false);
	}

	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Ring->Slot))
	{
		CanvasSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		CanvasSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		CanvasSlot->SetAutoSize(false);
		CanvasSlot->SetSize(FVector2D(Ring->GetOuterDiameter()));
		CanvasSlot->SetPosition(FVector2D::ZeroVector);
	}
}

void UResidueCollectCueWidget::SetResidue(AResidue* InResidue)
{
	if (InResidue == Residue.Get())
	{
		return;
	}
	UnbindResidue();
	Residue = InResidue;
	if (InResidue)
	{
		InResidue->OnCollectProgress.AddUniqueDynamic(this, &UResidueCollectCueWidget::HandleProgress);
		InResidue->OnCollected.AddUniqueDynamic(this, &UResidueCollectCueWidget::HandleCollected);
		ApplyStyle();
		HandleProgress(InResidue->GetCollectProgress());
	}
	else
	{
		Show(false);
	}
}

void UResidueCollectCueWidget::UnbindResidue()
{
	if (AResidue* Current = Residue.Get())
	{
		Current->OnCollectProgress.RemoveDynamic(this, &UResidueCollectCueWidget::HandleProgress);
		Current->OnCollected.RemoveDynamic(this, &UResidueCollectCueWidget::HandleCollected);
	}
	Residue.Reset();
}

void UResidueCollectCueWidget::SetChoice(AChoiceTerminal* InChoice)
{
	if (InChoice == Choice.Get())
	{
		return;
	}
	UnbindChoice();
	Choice = InChoice;
	if (InChoice)
	{
		// The same ring as a residue's collection, in the same colour.
		InChoice->OnHoldProgress.AddUniqueDynamic(this, &UResidueCollectCueWidget::HandleProgress);
		InChoice->OnChoiceMade.AddUniqueDynamic(this, &UResidueCollectCueWidget::HandleChoiceMade);
		ApplyStyle();
		HandleProgress(InChoice->GetHoldFraction());
	}
	else
	{
		Show(false);
	}
}

void UResidueCollectCueWidget::UnbindChoice()
{
	if (AChoiceTerminal* Current = Choice.Get())
	{
		Current->OnHoldProgress.RemoveDynamic(this, &UResidueCollectCueWidget::HandleProgress);
		Current->OnChoiceMade.RemoveDynamic(this, &UResidueCollectCueWidget::HandleChoiceMade);
	}
	Choice.Reset();
}

void UResidueCollectCueWidget::HandleChoiceMade(AChoiceTerminal* InChoice)
{
	Show(false);
}

bool UResidueCollectCueWidget::IsRingShown() const
{
	return Ring && Ring->GetVisibility() != ESlateVisibility::Hidden && Ring->GetVisibility() != ESlateVisibility::Collapsed;
}

float UResidueCollectCueWidget::GetRingProgress() const
{
	return Progress;
}

void UResidueCollectCueWidget::HandleProgress(float Fraction)
{
	Progress = FMath::Clamp(Fraction, 0.f, 1.f);
	const bool bShow = (Residue.IsValid() || Choice.IsValid()) && Fraction > 0.f;
	if (Ring)
	{
		Ring->SetProgress(FMath::Clamp(Fraction, 0.f, 1.f));
	}
	Show(bShow);
}

void UResidueCollectCueWidget::HandleCollected()
{
	Show(false);
}

void UResidueCollectCueWidget::Show(bool bShow)
{
	if (Ring)
	{
		Ring->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
}
