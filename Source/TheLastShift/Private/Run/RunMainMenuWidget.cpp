// SPDX-License-Identifier: MIT

#include "Run/RunMainMenuWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Run/RunActListWidget.h"
#include "Run/RunSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RunMainMenuWidget)

#define LOCTEXT_NAMESPACE "TheLastShift"

void URunMainMenuWidget::NativeOnInitialized()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildTree();
	}
	if (Button_Continue)
	{
		Button_Continue->OnClicked.AddUniqueDynamic(this, &URunMainMenuWidget::HandleContinueClicked);
	}
	if (Button_SelectAct)
	{
		Button_SelectAct->OnClicked.AddUniqueDynamic(this, &URunMainMenuWidget::HandleSelectActClicked);
	}
	if (Button_NewGame)
	{
		Button_NewGame->OnClicked.AddUniqueDynamic(this, &URunMainMenuWidget::HandleNewGameClicked);
	}
	RefreshEntries();
	// StdCore binds Settings, Credits and Quit, sets the title and the version, and the default focus.
	Super::NativeOnInitialized();
}

UButton* URunMainMenuWidget::AddButton(UVerticalBox* Box, const TCHAR* Name, const FText& Label, bool bLast)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *(FString(Name).Replace(TEXT("Button_"), TEXT("Text_"))));
	Text->SetText(Label);
	Text->SetJustification(ETextJustify::Center);
	Button->SetContent(Text);
	if (UVerticalBoxSlot* ButtonSlot = Box->AddChildToVerticalBox(Button))
	{
		ButtonSlot->SetPadding(FMargin(0.f, 0.f, 0.f, bLast ? 0.f : 12.f));
	}
	return Button;
}

void URunMainMenuWidget::BuildTree()
{
	// StdCore's WBP_MainMenu layout: backdrop, a 420 px column 160 px from the left, the title, the entries; the version
	// in the bottom right corner.
	UBorder* Background = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Border_Background"));
	Background->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.5f));
	WidgetTree->RootWidget = Background;

	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Overlay_Root"));
	Background->SetContent(Root);

	USizeBox* Column = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SizeBox_Column"));
	Column->SetWidthOverride(420.f);
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

	Button_Continue = AddButton(Main, TEXT("Button_Continue"), LOCTEXT("MenuContinue", "Continue"));
	Button_SelectAct = AddButton(Main, TEXT("Button_SelectAct"), LOCTEXT("MenuSelectAct", "Select act"));
	Button_NewGame = AddButton(Main, TEXT("Button_NewGame"), LOCTEXT("MenuNewGame", "New game"));
	Button_Settings = AddButton(Main, TEXT("Button_Settings"), LOCTEXT("MenuSettings", "Settings"));
	Button_Credits = AddButton(Main, TEXT("Button_Credits"), LOCTEXT("MenuCredits", "Credits"));
	Button_Quit = AddButton(Main, TEXT("Button_Quit"), LOCTEXT("MenuQuit", "Quit"), true);

	Text_Version = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Text_Version"));
	if (UOverlaySlot* VersionSlot = Root->AddChildToOverlay(Text_Version))
	{
		VersionSlot->SetPadding(FMargin(0.f, 0.f, 24.f, 24.f));
		VersionSlot->SetHorizontalAlignment(HAlign_Right);
		VersionSlot->SetVerticalAlignment(VAlign_Bottom);
	}
}

void URunMainMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (URunSubsystem* Run = URunSubsystem::Get(this))
	{
		Run->OnSnapshotsChanged.AddUniqueDynamic(this, &URunMainMenuWidget::RefreshEntries);
	}
	RefreshEntries();
}

void URunMainMenuWidget::NativeDestruct()
{
	if (URunSubsystem* Run = URunSubsystem::Get(this))
	{
		Run->OnSnapshotsChanged.RemoveDynamic(this, &URunMainMenuWidget::RefreshEntries);
	}
	Super::NativeDestruct();
}

void URunMainMenuWidget::RefreshEntries()
{
	const URunSubsystem* Run = URunSubsystem::Get(this);
	const bool bSaved = Run && Run->HasSavedRun();
	const ESlateVisibility SavedVisibility = bSaved ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
	const bool bFocusLost = (Button_Continue && Button_Continue->HasKeyboardFocus() && !bSaved)
		|| (Button_SelectAct && Button_SelectAct->HasKeyboardFocus() && !bSaved);
	if (Button_Continue)
	{
		Button_Continue->SetVisibility(SavedVisibility);
	}
	if (Button_SelectAct)
	{
		Button_SelectAct->SetVisibility(SavedVisibility);
	}
	if (UWidget* Focus = GetDefaultFocusWidget())
	{
		SetDesiredFocusWidget(Focus);
		if (bFocusLost)
		{
			Focus->SetFocus();
		}
	}
}

UWidget* URunMainMenuWidget::GetDefaultFocusWidget() const
{
	if (Button_Continue && Button_Continue->GetVisibility() != ESlateVisibility::Collapsed)
	{
		return Button_Continue;
	}
	return Button_NewGame;
}

UButton* URunMainMenuWidget::FindMenuButton(FName Name) const
{
	return WidgetTree ? Cast<UButton>(WidgetTree->FindWidget(Name)) : nullptr;
}

void URunMainMenuWidget::HandleContinueClicked()
{
	if (URunSubsystem* Run = URunSubsystem::Get(this))
	{
		Run->ContinueRun();
	}
}

void URunMainMenuWidget::HandleSelectActClicked()
{
	if (URunActListWidget* List = Cast<URunActListWidget>(OpenChildScreen(URunActListWidget::StaticClass())))
	{
		List->Configure(ERunActListMode::SelectAct);
	}
}

void URunMainMenuWidget::HandleNewGameClicked()
{
	if (URunSubsystem* Run = URunSubsystem::Get(this))
	{
		Run->StartNewRun();
	}
}

#undef LOCTEXT_NAMESPACE
