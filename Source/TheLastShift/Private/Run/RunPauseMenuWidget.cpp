// SPDX-License-Identifier: MIT

#include "Run/RunPauseMenuWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Run/ArchiveScreenWidget.h"
#include "Run/RunSubsystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(RunPauseMenuWidget)

#define LOCTEXT_NAMESPACE "TheLastShift"

namespace RunPauseMenuLayout
{
	// StdCore's WBP_PauseMenu, as its widget tree holds them (StdCore exposes no title or metrics in code): a change to
	// StdCore's panel is repeated here (AGENTS.md, StdCore plugin).
	static const FLinearColor BackgroundColor(0.f, 0.f, 0.f, 0.5f);
	static const FLinearColor PanelColor(0.01f, 0.012f, 0.016f, 0.92f);
	static constexpr float PanelPadding = 32.f;
	static constexpr float ColumnWidth = 376.f;
	static constexpr float TitleGap = 24.f;
	static constexpr float EntryGap = 12.f;
	static const FLinearColor EntryTextColor(0.02f, 0.02f, 0.02f, 1.f);
}

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
	Text->SetColorAndOpacity(FSlateColor(RunPauseMenuLayout::EntryTextColor));
	Button->SetContent(Text);
	if (UVerticalBoxSlot* ButtonSlot = Box->AddChildToVerticalBox(Button))
	{
		ButtonSlot->SetPadding(FMargin(0.f, 0.f, 0.f, bLast ? 0.f : RunPauseMenuLayout::EntryGap));
	}
	return Button;
}

void URunPauseMenuWidget::BuildTree()
{
	using namespace RunPauseMenuLayout;
	// StdCore's WBP_PauseMenu layout: a dim backdrop, a centred panel, the title, then the entries.
	UBorder* Background = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Border_Background"));
	Background->SetBrushColor(BackgroundColor);
	Background->SetHorizontalAlignment(HAlign_Center);
	Background->SetVerticalAlignment(VAlign_Center);
	WidgetTree->RootWidget = Background;

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Border_Panel"));
	Panel->SetBrushColor(PanelColor);
	Panel->SetPadding(FMargin(PanelPadding));
	Background->SetContent(Panel);

	USizeBox* Column = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SizeBox_Main"));
	Column->SetWidthOverride(ColumnWidth);
	Panel->SetContent(Column);

	UVerticalBox* Main = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Box_Main"));
	Column->SetContent(Main);

	Text_Title = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Text_Title"));
	Text_Title->SetText(TitleLabel);
	Text_Title->SetJustification(ETextJustify::Center);
	if (UVerticalBoxSlot* TitleSlot = Main->AddChildToVerticalBox(Text_Title))
	{
		TitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, TitleGap));
	}

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
