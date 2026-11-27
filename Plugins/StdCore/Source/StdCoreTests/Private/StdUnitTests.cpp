// Logic that needs no world: the volume formula, volume clamping, the video options (defaults, choices, clamping,
// hidden rows, a settings file saved before they existed), the styles built by the UI theme, the radial progress, the barcode text,
// the key prompt without a player, the order of the Controls tab rows, the wrapping of the covered screen's label, the
// covered screen's progress bar and marker (switched off, shape, progress, marker over simulated ticks).

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Input/StdInputSubsystem.h"
#include "Settings/StdSettingsSubsystem.h"
#include "Settings/StdUserSettings.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Flow/StdFlowSubsystem.h"
#include "Flow/StdTransitionProgress.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Settings/StdProjectSettings.h"
#include "Styling/SlateColor.h"
#include "Widgets/SWidget.h"
#include "UI/StdBarcodeText.h"
#include "UI/StdKeyPromptWidget.h"
#include "UI/StdRadialProgress.h"
#include "UI/StdUITheme.h"
#include "UObject/Package.h"

namespace StdUnitTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStdVolumesTest, "StdCore.Settings.Volumes", StdUnitTests::Flags)

bool FStdVolumesTest::RunTest(const FString& Parameters)
{
	const FStdClassVolumes Volumes = StdAudio::ComputeClassVolumes(0.5f, 0.8f, 1.f);
	TestEqual(TEXT("SC_Master stays at 1"), Volumes.Master, 1.f);
	TestEqual(TEXT("SC_Music = Music x Master"), Volumes.Music, 0.4f);
	TestEqual(TEXT("SC_SFX = SFX x Master"), Volumes.Sfx, 0.5f);
	TestEqual(TEXT("SC_UI = Master"), Volumes.Ui, 0.5f);

	const FStdClassVolumes Muted = StdAudio::ComputeClassVolumes(0.f, 1.f, 1.f);
	TestEqual(TEXT("Master 0 silences music"), Muted.Music, 0.f);
	TestEqual(TEXT("Master 0 silences effects"), Muted.Sfx, 0.f);
	TestEqual(TEXT("Master 0 silences the UI"), Muted.Ui, 0.f);

	// A transient instance: the class default object holds the player's saved values.
	UStdUserSettings* UserSettings = NewObject<UStdUserSettings>(GetTransientPackage());
	UserSettings->SetMasterVolume(1.5f);
	TestEqual(TEXT("Volumes above 1 are clamped"), UserSettings->GetMasterVolume(), 1.f);
	UserSettings->SetMusicVolume(-0.2f);
	TestEqual(TEXT("Volumes below 0 are clamped"), UserSettings->GetMusicVolume(), 0.f);
	UserSettings->SetSfxVolume(0.35f);
	TestEqual(TEXT("Volumes in range are kept"), UserSettings->GetSfxVolume(), 0.35f);
	return true;
}

namespace StdUnitTests
{
	/** The project's video defaults and hidden rows, set for one test and put back when it ends. */
	struct FVideoProjectScope
	{
		UStdProjectSettings* Settings = GetMutableDefault<UStdProjectSettings>();
		int32 Limit = Settings->DefaultFrameRateLimit;
		bool bUnlimited = Settings->bDefaultFrameRateUnlimited;
		int32 Method = Settings->DefaultAntiAliasingMethod;
		bool bVSync = Settings->bDefaultVSync;
		TArray<EStdVideoOption> Hidden = Settings->HiddenVideoOptions;

		~FVideoProjectScope()
		{
			Settings->DefaultFrameRateLimit = Limit;
			Settings->bDefaultFrameRateUnlimited = bUnlimited;
			Settings->DefaultAntiAliasingMethod = Method;
			Settings->bDefaultVSync = bVSync;
			Settings->HiddenVideoOptions = Hidden;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStdVideoOptionsTest, "StdCore.Settings.VideoOptions", StdUnitTests::Flags)

bool FStdVideoOptionsTest::RunTest(const FString& Parameters)
{
	StdUnitTests::FVideoProjectScope Scope;
	UStdProjectSettings* Settings = Scope.Settings;
	Settings->DefaultFrameRateLimit = 90;
	Settings->bDefaultFrameRateUnlimited = false;
	Settings->DefaultAntiAliasingMethod = 1;
	Settings->bDefaultVSync = true;
	Settings->HiddenVideoOptions.Reset();

	// A transient instance: the class default object holds the player's saved values.
	UStdUserSettings* User = NewObject<UStdUserSettings>(GetTransientPackage());
	User->ResetToDefaults();
	TestFalse(TEXT("Nothing chosen after a reset"), User->HasChosen(EStdVideoOption::FrameRateLimit) || User->HasChosen(EStdVideoOption::AntiAliasing) || User->HasChosen(EStdVideoOption::VSync));
	TestEqual(TEXT("Not chosen: the project's frame-rate limit"), User->GetFrameRateLimit(), 90);
	TestFalse(TEXT("Not chosen: the project's Unlimited (off)"), User->IsFrameRateUnlimited());
	TestEqual(TEXT("Not chosen: the project's anti-aliasing"), User->GetAntiAliasingMethod(), 1);
	TestTrue(TEXT("Not chosen: the project's V-Sync"), User->IsVSyncEnabled());

	Settings->DefaultFrameRateLimit = 144;
	TestEqual(TEXT("A default changed is followed while nothing is chosen"), User->GetFrameRateLimit(), 144);
	Settings->DefaultAntiAliasingMethod = -1;
	TestEqual(TEXT("Default -1: the project's own renderer method"), User->GetAntiAliasingMethod(), UStdUserSettings::GetProjectAntiAliasingMethod());

	User->SetFrameRateLimit(30);
	User->SetFrameRateUnlimited(true);
	User->SetAntiAliasingMethod(2);
	User->SetVSyncEnabled(false);
	TestEqual(TEXT("Chosen limit"), User->GetFrameRateLimit(), 30);
	TestTrue(TEXT("Chosen Unlimited"), User->IsFrameRateUnlimited());
	TestEqual(TEXT("Chosen anti-aliasing"), User->GetAntiAliasingMethod(), 2);
	TestFalse(TEXT("Chosen V-Sync off"), User->IsVSyncEnabled());
	TestTrue(TEXT("The limit is kept while unlimited"), User->HasChosen(EStdVideoOption::FrameRateLimit) && User->GetFrameRateLimit() == 30);

	User->SetFrameRateLimit(3);
	TestEqual(TEXT("A limit below 10 is clamped"), User->GetFrameRateLimit(), 10);
	User->SetFrameRateLimit(5000);
	TestEqual(TEXT("A limit above 1000 is clamped"), User->GetFrameRateLimit(), 1000);
	User->SetAntiAliasingMethod(9);
	TestEqual(TEXT("A method above 4 is clamped"), User->GetAntiAliasingMethod(), 4);

	// A hidden row: its option stays at the project's default whatever is stored.
	Settings->DefaultFrameRateLimit = 60;
	Settings->bDefaultFrameRateUnlimited = false;
	Settings->DefaultAntiAliasingMethod = 0;
	Settings->bDefaultVSync = true;
	Settings->HiddenVideoOptions = { EStdVideoOption::FrameRateLimit, EStdVideoOption::AntiAliasing, EStdVideoOption::VSync };
	TestEqual(TEXT("Hidden frame-rate row: the default limit"), User->GetFrameRateLimit(), 60);
	TestFalse(TEXT("Hidden frame-rate row: the default Unlimited"), User->IsFrameRateUnlimited());
	TestEqual(TEXT("Hidden anti-aliasing row: the default method"), User->GetAntiAliasingMethod(), 0);
	TestTrue(TEXT("Hidden V-Sync row: the default V-Sync"), User->IsVSyncEnabled());
	TestFalse(TEXT("Hidden: not chosen"), User->HasChosen(EStdVideoOption::AntiAliasing));
	Settings->HiddenVideoOptions.Reset();
	TestEqual(TEXT("Shown again: the stored choice"), User->GetAntiAliasingMethod(), 4);

	// The choices the screen offers.
	const TArray<int32> Choices = UStdSettingsSubsystem::GetAntiAliasingChoices();
	TestTrue(TEXT("Off, FXAA and TAA first"), Choices.Num() >= 3 && Choices[0] == 0 && Choices[1] == 1 && Choices[2] == 2);
	TestTrue(TEXT("The project's own method is offered"), Choices.Contains(UStdUserSettings::GetProjectAntiAliasingMethod()));
	TestTrue(TEXT("The project offers frame-rate limits"), GetDefault<UStdProjectSettings>()->FrameRateLimits.Num() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStdVideoOldFileTest, "StdCore.Settings.VideoOldFile", StdUnitTests::Flags)

bool FStdVideoOldFileTest::RunTest(const FString& Parameters)
{
	StdUnitTests::FVideoProjectScope Scope;
	Scope.Settings->DefaultFrameRateLimit = 60;
	Scope.Settings->bDefaultFrameRateUnlimited = false;
	Scope.Settings->DefaultAntiAliasingMethod = -1;
	Scope.Settings->bDefaultVSync = false;
	Scope.Settings->HiddenVideoOptions.Reset();

	// A settings file as the plugin wrote it before the video options existed: volumes only.
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("StdCoreVideoOldFile.ini")));
	const FString Text = TEXT("[/Script/Engine.GameUserSettings]\r\nbUseVSync=False\r\n\r\n[/Script/StdCore.StdUserSettings]\r\nMasterVolume=0.250000\r\nMusicVolume=0.500000\r\nSfxVolume=0.750000\r\n");
	if (!TestTrue(TEXT("The old file is written"), FFileHelper::SaveStringToFile(Text, *Path)))
	{
		return false;
	}

	UStdUserSettings* User = NewObject<UStdUserSettings>(GetTransientPackage());
	User->ResetToDefaults();
	User->SetFrameRateLimit(240);
	User->SetAntiAliasingMethod(2);
	// As a fresh process: everything at its default, then the file.
	User->ResetToDefaults();
	User->LoadConfig(nullptr, *Path);
	GConfig->UnloadFile(Path);
	IFileManager::Get().Delete(*Path);

	TestEqual(TEXT("Old file: master volume kept"), User->GetMasterVolume(), 0.25f);
	TestEqual(TEXT("Old file: music volume kept"), User->GetMusicVolume(), 0.5f);
	TestEqual(TEXT("Old file: effects volume kept"), User->GetSfxVolume(), 0.75f);
	TestFalse(TEXT("Old file: no video option chosen"), User->HasChosen(EStdVideoOption::FrameRateLimit) || User->HasChosen(EStdVideoOption::AntiAliasing) || User->HasChosen(EStdVideoOption::VSync));
	TestEqual(TEXT("Old file: the default frame-rate limit"), User->GetFrameRateLimit(), 60);
	TestFalse(TEXT("Old file: limited by default"), User->IsFrameRateUnlimited());
	TestEqual(TEXT("Old file: the project's anti-aliasing"), User->GetAntiAliasingMethod(), UStdUserSettings::GetProjectAntiAliasingMethod());
	TestFalse(TEXT("Old file: V-Sync off"), User->IsVSyncEnabled());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStdThemeStylesTest, "StdCore.Theme.Styles", StdUnitTests::Flags)

bool FStdThemeStylesTest::RunTest(const FString& Parameters)
{
	const UStdUITheme* Theme = GetDefault<UStdUITheme>();

	const FButtonStyle Button = Theme->MakeButtonStyle();
	TestTrue(TEXT("Buttons use rounded boxes"), Button.Normal.DrawAs == ESlateBrushDrawType::RoundedBox);
	TestEqual(TEXT("Button normal color"), Button.Normal.TintColor.GetSpecifiedColor(), Theme->ButtonColor);
	TestEqual(TEXT("Button hovered color is the accent"), Button.Hovered.TintColor.GetSpecifiedColor(), Theme->AccentColor);
	TestEqual(TEXT("Button label over the accent"), Button.HoveredForeground.GetSpecifiedColor(), Theme->TextOnAccentColor);

	const FButtonStyle Selected = Theme->MakeTabStyle(true);
	const FButtonStyle Unselected = Theme->MakeTabStyle(false);
	TestEqual(TEXT("The selected tab is filled"), Selected.Normal.TintColor.GetSpecifiedColor(), Theme->ButtonColor);
	TestEqual(TEXT("An unselected tab has no fill"), Unselected.Normal.TintColor.GetSpecifiedColor().A, 0.f);
	TestEqual(TEXT("Tabs show the accent on hover and focus"), Unselected.Hovered.TintColor.GetSpecifiedColor(), Theme->AccentColor);

	const FSliderStyle Slider = Theme->MakeSliderStyle();
	TestEqual(TEXT("Slider thumb in the accent color"), Slider.NormalThumbImage.TintColor.GetSpecifiedColor(), Theme->AccentColor);
	TestTrue(TEXT("Slider thumb has a size"), Slider.NormalThumbImage.ImageSize.X > 0.f);

	TestNotNull(TEXT("The default fonts point to the engine's Roboto"), Theme->BodyFont.FontObject.Get());

	const FCheckBoxStyle CheckBox = Theme->MakeCheckBoxStyle();
	TestTrue(TEXT("A check box is a box, not a toggle button"), CheckBox.CheckBoxType == ESlateCheckBoxType::CheckBox);
	TestTrue(TEXT("The check box has a size"), CheckBox.UncheckedImage.ImageSize.X > 0.f && CheckBox.CheckedImage.ImageSize.X > 0.f);
	TestEqual(TEXT("A ticked box is filled with the accent"), CheckBox.CheckedImage.TintColor.GetSpecifiedColor(), Theme->AccentColor);
	TestEqual(TEXT("An unticked box has no fill"), CheckBox.UncheckedImage.TintColor.GetSpecifiedColor().A, 0.f);
	TestEqual(TEXT("The focused box's frame takes the accent"), CheckBox.UncheckedHoveredImage.OutlineSettings.Color.GetSpecifiedColor(), Theme->AccentColor);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStdRadialProgressTest, "StdCore.UI.RadialProgress", StdUnitTests::Flags)

bool FStdRadialProgressTest::RunTest(const FString& Parameters)
{
	UStdRadialProgress* Ring = NewObject<UStdRadialProgress>(GetTransientPackage());
	TestFalse(TEXT("Starts empty"), Ring->IsFull());

	Ring->SetProgress(1.5f);
	TestEqual(TEXT("Progress above 1 is clamped"), Ring->Progress, 1.f);
	TestTrue(TEXT("Full at 1"), Ring->IsFull());
	Ring->SetProgress(-0.5f);
	TestEqual(TEXT("Progress below 0 is clamped"), Ring->Progress, 0.f);
	Ring->SetProgress(0.999f);
	TestFalse(TEXT("Not full just below 1"), Ring->IsFull());

	Ring->SetRadius(20.f);
	Ring->SetThickness(4.f);
	TestEqual(TEXT("Outer diameter = 2 x radius + thickness"), Ring->GetOuterDiameter(), 44.f);
	Ring->SetThickness(0.f);
	TestEqual(TEXT("Thickness is clamped to 0.5"), Ring->Thickness, 0.5f);

	// Builds the Slate widget and pushes the properties to it without a viewport.
	const TSharedRef<SWidget> Slate = Ring->TakeWidget();
	Slate->SlatePrepass(1.f);
	TestEqual(TEXT("Slate desired size is the outer diameter"), static_cast<float>(Slate->GetDesiredSize().X), Ring->GetOuterDiameter());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStdBarcodeTextTest, "StdCore.UI.BarcodeText", StdUnitTests::Flags)

bool FStdBarcodeTextTest::RunTest(const FString& Parameters)
{
	using namespace StdBarcode;

	// Patterns: deterministic, never empty for a printable character, distinct between characters, empty for a space.
	TestTrue(TEXT("The same character always gives the same bars"), GetBarMask(TEXT('k')) == GetBarMask(TEXT('k')));
	TestTrue(TEXT("Two different letters give different bars (a, b)"), GetBarMask(TEXT('a')) != GetBarMask(TEXT('b')));
	TestTrue(TEXT("Case matters (A, a)"), GetBarMask(TEXT('A')) != GetBarMask(TEXT('a')));
	TSet<uint32> Seen;
	int32 EmptyPrintable = 0;
	for (TCHAR Character = 33; Character <= 126; ++Character)
	{
		Seen.Add(GetBarMask(Character));
		EmptyPrintable += GetBarMask(Character) == 0 ? 1 : 0;
	}
	TestEqual(TEXT("The 94 printable ASCII characters give 94 different patterns"), Seen.Num(), 94);
	TestEqual(TEXT("No printable character is drawn empty"), EmptyPrintable, 0);
	TestTrue(TEXT("A space has no bars"), GetBarMask(TEXT(' ')) == 0u);
	TestTrue(TEXT("A space is a gap"), IsGap(TEXT(' ')));

	// The same character in two places of a text is drawn the same: the pattern does not depend on the position.
	TestTrue(TEXT("The pattern of a character is a function of the character only"), GetBarMask(FString(TEXT("level"))[0]) == GetBarMask(FString(TEXT("level"))[4]));

	// Counts: N cells shown, the first M of them characters, spaces as gaps.
	const FString Sample = TEXT("THE QUICK BROWN FOX JUMPS");
	const auto NonGapIn = [&Sample](int32 From, int32 To)
	{
		int32 Count = 0;
		for (int32 Index = From; Index < To; ++Index)
		{
			Count += IsGap(Sample[Index]) ? 0 : 1;
		}
		return Count;
	};
	for (const FIntPoint& NM : { FIntPoint(10, 4), FIntPoint(25, 0), FIntPoint(25, 25), FIntPoint(0, 0), FIntPoint(17, 11) })
	{
		const FDrawCounts Counts = CountDrawn(Sample, 40, NM.X, NM.Y);
		TestEqual(FString::Printf(TEXT("N=%d M=%d: exactly N cells shown"), NM.X, NM.Y), Counts.Total(), NM.X);
		TestEqual(FString::Printf(TEXT("N=%d M=%d: the first M are characters"), NM.X, NM.Y), Counts.Characters, NonGapIn(0, NM.Y));
		TestEqual(FString::Printf(TEXT("N=%d M=%d: the rest are bars"), NM.X, NM.Y), Counts.Bars, NonGapIn(NM.Y, NM.X));
		TestEqual(FString::Printf(TEXT("N=%d M=%d: spaces are gaps"), NM.X, NM.Y), Counts.Gaps, (NM.X - NonGapIn(0, NM.X)));
	}
	TestEqual(TEXT("M above N is clamped to N"), CountDrawn(Sample, 40, 6, 99).Characters, NonGapIn(0, 6));
	TestEqual(TEXT("N above the length is clamped"), CountDrawn(Sample, 40, 999, 0).Total(), Sample.Len());

	// Wrapping on the grid.
	const FString Long = TEXT("Every line of this sample wraps on a grid of twenty cells, words move whole to the next line, and nothing ever leaves the grid at all.");
	TArray<FCellPosition> Cells;
	int32 Rows = 0;
	Layout(Long, 20, Cells, Rows);
	TestTrue(FString::Printf(TEXT("A %d-character text wraps onto several lines at 20 columns (%d lines)"), Long.Len(), Rows), Rows >= FMath::DivideAndRoundUp(Long.Len(), 20));
	bool bInGrid = true;
	bool bReadingOrder = true;
	bool bWordsWhole = true;
	for (int32 Index = 0; Index < Long.Len(); ++Index)
	{
		if (!IsGap(Long[Index]))
		{
			bInGrid &= Cells[Index].Column >= 0 && Cells[Index].Column < 20;
			if (Index > 0 && !IsGap(Long[Index - 1]))
			{
				// Inside a word (every word here is shorter than a line): the next cell on the same line.
				bWordsWhole &= Cells[Index].Row == Cells[Index - 1].Row && Cells[Index].Column == Cells[Index - 1].Column + 1;
			}
		}
		if (Index > 0)
		{
			bReadingOrder &= Cells[Index].Row > Cells[Index - 1].Row || (Cells[Index].Row == Cells[Index - 1].Row && Cells[Index].Column > Cells[Index - 1].Column)
				|| (IsGap(Long[Index]) && Cells[Index].Column == 20);
		}
	}
	TestTrue(TEXT("Every drawn cell lies inside the 20 columns"), bInGrid);
	TestTrue(TEXT("No word shorter than a line is split"), bWordsWhole);
	TestTrue(TEXT("Cells follow reading order"), bReadingOrder);
	Layout(TEXT("ABCDEFGHIJKLMNOPQRSTUVWXYZ"), 10, Cells, Rows);
	TestEqual(TEXT("A word longer than a line breaks at the line's end (26 letters at 10 columns: 3 lines)"), Rows, 3);
	TestEqual(TEXT("... the 11th letter starts the second line"), Cells[10].Row * 100 + Cells[10].Column, 100);

	// The widget: counts resolved and clamped, desired size on the grid.
	UStdBarcodeText* Widget = NewObject<UStdBarcodeText>(GetTransientPackage());
	Widget->SetText(FText::FromString(Sample));
	Widget->SetCellSize(FVector2D(10.0, 20.0));
	Widget->SetLineSpacing(4.f);
	Widget->SetColumns(10);
	Widget->SetCounts(-1, 0);
	TestEqual(TEXT("Widget: shown -1 = every cell"), Widget->GetShownCells(), Sample.Len());
	TestEqual(TEXT("Widget: translated 0 = all bars"), Widget->GetDrawCounts().Characters, 0);
	Widget->SetCounts(5, 9);
	TestEqual(TEXT("Widget: translated clamped to the shown cells"), Widget->GetTranslatedCells(), 5);
	Widget->SetCounts(-1, -1);
	TestEqual(TEXT("Widget: translated -1 = every shown cell is a character"), Widget->GetDrawCounts().Bars, 0);
	const int32 Lines = Widget->GetLineCount();
	TestEqual(TEXT("Widget: the sample takes 3 lines at 10 columns"), Lines, 3);
	TestTrue(FString::Printf(TEXT("Widget: content size is the used grid (%s)"), *Widget->GetContentSize().ToString()), Widget->GetContentSize().Equals(FVector2D(10.0 * 10.0, 3 * 20.0 + 2 * 4.0), 0.01));
	const TSharedRef<SWidget> Slate = Widget->TakeWidget();
	Slate->SlatePrepass(1.f);
	TestTrue(TEXT("Widget: Slate desired size is the content size"), FVector2D(Slate->GetDesiredSize()).Equals(Widget->GetContentSize(), 0.01));
	Widget->SetSizeToShown(true);
	Widget->SetCounts(3, 0);
	TestTrue(FString::Printf(TEXT("Widget: sized to the shown cells, one line of 3 cells (%s)"), *Widget->GetContentSize().ToString()), Widget->GetContentSize().Equals(FVector2D(30.0, 20.0), 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStdKeyPromptTest, "StdCore.UI.KeyPrompt", StdUnitTests::Flags)

bool FStdKeyPromptTest::RunTest(const FString& Parameters)
{
	UStdKeyPromptWidget* Prompt = NewObject<UStdKeyPromptWidget>(GetTransientPackage());
	TestTrue(TEXT("Builds its own tree"), Prompt->Initialize());
	TestNotNull(TEXT("Has a root widget"), Prompt->WidgetTree ? Prompt->WidgetTree->RootWidget.Get() : nullptr);

	UTextBlock* Key = Prompt->WidgetTree ? Cast<UTextBlock>(Prompt->WidgetTree->FindWidget(TEXT("Text_Key"))) : nullptr;
	UTextBlock* Label = Prompt->WidgetTree ? Cast<UTextBlock>(Prompt->WidgetTree->FindWidget(TEXT("Text_Label"))) : nullptr;
	TestNotNull(TEXT("Has Text_Key"), Key);
	TestNotNull(TEXT("Has Text_Label"), Label);

	Prompt->RefreshKey();
	TestFalse(TEXT("No action, no player: no key"), Prompt->GetDisplayedKey().IsValid());
	TestEqual(TEXT("No key reads Unbound"), Prompt->GetKeyText().ToString(), FString(TEXT("Unbound")));
	if (Key)
	{
		TestEqual(TEXT("The cap shows the key text"), Key->GetText().ToString(), Prompt->GetKeyText().ToString());
	}

	TestFalse(TEXT("No fixed key by default"), Prompt->FixedKey.IsValid());
	Prompt->SetFixedKey(EKeys::Enter);
	TestEqual(TEXT("A fixed key is shown"), Prompt->GetDisplayedKey(), EKeys::Enter);
	TestEqual(TEXT("A fixed key reads its short name"), Prompt->GetKeyText().ToString(), EKeys::Enter.GetDisplayName(false).ToString());
	if (Key)
	{
		TestEqual(TEXT("The cap shows the fixed key"), Key->GetText().ToString(), EKeys::Enter.GetDisplayName(false).ToString());
	}
	Prompt->RefreshKey();
	TestEqual(TEXT("A refresh keeps the fixed key (the action is ignored)"), Prompt->GetDisplayedKey(), EKeys::Enter);
	Prompt->SetFixedKey(EKeys::Invalid);
	TestFalse(TEXT("Clearing the fixed key goes back to the action's key"), Prompt->GetDisplayedKey().IsValid());
	TestEqual(TEXT("Cleared: Unbound again"), Prompt->GetKeyText().ToString(), FString(TEXT("Unbound")));

	Prompt->SetLabel(FText::FromString(TEXT("Open")));
	if (Label)
	{
		TestEqual(TEXT("The label is shown"), Label->GetText().ToString(), FString(TEXT("Open")));
		TestTrue(TEXT("A label is visible"), Label->GetVisibility() != ESlateVisibility::Collapsed);
	}
	Prompt->SetLabel(FText::GetEmpty());
	if (Label)
	{
		TestEqual(TEXT("An empty label is collapsed"), Label->GetVisibility(), ESlateVisibility::Collapsed);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStdKeyBindingRowsTest, "StdCore.Input.RowOrder", StdUnitTests::Flags)

bool FStdKeyBindingRowsTest::RunTest(const FString& Parameters)
{
	auto MakeRow = [](const TCHAR* Name, const TCHAR* Label, const TCHAR* Category, EPlayerMappableKeySlot Slot = EPlayerMappableKeySlot::First)
	{
		FStdKeyBindingRow Row;
		Row.MappingName = Name;
		Row.Slot = Slot;
		Row.DisplayName = FText::FromString(Label);
		Row.Category = FText::FromString(Category);
		return Row;
	};
	auto Names = [](const TArray<FStdKeyBindingRow>& Rows)
	{
		FString Out;
		for (const FStdKeyBindingRow& Row : Rows)
		{
			Out += (Out.IsEmpty() ? TEXT("") : TEXT(",")) + Row.MappingName.ToString();
		}
		return Out;
	};

	// As the profile may return them: map order, not context order.
	const TArray<FStdKeyBindingRow> Profile = {
		MakeRow(TEXT("C"), TEXT("Cee"), TEXT("Actions")),
		MakeRow(TEXT("A"), TEXT("Ay"), TEXT("Movement")),
		MakeRow(TEXT("D"), TEXT("Dee"), TEXT("Actions")),
		MakeRow(TEXT("B"), TEXT("Bee"), TEXT("Movement")),
		MakeRow(TEXT("B"), TEXT("Bee 2"), TEXT("Movement"), EPlayerMappableKeySlot::Second),
	};
	const TArray<FName> ContextOrder = { TEXT("A"), TEXT("B"), TEXT("C"), TEXT("D") };

	TArray<FStdKeyBindingRow> Rows = Profile;
	UStdInputSubsystem::ArrangeKeyBindingRows(Rows, ContextOrder, {}, {});
	TestEqual(TEXT("No project rows: context order, grouped by category"), Names(Rows), FString(TEXT("A,B,B,C,D")));

	Rows = Profile;
	UStdInputSubsystem::ArrangeKeyBindingRows(Rows, ContextOrder, {}, { TEXT("B"), TEXT("Z") });
	TestEqual(TEXT("Hidden names are left out, unknown hidden names ignored"), Names(Rows), FString(TEXT("A,C,D")));

	FStdKeyBindingRowSetting ListD;
	ListD.MappingName = TEXT("D");
	ListD.DisplayName = FText::FromString(TEXT("Dee listed"));
	FStdKeyBindingRowSetting ListB;
	ListB.MappingName = TEXT("B");
	ListB.Category = FText::FromString(TEXT("Listed"));
	FStdKeyBindingRowSetting ListMissing;
	ListMissing.MappingName = TEXT("Missing");

	Rows = Profile;
	UStdInputSubsystem::ArrangeKeyBindingRows(Rows, ContextOrder, { ListD, ListMissing, ListB }, {});
	TestEqual(TEXT("Listed rows first in listed order (every slot), then the others"), Names(Rows), FString(TEXT("D,B,B,A,C")));
	if (Rows.Num() == 5)
	{
		TestEqual(TEXT("A listed label replaces the mapping's"), Rows[0].DisplayName.ToString(), FString(TEXT("Dee listed")));
		TestTrue(TEXT("A listed row takes the listed category, empty here"), Rows[0].Category.IsEmpty());
		TestEqual(TEXT("An empty listed label keeps the mapping's"), Rows[1].DisplayName.ToString(), FString(TEXT("Bee")));
		TestEqual(TEXT("Slots keep their order"), Rows[2].Slot, EPlayerMappableKeySlot::Second);
		TestEqual(TEXT("The listed category is used"), Rows[2].Category.ToString(), FString(TEXT("Listed")));
		TestEqual(TEXT("Unlisted rows keep their category"), Rows[4].Category.ToString(), FString(TEXT("Actions")));
	}

	Rows = Profile;
	UStdInputSubsystem::ArrangeKeyBindingRows(Rows, ContextOrder, { ListD, ListB }, { TEXT("D") });
	TestEqual(TEXT("Hidden wins over listed"), Names(Rows), FString(TEXT("B,B,A,C")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStdFlowLabelWrapTest, "StdCore.Flow.LabelWrap", StdUnitTests::Flags)

bool FStdFlowLabelWrapTest::RunTest(const FString& Parameters)
{
	const float MaxWidth = UStdProjectSettings::Get()->TransitionLabelMaxWidth;
	if (!TestTrue(TEXT("The project sets a maximum label width"), MaxWidth > 0.f))
	{
		return false;
	}
	// 120 characters: wider than the maximum width at the usual transition font size.
	FString Padded = TEXT("A long label for the covered screen, written to need more than one line");
	while (Padded.Len() < 120)
	{
		Padded += TEXT(" and more");
	}
	Padded.LeftInline(120);
	TestEqual(TEXT("The long label has 120 characters"), Padded.Len(), 120);

	const TSharedRef<SWidget> Short = UStdFlowSubsystem::MakeLabelWidget(FText::FromString(TEXT("Short")), FSlateColor(FLinearColor::White));
	const TSharedRef<SWidget> Wrapped = UStdFlowSubsystem::MakeLabelWidget(FText::FromString(Padded), FSlateColor(FLinearColor::White));
	Short->SlatePrepass(1.f);
	Wrapped->SlatePrepass(1.f);
	const FVector2D ShortSize = Short->GetDesiredSize();
	const FVector2D WrappedSize = Wrapped->GetDesiredSize();
	AddInfo(FString::Printf(TEXT("Max width %.0f: one line %.0f x %.0f, 120 characters %.0f x %.0f."), MaxWidth, ShortSize.X, ShortSize.Y, WrappedSize.X, WrappedSize.Y));
	TestTrue(TEXT("A short label is one line"), ShortSize.Y > 0.f);
	TestTrue(TEXT("120 characters wrap onto two lines or more"), WrappedSize.Y >= 1.8f * ShortSize.Y);
	TestTrue(TEXT("The wrapped label stays within the maximum width (and its side padding)"), WrappedSize.X <= MaxWidth + 96.f + 1.f);
	return true;
}

namespace StdUnitTests
{
	/** A settings object of its own for the layout tests: the project's values, with the bar and the marker as given. */
	UStdProjectSettings* MakeTransitionSettings(bool bBar, bool bMarker, float BarHeight = 4.f, const FVector2D& MarkerSize = FVector2D(12.0, 24.0))
	{
		UStdProjectSettings* Settings = NewObject<UStdProjectSettings>(GetTransientPackage());
		Settings->bTransitionProgressBar = bBar;
		Settings->bTransitionMarker = bMarker;
		Settings->TransitionProgressBarHeight = BarHeight;
		Settings->TransitionMarkerSize = MarkerSize;
		Settings->TransitionProgressBarColor = FLinearColor(0.3f, 0.3f, 0.3f, 1.f);
		Settings->TransitionMarkerColor = FLinearColor(0.3f, 0.3f, 0.3f, 1.f);
		return Settings;
	}

	/** Horizontal position of the marker as a fraction of the distance it can travel (0 left edge, 1 right edge). */
	float MarkerPositionFraction(const FStdTransitionLayout& Layout, float ScreenWidth)
	{
		const float Travel = ScreenWidth - Layout.MarkerSize.X;
		return Travel > 0.f ? Layout.MarkerPosition.X / Travel : 0.f;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStdFlowProgressOffTest, "StdCore.Flow.ProgressOff", StdUnitTests::Flags)

bool FStdFlowProgressOffTest::RunTest(const FString& Parameters)
{
	using namespace StdUnitTests;
	// Full bar, marker halfway, fully covered: everything there is to draw, and still nothing with both switches off.
	FStdTransitionProgress Progress;
	Progress.Reset(4.f);
	Progress.Report(1.f);
	Progress.UpdateCovered(2.f);
	const FVector2f Screen(1920.f, 1080.f);

	const FStdTransitionLayout Off = FStdTransitionLayout::Compute(*MakeTransitionSettings(false, false), Screen, Progress, 1.f);
	TestFalse(TEXT("Both switches off: no bar"), Off.bShowBar);
	TestFalse(TEXT("Both switches off: no marker"), Off.bShowMarker);

	const FStdTransitionLayout BarOnly = FStdTransitionLayout::Compute(*MakeTransitionSettings(true, false), Screen, Progress, 1.f);
	TestTrue(TEXT("Bar switch alone: a bar"), BarOnly.bShowBar);
	TestFalse(TEXT("Bar switch alone: no marker"), BarOnly.bShowMarker);

	const FStdTransitionLayout MarkerOnly = FStdTransitionLayout::Compute(*MakeTransitionSettings(false, true), Screen, Progress, 1.f);
	TestFalse(TEXT("Marker switch alone: no bar"), MarkerOnly.bShowBar);
	TestTrue(TEXT("Marker switch alone: a marker"), MarkerOnly.bShowMarker);
	TestEqual(TEXT("Marker switch alone: the marker stands on the bottom edge"), MarkerOnly.MarkerPosition.Y + MarkerOnly.MarkerSize.Y, Screen.Y);

	const FStdTransitionLayout Uncovered = FStdTransitionLayout::Compute(*MakeTransitionSettings(true, true), Screen, Progress, 0.f);
	TestFalse(TEXT("Overlay at opacity 0: no bar"), Uncovered.bShowBar);
	TestFalse(TEXT("Overlay at opacity 0: no marker"), Uncovered.bShowMarker);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStdFlowProgressBarShapeTest, "StdCore.Flow.ProgressBarShape", StdUnitTests::Flags)

bool FStdFlowProgressBarShapeTest::RunTest(const FString& Parameters)
{
	using namespace StdUnitTests;
	FStdTransitionProgress Full;
	Full.Reset(0.f);
	Full.Report(1.f);
	FStdTransitionProgress Half;
	Half.Reset(0.f);
	Half.Report(0.5f);

	for (const float Height : { 4.f, 7.f })
	{
		const UStdProjectSettings* Settings = MakeTransitionSettings(true, false, Height);
		// 16:9, 4:3, 21:9 and a small 16:9 window.
		for (const FVector2f Screen : { FVector2f(1920.f, 1080.f), FVector2f(1440.f, 1080.f), FVector2f(2560.f, 1080.f), FVector2f(1280.f, 720.f) })
		{
			const FString At = FString::Printf(TEXT("%.0fx%.0f, height %.0f"), Screen.X, Screen.Y, Height);
			const FStdTransitionLayout L = FStdTransitionLayout::Compute(*Settings, Screen, Full, 1.f);
			TestEqual(At + TEXT(": the full bar starts at the left edge"), L.BarPosition.X, 0.f);
			TestEqual(At + TEXT(": the full bar reaches the right edge"), L.BarPosition.X + L.BarSize.X, Screen.X);
			TestEqual(At + TEXT(": the bar touches the bottom edge"), L.BarPosition.Y + L.BarSize.Y, Screen.Y);
			TestEqual(At + TEXT(": the bar has the height of the setting"), L.BarSize.Y, Height);
			const FStdTransitionLayout H = FStdTransitionLayout::Compute(*Settings, Screen, Half, 1.f);
			TestEqual(At + TEXT(": at 0.5 the bar fills half the width from the left edge"), H.BarSize.X, Screen.X * 0.5f);
		}
	}
	TestEqual(TEXT("The bar height is clamped to 64"), FStdTransitionLayout::Compute(*MakeTransitionSettings(true, false, 500.f), FVector2f(1920.f, 1080.f), Full, 1.f).BarSize.Y, 64.f);
	TestEqual(TEXT("The bar colour's alpha follows the fade"), FStdTransitionLayout::Compute(*MakeTransitionSettings(true, false), FVector2f(1920.f, 1080.f), Full, 0.5f).BarColor.A, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStdFlowProgressReportTest, "StdCore.Flow.ProgressReport", StdUnitTests::Flags)

bool FStdFlowProgressReportTest::RunTest(const FString& Parameters)
{
	FStdTransitionProgress Progress;
	Progress.Reset(4.f);
	TestEqual(TEXT("A new covered period starts at 0"), Progress.GetBarFraction(), 0.f);
	Progress.Report(0.3f);
	TestEqual(TEXT("Report 0.3 shows 0.3"), Progress.GetBarFraction(), 0.3f);
	Progress.Report(0.2f);
	TestEqual(TEXT("Report 0.2 after 0.3 still shows 0.3"), Progress.GetBarFraction(), 0.3f);
	Progress.Report(0.7f);
	TestEqual(TEXT("Report 0.7 shows 0.7"), Progress.GetBarFraction(), 0.7f);
	Progress.Report(-1.f);
	TestEqual(TEXT("A negative report changes nothing"), Progress.GetBarFraction(), 0.7f);

	// No report: ten simulated minutes of covered time at 60 frames per second do not move it.
	FStdTransitionProgress Idle;
	Idle.Reset(4.f);
	float Covered = 0.f;
	bool bMoved = false;
	for (int32 Frame = 0; Frame < 36000; ++Frame)
	{
		Covered += 1.f / 60.f;
		Idle.UpdateCovered(Covered);
		bMoved |= Idle.GetBarFraction() != 0.f;
	}
	TestFalse(TEXT("With no report the bar does not move, however long the screen stays covered"), bMoved);
	Idle.FinishCovered();
	TestEqual(TEXT("The end of the covered time does not move the bar either"), Idle.GetBarFraction(), 0.f);

	Progress.Report(3.f);
	TestEqual(TEXT("Reports are clamped to 1"), Progress.GetBarFraction(), 1.f);
	Progress.Reset(0.f);
	TestEqual(TEXT("The next covered period starts at 0 again"), Progress.GetBarFraction(), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStdFlowMarkerTest, "StdCore.Flow.Marker", StdUnitTests::Flags)

bool FStdFlowMarkerTest::RunTest(const FString& Parameters)
{
	using namespace StdUnitTests;
	const UStdProjectSettings* Settings = MakeTransitionSettings(true, true);
	const FVector2f Screen(1920.f, 1080.f);
	const float Frame = 1.f / 60.f;

	for (const float Target : { 4.f, 5.86f })
	{
		FStdTransitionProgress Progress;
		Progress.Reset(Target);
		const FString For = FString::Printf(TEXT("covered time %.2f s"), Target);
		TestTrue(For + TEXT(": the marker exists"), Progress.HasMarker());
		TestEqual(For + TEXT(": the marker starts at the left edge"), FStdTransitionLayout::Compute(*Settings, Screen, Progress, 1.f).MarkerPosition.X, 0.f);

		// Simulated ticks: each frame advances the covered time by one frame; check the frame that reaches each mark.
		const float Marks[] = { 0.25f, 0.5f, 1.f };
		int32 NextMark = 0;
		float Covered = 0.f;
		float Previous = 0.f;
		bool bMovedBack = false;
		while (NextMark < UE_ARRAY_COUNT(Marks) && Covered < Target * 2.f)
		{
			Covered += Frame;
			Progress.UpdateCovered(Covered);
			const FStdTransitionLayout L = FStdTransitionLayout::Compute(*Settings, Screen, Progress, 1.f);
			const float Position = MarkerPositionFraction(L, Screen.X);
			bMovedBack |= Position < Previous;
			Previous = Position;
			if (Covered >= Marks[NextMark] * Target)
			{
				TestTrue(FString::Printf(TEXT("%s: at %.0f%% of the covered time the marker is at %.4f of the screen width (within one frame, %.4f)"), *For,
					Marks[NextMark] * 100.f, Position, Frame / Target), FMath::Abs(Position - Marks[NextMark]) <= Frame / Target + KINDA_SMALL_NUMBER);
				++NextMark;
			}
		}
		TestEqual(For + TEXT(": every mark reached"), NextMark, static_cast<int32>(UE_ARRAY_COUNT(Marks)));
		TestFalse(For + TEXT(": the marker never moves back"), bMovedBack);

		Progress.UpdateCovered(Target * 0.1f);
		TestEqual(For + TEXT(": an earlier time does not move it back"), Progress.GetMarkerFraction(), 1.f);

		// Loading outlasts the covered time: it waits at the right edge.
		Progress.UpdateCovered(Target * 3.f);
		const FStdTransitionLayout Waiting = FStdTransitionLayout::Compute(*Settings, Screen, Progress, 1.f);
		TestEqual(For + TEXT(": waiting at the right edge"), Waiting.MarkerPosition.X + Waiting.MarkerSize.X, Screen.X);
		TestEqual(For + TEXT(": the marker stands on top of the bar"), Waiting.MarkerPosition.Y + Waiting.MarkerSize.Y, Screen.Y - Waiting.BarSize.Y);
	}

	FStdTransitionProgress Zero;
	Zero.Reset(0.f);
	Zero.UpdateCovered(3.f);
	Zero.FinishCovered();
	TestFalse(TEXT("Covered time 0: no marker"), Zero.HasMarker() || FStdTransitionLayout::Compute(*Settings, Screen, Zero, 1.f).bShowMarker);

	FStdTransitionProgress Timed;
	Timed.Reset(4.f);
	Timed.UpdateCovered(2.f);
	TestFalse(TEXT("Switch off: no marker"), FStdTransitionLayout::Compute(*MakeTransitionSettings(true, false), Screen, Timed, 1.f).bShowMarker);

	FStdTransitionProgress Finished;
	Finished.Reset(4.f);
	Finished.UpdateCovered(1.f);
	Finished.FinishCovered();
	TestEqual(TEXT("The end of the covered time puts the marker at the right edge"), Finished.GetMarkerFraction(), 1.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
