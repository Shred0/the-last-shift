// SPDX-License-Identifier: MIT

#include "Run/TextChannelWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Styling/CoreStyle.h"
#include "TextChannel/TextChannelComponent.h"

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

		Text_Message = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Text_Message"));
		Border_Text->SetContent(Text_Message);
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
		Text_Message->SetColorAndOpacity(FSlateColor(TextColor));
		Text_Message->SetAutoWrapText(false);
		Text_Message->SetWrapTextAt(Width);
		Text_Message->SetJustification(ETextJustify::Left);
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
	}
	else
	{
		ShownText.Reset();
		bChannelVisible = false;
	}
	Refresh();
}

void UTextChannelWidget::Bind(UTextChannelComponent* InChannel)
{
	if (UTextChannelComponent* Old = Channel.Get())
	{
		Old->OnTextUpdated.RemoveDynamic(this, &UTextChannelWidget::HandleTextUpdated);
		Old->OnVisibilityChanged.RemoveDynamic(this, &UTextChannelWidget::HandleVisibilityChanged);
	}
	Channel = InChannel;
	if (InChannel)
	{
		InChannel->OnTextUpdated.AddUniqueDynamic(this, &UTextChannelWidget::HandleTextUpdated);
		InChannel->OnVisibilityChanged.AddUniqueDynamic(this, &UTextChannelWidget::HandleVisibilityChanged);
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

void UTextChannelWidget::Refresh()
{
	if (Text_Message)
	{
		Text_Message->SetText(FText::FromString(ShownText));
	}
	if (Border_Text)
	{
		// The empty box is not drawn in the instant between a message's start and its first character.
		Border_Text->SetVisibility(IsShown() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
}

bool UTextChannelWidget::IsShown() const
{
	return bChannelVisible && !ShownText.IsEmpty();
}

FString UTextChannelWidget::GetShownText() const
{
	return IsShown() ? ShownText : FString();
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
