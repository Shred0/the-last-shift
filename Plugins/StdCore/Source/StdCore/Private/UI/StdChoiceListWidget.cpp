// Copyright 2026 <OWNER>. All rights reserved. See Plugins/StdCore/LICENSE.

#include "UI/StdChoiceListWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "UI/StdUITheme.h"

// --- UStdChoiceButton ---

void UStdChoiceButton::InitChoice(UStdChoiceListWidget* InOwner, int32 Index)
{
	Owner = InOwner;
	ChoiceIndex = Index;
	OnClicked.AddUniqueDynamic(this, &UStdChoiceButton::HandleClicked);
}

void UStdChoiceButton::HandleClicked()
{
	if (UStdChoiceListWidget* List = Owner.Get())
	{
		List->ChooseEntry(ChoiceIndex);
	}
}

// --- UStdChoiceListWidget ---

void UStdChoiceListWidget::NativeOnInitialized()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultTree();
	}
	// Focused itself while it has no entry yet (OpenChildScreen focuses before the owner fills it): the back keys still work.
	SetIsFocusable(true);
	SetTitle(Title);
	SetBody(Body);
	RebuildButtons();
	Super::NativeOnInitialized();
}

void UStdChoiceListWidget::BuildDefaultTree()
{
	// The same layout as the Std Core main menu: a backdrop, a column on the left, a title above the entries.
	UBorder* Background = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Border_Background"));
	Background->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.5f));
	WidgetTree->RootWidget = Background;

	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Overlay_Root"));
	Background->SetContent(Root);

	USizeBox* Column = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SizeBox_Column"));
	Column->SetWidthOverride(FMath::Clamp(ColumnWidth, 100.f, 2000.f));
	if (UOverlaySlot* ColumnSlot = Root->AddChildToOverlay(Column))
	{
		ColumnSlot->SetPadding(FMargin(160.f, 0.f, 0.f, 0.f));
		ColumnSlot->SetHorizontalAlignment(HAlign_Left);
		ColumnSlot->SetVerticalAlignment(VAlign_Center);
	}

	UVerticalBox* Main = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Box_Main"));
	Column->SetContent(Main);

	Text_Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Text_Title"));
	if (UVerticalBoxSlot* TitleSlot = Main->AddChildToVerticalBox(Text_Title))
	{
		TitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 48.f));
	}

	Text_Body = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Text_Body"));
	Text_Body->SetAutoWrapText(true);
	if (UVerticalBoxSlot* BodySlot = Main->AddChildToVerticalBox(Text_Body))
	{
		BodySlot->SetPadding(FMargin(0.f, 0.f, 0.f, 40.f));
	}

	Box_Entries = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Box_Entries"));
	Main->AddChildToVerticalBox(Box_Entries);
}

void UStdChoiceListWidget::SetTitle(const FText& InTitle)
{
	Title = InTitle;
	if (Text_Title)
	{
		Text_Title->SetText(Title);
		Text_Title->SetVisibility(Title.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}
}

void UStdChoiceListWidget::SetBody(const FText& InBody)
{
	Body = InBody;
	if (Text_Body)
	{
		Text_Body->SetText(Body);
		Text_Body->SetVisibility(Body.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::SelfHitTestInvisible);
	}
}

void UStdChoiceListWidget::SetEntries(const TArray<FStdChoiceEntry>& InEntries)
{
	Entries = InEntries;
	RebuildButtons();
}

void UStdChoiceListWidget::SetBackLabel(const FText& InLabel)
{
	BackLabel = InLabel;
	RebuildButtons();
}

void UStdChoiceListWidget::SetAllowBack(bool bInAllowBack)
{
	bAllowBack = bInAllowBack;
	RebuildButtons();
}

UStdChoiceButton* UStdChoiceListWidget::MakeButton(int32 Index, const FText& Label, bool bEnabled)
{
	const FString Suffix = Index == INDEX_NONE ? FString(TEXT("Back")) : FString::Printf(TEXT("Choice_%d"), Index);
	UStdChoiceButton* Button = WidgetTree->ConstructWidget<UStdChoiceButton>(UStdChoiceButton::StaticClass(), *(TEXT("Button_") + Suffix));
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *(TEXT("Text_") + Suffix));
	Text->SetText(Label);
	Text->SetJustification(ETextJustify::Center);
	Button->SetContent(Text);
	Button->SetIsEnabled(bEnabled);
	Button->InitChoice(this, Index);
	if (UVerticalBoxSlot* ButtonSlot = Box_Entries->AddChildToVerticalBox(Button))
	{
		ButtonSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));
	}
	return Button;
}

void UStdChoiceListWidget::RebuildButtons()
{
	if (!Box_Entries || !WidgetTree)
	{
		return;
	}
	ResetFocusHighlight();
	for (UStdChoiceButton* Button : EntryButtons)
	{
		if (Button)
		{
			Button->RemoveFromParent();
		}
	}
	if (BackButton)
	{
		BackButton->RemoveFromParent();
	}
	EntryButtons.Reset();
	BackButton = nullptr;
	Box_Entries->ClearChildren();

	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		EntryButtons.Add(MakeButton(Index, Entries[Index].Label, Entries[Index].bEnabled));
	}
	if (bAllowBack && !BackLabel.IsEmpty())
	{
		BackButton = MakeButton(INDEX_NONE, BackLabel, true);
	}

	// The new buttons take the theme like the rest of the screen did in Pre Construct.
	if (bApplyTheme)
	{
		if (const UStdUITheme* Theme = StdUITheme::Get())
		{
			StdUITheme::ApplyToWidget(*Theme, *this);
		}
	}
	NativeOnEntriesRebuilt();

	if (UWidget* FocusWidget = GetDefaultFocusWidget())
	{
		SetDesiredFocusWidget(FocusWidget);
		if (IsInViewport() && GetVisibility() != ESlateVisibility::Collapsed)
		{
			FocusWidget->SetFocus();
		}
	}
}

UButton* UStdChoiceListWidget::GetEntryButton(int32 Index) const
{
	if (Index == INDEX_NONE)
	{
		return BackButton;
	}
	return EntryButtons.IsValidIndex(Index) ? EntryButtons[Index].Get() : nullptr;
}

UWidget* UStdChoiceListWidget::GetDefaultFocusWidget() const
{
	for (UStdChoiceButton* Button : EntryButtons)
	{
		if (Button && Button->GetIsEnabled())
		{
			return Button;
		}
	}
	return BackButton;
}

void UStdChoiceListWidget::ChooseEntry(int32 Index)
{
	if (Index == INDEX_NONE)
	{
		HandleBackAction();
		return;
	}
	if (!Entries.IsValidIndex(Index) || !Entries[Index].bEnabled)
	{
		return;
	}
	OnChoiceSelected.Broadcast(this, Index);
}

bool UStdChoiceListWidget::NativeHandleBackAction()
{
	if (!bAllowBack)
	{
		// Consumed: nothing to go back to.
		return true;
	}
	OnBack.Broadcast(this);
	return Super::NativeHandleBackAction();
}
