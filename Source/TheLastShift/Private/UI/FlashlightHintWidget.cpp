// SPDX-License-Identifier: MIT

#include "UI/FlashlightHintWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "UI/StdUIStyle.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Debug/TestLidarCharacter.h"
#include "Engine/World.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"
#include "UI/StdKeyPromptWidget.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(FlashlightHintWidget)

bool UFlashlightHintWidget::Initialize()
{
	const bool bSuperInitialized = Super::Initialize();
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Canvas"));
		WidgetTree->RootWidget = Canvas;

		Border_Hint = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Border_Hint"));
		Border_Hint->SetPadding(FMargin(12.f, 6.f));
		Canvas->AddChildToCanvas(Border_Hint);

		KeyPrompt = WidgetTree->ConstructWidget<UStdKeyPromptWidget>(UStdKeyPromptWidget::StaticClass(), TEXT("KeyPrompt"));
		Border_Hint->SetContent(KeyPrompt);
	}
	return bSuperInitialized;
}

void UFlashlightHintWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::HitTestInvisible);
	ApplyStyle();
	if (KeyPrompt)
	{
		KeyPrompt->SetInputAction(FlashlightAction);
		KeyPrompt->SetLabel(Label);
	}
	bShown = true; // so that the first SetShown(false) applies
	SetShown(false);
	Refresh();
}

void UFlashlightHintWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	Refresh();
}

void UFlashlightHintWidget::SetFlashlightAction(const UInputAction* InAction)
{
	FlashlightAction = InAction;
	if (KeyPrompt)
	{
		KeyPrompt->SetInputAction(InAction);
	}
}

void UFlashlightHintWidget::Activate()
{
	if (HintState != EFlashlightHintState::Idle)
	{
		return;
	}
	HintState = IsOwnerFlashlightOn() ? EFlashlightHintState::Done : EFlashlightHintState::Active;
	UE_LOG(LogTheLastShift, Log, TEXT("Flashlight hint: %s."), HintState == EFlashlightHintState::Active ? TEXT("active") : TEXT("not needed, the flashlight is already on"));
	Refresh();
}

bool UFlashlightHintWidget::IsOwnerFlashlightOn() const
{
	const ATestLidarCharacter* Character = Cast<ATestLidarCharacter>(GetOwningPlayerPawn());
	return Character && Character->IsFlashlightOn();
}

void UFlashlightHintWidget::ApplyStyle()
{
	const int32 Size = FMath::Clamp(FontSize, 8, 48);
	if (KeyPrompt)
	{
		KeyPrompt->FontSize = Size;
		KeyPrompt->Color = TextColor;
		KeyPrompt->ApplyStyle();
	}
	if (Border_Hint)
	{
		// The panel's frame line and cut corner (StdCore's style) around the hint's own fill.
		Border_Hint->SetBrush(StdUIStyle::MakeBoxBrush(BackdropColor));
	}
	if (UCanvasPanelSlot* CanvasSlot = Border_Hint ? Cast<UCanvasPanelSlot>(Border_Hint->Slot) : nullptr)
	{
		CanvasSlot->SetAnchors(FAnchors(0.5f, 1.f));
		CanvasSlot->SetAlignment(FVector2D(0.5f, 1.f));
		CanvasSlot->SetAutoSize(true);
		CanvasSlot->SetPosition(FVector2D(0.f, -FMath::Clamp(BottomMargin, 0.f, 600.f)));
	}
}

void UFlashlightHintWidget::Refresh()
{
	if (IsDesignTime())
	{
		return;
	}
	if (HintState == EFlashlightHintState::Active && IsOwnerFlashlightOn())
	{
		HintState = EFlashlightHintState::Done;
		UE_LOG(LogTheLastShift, Log, TEXT("Flashlight hint: done, the flashlight is on."));
	}
	const UWorld* World = GetWorld();
	const UTimeStateSubsystem* TimeState = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
	const bool bStill = TimeState && (TimeState->IsStopped() || TimeState->IsRunFrozen());
	SetShown(HintState == EFlashlightHintState::Active && !bStill);
}

void UFlashlightHintWidget::SetShown(bool bInShown)
{
	if (bInShown == bShown)
	{
		return;
	}
	bShown = bInShown;
	if (bShown && FirstShownReal < 0.0)
	{
		FirstShownReal = GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0;
	}
	if (Border_Hint)
	{
		Border_Hint->SetVisibility(bShown ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
}

FKey UFlashlightHintWidget::GetShownKey() const
{
	return bShown && KeyPrompt ? KeyPrompt->GetDisplayedKey() : FKey();
}

FString UFlashlightHintWidget::GetShownText() const
{
	if (!bShown)
	{
		return FString();
	}
	return FString::Printf(TEXT("[%s] %s"), KeyPrompt ? *KeyPrompt->GetKeyText().ToString() : TEXT("?"), *Label.ToString());
}
