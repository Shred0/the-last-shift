// SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TextChannel/TextDegradation.h"
#include "TextChannelWidget.generated.h"

class UBorder;
class UOverlay;
class UStdBarcodeText;
class UTextBlock;
class UTextChannelComponent;

/**
 * Shows the suit's text channel in the lower left corner of the screen, away from the Interact prompt and the
 * scan-ready ring (both bottom centre). A home AI message is the text the channel's OnTextUpdated gives it; a twin AI
 * message is drawn with StdCore's barcode text, the cells the channel's OnCellsUpdated gives it. Hides on
 * OnVisibilityChanged: it holds no copy of any value and decides nothing. Engine font; each voice in its colour of
 * UTheLastShiftSettings::TextColors (never a scan colour). The tree is built in C++, so the class is used as is; created
 * by the pawn, which owns the HUD.
 */
UCLASS()
class THELASTSHIFT_API UTextChannelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** The channel to show. Null = nothing shown. */
	UFUNCTION(BlueprintCallable, Category = "TextChannel|UI")
	void SetChannel(UTextChannelComponent* InChannel);

	/** Distance of the box from the left edge of the screen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextChannel|UI", meta = (ClampMin = "0.0", ClampMax = "600.0"))
	float LeftMargin = 48.f;

	/** Distance of the box's bottom edge from the bottom of the screen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextChannel|UI", meta = (ClampMin = "0.0", ClampMax = "600.0"))
	float BottomMargin = 48.f;

	/** The text wraps beyond this width (screen units at 1080p), so the box never reaches the bottom-centre prompt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextChannel|UI", meta = (ClampMin = "100.0", ClampMax = "1200.0"))
	float MaxWidth = 460.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextChannel|UI", meta = (ClampMin = "8", ClampMax = "48"))
	int32 FontSize = 18;


	/** Panel behind the text, so it stays readable over a lit surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TextChannel|UI")
	FLinearColor BackdropColor = FLinearColor(0.f, 0.f, 0.f, 0.6f);

	/** The text shown now (empty when hidden). */
	UFUNCTION(BlueprintPure, Category = "TextChannel|UI")
	FString GetShownText() const;

	/** True while the box is on screen. */
	UFUNCTION(BlueprintPure, Category = "TextChannel|UI")
	bool IsShown() const;

	/** The box's rectangle in absolute desktop pixels, after the last layout (zero size when hidden). */
	FBox2D GetShownRect() const;

	/** True while a twin AI message is on screen (drawn as barcode text). */
	UFUNCTION(BlueprintPure, Category = "TextChannel|UI")
	bool IsShowingTwin() const;

	/** The barcode text that draws twin AI messages (tests read its counts and colours). */
	UStdBarcodeText* GetTwinBarcode() const { return Barcode_Message; }

	/** The text block that draws home AI messages. */
	UTextBlock* GetHomeTextBlock() const { return Text_Message; }

	virtual bool Initialize() override;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "TextChannel|UI")
	TObjectPtr<UBorder> Border_Text;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "TextChannel|UI")
	TObjectPtr<UTextBlock> Text_Message;

	UPROPERTY(Transient, BlueprintReadOnly, Category = "TextChannel|UI")
	TObjectPtr<UStdBarcodeText> Barcode_Message;

private:
	UFUNCTION()
	void HandleTextUpdated(const FString& VisibleText);

	UFUNCTION()
	void HandleVisibilityChanged(bool bVisible);

	UFUNCTION()
	void HandleCellsUpdated(int32 ShownCells, int32 TranslatedCells);

	UFUNCTION()
	void HandleMessageStarted(FName RowName, ETextDegradation Level);

	void ApplyStyle();
	void Bind(UTextChannelComponent* InChannel);
	void Refresh();

	TWeakObjectPtr<UTextChannelComponent> Channel;
	FString ShownText;
	bool bChannelVisible = false;
	/** The current message is the twin voice, its full text and its cells as the channel gave them. */
	bool bTwin = false;
	FString TwinText;
	int32 TwinShown = 0;
	int32 TwinTranslated = 0;
};
