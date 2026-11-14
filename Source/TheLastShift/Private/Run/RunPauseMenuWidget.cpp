// SPDX-License-Identifier: MIT

#include "Run/RunPauseMenuWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Run/ArchiveScreenWidget.h"
#include "Run/RunSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RunPauseMenuWidget)

#define LOCTEXT_NAMESPACE "TheLastShift"

void URunPauseMenuWidget::NativeOnInitialized()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildTree();
	}
	if (Button_Archive)
	{
		Button_Archive->OnClicked.AddUniqueDynamic(this, &URunPauseMenuWidget::HandleArchiveClicked);
	}
	RefreshEntries();
	// StdCore binds Resume, Settings, Main menu and Quit and sets the default focus (Resume).
	Super::NativeOnInitialized();
}

UButton* URunPauseMenuWidget::AddButton(UVerticalBox* Box, const TCHAR* Name, const FText& Label, bool bLast)
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

void URunPauseMenuWidget::BuildTree()
{
	// StdCore's WBP_PauseMenu layout: a dim backdrop and a centred panel holding the entries.
	UBorder* Background = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Border_Background"));
	Background->SetBrushColor(FLinearColor(0.f, 0.f, 0.f, 0.5f));
	WidgetTree->RootWidget = Background;

	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Overlay_Root"));
	Background->SetContent(Root);

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Border_Panel"));
	Panel->SetPadding(FMargin(40.f));
	if (UOverlaySlot* PanelSlot = Root->AddChildToOverlay(Panel))
	{
		PanelSlot->SetHorizontalAlignment(HAlign_Center);
		PanelSlot->SetVerticalAlignment(VAlign_Center);
	}

	USizeBox* Column = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SizeBox_Column"));
	Column->SetWidthOverride(320.f);
	Panel->SetContent(Column);

	UVerticalBox* Main = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Box_Main"));
	Column->SetContent(Main);

	Button_Resume = AddButton(Main, TEXT("Button_Resume"), LOCTEXT("PauseResume", "Resume"));
	Button_Archive = AddButton(Main, TEXT("Button_Archive"), ArchiveLabel);
	Button_Settings = AddButton(Main, TEXT("Button_Settings"), LOCTEXT("PauseSettings", "Settings"));
	Button_MainMenu = AddButton(Main, TEXT("Button_MainMenu"), LOCTEXT("PauseMainMenu", "Main menu"));
	Button_Quit = AddButton(Main, TEXT("Button_Quit"), LOCTEXT("PauseQuit", "Quit"), true);
}

void URunPauseMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (URunSubsystem* Run = URunSubsystem::Get(this))
	{
		Run->OnArchiveChanged.AddUniqueDynamic(this, &URunPauseMenuWidget::RefreshEntries);
	}
	RefreshEntries();
}

void URunPauseMenuWidget::NativeDestruct()
{
	if (URunSubsystem* Run = URunSubsystem::Get(this))
	{
		Run->OnArchiveChanged.RemoveDynamic(this, &URunPauseMenuWidget::RefreshEntries);
	}
	Super::NativeDestruct();
}

void URunPauseMenuWidget::RefreshEntries()
{
	const URunSubsystem* Run = URunSubsystem::Get(this);
	if (Button_Archive)
	{
		Button_Archive->SetVisibility(Run && Run->HasArchiveItems() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void URunPauseMenuWidget::HandleArchiveClicked()
{
	OpenChildScreen(UArchiveScreenWidget::StaticClass());
}

#undef LOCTEXT_NAMESPACE
