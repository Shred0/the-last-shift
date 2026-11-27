// SPDX-License-Identifier: MIT

#include "UI/ChoiceDialogWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "Epilogue/ChoiceTerminal.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/StdPlayerController.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/SlateRenderer.h"
#include "Run/RunSceneSubsystem.h"
#include "Run/TheLastShiftSettings.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"
#include "UI/StdKeyPromptWidget.h"
#include "UI/StdTextSceneWidget.h"
#include "UI/StdUITheme.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ChoiceDialogWidget)

namespace ChoiceDialogImpl
{
	static const TCHAR* LexToString(EChoiceHoldSource Source)
	{
		switch (Source)
		{
		case EChoiceHoldSource::Mouse: return TEXT("the mouse");
		case EChoiceHoldSource::Key: return TEXT("the confirm key");
		default: return TEXT("nothing");
		}
	}

	/** A brush that draws nothing. */
	static FSlateBrush NoBrush()
	{
		FSlateBrush None;
		None.DrawAs = ESlateBrushDrawType::NoDrawType;
		return None;
	}

	/** A button that never takes the keyboard focus: read when the Slate button is built (the property has a getter only). */
	static void MakeUnfocusable(UButton* Button)
	{
		if (const FBoolProperty* Focusable = FindFProperty<FBoolProperty>(UButton::StaticClass(), TEXT("IsFocusable")))
		{
			Focusable->SetPropertyValue_InContainer(Button, false);
		}
	}
}

UChoiceDialogWidget::UChoiceDialogWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

FText UChoiceDialogWidget::GetBackLabel()
{
	return NSLOCTEXT("TheLastShift", "ChoiceDialogBack", "Back");
}

UChoiceDialogWidget* UChoiceDialogWidget::Open(APlayerController* Player, AChoiceTerminal* InTerminal)
{
	if (!Player || !InTerminal || !Player->IsLocalController())
	{
		return nullptr;
	}
	UChoiceDialogWidget* Dialog = CreateWidget<UChoiceDialogWidget>(Player, UChoiceDialogWidget::StaticClass());
	if (!Dialog)
	{
		return nullptr;
	}
	Dialog->Configure(InTerminal);
	if (Dialog->Buttons.Num() == 0)
	{
		return nullptr;
	}
	Dialog->ShowAsRoot(FMath::Clamp(Dialog->DialogZOrder, 0, 1000));
	Dialog->TakeInput(Player);
	UE_LOG(LogTheLastShift, Log, TEXT("ChoiceDialog: opened with %d buttons (%s); no button focused, the hold %.2f s."), Dialog->Buttons.Num(),
		*FString::JoinBy(Dialog->Endings, TEXT(", "), [](FName E) { return E.ToString(); }), UTheLastShiftSettings::Get()->GetChoiceHoldSeconds());
	return Dialog;
}

void UChoiceDialogWidget::NativeOnInitialized()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildTree();
	}
	// The screen itself holds the focus while no button does: the back keys work, the confirm key presses nothing.
	SetIsFocusable(true);
	const UWorld* World = GetWorld();
	TimeState = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
	Super::NativeOnInitialized();
}

void UChoiceDialogWidget::BuildTree()
{
	// Over the game's view: no backdrop, the panel alone at the centre.
	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Overlay_Root"));
	WidgetTree->RootWidget = Root;

	Border_Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Border_Panel"));
	Border_Panel->SetPadding(FMargin(FMath::Clamp(PanelPadding, 0.f, 200.f)));
	if (UOverlaySlot* PanelSlot = Root->AddChildToOverlay(Border_Panel))
	{
		PanelSlot->SetHorizontalAlignment(HAlign_Center);
		PanelSlot->SetVerticalAlignment(VAlign_Center);
	}

	USizeBox* Column = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SizeBox_Column"));
	Column->SetWidthOverride(FMath::Clamp(ColumnWidth, 200.f, 1200.f));
	Border_Panel->SetContent(Column);

	UVerticalBox* Main = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Box_Main"));
	Column->SetContent(Main);

	Box_Buttons = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Box_Buttons"));
	Main->AddChildToVerticalBox(Box_Buttons);

	// The back control, as the text scenes show theirs: the back key's prompt and its label, never focused.
	Button_Back = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("Button_Back"));
	ChoiceDialogImpl::MakeUnfocusable(Button_Back);
	KeyPrompt_Back = WidgetTree->ConstructWidget<UStdKeyPromptWidget>(UStdKeyPromptWidget::StaticClass(), TEXT("KeyPrompt_Back"));
	KeyPrompt_Back->SetFixedKey(StdTextScene::SkipControlKey());
	KeyPrompt_Back->SetLabel(GetBackLabel());
	Button_Back->SetContent(KeyPrompt_Back);
	if (UVerticalBoxSlot* BackSlot = Main->AddChildToVerticalBox(Button_Back))
	{
		BackSlot->SetHorizontalAlignment(HAlign_Right);
		BackSlot->SetPadding(FMargin(0.f, 12.f, 0.f, 0.f));
	}
	Button_Back->OnClicked.AddUniqueDynamic(this, &UChoiceDialogWidget::HandleBackClicked);
}

void UChoiceDialogWidget::Configure(AChoiceTerminal* InTerminal)
{
	Terminal = InTerminal;
	Endings = InTerminal ? InTerminal->GetEndings() : TArray<FName>();
	if (!Box_Buttons || !WidgetTree)
	{
		return;
	}
	Box_Buttons->ClearChildren();
	Buttons.Reset();
	Labels.Reset();
	Fills.Reset();
	for (int32 Index = 0; Index < Endings.Num(); ++Index)
	{
		UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), *FString::Printf(TEXT("Button_Choice_%d"), Index));
		UOverlay* Content = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), *FString::Printf(TEXT("Overlay_Choice_%d"), Index));
		if (UButtonSlot* ContentSlot = Cast<UButtonSlot>(Button->AddChild(Content)))
		{
			ContentSlot->SetHorizontalAlignment(HAlign_Fill);
			ContentSlot->SetVerticalAlignment(VAlign_Fill);
		}
		// The fill under the label: empty at rest, from the left edge to the right as the button is held.
		UProgressBar* Fill = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), *FString::Printf(TEXT("Fill_Choice_%d"), Index));
		FProgressBarStyle FillStyle;
		FillStyle.SetBackgroundImage(ChoiceDialogImpl::NoBrush());
		FillStyle.SetMarqueeImage(ChoiceDialogImpl::NoBrush());
		FSlateBrush Solid;
		Solid.DrawAs = ESlateBrushDrawType::Box;
		Solid.TintColor = FSlateColor(FLinearColor::White);
		FillStyle.SetFillImage(Solid);
		Fill->SetWidgetStyle(FillStyle);
		Fill->SetBarFillType(EProgressBarFillType::LeftToRight);
		Fill->SetPercent(0.f);
		Fill->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (UOverlaySlot* FillSlot = Content->AddChildToOverlay(Fill))
		{
			FillSlot->SetHorizontalAlignment(HAlign_Fill);
			FillSlot->SetVerticalAlignment(VAlign_Fill);
		}
		UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), *FString::Printf(TEXT("Label_Choice_%d"), Index));
		Label->SetText(URunSceneSubsystem::GetEndingLabel(Endings[Index]));
		Label->SetVisibility(ESlateVisibility::HitTestInvisible);
		if (UOverlaySlot* LabelSlot = Content->AddChildToOverlay(Label))
		{
			LabelSlot->SetHorizontalAlignment(HAlign_Left);
			LabelSlot->SetVerticalAlignment(VAlign_Center);
		}
		if (UVerticalBoxSlot* ButtonSlot = Box_Buttons->AddChildToVerticalBox(Button))
		{
			ButtonSlot->SetPadding(FMargin(0.f, 0.f, 0.f, Index + 1 < Endings.Num() ? 12.f : 0.f));
		}
		Buttons.Add(Button);
		Labels.Add(Label);
		Fills.Add(Fill);
	}
	PressSpent.Init(false, Buttons.Num());
	HeldIndex = INDEX_NONE;
	HoldSource = EChoiceHoldSource::None;
	if (const UStdUITheme* Theme = StdUITheme::Get())
	{
		StdUITheme::ApplyToWidget(*Theme, *this);
	}
	StyleAfterTheme();
}

void UChoiceDialogWidget::NativePreConstruct()
{
	// The theme first (the screen base), then what it does not know: the labels inside the overlays, the fills, the back control.
	Super::NativePreConstruct();
	StyleAfterTheme();
}

void UChoiceDialogWidget::StyleAfterTheme()
{
	const UStdUITheme* Theme = StdUITheme::Get();
	if (!Theme)
	{
		return;
	}
	const float Available = FMath::Clamp(ColumnWidth, 200.f, 1200.f) - Theme->ButtonPadding.Left - Theme->ButtonPadding.Right - 2.f * Theme->FrameThickness;
	const bool bCanMeasure = FSlateApplication::IsInitialized() && FSlateApplication::Get().GetRenderer();
	for (UTextBlock* Label : Labels)
	{
		if (!Label)
		{
			continue;
		}
		// A button label: the theme's button font, its colour following the button's state, capitals as the theme says.
		FSlateFontInfo Font = Theme->ButtonFont;
		if (bCanMeasure)
		{
			const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
			FString Text = Label->GetText().ToString();
			Text = Theme->bUpperCaseLabels ? Text.ToUpper() : Text;
			if (!Text.IsEmpty() && Available > 0.f && Measure->Measure(Text, Font).X > Available)
			{
				// As the theme fits a menu's label: no letter spacing, then a smaller size.
				Font.LetterSpacing = 0;
				const float Width = static_cast<float>(Measure->Measure(Text, Font).X);
				if (Width > Available)
				{
					Font.Size = FMath::Max(8.f, FMath::FloorToFloat(Font.Size * Available / Width));
				}
			}
		}
		Label->SetFont(Font);
		Label->SetColorAndOpacity(FSlateColor::UseForeground());
		Label->SetTextTransformPolicy(Theme->bUpperCaseLabels ? ETextTransformPolicy::ToUpper : ETextTransformPolicy::None);
	}
	FLinearColor FillColor = Theme->TextColor;
	FillColor.A = FMath::Clamp(FillOpacity, 0.05f, 1.f);
	for (UProgressBar* Fill : Fills)
	{
		if (Fill)
		{
			Fill->SetFillColorAndOpacity(FillColor);
		}
	}
	if (Button_Back)
	{
		// No background in any state: the control is its key cap and label (the theme gave it a menu button's look).
		FButtonStyle Style;
		Style.SetNormal(ChoiceDialogImpl::NoBrush());
		Style.SetHovered(ChoiceDialogImpl::NoBrush());
		Style.SetPressed(ChoiceDialogImpl::NoBrush());
		Style.SetDisabled(ChoiceDialogImpl::NoBrush());
		Style.SetNormalPadding(FMargin(0.f));
		Style.SetPressedPadding(FMargin(0.f));
		Button_Back->SetStyle(Style);
		Button_Back->SetBackgroundColor(FLinearColor::White);
		Button_Back->SetColorAndOpacity(FLinearColor::White);
	}
	if (KeyPrompt_Back)
	{
		FLinearColor Color = Theme->TextColor;
		Color.A *= FMath::Clamp(BackControlOpacity, 0.05f, 1.f);
		KeyPrompt_Back->Color = Color;
		KeyPrompt_Back->FontSize = FMath::Clamp(static_cast<int32>(Theme->BodyFont.Size), 8, 64);
		KeyPrompt_Back->ApplyStyle();
	}
	if (Border_Panel)
	{
		Border_Panel->SetPadding(FMargin(FMath::Clamp(PanelPadding, 0.f, 200.f)));
	}
}

void UChoiceDialogWidget::TakeInput(APlayerController* Player)
{
	Owner = Player;
	if (AStdPlayerController* StdPlayer = Cast<AStdPlayerController>(Player))
	{
		bCanPauseBefore = StdPlayer->bCanPause;
		StdPlayer->bCanPause = false;
	}
	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(TakeWidget());
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Player->SetInputMode(Mode);
	Player->SetShowMouseCursor(true);
	Player->FlushPressedKeys();
	// The game viewport may still hold the pointer from game input: the cursor is the dialog's now.
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().ReleaseAllPointerCapture();
	}
	bInputTaken = true;
	FocusDefaultWidget();
	PutCursorBelowPanel();
}

void UChoiceDialogWidget::PutCursorBelowPanel()
{
	// Wherever the cursor shows (in game it rests at the view's centre, over the panel), it would move the focus to the
	// button under it: it is put below the panel, so the mouse reaches a button only when the player moves it there.
	APlayerController* Player = Owner.Get();
	int32 SizeX = 0;
	int32 SizeY = 0;
	if (Player)
	{
		Player->GetViewportSize(SizeX, SizeY);
	}
	if (SizeX > 0 && SizeY > 0)
	{
		Player->SetMouseLocation(SizeX / 2, FMath::RoundToInt(SizeY * 0.85f));
	}
	bCursorPlaced = true;
}

void UChoiceDialogWidget::GiveInputBack()
{
	if (!bInputTaken)
	{
		return;
	}
	bInputTaken = false;
	if (APlayerController* Player = Owner.Get())
	{
		Player->SetInputMode(FInputModeGameOnly());
		Player->SetShowMouseCursor(false);
		Player->FlushPressedKeys();
		if (AStdPlayerController* StdPlayer = Cast<AStdPlayerController>(Player))
		{
			StdPlayer->bCanPause = bCanPauseBefore;
		}
	}
}

UButton* UChoiceDialogWidget::GetButton(int32 Index) const
{
	return Buttons.IsValidIndex(Index) ? Buttons[Index].Get() : nullptr;
}

UTextBlock* UChoiceDialogWidget::GetButtonLabel(int32 Index) const
{
	return Labels.IsValidIndex(Index) ? Labels[Index].Get() : nullptr;
}

UProgressBar* UChoiceDialogWidget::GetButtonFill(int32 Index) const
{
	return Fills.IsValidIndex(Index) ? Fills[Index].Get() : nullptr;
}

int32 UChoiceDialogWidget::GetFocusedButton() const
{
	APlayerController* Player = Owner.Get() ? Owner.Get() : GetOwningPlayer();
	for (int32 Index = 0; Index < Buttons.Num(); ++Index)
	{
		const UButton* Button = Buttons[Index];
		if (Button && Player && Button->HasUserFocus(Player))
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

double UChoiceDialogWidget::RunNow() const
{
	if (TimeState)
	{
		return TimeState->GetRunSeconds();
	}
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

float UChoiceDialogWidget::GetHoldFraction() const
{
	if (HeldIndex == INDEX_NONE)
	{
		return ChosenEnding.IsNone() ? 0.f : 1.f;
	}
	return FMath::Clamp(static_cast<float>((RunNow() - HoldStartRun) / UTheLastShiftSettings::Get()->GetChoiceHoldSeconds()), 0.f, 1.f);
}

void UChoiceDialogWidget::SetFill(int32 Index, float Fraction)
{
	if (UProgressBar* Fill = GetButtonFill(Index))
	{
		Fill->SetPercent(FMath::Clamp(Fraction, 0.f, 1.f));
	}
}

void UChoiceDialogWidget::StartHold(int32 Index, EChoiceHoldSource Source)
{
	HeldIndex = Index;
	HoldSource = Source;
	HoldStartRun = RunNow();
	++HoldsStarted;
	SetFill(Index, 0.f);
	UE_LOG(LogTheLastShift, Log, TEXT("ChoiceDialog: %s held with %s (%.2f s to choose)."), *GetEndingOfButton(Index).ToString(), ChoiceDialogImpl::LexToString(Source),
		UTheLastShiftSettings::Get()->GetChoiceHoldSeconds());
}

void UChoiceDialogWidget::CancelHold(const TCHAR* Why)
{
	if (HeldIndex == INDEX_NONE)
	{
		return;
	}
	const int32 Index = HeldIndex;
	const float Fraction = GetHoldFraction();
	HeldIndex = INDEX_NONE;
	HoldSource = EChoiceHoldSource::None;
	++HoldsCancelled;
	LastCancelReason = Why;
	// The press is spent: the button holds again only after it is released and pressed again.
	if (PressSpent.IsValidIndex(Index))
	{
		PressSpent[Index] = true;
	}
	SetFill(Index, 0.f);
	UE_LOG(LogTheLastShift, Log, TEXT("ChoiceDialog: the hold of %s cancelled at %.2f (%s): nothing chosen."), *GetEndingOfButton(Index).ToString(), Fraction, Why);
}

void UChoiceDialogWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (bClosed || !FSlateApplication::IsInitialized())
	{
		return;
	}
	const bool bMouseDown = FSlateApplication::Get().GetPressedMouseButtons().Contains(EKeys::LeftMouseButton);
	for (int32 Index = 0; Index < Buttons.Num(); ++Index)
	{
		if (Buttons[Index] && !Buttons[Index]->IsPressed() && PressSpent.IsValidIndex(Index))
		{
			PressSpent[Index] = false;
		}
	}

	if (HeldIndex == INDEX_NONE)
	{
		for (int32 Index = 0; Index < Buttons.Num(); ++Index)
		{
			const UButton* Button = Buttons[Index];
			if (Button && Button->IsPressed() && PressSpent.IsValidIndex(Index) && !PressSpent[Index])
			{
				StartHold(Index, bMouseDown && Button->IsHovered() ? EChoiceHoldSource::Mouse : EChoiceHoldSource::Key);
				break;
			}
		}
		return;
	}

	const UButton* Held = Buttons[HeldIndex];
	if (!Held || !Held->IsPressed())
	{
		CancelHold(TEXT("let go"));
		return;
	}
	if (HoldSource == EChoiceHoldSource::Mouse && !bMouseDown)
	{
		CancelHold(TEXT("let go"));
		return;
	}
	if (HoldSource == EChoiceHoldSource::Mouse && !Held->IsHovered())
	{
		CancelHold(TEXT("the cursor off the button"));
		return;
	}
	if (HoldSource == EChoiceHoldSource::Key && GetFocusedButton() != HeldIndex)
	{
		CancelHold(TEXT("the focus left the button"));
		return;
	}
	const float Fraction = GetHoldFraction();
	SetFill(HeldIndex, Fraction);
	if (Fraction < 1.f)
	{
		return;
	}

	// Held through: the terminal is told, then the dialog closes.
	const FName Ending = GetEndingOfButton(HeldIndex);
	UE_LOG(LogTheLastShift, Log, TEXT("ChoiceDialog: %s held through (%.2f s of the run's clock) with %s."), *Ending.ToString(), RunNow() - HoldStartRun,
		ChoiceDialogImpl::LexToString(HoldSource));
	HeldIndex = INDEX_NONE;
	HoldSource = EChoiceHoldSource::None;
	ChosenEnding = Ending;
	AChoiceTerminal* Chosen = Terminal.Get();
	const bool bAccepted = Chosen && Chosen->Choose(Ending);
	Close(bAccepted ? FString::Printf(TEXT("%s chosen"), *Ending.ToString()) : FString::Printf(TEXT("%s refused by the terminal"), *Ending.ToString()));
}

FReply UChoiceDialogWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	// No button focused (as on opening): a navigation key or Tab reaches the first button (the last one going back or up);
	// once a button holds the focus, the menus' own navigation moves it.
	if (!bClosed && Buttons.Num() > 0 && GetFocusedButton() == INDEX_NONE && FSlateApplication::IsInitialized())
	{
		const EUINavigation Direction = FSlateApplication::Get().GetNavigationDirectionFromKey(InKeyEvent);
		const bool bTab = InKeyEvent.GetKey() == EKeys::Tab;
		if (Direction != EUINavigation::Invalid || bTab)
		{
			const bool bBackwards = Direction == EUINavigation::Up || Direction == EUINavigation::Left || Direction == EUINavigation::Previous
				|| (bTab && InKeyEvent.IsShiftDown());
			if (UButton* Target = Buttons[bBackwards ? Buttons.Num() - 1 : 0])
			{
				Target->SetFocus();
				return FReply::Handled();
			}
		}
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

bool UChoiceDialogWidget::NativeHandleBackAction()
{
	if (bClosed)
	{
		return true;
	}
	// During a hold the back key cancels it; otherwise it closes the dialog without a choice.
	if (HeldIndex != INDEX_NONE)
	{
		CancelHold(TEXT("the back key"));
		return true;
	}
	Close(TEXT("the back key"));
	return true;
}

void UChoiceDialogWidget::HandleBackClicked()
{
	HandleBackAction();
}

void UChoiceDialogWidget::Close(const FString& Why)
{
	if (bClosed)
	{
		return;
	}
	if (HeldIndex != INDEX_NONE)
	{
		CancelHold(TEXT("the dialog closed"));
	}
	bClosed = true;
	UE_LOG(LogTheLastShift, Log, TEXT("ChoiceDialog: closed (%s)."), *Why);
	CloseScreen();
}

void UChoiceDialogWidget::NativeOnScreenClosing()
{
	bClosed = true;
	GiveInputBack();
	if (AChoiceTerminal* Owning = Terminal.Get())
	{
		Owning->NotifyDialogClosed(this);
	}
	Super::NativeOnScreenClosing();
}
