#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Fonts/SlateFontInfo.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Styling/SlateTypes.h"
#include "StdUITheme.generated.h"

class UButton;
class UInputKeySelector;
class USoundBase;
class UUserWidget;
struct FStdUIStyle;

/** How panels and buttons are drawn. */
UENUM(BlueprintType)
enum class EStdUIShape : uint8
{
	/** Filled boxes with rounded corners (CornerRadius). */
	Rounded,
	/**
	 * Flat fills inside a thin frame line (FrameThickness, FrameColor) with the top right corner cut at 45 degrees
	 * (CornerCut). Buttons have no fill at rest and a left-aligned label; with the focus or the pointer they fill with
	 * the accent and show a small marker at their left; disabled, their frame and label are at a third of their level.
	 */
	Framed
};

/**
 * The look of every Std Core menu in one asset: a few colors, a corner radius and four fonts. The button, slider and
 * key selector styles are built from them, so restyling the menus means editing this asset only.
 *
 * Applied by name and type (see StdUITheme::ApplyToWidget): buttons, tab buttons (Button_Tab*), button labels,
 * sliders, key selectors, Text_Title, Text_Heading*, Text_Version, other texts, Border_Background and Border_Panel.
 */
UCLASS(BlueprintType)
class STDCORE_API UStdUITheme : public UDataAsset
{
	GENERATED_BODY()

public:
	UStdUITheme();

	// Colors

	/** Focus, hover, pressed and the selected tab. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor AccentColor;

	/** Menu panels (Border_Panel). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor PanelColor;

	/** Full-screen layer behind a menu (Border_Background). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor BackdropColor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor ButtonColor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor TextColor;

	/** Secondary texts (Text_Version, unselected tabs) and slider bars. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor MutedTextColor;

	/** Labels over the accent color (focused, hovered or pressed buttons, the selected tab). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Colors")
	FLinearColor TextOnAccentColor;

	// Shape

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape", meta = (ClampMin = "0.0"))
	float CornerRadius = 6.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape")
	FMargin ButtonPadding = FMargin(16.f, 10.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape")
	EStdUIShape Shape = EStdUIShape::Rounded;

	/** Framed: the frame line of panels and buttons and the rule under a title. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape")
	FLinearColor FrameColor = FLinearColor(0.3f, 0.3f, 0.3f, 1.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape", meta = (ClampMin = "0.5", ClampMax = "8.0"))
	float FrameThickness = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape", meta = (ClampMin = "0.0", ClampMax = "64.0"))
	float CornerCut = 10.f;

	/** Framed: height of a thin rule shown under Text_Title (0: none). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Shape", meta = (ClampMin = "0.0", ClampMax = "8.0"))
	float TitleRuleThickness = 0.f;

	/** Titles, headings and button labels in capitals. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fonts")
	bool bUpperCaseLabels = false;

	// Fonts

	/** Text_Title. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fonts")
	FSlateFontInfo TitleFont;

	/** Text_Heading* and the category headers of the Controls tab. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fonts")
	FSlateFontInfo HeadingFont;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fonts")
	FSlateFontInfo BodyFont;

	/** Button labels and key selectors. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fonts")
	FSlateFontInfo ButtonFont;

	// Sounds

	/** Optional. Give it the SC_UI Sound Class so it plays while paused. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sounds")
	TObjectPtr<USoundBase> HoverSound;

	/** Optional. Give it the SC_UI Sound Class so it plays while paused. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sounds")
	TObjectPtr<USoundBase> PressSound;

	/** Takes every value of Style, with Accent as the accent (Framed shape). */
	void InitFromStyle(const FStdUIStyle& Style, const FLinearColor& Accent);

	bool IsFramed() const { return Shape == EStdUIShape::Framed; }

	FSlateBrush MakeRoundedBrush(const FLinearColor& Color, float Radius) const;

	/** A panel's brush: Rounded, a rounded box of Fill; Framed, Fill inside the frame line with the cut corner. */
	FSlateBrush MakePanelBrush(const FLinearColor& Fill) const;
	FButtonStyle MakeButtonStyle() const;
	FButtonStyle MakeTabStyle(bool bSelected) const;
	FSliderStyle MakeSliderStyle() const;
	FTextBlockStyle MakeTextStyle(const FSlateFontInfo& Font, const FLinearColor& Color) const;
};

namespace StdUITheme
{
	/**
	 * With the style on (UStdProjectSettings::bUseStyle), a theme built from UStdProjectSettings::Style (kept, rebuilt
	 * when the style's accent changes); else the theme of UStdProjectSettings, or null when the setting is empty or the
	 * asset is missing (warned once).
	 */
	STDCORE_API const UStdUITheme* Get();

	/** Name of the thin rule AddTitleRule puts under Text_Title (Framed, TitleRuleThickness above 0). */
	inline const TCHAR* TitleRuleName = TEXT("Image_TitleRule");

	/**
	 * Puts the thin rule under Widget's Text_Title when it sits in a vertical box (Framed, TitleRuleThickness above 0),
	 * once, the title's gap below moved under the rule. Call it before the widget's Slate tree is built (a screen does it
	 * when it is initialized); ApplyToWidget then keeps its colour and shows it with the title.
	 */
	STDCORE_API void AddTitleRule(const UStdUITheme& Theme, UUserWidget& Widget);

	/** Applies the theme to every widget of Widget's tree. Nested user widgets apply it themselves. */
	STDCORE_API void ApplyToWidget(const UStdUITheme& Theme, UUserWidget& Widget);

	/** Restyles a Button_Tab* button as selected or not. Does nothing without a theme. */
	STDCORE_API void SetTabSelected(UButton* TabButton, bool bSelected);

	/**
	 * UInputKeySelector::SetButtonStyle gives Slate a pointer to its argument, which dangles once a temporary style
	 * goes out of scope: this sets the style, then hands Slate the copy the widget keeps.
	 */
	STDCORE_API void SetKeySelectorStyle(UInputKeySelector* KeySelector, const FButtonStyle& ButtonStyle);
}

UCLASS()
class STDCORE_API UStdUIThemeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Applies the Std Core UI theme (Project Settings > Std Core > Theme) to a widget of the game. Call it in Pre Construct. */
	UFUNCTION(BlueprintCallable, Category = "Std Core|UI", meta = (DisplayName = "Apply Std UI Theme", DefaultToSelf = "Widget"))
	static void ApplyStdUITheme(UUserWidget* Widget);
};
