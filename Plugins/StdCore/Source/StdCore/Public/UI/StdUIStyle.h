// Copyright 2026 <OWNER>. All rights reserved. See Plugins/StdCore/LICENSE.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Layout/Margin.h"
#include "Styling/SlateBrush.h"
#include "StdUIStyle.generated.h"

class UTexture2D;

/**
 * The look of every menu, prompt, key cap and text Std Core draws, as plain values a project sets as text in its config
 * (UStdProjectSettings::Style, used while UStdProjectSettings::bUseStyle is on): one font with its sizes, a few colours
 * and the shape of panels and buttons (flat fills, a thin frame line with one corner cut at 45 degrees).
 *
 * While the style is on, StdUITheme::Get() returns a theme built from these values instead of the Theme asset, so every
 * screen themed by name (StdUITheme::ApplyToWidget) takes it; StdUIStyle::MakeFont and StdUIStyle::MakeBoxBrush give the
 * same font and frame to what a project draws itself. Off (the default): everything looks as before.
 */
USTRUCT(BlueprintType)
struct STDCORE_API FStdUIStyle
{
	GENERATED_BODY()

	FStdUIStyle();

	/** The one font of every text. Empty: the engine's default font. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Style|Font", meta = (AllowedClasses = "/Script/Engine.Font"))
	FSoftObjectPath Font;

	/** Typeface of Font used by every text (a font with one typeface uses it whatever this names). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Style|Font")
	FName Typeface = TEXT("Regular");

	/** Text_Title. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Style|Font", meta = (ClampMin = "8", ClampMax = "96"))
	int32 TitleSize = 40;

	/** Text_Heading* and the section headers of the settings screen. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Style|Font", meta = (ClampMin = "8", ClampMax = "96"))
	int32 HeadingSize = 22;

	/** Other texts, key prompts. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Style|Font", meta = (ClampMin = "8", ClampMax = "96"))
	int32 BodySize = 18;

	/** Button labels and key selectors. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Style|Font", meta = (ClampMin = "8", ClampMax = "96"))
	int32 ButtonSize = 18;

	/** Letter spacing of titles, headings and button labels, in thousandths of an em (Slate's unit). Body text has none. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Style|Font", meta = (ClampMin = "0", ClampMax = "1000"))
	int32 LabelLetterSpacing = 0;

	/** Titles, headings and button labels in capitals (the text itself is unchanged). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Style|Font")
	bool bUpperCaseLabels = false;

	/** Focus, hover and pressed fills, the selected tab's frame, slider thumbs. StdUIStyle::SetAccentColor replaces it at runtime. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Style|Colours")
	FLinearColor AccentColor;

	/** Panels (Border_Panel) and the label on an accent fill. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Style|Colours")
	FLinearColor PanelColor;

	/** The full-screen layer behind a menu (Border_Background). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Style|Colours")
	FLinearColor BackdropColor;

	/** The frame line of panels and buttons and the rule under a title. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Style|Colours")
	FLinearColor FrameColor;

	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Style|Colours")
	FLinearColor TextColor;

	/** Secondary texts, unselected tabs, slider bars. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Style|Colours")
	FLinearColor MutedTextColor;

	/** Width of the frame line, Slate units. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Style|Shape", meta = (ClampMin = "0.5", ClampMax = "8.0"))
	float FrameThickness = 1.5f;

	/** Length of the legs of the 45-degree cut at a panel's or a button's top right corner, Slate units (0: square). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Style|Shape", meta = (ClampMin = "0.0", ClampMax = "64.0"))
	float CornerCut = 10.f;

	/** Height of the rule under a title, Slate units (0: none). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Style|Shape", meta = (ClampMin = "0.0", ClampMax = "8.0"))
	float TitleRuleThickness = 1.f;

	/** Space around a button's label; the left one leaves room for the focus marker. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category = "Style|Shape")
	FMargin ButtonPadding = FMargin(28.f, 10.f, 16.f, 10.f);
};

namespace StdUIStyle
{
	/** UStdProjectSettings::bUseStyle. */
	STDCORE_API bool IsEnabled();

	/** The style of the project settings. */
	STDCORE_API const FStdUIStyle& Get();

	/**
	 * The accent a project gives at runtime, in place of the style's AccentColor (for a colour the project owns elsewhere).
	 * Every theme and brush built after the call uses it.
	 */
	STDCORE_API void SetAccentColor(const FLinearColor& Color);
	STDCORE_API void ClearAccentColor();

	/** The accent in use: the one set at runtime, else the style's. */
	STDCORE_API FLinearColor GetAccentColor();

	/** Bumped by every change that makes built themes stale (the accent). */
	STDCORE_API uint32 GetVersion();

	/** The style's font object, or null (the engine's default font). */
	STDCORE_API const UObject* GetFontObject();

	/**
	 * A font of Size for text a project draws itself: with the style on, the style's font and typeface; else the Theme
	 * asset's body font when there is one; else the engine's default font. LetterSpacing in thousandths of an em.
	 */
	STDCORE_API FSlateFontInfo MakeFont(int32 Size, int32 LetterSpacing = 0);

	/**
	 * A brush filled with Fill inside a frame line of Frame, Thickness wide, with the top right corner cut at 45 degrees
	 * (legs of Cut), drawn as a box so the line and the cut keep their size at any widget size. With bMarker, a small
	 * vertical bar in MarkerColor near the left edge, as tall as the brush less its corners. Built once for each set of
	 * values from a texture made in code (no asset).
	 */
	STDCORE_API FSlateBrush MakeFrameBrush(const FLinearColor& Fill, const FLinearColor& Frame, float Thickness, float Cut, bool bMarker = false,
		const FLinearColor& MarkerColor = FLinearColor::Black);

	/** The box of a prompt or a message filled with Fill: with the style on, the style's frame and cut; else a box with rounded corners (Radius). */
	STDCORE_API FSlateBrush MakeBoxBrush(const FLinearColor& Fill, float Radius = 4.f);

	/** One font's name and size, as "<object name>/<typeface> <size>" (tests and reports). */
	STDCORE_API FString DescribeFont(const FSlateFontInfo& Font);

	/**
	 * A texture of fine grey noise (each texel an independent random level, uniform from 0 to 255), Width x Height, made
	 * once in code and kept (no asset). Shown through a moving UV region it reads as animated static.
	 */
	STDCORE_API UTexture2D* GetNoiseTexture();

	/**
	 * A white square texture whose alpha is 1 in its middle and falls smoothly to 0 at each edge (Size texels), made once
	 * in code: drawn as a box with margins 0.5 it gives a patch with a soft edge of a fixed width on every side.
	 */
	STDCORE_API UTexture2D* GetSoftPatchTexture();
}

UCLASS()
class STDCORE_API UStdUIStyleLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Replaces the style's accent at runtime (Project Settings > Std Core > Style). */
	UFUNCTION(BlueprintCallable, Category = "Std Core|UI", meta = (DisplayName = "Set Std UI Accent Color"))
	static void SetStdUIAccentColor(const FLinearColor& Color) { StdUIStyle::SetAccentColor(Color); }

	/** A font of Size in the style's font (or the theme's or the engine's when the style is off). */
	UFUNCTION(BlueprintPure, Category = "Std Core|UI", meta = (DisplayName = "Make Std UI Font"))
	static FSlateFontInfo MakeStdUIFont(int32 Size) { return StdUIStyle::MakeFont(Size); }
};
