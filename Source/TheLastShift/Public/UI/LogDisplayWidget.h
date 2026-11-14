// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Run/TheLastShiftSettings.h"
#include "LogDisplayWidget.generated.h"

class UBorder;
class UStdBarcodeText;

/**
 * Shows one log entry of the other humanity: the whole entry as barcode writing (StdCore's UStdBarcodeText) with the
 * suit's translation typed over it, cell by cell. The same widget is drawn on a log terminal's surface (a world-space
 * widget component) and as a suit panel on screen (ELogDisplay); the terminal that owns it gives it the entry and the
 * translated count and decides nothing else here. Colours come from UTheLastShiftSettings::TextColors (LogTranslation,
 * for the characters and their bars); the dark backdrop keeps it readable under the flashlight. Tree built in C++.
 */
UCLASS()
class THELASTSHIFT_API ULogDisplayWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Layout for Display: the surface (large cells, the terminal's draw size) or the suit panel (screen-sized cells). */
	UFUNCTION(BlueprintCallable, Category = "Twin Writing|UI")
	void SetDisplay(ELogDisplay InDisplay);

	/** The entry to show, whole, as barcodes; the translation restarts from its first character. */
	UFUNCTION(BlueprintCallable, Category = "Twin Writing|UI")
	void SetEntry(const FText& InText);

	/** How many cells of the entry are translated (shown as characters). */
	UFUNCTION(BlueprintCallable, Category = "Twin Writing|UI")
	void SetTranslatedCells(int32 InTranslated);

	/** Re-reads the colours from the project settings. */
	UFUNCTION(BlueprintCallable, Category = "Twin Writing|UI")
	void ApplyColors();

	/** Cell width and height on the terminal's surface, in widget pixels. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Twin Writing|UI", meta = (ClampMin = "2.0", ClampMax = "256.0"))
	FVector2D SurfaceCellSize = FVector2D(24.0, 50.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Twin Writing|UI", meta = (ClampMin = "4", ClampMax = "200"))
	int32 SurfaceColumns = 38;

	/** Cell width and height in the suit panel, in screen units at 1080p. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Twin Writing|UI", meta = (ClampMin = "2.0", ClampMax = "256.0"))
	FVector2D PanelCellSize = FVector2D(13.0, 28.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Twin Writing|UI", meta = (ClampMin = "4", ClampMax = "200"))
	int32 PanelColumns = 52;

	/** Behind the writing: dark and opaque, so the flashlight cannot wash it out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Twin Writing|UI")
	FLinearColor BackdropColor = FLinearColor(0.004f, 0.004f, 0.005f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Twin Writing|UI", meta = (ClampMin = "0.0", ClampMax = "200.0"))
	float BackdropPadding = 40.f;

	/** Distance of the suit panel from the top of the screen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Twin Writing|UI", meta = (ClampMin = "0.0", ClampMax = "600.0"))
	float PanelTopMargin = 90.f;

	UFUNCTION(BlueprintPure, Category = "Twin Writing|UI")
	ELogDisplay GetDisplay() const { return Display; }

	/** The barcode text widget (tests read its counts). */
	UStdBarcodeText* GetBarcode() const { return Barcode_Entry; }

	virtual bool Initialize() override;

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Twin Writing|UI")
	TObjectPtr<UBorder> Border_Backdrop;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "Twin Writing|UI")
	TObjectPtr<UStdBarcodeText> Barcode_Entry;

private:
	void ApplyLayout();

	ELogDisplay Display = ELogDisplay::Surface;
};
