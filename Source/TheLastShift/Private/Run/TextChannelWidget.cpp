// SPDX-License-Identifier: MIT

#include "Run/TextChannelWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "Components/TextBlock.h"
#include "Run/TheLastShiftSettings.h"
#include "Styling/CoreStyle.h"
#include "TextChannel/TextChannelComponent.h"
#include "UI/StdBarcodeText.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TextChannelWidget)

bool UTextChannelWidget::Initialize()
{
	const bool bSuperInitialized = Super::Initialize();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Canvas"));
		WidgetTree->RootWidget = Canvas;

		Border_Text = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Border_Text"));
		Border_Text->SetPadding(FMargin(14.f, 8.f));
		Canvas->AddChildToCanvas(Border_Text);

		UOverlay* Content = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Overlay_Message"));
		Border_Text->SetContent(Content);

		Text_Message = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Text_Message"));
		Content->AddChildToOverlay(Text_Message);

		// The twin AI's messages: barcode characters with the translation over them, in cells of the home text's size.
		Barcode_Message = WidgetTree->ConstructWidget<UStdBarcodeText>(UStdBarcodeText::StaticClass(), TEXT("Barcode_Message"));
		Barcode_Message->SetSizeToShown(true);
		Barcode_Message->SetVisibility(ESlateVisibility::Collapsed);
		Content->AddChildToOverlay(Barcode_Message);
	}
	return bSuperInitialized;
}

void UTextChannelWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::HitTestInvisible);
	ApplyStyle();
	Refresh();
}

void UTextChannelWidget::NativeDestruct()
{
	Bind(nullptr);
	Super::NativeDestruct();
}

void UTextChannelWidget::ApplyStyle()
{
	const float Width = FMath::Clamp(MaxWidth, 100.f, 1200.f);
	if (Text_Message)
	{
		// The engine's own font: no imported font.
		Text_Message->SetFont(FCoreStyle::GetDefaultFontStyle("Regular", FMath::Clamp(FontSize, 8, 48)));
		Text_Message->SetColorAndOpacity(FSlateColor(UTheLastShiftSettings::GetTextColors().HomeAI));
		Text_Message->SetAutoWrapText(false);
		Text_Message->SetWrapTextAt(Width);
		Text_Message->SetJustification(ETextJustify::Left);
	}
	if (Barcode_Message)
	{
		// One cell per character at the home text's size: Roboto's average advance is about 0.72 of the font size.
		const int32 Size = FMath::Clamp(FontSize, 8, 48);
		const FVector2D Cell(FMath::RoundToDouble(Size * 0.72), FMath::RoundToDouble(Size * 1.6));
		Barcode_Message->SetCellSize(Cell);
		Barcode_Message->SetFontScale(static_cast<float>(Size / Cell.Y));
		Barcode_Message->SetLineSpacing(0.f);
		Barcode_Message->SetColumns(FMath::Max(1, FMath::FloorToInt(Width / Cell.X)));
	}
	if (Border_Text)
	{
		Border_Text->SetBrush(FSlateRoundedBoxBrush(BackdropColor, 4.f));
	}
	if (UCanvasPanelSlot* CanvasSlot = Border_Text ? Cast<UCanvasPanelSlot>(Border_Text->Slot) : nullptr)
	{
		CanvasSlot->SetAnchors(FAnchors(0.f, 1.f));
		CanvasSlot->SetAlignment(FVector2D(0.f, 1.f));
		CanvasSlot->SetAutoSize(true);
		CanvasSlot->SetPosition(FVector2D(FMath::Clamp(LeftMargin, 0.f, 600.f), -FMath::Clamp(BottomMargin, 0.f, 600.f)));
	}
}

void UTextChannelWidget::SetChannel(UTextChannelComponent* InChannel)
{
	Bind(InChannel);
	if (UTextChannelComponent* Bound = Channel.Get())
	{
		ShownText = Bound->GetVisibleText();
		bChannelVisible = Bound->IsVisible();
		bTwin = Bound->GetCurrentVoice() == ETextVoice::Twin;
		TwinText = bTwin ? Bound->GetCurrentText() : FString();
		TwinShown = bTwin ? Bound->GetShownCells() : 0;
		TwinTranslated = bTwin ? Bound->GetTranslatedCells() : 0;
	}
	else
	{
		ShownText.Reset();
		bChannelVisible = false;
		bTwin = false;
		TwinText.Reset();
		TwinShown = TwinTranslated = 0;
	}
	Refresh();
}

void UTextChannelWidget::Bind(UTextChannelComponent* InChannel)
{
	if (UTextChannelComponent* Old = Channel.Get())
	{
		Old->OnTextUpdated.RemoveDynamic(this, &UTextChannelWidget::HandleTextUpdated);
		Old->OnVisibilityChanged.RemoveDynamic(this, &UTextChannelWidget::HandleVisibilityChanged);
		Old->OnCellsUpdated.RemoveDynamic(this, &UTextChannelWidget::HandleCellsUpdated);
		Old->OnMessageStarted.RemoveDynamic(this, &UTextChannelWidget::HandleMessageStarted);
	}
	Channel = InChannel;
	if (InChannel)
	{
		InChannel->OnTextUpdated.AddUniqueDynamic(this, &UTextChannelWidget::HandleTextUpdated);
		InChannel->OnVisibilityChanged.AddUniqueDynamic(this, &UTextChannelWidget::HandleVisibilityChanged);
		InChannel->OnCellsUpdated.AddUniqueDynamic(this, &UTextChannelWidget::HandleCellsUpdated);
		InChannel->OnMessageStarted.AddUniqueDynamic(this, &UTextChannelWidget::HandleMessageStarted);
	}
}

void UTextChannelWidget::HandleTextUpdated(const FString& VisibleText)
{
	ShownText = VisibleText;
	Refresh();
}

void UTextChannelWidget::HandleVisibilityChanged(bool bVisible)
{
	bChannelVisible = bVisible;
	Refresh();
}

void UTextChannelWidget::HandleMessageStarted(FName RowName, ETextDegradation Level)
{
	const UTextChannelComponent* Bound = Channel.Get();
	bTwin = Bound && Bound->GetCurrentVoice() == ETextVoice::Twin;
	TwinText = bTwin ? Bound->GetCurrentText() : FString();
	TwinShown = bTwin ? Bound->GetShownCells() : 0;
	TwinTranslated = bTwin ? Bound->GetTranslatedCells() : 0;
	Refresh();
}

void UTextChannelWidget::HandleCellsUpdated(int32 ShownCells, int32 TranslatedCells)
{
	TwinShown = ShownCells;
	TwinTranslated = TranslatedCells;
	Refresh();
}

bool UTextChannelWidget::IsShowingTwin() const
{
	return bTwin && IsShown();
}

void UTextChannelWidget::Refresh()
{
	const FSuitTextColors& Colors = UTheLastShiftSettings::GetTextColors();
	if (Text_Message)
	{
		Text_Message->SetText(FText::FromString(bTwin ? FString() : ShownText));
		Text_Message->SetColorAndOpacity(FSlateColor(Colors.HomeAI));
		Text_Message->SetVisibility(bTwin ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
	if (Barcode_Message)
	{
		if (bTwin)
		{
			Barcode_Message->SetText(FText::FromString(TwinText));
			Barcode_Message->SetBarColor(Colors.TwinAI);
			Barcode_Message->SetTextColor(Colors.TwinAI);
			Barcode_Message->SetCounts(TwinShown, TwinTranslated);
		}
		Barcode_Message->SetVisibility(bTwin ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	if (Border_Text)
	{
		// The empty box is not drawn in the instant between a message's start and its first character.
		Border_Text->SetVisibility(IsShown() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
}

bool UTextChannelWidget::IsShown() const
{
	return bChannelVisible && (bTwin ? TwinShown > 0 : !ShownText.IsEmpty());
}

FString UTextChannelWidget::GetShownText() const
{
	// A twin message: the cells shown so far (barcodes and translated characters alike).
	return IsShown() ? (bTwin ? TwinText.Left(TwinShown) : ShownText) : FString();
}

FBox2D UTextChannelWidget::GetShownRect() const
{
	if (!Border_Text || !IsShown())
	{
		return FBox2D(ForceInit);
	}
	const FGeometry& Geometry = Border_Text->GetCachedGeometry();
	const FVector2D TopLeft = Geometry.GetAbsolutePosition();
	return FBox2D(TopLeft, TopLeft + Geometry.GetAbsoluteSize());
}
