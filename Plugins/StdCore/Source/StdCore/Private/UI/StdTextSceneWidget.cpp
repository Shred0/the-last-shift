// Copyright 2026 <OWNER>. All rights reserved. See Plugins/StdCore/LICENSE.

#include "UI/StdTextSceneWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/TextBlock.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Rendering/SlateRenderer.h"
#include "StdLog.h"
#include "Styling/CoreStyle.h"

namespace StdTextScene
{
	/** A hitch (a map load, a breakpoint) never types a whole entry at once. */
	constexpr float MaxStep = 0.25f;

	bool IsConfirmKey(const FKey& Key)
	{
		return Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Gamepad_FaceButton_Bottom;
	}

	FString BreakLines(const FString& Text, const FSlateFontInfo& Font, float MaxWidth, const TSharedRef<FSlateFontMeasure>& Measure)
	{
		TArray<FString> Paragraphs;
		Text.ParseIntoArray(Paragraphs, TEXT("\n"), false);
		TArray<FString> Lines;
		for (const FString& Paragraph : Paragraphs)
		{
			TArray<FString> Words;
			Paragraph.ParseIntoArray(Words, TEXT(" "), true);
			FString Line;
			for (const FString& Word : Words)
			{
				const FString Candidate = Line.IsEmpty() ? Word : Line + TEXT(" ") + Word;
				if (!Line.IsEmpty() && Measure->Measure(Candidate, Font).X > MaxWidth)
				{
					Lines.Add(Line);
					Line = Word;
				}
				else
				{
					Line = Candidate;
				}
			}
			Lines.Add(Line);
		}
		return FString::Join(Lines, TEXT("\n"));
	}

	static TSharedPtr<FSlateFontMeasure> GetMeasure()
	{
		if (!FSlateApplication::IsInitialized() || !FSlateApplication::Get().GetRenderer())
		{
			return nullptr;
		}
		return FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	}
}

UStdTextSceneWidget::UStdTextSceneWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Each entry sets its own colours; there is no button to highlight.
	bApplyTheme = false;
	bHighlightFocus = false;
}

void UStdTextSceneWidget::NativeOnInitialized()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		BuildDefaultTree();
	}
	// The screen itself takes the keys: it has no button to focus.
	SetIsFocusable(true);
	RefreshText();
	Super::NativeOnInitialized();
}

void UStdTextSceneWidget::BuildDefaultTree()
{
	Border_Background = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Border_Background"));
	// A plain box in the brush colour: no texture.
	FSlateBrush Solid;
	Solid.DrawAs = ESlateBrushDrawType::Box;
	Solid.TintColor = FSlateColor(FLinearColor::White);
	Border_Background->SetBrush(Solid);
	Border_Background->SetBrushColor(FLinearColor::Black);
	Border_Background->SetPadding(FMargin(0.f));
	WidgetTree->RootWidget = Border_Background;

	UOverlay* Root = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Overlay_Root"));
	if (UBorderSlot* RootSlot = Cast<UBorderSlot>(Border_Background->AddChild(Root)))
	{
		RootSlot->SetHorizontalAlignment(HAlign_Fill);
		RootSlot->SetVerticalAlignment(VAlign_Fill);
	}

	SizeBox_Line = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SizeBox_Line"));
	if (UOverlaySlot* LineSlot = Root->AddChildToOverlay(SizeBox_Line))
	{
		LineSlot->SetHorizontalAlignment(HAlign_Center);
		LineSlot->SetVerticalAlignment(VAlign_Center);
	}

	Text_Line = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Text_Line"));
	Text_Line->SetFont(GetFont());
	Text_Line->SetJustification(ETextJustify::Left);
	Text_Line->SetAutoWrapText(false);
	if (USizeBoxSlot* TextSlot = Cast<USizeBoxSlot>(SizeBox_Line->AddChild(Text_Line)))
	{
		TextSlot->SetHorizontalAlignment(HAlign_Left);
		TextSlot->SetVerticalAlignment(VAlign_Top);
	}
}

FSlateFontInfo UStdTextSceneWidget::GetFont() const
{
	return FCoreStyle::GetDefaultFontStyle("Regular", FMath::Clamp(FontSize, 8, 96));
}

void UStdTextSceneWidget::SetEntries(const TArray<FStdTextSceneEntry>& InEntries)
{
	Entries = InEntries;
	CurrentIndex = INDEX_NONE;
	LaidOut.Reset();
	BlockSize = FVector2D::ZeroVector;
	Elapsed = 0.0;
	ShownChars = 0;
	bFinished = false;
	bSkipped = false;
	RefreshText();
}

void UStdTextSceneWidget::StartScene()
{
	bFinished = false;
	bSkipped = false;
	if (Entries.IsEmpty())
	{
		Finish(false);
		return;
	}
	ShowEntry(0);
}

void UStdTextSceneWidget::ShowEntry(int32 Index)
{
	CurrentIndex = Index;
	const FStdTextSceneEntry& Entry = Entries[Index];
	const FSlateFontInfo Font = GetFont();
	const float Width = FMath::Clamp(ColumnWidth, 100.f, 3000.f);
	const FString Full = Entry.Text.ToString();
	if (const TSharedPtr<FSlateFontMeasure> Measure = StdTextScene::GetMeasure())
	{
		// Laid out once: typing fills a block whose lines are already fixed, so nothing moves.
		LaidOut = StdTextScene::BreakLines(Full, Font, Width, Measure.ToSharedRef());
		BlockSize = Measure->Measure(LaidOut, Font);
	}
	else
	{
		LaidOut = Full;
		BlockSize = FVector2D(Width, 0.f);
	}
	Elapsed = 0.0;
	ShownChars = 0;
	if (Text_Line)
	{
		Text_Line->SetFont(Font);
		Text_Line->SetColorAndOpacity(FSlateColor(Entry.TextColor));
	}
	if (SizeBox_Line)
	{
		SizeBox_Line->SetWidthOverride(FMath::Max(BlockSize.X, 1.f) + 2.f);
		if (BlockSize.Y > 0.f)
		{
			SizeBox_Line->SetHeightOverride(BlockSize.Y + 2.f);
		}
		else
		{
			SizeBox_Line->ClearHeightOverride();
		}
	}
	if (Border_Background)
	{
		Border_Background->SetBrushColor(Entry.BackgroundColor);
	}
	RefreshText();
	UE_LOG(LogStdCore, Log, TEXT("%s: entry %d of %d started (%d characters, %d lines)."), *GetName(), Index + 1, Entries.Num(), LaidOut.Len(), GetLineCount());
	OnEntryStarted.Broadcast(this, Index);
}

void UStdTextSceneWidget::Confirm()
{
	if (bFinished || CurrentIndex == INDEX_NONE)
	{
		return;
	}
	if (IsTyping())
	{
		ShownChars = LaidOut.Len();
		RefreshText();
		return;
	}
	if (CurrentIndex + 1 < Entries.Num())
	{
		ShowEntry(CurrentIndex + 1);
	}
	else
	{
		Finish(false);
	}
}

void UStdTextSceneWidget::Skip()
{
	if (!bFinished)
	{
		Finish(true);
	}
}

void UStdTextSceneWidget::Finish(bool bInSkipped)
{
	bFinished = true;
	bSkipped = bInSkipped;
	RefreshText();
	UE_LOG(LogStdCore, Log, TEXT("%s: scene %s at entry %d of %d."), *GetName(), bSkipped ? TEXT("skipped") : TEXT("finished"), CurrentIndex + 1, Entries.Num());
	OnSceneFinished.Broadcast(this, bSkipped);
}

void UStdTextSceneWidget::AdvanceSeconds(float DeltaSeconds)
{
	if (bFinished || CurrentIndex == INDEX_NONE || !IsTyping())
	{
		return;
	}
	Elapsed += FMath::Max(DeltaSeconds, 0.f);
	const int32 Reached = FMath::Min(LaidOut.Len(), FMath::FloorToInt(Elapsed * FMath::Clamp(CharsPerSecond, 1.f, 500.f)));
	if (Reached != ShownChars)
	{
		ShownChars = Reached;
		RefreshText();
	}
}

bool UStdTextSceneWidget::IsTyping() const
{
	return !bFinished && CurrentIndex != INDEX_NONE && ShownChars < LaidOut.Len();
}

FString UStdTextSceneWidget::GetVisibleText() const
{
	return bFinished || CurrentIndex == INDEX_NONE ? FString() : LaidOut.Left(ShownChars);
}

int32 UStdTextSceneWidget::GetLineCount() const
{
	if (LaidOut.IsEmpty())
	{
		return 0;
	}
	int32 Lines = 1;
	for (const TCHAR C : LaidOut)
	{
		Lines += C == TEXT('\n') ? 1 : 0;
	}
	return Lines;
}

FLinearColor UStdTextSceneWidget::GetShownTextColor() const
{
	return Text_Line ? Text_Line->GetColorAndOpacity().GetSpecifiedColor() : FLinearColor::Transparent;
}

FLinearColor UStdTextSceneWidget::GetShownBackgroundColor() const
{
	return Border_Background ? Border_Background->GetBrushColor() : FLinearColor::Transparent;
}

void UStdTextSceneWidget::RefreshText()
{
	if (Text_Line)
	{
		Text_Line->SetText(FText::FromString(GetVisibleText()));
	}
}

void UStdTextSceneWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	AdvanceSeconds(FMath::Clamp(InDeltaTime, 0.f, StdTextScene::MaxStep));
}

FReply UStdTextSceneWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (StdTextScene::IsConfirmKey(InKeyEvent.GetKey()) && !InKeyEvent.IsRepeat() && !IsKeyCaptureActive())
	{
		Confirm();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

FReply UStdTextSceneWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bClickConfirms && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		Confirm();
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

bool UStdTextSceneWidget::NativeHandleBackAction()
{
	// Back skips the whole scene; it never closes the screen (the owner does, once the scene has finished).
	Skip();
	return true;
}
