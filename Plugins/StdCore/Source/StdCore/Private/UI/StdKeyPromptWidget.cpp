// Copyright 2026 <OWNER>. All rights reserved. See Plugins/StdCore/LICENSE.

#include "UI/StdKeyPromptWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Engine/LocalPlayer.h"
#include "Input/StdInputSubsystem.h"
#include "InputAction.h"
#include "Styling/CoreStyle.h"
#include "UI/StdUITheme.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(StdKeyPromptWidget)

#define LOCTEXT_NAMESPACE "StdCore"

namespace StdKeyPrompt
{
	/** Frames a remap keeps re-reading the key: Enhanced Input rebuilds the mappings on the tick after the change. */
	static constexpr int32 RefreshFramesAfterRemap = 3;
}

bool UStdKeyPromptWidget::Initialize()
{
	const bool bSuperInitialized = Super::Initialize();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Box_Row"));
		WidgetTree->RootWidget = Row;

		Border_Key = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Border_Key"));
		Text_Key = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Text_Key"));
		Border_Key->SetContent(Text_Key);
		Border_Key->SetPadding(FMargin(8.f, 2.f));
		Border_Key->SetHorizontalAlignment(HAlign_Center);
		Border_Key->SetVerticalAlignment(VAlign_Center);
		if (UHorizontalBoxSlot* KeySlot = Row->AddChildToHorizontalBox(Border_Key))
		{
			KeySlot->SetVerticalAlignment(VAlign_Center);
		}

		Text_Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Text_Label"));
		if (UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(Text_Label))
		{
			LabelSlot->SetVerticalAlignment(VAlign_Center);
		}
	}
	return bSuperInitialized;
}

void UStdKeyPromptWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::HitTestInvisible);
	ApplyStyle();
	SetLabel(Label);
	BindInputSubsystem();
	RefreshKey();
}

void UStdKeyPromptWidget::NativeDestruct()
{
	UnbindInputSubsystem();
	Super::NativeDestruct();
}

void UStdKeyPromptWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (!BoundSubsystem.IsValid())
	{
		// The owning player's subsystems may not exist yet when the prompt is created.
		BindInputSubsystem();
	}
	if (PendingRefreshFrames > 0)
	{
		--PendingRefreshFrames;
		RefreshKey();
	}
}

UStdInputSubsystem* UStdKeyPromptWidget::FindInputSubsystem() const
{
	const ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	return LocalPlayer ? LocalPlayer->GetSubsystem<UStdInputSubsystem>() : nullptr;
}

void UStdKeyPromptWidget::BindInputSubsystem()
{
	UStdInputSubsystem* Subsystem = FindInputSubsystem();
	if (!Subsystem || Subsystem == BoundSubsystem.Get())
	{
		return;
	}
	UnbindInputSubsystem();
	Subsystem->OnKeyMappingsChanged.AddUniqueDynamic(this, &UStdKeyPromptWidget::HandleKeyMappingsChanged);
	BoundSubsystem = Subsystem;
	PendingRefreshFrames = StdKeyPrompt::RefreshFramesAfterRemap;
}

void UStdKeyPromptWidget::UnbindInputSubsystem()
{
	if (UStdInputSubsystem* Subsystem = BoundSubsystem.Get())
	{
		Subsystem->OnKeyMappingsChanged.RemoveDynamic(this, &UStdKeyPromptWidget::HandleKeyMappingsChanged);
	}
	BoundSubsystem.Reset();
}

void UStdKeyPromptWidget::HandleKeyMappingsChanged()
{
	RefreshKey();
	PendingRefreshFrames = StdKeyPrompt::RefreshFramesAfterRemap;
}

void UStdKeyPromptWidget::SetInputAction(const UInputAction* InAction)
{
	InputAction = InAction;
	RefreshKey();
	PendingRefreshFrames = StdKeyPrompt::RefreshFramesAfterRemap;
}

void UStdKeyPromptWidget::SetLabel(const FText& InLabel)
{
	Label = InLabel;
	if (Text_Label)
	{
		Text_Label->SetText(Label);
		Text_Label->SetVisibility(Label.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}
}

void UStdKeyPromptWidget::SetPreferGamepad(bool bInPreferGamepad)
{
	if (bPreferGamepad != bInPreferGamepad)
	{
		bPreferGamepad = bInPreferGamepad;
		RefreshKey();
	}
}

void UStdKeyPromptWidget::RefreshKey()
{
	const UStdInputSubsystem* Subsystem = FindInputSubsystem();
	DisplayedKey = Subsystem && InputAction ? Subsystem->GetKeyForAction(InputAction, bPreferGamepad) : EKeys::Invalid;
	if (Text_Key)
	{
		Text_Key->SetText(GetKeyText());
	}
}

FText UStdKeyPromptWidget::GetKeyText() const
{
	return DisplayedKey.IsValid() ? DisplayedKey.GetDisplayName(/*bLongDisplayName*/ false) : LOCTEXT("KeyPromptUnbound", "Unbound");
}

void UStdKeyPromptWidget::ApplyStyle()
{
	FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 18);
	if (const UStdUITheme* Theme = StdUITheme::Get())
	{
		Font = Theme->BodyFont;
	}
	Font.Size = FMath::Clamp(FontSize, 8, 64);

	if (Text_Key)
	{
		Text_Key->SetFont(Font);
		Text_Key->SetColorAndOpacity(FSlateColor(Color));
	}
	if (Text_Label)
	{
		Text_Label->SetFont(Font);
		Text_Label->SetColorAndOpacity(FSlateColor(Color));
		if (UHorizontalBoxSlot* LabelSlot = Cast<UHorizontalBoxSlot>(Text_Label->Slot))
		{
			LabelSlot->SetPadding(FMargin(FMath::Clamp(LabelGap, 0.f, 64.f), 0.f, 0.f, 0.f));
		}
	}
	if (Border_Key)
	{
		const float Outline = FMath::Clamp(CapOutline, 0.f, 4.f);
		Border_Key->SetBrush(FSlateRoundedBoxBrush(FLinearColor::Transparent, 4.f, Outline > 0.f ? Color : FLinearColor::Transparent, Outline));
	}
}

#undef LOCTEXT_NAMESPACE
