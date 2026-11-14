// Copyright 2026 <OWNER>. All rights reserved. See Plugins/StdCore/LICENSE.

#include "UI/StdBarcodeText.h"

#include "UI/SStdBarcodeText.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(StdBarcodeText)

#define LOCTEXT_NAMESPACE "StdCore"

namespace StdBarcodeTextDetail
{
	// Mirrors of the UPROPERTY clamps.
	static int32 ClampCount(int32 Value) { return FMath::Clamp(Value, -1, 100000); }
	static FVector2D ClampCellSize(const FVector2D& Value) { return FVector2D(FMath::Clamp(Value.X, 2.0, 256.0), FMath::Clamp(Value.Y, 2.0, 256.0)); }
	static int32 ClampColumns(int32 Value) { return FMath::Clamp(Value, 1, 500); }
	static float ClampLineSpacing(float Value) { return FMath::Clamp(Value, 0.f, 256.f); }
	static float ClampFontScale(float Value) { return FMath::Clamp(Value, 0.1f, 1.5f); }
}

UStdBarcodeText::UStdBarcodeText()
{
	// Purely visual by default; a parent can still make it hit-testable.
	SetVisibilityInternal(ESlateVisibility::HitTestInvisible);
}

TSharedRef<SWidget> UStdBarcodeText::RebuildWidget()
{
	MyText = SNew(SStdBarcodeText);
	return MyText.ToSharedRef();
}

void UStdBarcodeText::ReleaseSlateResources(bool bReleaseChildren)
{
	Super::ReleaseSlateResources(bReleaseChildren);
	MyText.Reset();
}

void UStdBarcodeText::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	PushToSlate();
}

int32 UStdBarcodeText::GetShownCells() const
{
	const int32 Cells = GetCellCount();
	return ShownCount < 0 ? Cells : FMath::Clamp(ShownCount, 0, Cells);
}

int32 UStdBarcodeText::GetTranslatedCells() const
{
	const int32 Shown = GetShownCells();
	return TranslatedCount < 0 ? Shown : FMath::Clamp(TranslatedCount, 0, Shown);
}

int32 UStdBarcodeText::GetLineCount() const
{
	TArray<StdBarcode::FCellPosition> Cells;
	int32 Rows = 0;
	StdBarcode::Layout(Text.ToString(), StdBarcodeTextDetail::ClampColumns(Columns), Cells, Rows);
	return Rows;
}

StdBarcode::FDrawCounts UStdBarcodeText::GetDrawCounts() const
{
	return StdBarcode::CountDrawn(Text.ToString(), StdBarcodeTextDetail::ClampColumns(Columns), GetShownCells(), GetTranslatedCells());
}

FVector2D UStdBarcodeText::GetContentSize() const
{
	using namespace StdBarcodeTextDetail;
	FStdBarcodeTextParams Params;
	Params.Text = Text.ToString();
	Params.Shown = GetShownCells();
	Params.CellSize = FVector2f(ClampCellSize(CellSize));
	Params.Columns = ClampColumns(Columns);
	Params.LineSpacing = ClampLineSpacing(LineSpacing);
	Params.bSizeToShown = bSizeToShown;
	TArray<StdBarcode::FCellPosition> Cells;
	int32 Rows = 0;
	StdBarcode::Layout(Params.Text, Params.Columns, Cells, Rows);
	const FVector2f Size = StdBarcode::ComputeContentSize(Params, Cells, Rows);
	return FVector2D(Size.X, Size.Y);
}

void UStdBarcodeText::PushToSlate()
{
	if (!MyText.IsValid())
	{
		return;
	}
	using namespace StdBarcodeTextDetail;
	FStdBarcodeTextParams Params;
	Params.Text = Text.ToString();
	Params.Shown = GetShownCells();
	Params.Translated = GetTranslatedCells();
	Params.BarColor = BarColor;
	Params.TextColor = TextColor;
	Params.CellSize = FVector2f(ClampCellSize(CellSize));
	Params.Columns = ClampColumns(Columns);
	Params.LineSpacing = ClampLineSpacing(LineSpacing);
	Params.FontScale = ClampFontScale(FontScale);
	Params.bSizeToShown = bSizeToShown;
	MyText->SetParams(Params);
}

void UStdBarcodeText::SetText(const FText& InText)
{
	Text = InText;
	PushToSlate();
}

void UStdBarcodeText::SetShownCount(int32 InCount)
{
	ShownCount = StdBarcodeTextDetail::ClampCount(InCount);
	PushToSlate();
}

void UStdBarcodeText::SetTranslatedCount(int32 InCount)
{
	TranslatedCount = StdBarcodeTextDetail::ClampCount(InCount);
	PushToSlate();
}

void UStdBarcodeText::SetCounts(int32 InShown, int32 InTranslated)
{
	ShownCount = StdBarcodeTextDetail::ClampCount(InShown);
	TranslatedCount = StdBarcodeTextDetail::ClampCount(InTranslated);
	PushToSlate();
}

void UStdBarcodeText::SetBarColor(FLinearColor InColor)
{
	BarColor = InColor;
	PushToSlate();
}

void UStdBarcodeText::SetTextColor(FLinearColor InColor)
{
	TextColor = InColor;
	PushToSlate();
}

void UStdBarcodeText::SetCellSize(FVector2D InSize)
{
	CellSize = StdBarcodeTextDetail::ClampCellSize(InSize);
	PushToSlate();
}

void UStdBarcodeText::SetColumns(int32 InColumns)
{
	Columns = StdBarcodeTextDetail::ClampColumns(InColumns);
	PushToSlate();
}

void UStdBarcodeText::SetLineSpacing(float InSpacing)
{
	LineSpacing = StdBarcodeTextDetail::ClampLineSpacing(InSpacing);
	PushToSlate();
}

void UStdBarcodeText::SetFontScale(float InScale)
{
	FontScale = StdBarcodeTextDetail::ClampFontScale(InScale);
	PushToSlate();
}

void UStdBarcodeText::SetSizeToShown(bool bInSizeToShown)
{
	bSizeToShown = bInSizeToShown;
	PushToSlate();
}

#if WITH_EDITOR
const FText UStdBarcodeText::GetPaletteCategory()
{
	return LOCTEXT("PaletteCategory", "Std Core");
}
#endif

#undef LOCTEXT_NAMESPACE
