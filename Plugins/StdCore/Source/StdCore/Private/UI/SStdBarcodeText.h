// Copyright 2026 <OWNER>. All rights reserved. See Plugins/StdCore/LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "UI/StdBarcodeText.h"
#include "Widgets/SLeafWidget.h"

/** Everything the barcode text needs to draw itself; values are expected already clamped. */
struct FStdBarcodeTextParams
{
	FString Text;
	int32 Shown = 0;
	int32 Translated = 0;
	FLinearColor BarColor = FLinearColor::White;
	FLinearColor TextColor = FLinearColor::White;
	FVector2f CellSize = FVector2f(13.f, 24.f);
	int32 Columns = 40;
	float LineSpacing = 4.f;
	float FontScale = 0.56f;
	bool bSizeToShown = false;
};

namespace StdBarcode
{
	/** Size of the laid-out text: the widest used column and the lines used (only up to the last shown cell when bSizeToShown). */
	FVector2f ComputeContentSize(const FStdBarcodeTextParams& Params, const TArray<FCellPosition>& Cells, int32 Rows);
}

/** Slate side of UStdBarcodeText: solid boxes for the bars and one text element per translated character. */
class SStdBarcodeText : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SStdBarcodeText) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Replaces all parameters; lays the text out again only when the text or the columns changed. */
	void SetParams(const FStdBarcodeTextParams& InParams);

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

protected:
	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;

private:
	FStdBarcodeTextParams Params;
	TArray<StdBarcode::FCellPosition> Cells;
	int32 Rows = 0;
};
