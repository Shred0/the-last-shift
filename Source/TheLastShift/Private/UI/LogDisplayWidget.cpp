// SPDX-License-Identifier: MIT

#include "UI/LogDisplayWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "UI/StdBarcodeText.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(LogDisplayWidget)

bool ULogDisplayWidget::Initialize()
{
	const bool bSuperInitialized = Super::Initialize();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		Border_Backdrop = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Border_Backdrop"));
		Border_Backdrop->SetHorizontalAlignment(HAlign_Left);
		Border_Backdrop->SetVerticalAlignment(VAlign_Top);
		WidgetTree->RootWidget = Border_Backdrop;

		Barcode_Entry = WidgetTree->ConstructWidget<UStdBarcodeText>(UStdBarcodeText::StaticClass(), TEXT("Barcode_Entry"));
		Barcode_Entry->SetCounts(-1, 0);
		Border_Backdrop->SetContent(Barcode_Entry);
	}
	return bSuperInitialized;
}

void ULogDisplayWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::HitTestInvisible);
	ApplyLayout();
	ApplyColors();
}

void ULogDisplayWidget::SetDisplay(ELogDisplay InDisplay)
{
	Display = InDisplay;
	ApplyLayout();
}

void ULogDisplayWidget::ApplyLayout()
{
	const bool bSurface = Display == ELogDisplay::Surface;
	if (Barcode_Entry)
	{
		Barcode_Entry->SetCellSize(bSurface ? SurfaceCellSize : PanelCellSize);
		Barcode_Entry->SetColumns(bSurface ? SurfaceColumns : PanelColumns);
		Barcode_Entry->SetLineSpacing((bSurface ? SurfaceCellSize.Y : PanelCellSize.Y) * 0.18f);
	}
	if (Border_Backdrop)
	{
		const float Pad = FMath::Clamp(BackdropPadding, 0.f, 200.f) * (bSurface ? 1.f : 0.5f);
		Border_Backdrop->SetPadding(FMargin(Pad));
		Border_Backdrop->SetBrushColor(BackdropColor);
	}
}

void ULogDisplayWidget::ApplyColors()
{
	if (Barcode_Entry)
	{
		const FLinearColor Color = UTheLastShiftSettings::GetTextColors().LogTranslation;
		Barcode_Entry->SetBarColor(Color);
		Barcode_Entry->SetTextColor(Color);
	}
}

void ULogDisplayWidget::SetEntry(const FText& InText)
{
	if (Barcode_Entry)
	{
		Barcode_Entry->SetText(InText);
		Barcode_Entry->SetCounts(-1, 0);
	}
}

void ULogDisplayWidget::SetTranslatedCells(int32 InTranslated)
{
	if (Barcode_Entry)
	{
		Barcode_Entry->SetCounts(-1, FMath::Max(InTranslated, 0));
	}
}
