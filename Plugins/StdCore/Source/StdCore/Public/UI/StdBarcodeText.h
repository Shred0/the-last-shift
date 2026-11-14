// Copyright 2026 <OWNER>. All rights reserved. See Plugins/StdCore/LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "Components/Widget.h"
#include "StdBarcodeText.generated.h"

class SStdBarcodeText;

/**
 * The layout and the bar patterns of UStdBarcodeText, with no widget: every function is deterministic, and the Slate
 * widget paints exactly what ForEachCell visits, so tests can count what is drawn without rendering.
 */
namespace StdBarcode
{
	/** Modules (vertical stripes) across the bar part of a cell; bit i of a mask is module i, left to right. */
	constexpr int32 NumModules = 10;

	/** Whitespace is never drawn: a space stays a gap in both forms. */
	STDCORE_API bool IsGap(TCHAR Character);

	/**
	 * The bar pattern of Character: bit i set = module i is a bar. Deterministic, 0 for whitespace, never 0 otherwise
	 * (the first and the last module are always bars). Two different characters below U+0100 never share a pattern;
	 * above it the code is folded to 8 bits first, so distant characters may.
	 */
	STDCORE_API uint32 GetBarMask(TCHAR Character);

	/** Where a character sits on the grid. Column == Columns marks a space swallowed by a line break (never drawn). */
	struct FCellPosition
	{
		int32 Column = 0;
		int32 Row = 0;
	};

	/**
	 * Grid positions of every character of Text wrapped at Columns cells per line: a word moves whole to the next line
	 * when it does not fit, a word longer than a line breaks at the line's end, a space that ends a full line is
	 * swallowed, a line break starts a new line. OutRows is the number of lines used (0 for an empty text).
	 */
	STDCORE_API void Layout(const FString& Text, int32 Columns, TArray<FCellPosition>& OutCells, int32& OutRows);

	/** How each of the first Shown characters is drawn. */
	enum class ECellForm : uint8
	{
		/** Whitespace or a swallowed space: nothing drawn. */
		Gap,
		/** The bar pattern of the character. */
		Bars,
		/** The character itself. */
		Character
	};

	/**
	 * Visits the first Shown characters of Text (clamped to its length) in order with their position and form: the first
	 * Translated of them (clamped to Shown) as characters, the rest as bars, whitespace as gaps.
	 */
	STDCORE_API void ForEachCell(const FString& Text, const TArray<FCellPosition>& Cells, int32 Shown, int32 Translated,
		TFunctionRef<void(int32 Index, const FCellPosition& Cell, ECellForm Form)> Visit);

	struct FDrawCounts
	{
		int32 Bars = 0;
		int32 Characters = 0;
		int32 Gaps = 0;

		/** Cells shown: drawn ones and gaps. */
		int32 Total() const { return Bars + Characters + Gaps; }
	};

	/** What a paint of Text at Columns with these counts draws (the same visit as the paint). */
	STDCORE_API FDrawCounts CountDrawn(const FString& Text, int32 Columns, int32 Shown, int32 Translated);
}

/**
 * A text in fixed-width cells, each character shown either as a generated pattern of vertical bars or as itself.
 * Two counts drive it: ShownCount cells are shown (-1 = all), and the first TranslatedCount of those are characters,
 * the rest bars. A character's bars are a deterministic function of the character (StdBarcode::GetBarMask); a space
 * stays a gap in both forms, so the shape of the words is the same in both. Wraps on the cell grid at Columns cells
 * per line; the layout comes from the whole text, so nothing moves while the counts change.
 *
 * Drawn with solid Slate boxes and the engine's default font: no texture, no material. Place it from the UMG palette
 * (Std Core) or create it in C++.
 */
UCLASS(meta = (DisplayName = "Barcode Text"))
class STDCORE_API UStdBarcodeText : public UWidget
{
	GENERATED_BODY()

public:
	UStdBarcodeText();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetText, Category = "Std Core|Barcode Text", meta = (MultiLine = "true"))
	FText Text;

	/** Cells shown, from the first character; -1 = the whole text. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetShownCount, Category = "Std Core|Barcode Text", meta = (ClampMin = "-1", ClampMax = "100000"))
	int32 ShownCount = -1;

	/** Of the shown cells, how many from the first are characters; the rest are bars. -1 = all of them. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetTranslatedCount, Category = "Std Core|Barcode Text", meta = (ClampMin = "-1", ClampMax = "100000"))
	int32 TranslatedCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetBarColor, Category = "Std Core|Barcode Text")
	FLinearColor BarColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetTextColor, Category = "Std Core|Barcode Text")
	FLinearColor TextColor = FLinearColor::White;

	/** Width and height of one cell, in slate units. The character is drawn in the engine font sized from the height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetCellSize, Category = "Std Core|Barcode Text", meta = (ClampMin = "2.0", ClampMax = "256.0"))
	FVector2D CellSize = FVector2D(13.0, 24.0);

	/** Cells per line; the text wraps beyond it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetColumns, Category = "Std Core|Barcode Text", meta = (ClampMin = "1", ClampMax = "500"))
	int32 Columns = 40;

	/** Extra space between two lines, in slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetLineSpacing, Category = "Std Core|Barcode Text", meta = (ClampMin = "0.0", ClampMax = "256.0"))
	float LineSpacing = 4.f;

	/** Font size of the characters as a fraction of the cell height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetFontScale, Category = "Std Core|Barcode Text", meta = (ClampMin = "0.1", ClampMax = "1.5"))
	float FontScale = 0.56f;

	/** Desired size covers only the lines that hold shown cells (it grows as cells are shown), else the whole text. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintSetter = SetSizeToShown, Category = "Std Core|Barcode Text")
	bool bSizeToShown = false;

	UFUNCTION(BlueprintCallable, Category = "Std Core|Barcode Text")
	void SetText(const FText& InText);

	UFUNCTION(BlueprintCallable, Category = "Std Core|Barcode Text")
	void SetShownCount(int32 InCount);

	UFUNCTION(BlueprintCallable, Category = "Std Core|Barcode Text")
	void SetTranslatedCount(int32 InCount);

	/** Both counts at once (one repaint). */
	UFUNCTION(BlueprintCallable, Category = "Std Core|Barcode Text")
	void SetCounts(int32 InShown, int32 InTranslated);

	UFUNCTION(BlueprintCallable, Category = "Std Core|Barcode Text")
	void SetBarColor(FLinearColor InColor);

	UFUNCTION(BlueprintCallable, Category = "Std Core|Barcode Text")
	void SetTextColor(FLinearColor InColor);

	UFUNCTION(BlueprintCallable, Category = "Std Core|Barcode Text")
	void SetCellSize(FVector2D InSize);

	UFUNCTION(BlueprintCallable, Category = "Std Core|Barcode Text")
	void SetColumns(int32 InColumns);

	UFUNCTION(BlueprintCallable, Category = "Std Core|Barcode Text")
	void SetLineSpacing(float InSpacing);

	UFUNCTION(BlueprintCallable, Category = "Std Core|Barcode Text")
	void SetFontScale(float InScale);

	UFUNCTION(BlueprintCallable, Category = "Std Core|Barcode Text")
	void SetSizeToShown(bool bInSizeToShown);

	/** Characters in the text (every one takes a cell). */
	UFUNCTION(BlueprintPure, Category = "Std Core|Barcode Text")
	int32 GetCellCount() const { return Text.ToString().Len(); }

	/** ShownCount resolved and clamped to [0, cell count]. */
	UFUNCTION(BlueprintPure, Category = "Std Core|Barcode Text")
	int32 GetShownCells() const;

	/** TranslatedCount resolved and clamped to [0, shown cells]. */
	UFUNCTION(BlueprintPure, Category = "Std Core|Barcode Text")
	int32 GetTranslatedCells() const;

	/** Lines of the whole text on the grid. */
	UFUNCTION(BlueprintPure, Category = "Std Core|Barcode Text")
	int32 GetLineCount() const;

	/** What the next paint draws: bars, characters and gaps among the shown cells. */
	StdBarcode::FDrawCounts GetDrawCounts() const;

	/** The size the widget asks for, in slate units (see bSizeToShown). */
	UFUNCTION(BlueprintPure, Category = "Std Core|Barcode Text")
	FVector2D GetContentSize() const;

	virtual void SynchronizeProperties() override;
	virtual void ReleaseSlateResources(bool bReleaseChildren) override;

#if WITH_EDITOR
	virtual const FText GetPaletteCategory() override;
#endif

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

private:
	/** Clamps every property (Blueprint and C++ writes bypass ClampMin/ClampMax) and pushes them to the Slate widget. */
	void PushToSlate();

	TSharedPtr<SStdBarcodeText> MyText;
};
