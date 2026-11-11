// SPDX-License-Identifier: MIT

#include "UI/InteractPromptWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Energy/EnergySubsystem.h"
#include "Energy/PowerTerminal.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Residue/Residue.h"
#include "Styling/CoreStyle.h"
#include "Time/TimeStateSubsystem.h"
#include "UI/StdKeyPromptWidget.h"
#include "UI/StdUITheme.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(InteractPromptWidget)

bool UInteractPromptWidget::Initialize()
{
	const bool bSuperInitialized = Super::Initialize();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Canvas"));
		WidgetTree->RootWidget = Canvas;

		Border_Prompt = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Border_Prompt"));
		Border_Prompt->SetPadding(FMargin(12.f, 6.f));
		Canvas->AddChildToCanvas(Border_Prompt);

		Box_Prompt = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Box_Prompt"));
		Border_Prompt->SetContent(Box_Prompt);

		Text_Prefix = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Text_Prefix"));
		if (UHorizontalBoxSlot* PrefixSlot = Box_Prompt->AddChildToHorizontalBox(Text_Prefix))
		{
			PrefixSlot->SetVerticalAlignment(VAlign_Center);
			PrefixSlot->SetPadding(FMargin(0.f, 0.f, 10.f, 0.f));
		}

		KeyPrompt = WidgetTree->ConstructWidget<UStdKeyPromptWidget>(UStdKeyPromptWidget::StaticClass(), TEXT("KeyPrompt"));
		if (UHorizontalBoxSlot* KeySlot = Box_Prompt->AddChildToHorizontalBox(KeyPrompt))
		{
			KeySlot->SetVerticalAlignment(VAlign_Center);
		}
	}
	return bSuperInitialized;
}

void UInteractPromptWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::HitTestInvisible);
	ApplyStyle();
	if (KeyPrompt)
	{
		KeyPrompt->SetInputAction(InteractAction);
	}
	Mode = EInteractPromptMode::Refusal; // so the first ShowMode(Hidden) applies
	ShowMode(EInteractPromptMode::Hidden, FText::GetEmpty(), FText::GetEmpty());
	Refresh();
}

void UInteractPromptWidget::NativeDestruct()
{
	BindTerminal(nullptr);
	Super::NativeDestruct();
}

void UInteractPromptWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	Refresh();
}

void UInteractPromptWidget::SetInteractAction(const UInputAction* InAction)
{
	InteractAction = InAction;
	if (KeyPrompt)
	{
		KeyPrompt->SetInputAction(InAction);
	}
}

void UInteractPromptWidget::ApplyStyle()
{
	const int32 Size = FMath::Clamp(FontSize, 8, 48);
	if (Text_Prefix)
	{
		FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", Size);
		if (const UStdUITheme* Theme = StdUITheme::Get())
		{
			Font = Theme->BodyFont;
		}
		Font.Size = Size;
		Text_Prefix->SetFont(Font);
		Text_Prefix->SetColorAndOpacity(FSlateColor(TextColor));
	}
	if (KeyPrompt)
	{
		KeyPrompt->FontSize = Size;
		KeyPrompt->Color = TextColor;
		KeyPrompt->ApplyStyle();
	}
	if (Border_Prompt)
	{
		Border_Prompt->SetBrush(FSlateRoundedBoxBrush(BackdropColor, 4.f));
	}
	if (UCanvasPanelSlot* CanvasSlot = Border_Prompt ? Cast<UCanvasPanelSlot>(Border_Prompt->Slot) : nullptr)
	{
		CanvasSlot->SetAnchors(FAnchors(0.5f, 1.f));
		CanvasSlot->SetAlignment(FVector2D(0.5f, 1.f));
		CanvasSlot->SetAutoSize(true);
		CanvasSlot->SetPosition(FVector2D(0.f, -FMath::Clamp(BottomMargin, 0.f, 600.f)));
	}
}

double UInteractPromptWidget::GetRealNow() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetRealTimeSeconds() : 0.0;
}

void UInteractPromptWidget::BindTerminal(APowerTerminal* Terminal)
{
	if (Terminal == BoundTerminal.Get())
	{
		return;
	}
	if (APowerTerminal* Old = BoundTerminal.Get())
	{
		Old->OnInsufficientEnergy.RemoveDynamic(this, &UInteractPromptWidget::HandleInsufficientEnergy);
	}
	BoundTerminal = Terminal;
	if (Terminal)
	{
		Terminal->OnInsufficientEnergy.AddUniqueDynamic(this, &UInteractPromptWidget::HandleInsufficientEnergy);
	}
}

void UInteractPromptWidget::HandleInsufficientEnergy(int32 Cost, int32 BankedEnergy, bool bTimeStopped)
{
	// While Stopped terminals refuse too, but the prompt says nothing then.
	if (!bTimeStopped)
	{
		RefusalUntil = GetRealNow() + FMath::Clamp(RefusalSeconds, 0.5f, 10.f);
		Refresh();
	}
}

void UInteractPromptWidget::Refresh()
{
	if (IsDesignTime())
	{
		return;
	}

	APawn* Pawn = GetOwningPlayerPawn();
	const UWorld* World = GetWorld();
	const UTimeStateSubsystem* TimeState = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
	if (!Pawn || !TimeState)
	{
		BindTerminal(nullptr);
		ShowMode(EInteractPromptMode::Hidden, FText::GetEmpty(), FText::GetEmpty());
		return;
	}

	if (TimeState->IsStopped())
	{
		RefusalUntil = -1.0;
		BindTerminal(nullptr);
		const AResidue* Residue = AResidue::FindCollectable(Pawn);
		if (Residue && !Residue->IsCollecting())
		{
			ShowMode(EInteractPromptMode::Collect, HoldLabel, CollectLabel);
		}
		else
		{
			ShowMode(EInteractPromptMode::Hidden, FText::GetEmpty(), FText::GetEmpty());
		}
		return;
	}

	APowerTerminal* Terminal = APowerTerminal::FindNearestInRange(Pawn);
	BindTerminal(Terminal);

	if (RefusalUntil >= 0.0 && GetRealNow() < RefusalUntil)
	{
		ShowMode(EInteractPromptMode::Refusal, RefusalLabel, FText::GetEmpty());
		return;
	}
	RefusalUntil = -1.0;

	if (Terminal && Terminal->HasUnpoweredTarget())
	{
		const UEnergySubsystem* Energy = UEnergySubsystem::Get(this);
		FFormatNamedArguments Args;
		Args.Add(TEXT("Cost"), Terminal->GetCost());
		Args.Add(TEXT("Energy"), Energy ? Energy->GetBankedEnergy() : 0);
		ShowMode(EInteractPromptMode::Power, FText::GetEmpty(), FText::Format(PowerLabel, Args));
		return;
	}
	ShowMode(EInteractPromptMode::Hidden, FText::GetEmpty(), FText::GetEmpty());
}

void UInteractPromptWidget::ShowMode(EInteractPromptMode NewMode, const FText& Prefix, const FText& Label)
{
	const bool bModeChanged = NewMode != Mode;
	if (bModeChanged || !Prefix.EqualTo(ShownPrefix))
	{
		ShownPrefix = Prefix;
		if (Text_Prefix)
		{
			Text_Prefix->SetText(Prefix);
			Text_Prefix->SetVisibility(Prefix.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
		}
	}
	if (bModeChanged || !Label.EqualTo(ShownLabel))
	{
		ShownLabel = Label;
		if (KeyPrompt)
		{
			KeyPrompt->SetLabel(Label);
		}
	}
	if (bModeChanged)
	{
		Mode = NewMode;
		const bool bShowKey = NewMode == EInteractPromptMode::Collect || NewMode == EInteractPromptMode::Power;
		if (KeyPrompt)
		{
			KeyPrompt->SetVisibility(bShowKey ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		}
		// The gap after the prefix only separates it from the key cap.
		if (UHorizontalBoxSlot* PrefixSlot = Text_Prefix ? Cast<UHorizontalBoxSlot>(Text_Prefix->Slot) : nullptr)
		{
			PrefixSlot->SetPadding(FMargin(0.f, 0.f, bShowKey ? 10.f : 0.f, 0.f));
		}
		if (Border_Prompt)
		{
			Border_Prompt->SetVisibility(NewMode == EInteractPromptMode::Hidden ? ESlateVisibility::Hidden : ESlateVisibility::HitTestInvisible);
		}
	}
}

FKey UInteractPromptWidget::GetShownKey() const
{
	return KeyPrompt && (Mode == EInteractPromptMode::Collect || Mode == EInteractPromptMode::Power) ? KeyPrompt->GetDisplayedKey() : FKey();
}

FString UInteractPromptWidget::GetShownText() const
{
	if (Mode == EInteractPromptMode::Hidden)
	{
		return FString();
	}
	TArray<FString> Parts;
	if (!ShownPrefix.IsEmpty())
	{
		Parts.Add(ShownPrefix.ToString());
	}
	if (Mode == EInteractPromptMode::Collect || Mode == EInteractPromptMode::Power)
	{
		Parts.Add(FString::Printf(TEXT("[%s]"), KeyPrompt ? *KeyPrompt->GetKeyText().ToString() : TEXT("?")));
	}
	if (!ShownLabel.IsEmpty())
	{
		Parts.Add(ShownLabel.ToString());
	}
	return FString::Join(Parts, TEXT(" "));
}
