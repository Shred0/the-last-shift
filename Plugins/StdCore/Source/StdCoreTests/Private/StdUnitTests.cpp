// Logic that needs no world: the volume formula, volume clamping, the styles built by the UI theme, the radial progress,
// the key prompt without a player, the order of the Controls tab rows, the wrapping of the covered screen's label.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Input/StdInputSubsystem.h"
#include "Settings/StdSettingsSubsystem.h"
#include "Settings/StdUserSettings.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Flow/StdFlowSubsystem.h"
#include "Settings/StdProjectSettings.h"
#include "Styling/SlateColor.h"
#include "Widgets/SWidget.h"
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

#endif // WITH_DEV_AUTOMATION_TESTS
