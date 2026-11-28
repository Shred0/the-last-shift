#include "UI/StdUITheme.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CheckBox.h"
#include "Components/Image.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/InputKeySelector.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Components/TextBlock.h"
#include "Engine/Font.h"
#include "Settings/StdProjectSettings.h"
#include "Sound/SoundBase.h"
#include "UI/StdUIStyle.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"

namespace StdUIThemeDetail
{
	FLinearColor Darken(const FLinearColor& Color, float Factor)
	{
		return FLinearColor(Color.R * Factor, Color.G * Factor, Color.B * Factor, Color.A);
	}

	FLinearColor WithAlpha(const FLinearColor& Color, float Alpha)
	{
		return FLinearColor(Color.R, Color.G, Color.B, Alpha);
	}

	FSlateSound MakeSound(USoundBase* Sound)
	{
		FSlateSound SlateSound;
		SlateSound.SetResourceObject(Sound);
		return SlateSound;
	}
}

UStdUITheme::UStdUITheme()
{
	// sRGB values, converted to linear by FLinearColor(FColor).
	AccentColor = FLinearColor(FColor(45, 127, 249));
	PanelColor = FLinearColor(FColor(18, 20, 26, 235));
	BackdropColor = FLinearColor(FColor(0, 0, 0, 140));
	ButtonColor = FLinearColor(FColor(44, 48, 58));
	TextColor = FLinearColor(FColor(236, 238, 242));
	MutedTextColor = FLinearColor(FColor(150, 156, 168));
	TextOnAccentColor = FLinearColor(FColor(255, 255, 255));

	// The engine's Roboto font asset, also used by UMG's default text: always cooked with the engine content it needs.
	static ConstructorHelpers::FObjectFinder<UFont> RobotoFont(*UWidget::GetDefaultFontName());
	if (RobotoFont.Succeeded())
	{
		TitleFont = FSlateFontInfo(RobotoFont.Object, 40, TEXT("Bold"));
		HeadingFont = FSlateFontInfo(RobotoFont.Object, 22, TEXT("Bold"));
		BodyFont = FSlateFontInfo(RobotoFont.Object, 18, TEXT("Regular"));
		ButtonFont = FSlateFontInfo(RobotoFont.Object, 18, TEXT("Bold"));
	}
}

void UStdUITheme::InitFromStyle(const FStdUIStyle& Style, const FLinearColor& Accent)
{
	Shape = EStdUIShape::Framed;
	AccentColor = Accent;
	PanelColor = Style.PanelColor;
	BackdropColor = Style.BackdropColor;
	// No fill at rest: the button colour is the panel's, used only where a rounded theme would fill.
	ButtonColor = Style.PanelColor;
	TextColor = Style.TextColor;
	MutedTextColor = Style.MutedTextColor;
	// The label on an accent fill in the panel's dark colour, opaque.
	TextOnAccentColor = FLinearColor(Style.PanelColor.R, Style.PanelColor.G, Style.PanelColor.B, 1.f);
	FrameColor = Style.FrameColor;
	FrameThickness = FMath::Clamp(Style.FrameThickness, 0.5f, 8.f);
	CornerCut = FMath::Clamp(Style.CornerCut, 0.f, 64.f);
	TitleRuleThickness = FMath::Clamp(Style.TitleRuleThickness, 0.f, 8.f);
	CornerRadius = 0.f;
	ButtonPadding = Style.ButtonPadding;
	bUpperCaseLabels = Style.bUpperCaseLabels;
	const int32 Spacing = FMath::Clamp(Style.LabelLetterSpacing, 0, 1000);
	TitleFont = StdUIStyle::MakeFont(Style.TitleSize, Spacing);
	HeadingFont = StdUIStyle::MakeFont(Style.HeadingSize, Spacing);
	BodyFont = StdUIStyle::MakeFont(Style.BodySize, 0);
	ButtonFont = StdUIStyle::MakeFont(Style.ButtonSize, Spacing);
}

FSlateBrush UStdUITheme::MakeRoundedBrush(const FLinearColor& Color, float Radius) const
{
	FSlateBrush Brush;
	Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
	Brush.TintColor = FSlateColor(Color);
	Brush.OutlineSettings = FSlateBrushOutlineSettings(IsFramed() ? 0.f : Radius);
	Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
	return Brush;
}

FSlateBrush UStdUITheme::MakePanelBrush(const FLinearColor& Fill) const
{
	if (IsFramed())
	{
		return StdUIStyle::MakeFrameBrush(Fill, FrameColor, FrameThickness, CornerCut);
	}
	return MakeRoundedBrush(Fill, CornerRadius * 2.f);
}

namespace StdUIThemeDetail
{
	FButtonStyle MakeFramedButtonStyle(const UStdUITheme& Theme)
	{
		const FLinearColor Clear = FLinearColor::Transparent;
		const FLinearColor Pressed = Darken(Theme.AccentColor, 0.78f);
		const FLinearColor Marker = Theme.TextOnAccentColor;
		const float T = Theme.FrameThickness;
		const float C = Theme.CornerCut;

		FButtonStyle Style;
		Style.SetNormal(StdUIStyle::MakeFrameBrush(Clear, Theme.FrameColor, T, C));
		Style.SetHovered(StdUIStyle::MakeFrameBrush(Theme.AccentColor, Theme.AccentColor, T, C, true, Marker));
		Style.SetPressed(StdUIStyle::MakeFrameBrush(Pressed, Pressed, T, C, true, Marker));
		Style.SetDisabled(StdUIStyle::MakeFrameBrush(Clear, WithAlpha(Theme.FrameColor, Theme.FrameColor.A / 3.f), T, C));
		Style.SetNormalForeground(FSlateColor(Theme.TextColor));
		Style.SetHoveredForeground(FSlateColor(Theme.TextOnAccentColor));
		Style.SetPressedForeground(FSlateColor(Theme.TextOnAccentColor));
		Style.SetDisabledForeground(FSlateColor(WithAlpha(Theme.TextColor, Theme.TextColor.A / 3.f)));
		Style.SetNormalPadding(Theme.ButtonPadding);
		Style.SetPressedPadding(Theme.ButtonPadding);
		return Style;
	}
}

FButtonStyle UStdUITheme::MakeButtonStyle() const
{
	using namespace StdUIThemeDetail;

	if (IsFramed())
	{
		FButtonStyle Framed = MakeFramedButtonStyle(*this);
		if (HoverSound)
		{
			Framed.SetHoveredSound(MakeSound(HoverSound));
		}
		if (PressSound)
		{
			Framed.SetPressedSound(MakeSound(PressSound));
		}
		return Framed;
	}

	FButtonStyle Style;
	Style.SetNormal(MakeRoundedBrush(ButtonColor, CornerRadius));
	Style.SetHovered(MakeRoundedBrush(AccentColor, CornerRadius));
	Style.SetPressed(MakeRoundedBrush(Darken(AccentColor, 0.7f), CornerRadius));
	Style.SetDisabled(MakeRoundedBrush(WithAlpha(ButtonColor, ButtonColor.A * 0.5f), CornerRadius));
	Style.SetNormalForeground(FSlateColor(TextColor));
	Style.SetHoveredForeground(FSlateColor(TextOnAccentColor));
	Style.SetPressedForeground(FSlateColor(TextOnAccentColor));
	Style.SetDisabledForeground(FSlateColor(MutedTextColor));
	Style.SetNormalPadding(ButtonPadding);
	Style.SetPressedPadding(ButtonPadding);
	if (HoverSound)
	{
		Style.SetHoveredSound(MakeSound(HoverSound));
	}
	if (PressSound)
	{
		Style.SetPressedSound(MakeSound(PressSound));
	}
	return Style;
}

FButtonStyle UStdUITheme::MakeTabStyle(bool bSelected) const
{
	using namespace StdUIThemeDetail;

	// Selected: filled like a button. Not selected: no fill, muted label. Focus and hover use the accent, so the
	// three states stay distinct.
	FButtonStyle Style = MakeButtonStyle();
	if (IsFramed())
	{
		// Selected: the frame in the accent. Not selected: no frame, muted label. Focus and hover fill with the accent.
		if (bSelected)
		{
			Style.SetNormal(StdUIStyle::MakeFrameBrush(FLinearColor::Transparent, AccentColor, FrameThickness, CornerCut));
			Style.SetNormalForeground(FSlateColor(TextColor));
		}
		else
		{
			Style.SetNormal(StdUIStyle::MakeFrameBrush(FLinearColor::Transparent, FLinearColor::Transparent, FrameThickness, CornerCut));
			Style.SetNormalForeground(FSlateColor(MutedTextColor));
		}
		return Style;
	}
	if (bSelected)
	{
		Style.SetNormal(MakeRoundedBrush(ButtonColor, CornerRadius));
		Style.SetNormalForeground(FSlateColor(TextColor));
	}
	else
	{
		Style.SetNormal(MakeRoundedBrush(WithAlpha(ButtonColor, 0.f), CornerRadius));
		Style.SetNormalForeground(FSlateColor(MutedTextColor));
	}
	return Style;
}

FSliderStyle UStdUITheme::MakeSliderStyle() const
{
	constexpr float ThumbSize = 18.f;

	FSliderStyle Style;
	Style.SetNormalBarImage(MakeRoundedBrush(MutedTextColor, 2.f));
	Style.SetHoveredBarImage(MakeRoundedBrush(AccentColor, 2.f));
	Style.SetDisabledBarImage(MakeRoundedBrush(StdUIThemeDetail::WithAlpha(MutedTextColor, 0.3f), 2.f));

	FSlateBrush Thumb = MakeRoundedBrush(AccentColor, ThumbSize * 0.5f);
	Thumb.ImageSize = FVector2D(ThumbSize, ThumbSize);
	Style.SetNormalThumbImage(Thumb);
	Thumb.TintColor = FSlateColor(TextColor);
	Style.SetHoveredThumbImage(Thumb);
	Thumb.TintColor = FSlateColor(MutedTextColor);
	Style.SetDisabledThumbImage(Thumb);

	Style.SetBarThickness(4.f);
	return Style;
}

FCheckBoxStyle UStdUITheme::MakeCheckBoxStyle() const
{
	using namespace StdUIThemeDetail;
	constexpr float BoxSize = 22.f;
	const FLinearColor Clear = FLinearColor::Transparent;

	const auto Box = [this, BoxSize](const FLinearColor& Fill, const FLinearColor& Frame)
	{
		FSlateBrush Brush;
		if (IsFramed())
		{
			Brush = StdUIStyle::MakeFrameBrush(Fill, Frame, FrameThickness, FMath::Min(CornerCut, BoxSize * 0.3f));
		}
		else
		{
			// A filled rounded box, its outline in the frame's colour.
			Brush = MakeRoundedBrush(Fill, CornerRadius * 0.5f);
			Brush.OutlineSettings.Color = FSlateColor(Frame);
			Brush.OutlineSettings.Width = FrameThickness;
		}
		Brush.ImageSize = FVector2D(BoxSize, BoxSize);
		return Brush;
	};

	const FLinearColor Pressed = Darken(AccentColor, 0.78f);
	FCheckBoxStyle Style;
	Style.SetCheckBoxType(ESlateCheckBoxType::CheckBox);
	Style.SetUncheckedImage(Box(Clear, FrameColor));
	Style.SetUncheckedHoveredImage(Box(Clear, AccentColor));
	Style.SetUncheckedPressedImage(Box(Pressed, Pressed));
	Style.SetCheckedImage(Box(AccentColor, AccentColor));
	Style.SetCheckedHoveredImage(Box(AccentColor, TextColor));
	Style.SetCheckedPressedImage(Box(Pressed, Pressed));
	Style.SetUndeterminedImage(Box(WithAlpha(AccentColor, AccentColor.A * 0.5f), FrameColor));
	Style.SetUndeterminedHoveredImage(Box(WithAlpha(AccentColor, AccentColor.A * 0.5f), AccentColor));
	Style.SetUndeterminedPressedImage(Box(Pressed, Pressed));
	Style.SetPadding(FMargin(10.f, 0.f, 0.f, 0.f));
	Style.SetForegroundColor(FSlateColor(TextColor));
	Style.SetHoveredForegroundColor(FSlateColor(TextColor));
	Style.SetPressedForegroundColor(FSlateColor(TextColor));
	Style.SetCheckedForegroundColor(FSlateColor(TextColor));
	Style.SetCheckedHoveredForegroundColor(FSlateColor(TextColor));
	Style.SetCheckedPressedForegroundColor(FSlateColor(TextColor));
	if (HoverSound)
	{
		Style.SetHoveredSound(MakeSound(HoverSound));
	}
	if (PressSound)
	{
		Style.SetCheckedSound(MakeSound(PressSound));
		Style.SetUncheckedSound(MakeSound(PressSound));
	}
	return Style;
}

FTextBlockStyle UStdUITheme::MakeTextStyle(const FSlateFontInfo& Font, const FLinearColor& Color) const
{
	FTextBlockStyle Style;
	Style.SetFont(Font);
	Style.SetColorAndOpacity(FSlateColor(Color));
	return Style;
}

const UStdUITheme* StdUITheme::Get()
{
	const UStdProjectSettings* Settings = UStdProjectSettings::Get();
	if (Settings->bUseStyle)
	{
		// Built from the style's values, kept for the life of the process, rebuilt when the accent changes.
		static UStdUITheme* StyleTheme = nullptr;
		static uint32 BuiltVersion = 0;
		if (!StyleTheme)
		{
			StyleTheme = NewObject<UStdUITheme>(GetTransientPackage(), TEXT("StdUIStyleTheme"));
			StyleTheme->AddToRoot();
		}
		if (BuiltVersion != StdUIStyle::GetVersion())
		{
			StyleTheme->InitFromStyle(Settings->Style, StdUIStyle::GetAccentColor());
			BuiltVersion = StdUIStyle::GetVersion();
		}
		return StyleTheme;
	}
	if (Settings->Theme.IsNull())
	{
		return nullptr;
	}
	return StdSettings::LoadAsset(Settings->Theme, TEXT("Theme"), StdCoreDefaults::UITheme);
}

namespace StdUIThemeDetail
{
	/**
	 * A framed button's label in a box of fixed width (a size box around the button): when it does not fit there with
	 * the button font, it drops its letter spacing, then takes a smaller size, until it fits.
	 */
	void FitLabel(const UStdUITheme& Theme, UTextBlock& Text)
	{
		const UButton* Button = Cast<UButton>(Text.GetParent());
		const USizeBox* Box = Button ? Cast<USizeBox>(Button->GetParent()) : nullptr;
		if (!Box || Box->GetWidthOverride() <= 0.f || !FSlateApplication::IsInitialized() || !FSlateApplication::Get().GetRenderer())
		{
			return;
		}
		const float Available = Box->GetWidthOverride() - Theme.ButtonPadding.Left - Theme.ButtonPadding.Right - 2.f * Theme.FrameThickness;
		const TSharedRef<FSlateFontMeasure> Measure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
		FString Label = Text.GetText().ToString();
		if (Theme.bUpperCaseLabels)
		{
			Label = Label.ToUpper();
		}
		FSlateFontInfo Font = Theme.ButtonFont;
		if (Available <= 0.f || Label.IsEmpty() || Measure->Measure(Label, Font).X <= Available)
		{
			return;
		}
		Font.LetterSpacing = 0;
		const float Width = static_cast<float>(Measure->Measure(Label, Font).X);
		if (Width > Available)
		{
			Font.Size = FMath::Max(8.f, FMath::FloorToFloat(Font.Size * Available / Width));
		}
		Text.SetFont(Font);
	}

	/** The rule's colour and height, shown while the title is. */
	void StyleTitleRule(const UStdUITheme& Theme, UImage& Rule, const UTextBlock& Title)
	{
		FSlateBrush Brush;
		Brush.DrawAs = ESlateBrushDrawType::Image;
		Brush.TintColor = FSlateColor(Theme.FrameColor);
		Brush.ImageSize = FVector2D(1.f, FMath::Max(Theme.TitleRuleThickness, 0.5f));
		Rule.SetBrush(Brush);
		Rule.SetVisibility(Title.GetVisibility() == ESlateVisibility::Collapsed ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
	}

	/**
	 * A thin rule under a title that sits in a vertical box: inserted once after it, with the title's gap below moved under
	 * the rule. Before the Slate tree is built only: a live vertical box would add it at its end.
	 */
	void InsertTitleRule(const UStdUITheme& Theme, UUserWidget& Widget, UTextBlock& Title)
	{
		UVerticalBox* Box = Cast<UVerticalBox>(Title.GetParent());
		if (!Box || !Widget.WidgetTree)
		{
			return;
		}
		UImage* Rule = Cast<UImage>(Widget.WidgetTree->FindWidget(StdUITheme::TitleRuleName));
		if (!Rule)
		{
			Rule = Widget.WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), StdUITheme::TitleRuleName);
			const int32 Index = Box->GetChildIndex(&Title);
			Box->InsertChildAt(Index + 1, Rule);
			UVerticalBoxSlot* TitleSlot = Cast<UVerticalBoxSlot>(Title.Slot);
			UVerticalBoxSlot* RuleSlot = Cast<UVerticalBoxSlot>(Rule->Slot);
			if (TitleSlot && RuleSlot)
			{
				constexpr float Gap = 10.f;
				const FMargin TitlePadding = TitleSlot->GetPadding();
				RuleSlot->SetPadding(FMargin(TitlePadding.Left, 0.f, TitlePadding.Right, FMath::Max(TitlePadding.Bottom - Gap, 0.f)));
				RuleSlot->SetHorizontalAlignment(HAlign_Fill);
				TitleSlot->SetPadding(FMargin(TitlePadding.Left, TitlePadding.Top, TitlePadding.Right, Gap));
			}
		}
		StyleTitleRule(Theme, *Rule, Title);
	}
}

void StdUITheme::ApplyToWidget(const UStdUITheme& Theme, UUserWidget& Widget)
{
	using namespace StdUIThemeDetail;

	if (!Widget.WidgetTree)
	{
		return;
	}

	const FButtonStyle ButtonStyle = Theme.MakeButtonStyle();
	const FButtonStyle TabStyle = Theme.MakeTabStyle(false);
	const FSliderStyle SliderStyle = Theme.MakeSliderStyle();
	const FCheckBoxStyle CheckBoxStyle = Theme.MakeCheckBoxStyle();
	FTextBlockStyle KeyTextStyle = Theme.MakeTextStyle(Theme.ButtonFont, Theme.TextColor);
	if (Theme.IsFramed())
	{
		// A framed key selector is a framed button: its key's label follows the button's state (normal, hovered or
		// focused, pressed, disabled) like every other button label, so that it reads on the accent fill.
		KeyTextStyle.SetColorAndOpacity(FSlateColor::UseForeground());
	}
	TArray<UTextBlock*> Titles;

	Widget.WidgetTree->ForEachWidget([&](UWidget* Child)
	{
		const FString Name = Child->GetName();

		if (UButton* Button = Cast<UButton>(Child))
		{
			Button->SetStyle(Name.StartsWith(TEXT("Button_Tab")) ? TabStyle : ButtonStyle);
		}
		else if (UTextBlock* Text = Cast<UTextBlock>(Child))
		{
			if (Cast<UButton>(Text->GetParent()))
			{
				// A button label: its color follows the button state (normal, hovered, pressed).
				Text->SetFont(Theme.ButtonFont);
				Text->SetColorAndOpacity(FSlateColor::UseForeground());
				Text->SetTextTransformPolicy(Theme.bUpperCaseLabels ? ETextTransformPolicy::ToUpper : ETextTransformPolicy::None);
				if (Theme.IsFramed())
				{
					// Framed buttons: the label at the left, after the room the focus marker takes.
					Text->SetJustification(ETextJustify::Left);
					if (UButtonSlot* LabelSlot = Cast<UButtonSlot>(Text->Slot))
					{
						LabelSlot->SetHorizontalAlignment(HAlign_Left);
					}
					FitLabel(Theme, *Text);
				}
			}
			else if (Name == TEXT("Text_Title"))
			{
				Text->SetFont(Theme.TitleFont);
				Text->SetColorAndOpacity(FSlateColor(Theme.TextColor));
				Text->SetTextTransformPolicy(Theme.bUpperCaseLabels ? ETextTransformPolicy::ToUpper : ETextTransformPolicy::None);
				if (Theme.IsFramed() && Theme.TitleRuleThickness > 0.f && !Widget.IsDesignTime())
				{
					Titles.Add(Text);
				}
			}
			else if (Name.StartsWith(TEXT("Text_Heading")))
			{
				Text->SetFont(Theme.HeadingFont);
				Text->SetColorAndOpacity(FSlateColor(Theme.TextColor));
				Text->SetTextTransformPolicy(Theme.bUpperCaseLabels ? ETextTransformPolicy::ToUpper : ETextTransformPolicy::None);
			}
			else
			{
				Text->SetFont(Theme.BodyFont);
				Text->SetColorAndOpacity(FSlateColor(Name == TEXT("Text_Version") ? Theme.MutedTextColor : Theme.TextColor));
			}
		}
		else if (USlider* Slider = Cast<USlider>(Child))
		{
			Slider->SetWidgetStyle(SliderStyle);
			Slider->SetSliderBarColor(FLinearColor::White);
			Slider->SetSliderHandleColor(FLinearColor::White);
		}
		else if (UCheckBox* CheckBox = Cast<UCheckBox>(Child))
		{
			CheckBox->SetWidgetStyle(CheckBoxStyle);
		}
		else if (UInputKeySelector* KeySelector = Cast<UInputKeySelector>(Child))
		{
			SetKeySelectorStyle(KeySelector, ButtonStyle);
			// Same pointer hand-off as the button style.
			KeySelector->SetTextStyle(KeyTextStyle);
			KeySelector->SetTextStyle(KeySelector->GetTextStyle());
		}
		else if (UBorder* Border = Cast<UBorder>(Child))
		{
			if (Name == TEXT("Border_Background"))
			{
				Border->SetBrush(Theme.MakeRoundedBrush(Theme.BackdropColor, 0.f));
				Border->SetBrushColor(FLinearColor::White);
			}
			else if (Name == TEXT("Border_Panel"))
			{
				Border->SetBrush(Theme.MakePanelBrush(Theme.PanelColor));
				Border->SetBrushColor(FLinearColor::White);
			}
		}
	});

	// The rule AddTitleRule put under the title, if any, takes the theme's colour and the title's visibility.
	for (UTextBlock* Title : Titles)
	{
		if (UImage* Rule = Cast<UImage>(Widget.WidgetTree->FindWidget(TitleRuleName)))
		{
			StyleTitleRule(Theme, *Rule, *Title);
		}
	}
}

void StdUITheme::AddTitleRule(const UStdUITheme& Theme, UUserWidget& Widget)
{
	if (!Theme.IsFramed() || Theme.TitleRuleThickness <= 0.f || Widget.IsDesignTime() || !Widget.WidgetTree)
	{
		return;
	}
	if (UTextBlock* Title = Cast<UTextBlock>(Widget.WidgetTree->FindWidget(TEXT("Text_Title"))))
	{
		StdUIThemeDetail::InsertTitleRule(Theme, Widget, *Title);
	}
}

void StdUITheme::SetTabSelected(UButton* TabButton, bool bSelected)
{
	if (TabButton)
	{
		if (const UStdUITheme* Theme = Get())
		{
			TabButton->SetStyle(Theme->MakeTabStyle(bSelected));
		}
	}
}

void StdUITheme::SetKeySelectorStyle(UInputKeySelector* KeySelector, const FButtonStyle& ButtonStyle)
{
	if (KeySelector)
	{
		KeySelector->SetButtonStyle(ButtonStyle);
		KeySelector->SetButtonStyle(KeySelector->GetButtonStyle());
	}
}

void UStdUIThemeLibrary::ApplyStdUITheme(UUserWidget* Widget)
{
	if (Widget)
	{
		if (const UStdUITheme* Theme = StdUITheme::Get())
		{
			StdUITheme::ApplyToWidget(*Theme, *Widget);
		}
	}
}
