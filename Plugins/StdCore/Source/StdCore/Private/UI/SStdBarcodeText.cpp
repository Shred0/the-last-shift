// Copyright 2026 <OWNER>. All rights reserved. See Plugins/StdCore/LICENSE.

#include "UI/SStdBarcodeText.h"

#include "Brushes/SlateColorBrush.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

namespace StdBarcode
{
	/** Part of the cell width left empty, half on each side, so neighbouring cells read as separate glyphs. */
	static constexpr float CellGapFraction = 0.2f;
	/** Part of the cell height left empty above and below the bars. */
	static constexpr float BarInsetFraction = 0.14f;

	bool IsGap(TCHAR Character)
	{
		return FChar::IsWhitespace(Character);
	}

	uint32 GetBarMask(TCHAR Character)
	{
		if (IsGap(Character))
		{
			return 0;
		}
		const uint32 Code = static_cast<uint32>(Character);
		const uint32 Low = Code <= 0xFF ? Code : ((Code ^ (Code >> 8) ^ (Code >> 16)) & 0xFF);
		// An odd multiplier and an offset modulo 256 are a bijection of 0..255: distinct low codes keep distinct patterns.
		const uint32 Mixed = (Low * 167u + 91u) & 0xFFu;
		// Eight free modules between two guard bars.
		return 1u | (Mixed << 1) | (1u << (NumModules - 1));
	}

	void Layout(const FString& Text, int32 Columns, TArray<FCellPosition>& OutCells, int32& OutRows)
	{
		const int32 Width = FMath::Max(Columns, 1);
		OutCells.Reset(Text.Len());
		OutCells.SetNum(Text.Len());
		int32 Column = 0;
		int32 Row = 0;
		int32 LastRow = -1;
		for (int32 Index = 0; Index < Text.Len(); ++Index)
		{
			const TCHAR Character = Text[Index];
			if (Character == TEXT('\n'))
			{
				OutCells[Index] = { FMath::Min(Column, Width), Row };
				LastRow = Row;
				++Row;
				Column = 0;
				continue;
			}
			if (IsGap(Character))
			{
				// A space that ends a full line is swallowed by the break.
				OutCells[Index] = { FMath::Min(Column, Width), Row };
				LastRow = FMath::Max(LastRow, Row);
				Column = FMath::Min(Column + 1, Width);
				continue;
			}
			const bool bWordStart = Index == 0 || IsGap(Text[Index - 1]);
			if (bWordStart && Column > 0)
			{
				int32 WordLength = 0;
				while (Index + WordLength < Text.Len() && !IsGap(Text[Index + WordLength]))
				{
					++WordLength;
				}
				if (Column + WordLength > Width)
				{
					++Row;
					Column = 0;
				}
			}
			if (Column >= Width)
			{
				// A word longer than a line breaks at the line's end.
				++Row;
				Column = 0;
			}
			OutCells[Index] = { Column, Row };
			LastRow = Row;
			++Column;
		}
		OutRows = Text.IsEmpty() ? 0 : LastRow + 1;
	}

	void ForEachCell(const FString& Text, const TArray<FCellPosition>& Cells, int32 Shown, int32 Translated,
		TFunctionRef<void(int32 Index, const FCellPosition& Cell, ECellForm Form)> Visit)
	{
		const int32 Count = FMath::Clamp(Shown, 0, FMath::Min(Text.Len(), Cells.Num()));
		const int32 AsCharacters = FMath::Clamp(Translated, 0, Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			ECellForm Form = Index < AsCharacters ? ECellForm::Character : ECellForm::Bars;
			if (IsGap(Text[Index]))
			{
				Form = ECellForm::Gap;
			}
			Visit(Index, Cells[Index], Form);
		}
	}

	FDrawCounts CountDrawn(const FString& Text, int32 Columns, int32 Shown, int32 Translated)
	{
		TArray<FCellPosition> Cells;
		int32 Rows = 0;
		Layout(Text, Columns, Cells, Rows);
		FDrawCounts Counts;
		ForEachCell(Text, Cells, Shown, Translated, [&Counts](int32, const FCellPosition&, ECellForm Form)
		{
			switch (Form)
			{
			case ECellForm::Gap: ++Counts.Gaps; break;
			case ECellForm::Bars: ++Counts.Bars; break;
			case ECellForm::Character: ++Counts.Characters; break;
			}
		});
		return Counts;
	}

	FVector2f ComputeContentSize(const FStdBarcodeTextParams& Params, const TArray<FCellPosition>& Cells, int32 Rows)
	{
		const int32 Last = Params.bSizeToShown ? FMath::Min(Params.Shown, Cells.Num()) : Cells.Num();
		int32 UsedColumns = 0;
		int32 UsedRows = Params.bSizeToShown ? 0 : Rows;
		for (int32 Index = 0; Index < Last; ++Index)
		{
			const FCellPosition& Cell = Cells[Index];
			if (Cell.Column < Params.Columns)
			{
				UsedColumns = FMath::Max(UsedColumns, Cell.Column + 1);
			}
			UsedRows = FMath::Max(UsedRows, Cell.Row + 1);
		}
		if (UsedRows == 0)
		{
			return FVector2f::ZeroVector;
		}
		return FVector2f(UsedColumns * Params.CellSize.X, UsedRows * Params.CellSize.Y + (UsedRows - 1) * Params.LineSpacing);
	}
}

void SStdBarcodeText::Construct(const FArguments& InArgs)
{
	SetCanTick(false);
}

void SStdBarcodeText::SetParams(const FStdBarcodeTextParams& InParams)
{
	const bool bLayoutChanged = InParams.Text != Params.Text || InParams.Columns != Params.Columns;
	const FVector2f OldSize = StdBarcode::ComputeContentSize(Params, Cells, Rows);
	Params = InParams;
	if (bLayoutChanged)
	{
		StdBarcode::Layout(Params.Text, Params.Columns, Cells, Rows);
	}
	if (StdBarcode::ComputeContentSize(Params, Cells, Rows) != OldSize)
	{
		Invalidate(EInvalidateWidgetReason::Layout);
	}
	else
	{
		Invalidate(EInvalidateWidgetReason::Paint);
	}
}

FVector2D SStdBarcodeText::ComputeDesiredSize(float LayoutScaleMultiplier) const
{
	const FVector2f Size = StdBarcode::ComputeContentSize(Params, Cells, Rows);
	return FVector2D(Size.X, Size.Y);
}

int32 SStdBarcodeText::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	using namespace StdBarcode;
	static const FSlateColorBrush Solid(FLinearColor::White);

	const FVector2f Cell = Params.CellSize;
	const float Pitch = Cell.Y + Params.LineSpacing;
	const float InnerWidth = Cell.X * (1.f - CellGapFraction);
	const float ModuleWidth = InnerWidth / NumModules;
	const float BarTop = Cell.Y * BarInsetFraction;
	const float BarHeight = Cell.Y * (1.f - 2.f * BarInsetFraction);
	const FLinearColor BarTint = Params.BarColor * InWidgetStyle.GetColorAndOpacityTint();
	const FLinearColor TextTint = Params.TextColor * InWidgetStyle.GetColorAndOpacityTint();
	const ESlateDrawEffect Effect = ShouldBeEnabled(bParentEnabled) ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;

	// The engine's own font, sized from the cell height.
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", FMath::Max(1, FMath::RoundToInt(Cell.Y * Params.FontScale)));
	const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

	const int32 BarLayer = LayerId;
	const int32 TextLayer = LayerId + 1;
	ForEachCell(Params.Text, Cells, Params.Shown, Params.Translated, [&](int32 Index, const FCellPosition& Position, ECellForm Form)
	{
		if (Form == ECellForm::Gap || Position.Column >= Params.Columns)
		{
			return;
		}
		const FVector2f Origin(Position.Column * Cell.X, Position.Row * Pitch);
		if (Form == ECellForm::Character)
		{
			const FString Glyph = Params.Text.Mid(Index, 1);
			const FVector2f GlyphSize(Measure->Measure(Glyph, Font));
			const FVector2f At = Origin + FVector2f((Cell.X - GlyphSize.X) * 0.5f, (Cell.Y - GlyphSize.Y) * 0.5f);
			FSlateDrawElement::MakeText(OutDrawElements, TextLayer, AllottedGeometry.ToPaintGeometry(GlyphSize, FSlateLayoutTransform(At)), Glyph, Font,
				Effect, TextTint);
			return;
		}
		// Bars: each run of neighbouring set modules is one box.
		const uint32 Mask = GetBarMask(Params.Text[Index]);
		const float Left = Origin.X + Cell.X * CellGapFraction * 0.5f;
		int32 Module = 0;
		while (Module < NumModules)
		{
			if (!(Mask & (1u << Module)))
			{
				++Module;
				continue;
			}
			const int32 RunStart = Module;
			while (Module < NumModules && (Mask & (1u << Module)))
			{
				++Module;
			}
			const FVector2f BarAt(Left + RunStart * ModuleWidth, Origin.Y + BarTop);
			const FVector2f BarSize((Module - RunStart) * ModuleWidth, BarHeight);
			FSlateDrawElement::MakeBox(OutDrawElements, BarLayer, AllottedGeometry.ToPaintGeometry(BarSize, FSlateLayoutTransform(BarAt)), &Solid, Effect, BarTint);
		}
	});
	return TextLayer;
}
