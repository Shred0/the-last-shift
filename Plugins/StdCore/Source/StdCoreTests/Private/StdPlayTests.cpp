// Play In Editor tests: map transitions, pause and key remapping, through the same calls the menus use, and the choice list
// screen built for a player (its tree is built when a player owns it).

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Editor.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Flow/StdFlowSubsystem.h"
#include "Framework/StdPlayerController.h"
#include "Misc/PackageName.h"
#include "Input/StdInputSubsystem.h"
#include "InputMappingContext.h"
#include "Settings/StdProjectSettings.h"
#include "StdGameplayTags.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UI/StdChoiceListWidget.h"
#include "UI/StdMainMenuWidget.h"

namespace StdPlayTests
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	UWorld* GetPlayWorld()
	{
		return GEditor ? GEditor->PlayWorld.Get() : nullptr;
	}

	UStdFlowSubsystem* GetFlow()
	{
		const UWorld* World = GetPlayWorld();
		const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<UStdFlowSubsystem>() : nullptr;
	}

	APlayerController* GetPlayerController()
	{
		const UWorld* World = GetPlayWorld();
		return World ? World->GetFirstPlayerController() : nullptr;
	}

	UStdInputSubsystem* GetInput()
	{
		const APlayerController* PlayerController = GetPlayerController();
		return PlayerController ? ULocalPlayer::GetSubsystem<UStdInputSubsystem>(PlayerController->GetLocalPlayer()) : nullptr;
	}

	/** Short name of a map setting ("L_MainMenu" for /StdCore/Maps/L_MainMenu.L_MainMenu), as World->GetMapName() ends. */
	FString GetMapShortName(const TSoftObjectPtr<UWorld>& Map)
	{
		return FPackageName::GetShortName(Map.ToSoftObjectPath().GetLongPackageName());
	}

	bool IsOnMap(const FString& MapName)
	{
		const UWorld* World = GetPlayWorld();
		const UStdFlowSubsystem* Flow = GetFlow();
		return World && World->GetMapName().EndsWith(MapName) && Flow && !Flow->IsTransitioning() && GetPlayerController();
	}

	/** Queues a step that runs once. */
	void Step(TFunction<void()> Action)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Action]()
		{
			Action();
			return true;
		}));
	}

	/** Queues a wait for Condition; after TimeoutSeconds the test fails and the next steps run anyway. */
	void WaitUntil(FAutomationTestBase* Test, const FString& What, double TimeoutSeconds, TFunction<bool()> Condition)
	{
		TSharedRef<double> StartTime = MakeShared<double>(0.0);
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand([Test, What, TimeoutSeconds, Condition, StartTime]()
		{
			if (*StartTime == 0.0)
			{
				*StartTime = FPlatformTime::Seconds();
			}
			if (Condition())
			{
				return true;
			}
			if (FPlatformTime::Seconds() - *StartTime > TimeoutSeconds)
			{
				Test->AddError(FString::Printf(TEXT("Timed out after %.0f s waiting for %s."), TimeoutSeconds, *What));
				return true;
			}
			return false;
		}));
	}

	/** The Player Mappable Key names of the plugin's IMC_Default. */
	const TArray<FName>& PluginMappingNames()
	{
		static const TArray<FName> Names = { TEXT("MoveForward"), TEXT("MoveBackward"), TEXT("MoveLeft"), TEXT("MoveRight"), TEXT("Jump"), TEXT("Interact") };
		return Names;
	}

	/** The project's Controls tab settings, saved by IsolatePluginRows and put back by RestoreRowSettings. */
	struct FSavedRowSettings
	{
		TArray<FStdKeyBindingRowSetting> KeyBindingRows;
		TArray<FName> HiddenKeyMappings;
	};

	FSavedRowSettings& SavedRowSettings()
	{
		static FSavedRowSettings Saved;
		return Saved;
	}

	/**
	 * Makes the remapping tests independent of the host project: saves its row settings, lists Listed, and hides every
	 * registered mapping that is not one of the plugin's, plus ExtraHidden. Call once a player exists.
	 */
	void IsolatePluginRows(UStdInputSubsystem* Input, const TArray<FStdKeyBindingRowSetting>& Listed, const TArray<FName>& ExtraHidden)
	{
		UStdProjectSettings* Settings = GetMutableDefault<UStdProjectSettings>();
		SavedRowSettings() = { Settings->KeyBindingRows, Settings->HiddenKeyMappings };
		Settings->KeyBindingRows.Reset();
		Settings->HiddenKeyMappings.Reset();

		TArray<FName> Hidden = ExtraHidden;
		if (Input)
		{
			for (const FStdKeyBindingRow& Row : Input->GetRemappableKeys())
			{
				if (!PluginMappingNames().Contains(Row.MappingName))
				{
					Hidden.AddUnique(Row.MappingName);
				}
			}
		}
		Settings->KeyBindingRows = Listed;
		Settings->HiddenKeyMappings = MoveTemp(Hidden);
	}

	void RestoreRowSettings()
	{
		UStdProjectSettings* Settings = GetMutableDefault<UStdProjectSettings>();
		Settings->KeyBindingRows = SavedRowSettings().KeyBindingRows;
		Settings->HiddenKeyMappings = SavedRowSettings().HiddenKeyMappings;
	}

	/** A keyboard key that no configured mapping context maps and the player may assign, or an invalid key. */
	FKey FindKeyNoContextMaps()
	{
		TSet<FKey> Mapped;
		for (const FStdMappingContextSetting& Entry : UStdProjectSettings::Get()->MappingContexts)
		{
			if (const UInputMappingContext* Context = Entry.MappingContext.LoadSynchronous())
			{
				for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
				{
					Mapped.Add(Mapping.Key);
				}
			}
		}
		const UStdInputSubsystem* Input = GetInput();
		for (const FKey& Candidate : { EKeys::K, EKeys::L, EKeys::J, EKeys::H, EKeys::U, EKeys::I, EKeys::O, EKeys::P })
		{
			FText Reason;
			if (!Mapped.Contains(Candidate) && Input && Input->CanAssignKey(Candidate, Reason))
			{
				return Candidate;
			}
		}
		return FKey();
	}

	void StartPlayOnMap(FAutomationTestBase* Test, const TSoftObjectPtr<UWorld>& Map, const FString& MapName)
	{
		FAutomationEditorCommonUtils::LoadMap(Map.ToSoftObjectPath().GetLongPackageName());
		ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
		WaitUntil(Test, FString::Printf(TEXT("%s to start and fade in"), *MapName), 20.0, [MapName]() { return IsOnMap(MapName); });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStdFlowPlayTest, "StdCore.Flow.Play", StdPlayTests::Flags)

bool FStdFlowPlayTest::RunTest(const FString& Parameters)
{
	using namespace StdPlayTests;
	const UStdProjectSettings* Settings = UStdProjectSettings::Get();

	// Logged on purpose by the refused travel below.
	AddExpectedError(TEXT("does not exist; nothing happens"), EAutomationExpectedErrorFlags::Contains, 1);

	// The project's own main menu map, whatever it is called.
	const FString MainMenuName = GetMapShortName(Settings->MainMenuMap);
	StartPlayOnMap(this, Settings->MainMenuMap, MainMenuName);

	Step([this]()
	{
		TArray<UUserWidget*> Menus;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(GetPlayWorld(), Menus, UStdMainMenuWidget::StaticClass(), false);
		TestEqual(TEXT("The main menu is shown"), Menus.Num(), 1);

		UStdFlowSubsystem* Flow = GetFlow();
		if (TestNotNull(TEXT("Flow subsystem"), Flow))
		{
			TestFalse(TEXT("A map that does not exist is refused"),
				Flow->TravelToLevel(TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/StdCore/Maps/L_DoesNotExist.L_DoesNotExist")))));
			TestFalse(TEXT("The refused travel starts no transition"), Flow->IsTransitioning());
			TestTrue(TEXT("Start Gameplay starts a travel"), Flow->StartGameplay());
			TestFalse(TEXT("A second request during the transition is ignored"), Flow->StartGameplay());
		}
	});
	// The project's own gameplay map, whatever it is called.
	const FString GameplayName = GetMapShortName(Settings->GameplayMap);
	WaitUntil(this, GameplayName + TEXT(" to load and fade in"), 20.0, [GameplayName]() { return IsOnMap(GameplayName); });

	Step([this]()
	{
		AStdPlayerController* PlayerController = Cast<AStdPlayerController>(GetPlayerController());
		if (!TestNotNull(TEXT("The gameplay map uses AStdPlayerController"), PlayerController))
		{
			return;
		}
		TestTrue(TEXT("Pause Game succeeds"), PlayerController->PauseGame());
		TestTrue(TEXT("The world is paused"), GetPlayWorld()->IsPaused());
		TestNotNull(TEXT("The pause menu is shown"), PlayerController->GetPauseMenu());
		TestFalse(TEXT("Pausing twice is refused"), PlayerController->PauseGame());
		TestTrue(TEXT("Resume Game succeeds"), PlayerController->ResumeGame());
		TestFalse(TEXT("The world runs again"), GetPlayWorld()->IsPaused());
		TestNull(TEXT("The pause menu is closed"), PlayerController->GetPauseMenu());

		UStdFlowSubsystem* Flow = GetFlow();
		TestTrue(TEXT("Return To Main Menu starts a travel"), Flow && Flow->ReturnToMainMenu());
	});
	WaitUntil(this, MainMenuName + TEXT(" to load again"), 20.0, [MainMenuName]() { return IsOnMap(MainMenuName); });

	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStdInputRemapTest, "StdCore.Input.Remap", StdPlayTests::Flags)

bool FStdInputRemapTest::RunTest(const FString& Parameters)
{
	using namespace StdPlayTests;
	static const FName Jump(TEXT("Jump"));
	const EPlayerMappableKeySlot Slot = EPlayerMappableKeySlot::First;

	const TSoftObjectPtr<UWorld>& GameplayMap = UStdProjectSettings::Get()->GameplayMap;
	StartPlayOnMap(this, GameplayMap, GetMapShortName(GameplayMap));

	// A key no configured mapping context maps: the host project's contexts may take Space Bar before the plugin's.
	TSharedRef<FKey> FreeKey = MakeShared<FKey>();

	Step([this, Slot, FreeKey]()
	{
		UStdInputSubsystem* Input = GetInput();
		if (!TestNotNull(TEXT("Std input subsystem"), Input))
		{
			return;
		}

		// Start from the defaults whatever the player saved before, with only the plugin's rows shown.
		IsolatePluginRows(Input, {}, {});
		Input->ResetKeysToDefaults();

		FText Reason;
		TestFalse(TEXT("Escape is reserved"), Input->CanAssignKey(EKeys::Escape, Reason));
		TestFalse(TEXT("Gamepad buttons are refused"), Input->CanAssignKey(EKeys::Gamepad_FaceButton_Bottom, Reason));
		TestFalse(TEXT("Mouse axes are refused"), Input->CanAssignKey(EKeys::MouseX, Reason));
		TestTrue(TEXT("A free keyboard key is accepted"), Input->CanAssignKey(EKeys::F, Reason));

		*FreeKey = FindKeyNoContextMaps();
		if (!TestTrue(TEXT("A key no mapping context maps"), FreeKey->IsValid()))
		{
			return;
		}

		FText Message;
		TestTrue(TEXT("Jump to a free key is a plain remap"), Input->RemapKey(Jump, Slot, *FreeKey, Message) == EStdRemapResult::Remapped);
		TestTrue(TEXT("Jump to E swaps with Interact"), Input->RemapKey(Jump, Slot, EKeys::E, Message) == EStdRemapResult::Swapped);
		TestFalse(TEXT("A swap explains itself"), Message.IsEmpty());
		TestTrue(TEXT("Assigning the same key again changes nothing"), Input->RemapKey(Jump, Slot, EKeys::E, Message) == EStdRemapResult::Unchanged);
		TestTrue(TEXT("A reserved key is rejected"), Input->RemapKey(Jump, Slot, EKeys::Escape, Message) == EStdRemapResult::Rejected);
	});

	// The player's mappings are rebuilt on the next frames.
	WaitUntil(this, TEXT("Jump on E and Interact on the free key"), 5.0, [FreeKey]()
	{
		const UStdInputSubsystem* Input = GetInput();
		return Input && Input->GetKeyForTag(StdGameplayTags::InputTag_Jump) == EKeys::E
			&& Input->GetKeyForTag(StdGameplayTags::InputTag_Interact) == *FreeKey;
	});

	Step([]()
	{
		if (UStdInputSubsystem* Input = GetInput())
		{
			Input->ResetKeysToDefaults();
		}
	});
	WaitUntil(this, TEXT("the default keys after a reset"), 5.0, []()
	{
		const UStdInputSubsystem* Input = GetInput();
		if (!Input || Input->GetKeyForTag(StdGameplayTags::InputTag_Interact) != EKeys::E)
		{
			return false;
		}
		// Jump's Space Bar may be taken by a host context of higher priority: check its row, not the live key.
		for (const FStdKeyBindingRow& Row : Input->GetRemappableKeys())
		{
			if (Row.CurrentKey != Row.DefaultKey)
			{
				return false;
			}
		}
		return true;
	});

	Step([]() { StdPlayTests::RestoreRowSettings(); });
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStdInputProjectRowsTest, "StdCore.Input.ProjectRows", StdPlayTests::Flags)

bool FStdInputProjectRowsTest::RunTest(const FString& Parameters)
{
	using namespace StdPlayTests;
	static const FName Jump(TEXT("Jump"));
	static const FName Interact(TEXT("Interact"));
	static const FName MoveForward(TEXT("MoveForward"));
	static const FName MoveRight(TEXT("MoveRight"));
	const EPlayerMappableKeySlot Slot = EPlayerMappableKeySlot::First;

	const TSoftObjectPtr<UWorld>& GameplayMap = UStdProjectSettings::Get()->GameplayMap;
	StartPlayOnMap(this, GameplayMap, GetMapShortName(GameplayMap));

	Step([this, Slot]()
	{
		UStdInputSubsystem* Input = GetInput();
		if (!TestNotNull(TEXT("Std input subsystem"), Input))
		{
			return;
		}

		// The project lists Interact then Jump, relabels Interact, and hides MoveForward.
		FStdKeyBindingRowSetting UseRow;
		UseRow.MappingName = Interact;
		UseRow.DisplayName = FText::FromString(TEXT("Use"));
		UseRow.Category = FText::FromString(TEXT("Listed"));
		FStdKeyBindingRowSetting JumpRow;
		JumpRow.MappingName = Jump;
		IsolatePluginRows(Input, { UseRow, JumpRow }, { MoveForward });
		Input->ResetKeysToDefaults();

		const TArray<FStdKeyBindingRow> Rows = Input->GetRemappableKeys();
		TestEqual(TEXT("Five rows: two listed, three not listed, one hidden"), Rows.Num(), 5);
		if (Rows.Num() >= 2)
		{
			TestEqual(TEXT("The first listed row comes first"), Rows[0].MappingName, Interact);
			TestEqual(TEXT("A listed label replaces the mapping's"), Rows[0].DisplayName.ToString(), FString(TEXT("Use")));
			TestEqual(TEXT("A listed category replaces the mapping's"), Rows[0].Category.ToString(), FString(TEXT("Listed")));
			TestEqual(TEXT("The second listed row comes second"), Rows[1].MappingName, Jump);
			TestEqual(TEXT("An empty listed label keeps the mapping's"), Rows[1].DisplayName.ToString(), FString(TEXT("Jump")));
			TestTrue(TEXT("An empty listed category shows no header"), Rows[1].Category.IsEmpty());
		}
		TestFalse(TEXT("A hidden mapping is not listed"),
			Rows.ContainsByPredicate([](const FStdKeyBindingRow& Row) { return Row.MappingName == MoveForward; }));

		FText Message;
		TestTrue(TEXT("A hidden mapping cannot be remapped"), Input->RemapKey(MoveForward, Slot, EKeys::I, Message) == EStdRemapResult::Rejected);

		// A key held by a shown row swaps; the notice names the other row by its listed label.
		TestTrue(TEXT("Jump to E swaps with the listed Interact"), Input->RemapKey(Jump, Slot, EKeys::E, Message) == EStdRemapResult::Swapped);
		TestTrue(TEXT("The swap notice uses the listed label"), Message.ToString().Contains(TEXT("\"Use\"")));

		// A key held only by a hidden row is free: hidden rows take no part in swaps.
		TestTrue(TEXT("MoveRight to W (held by the hidden MoveForward) is a plain remap"),
			Input->RemapKey(MoveRight, Slot, EKeys::W, Message) == EStdRemapResult::Remapped);

		// The pause-menu key is refused with a notice and changes nothing.
		const TArray<FStdKeyBindingRow> Before = Input->GetRemappableKeys();
		TestTrue(TEXT("Escape is refused"), Input->RemapKey(Interact, Slot, EKeys::Escape, Message) == EStdRemapResult::Rejected);
		TestFalse(TEXT("The refusal has a notice"), Message.IsEmpty());
		const TArray<FStdKeyBindingRow> After = Input->GetRemappableKeys();
		bool bUnchanged = Before.Num() == After.Num();
		for (int32 Index = 0; bUnchanged && Index < Before.Num(); ++Index)
		{
			bUnchanged = Before[Index].MappingName == After[Index].MappingName && Before[Index].CurrentKey == After[Index].CurrentKey;
		}
		TestTrue(TEXT("A refusal changes no key"), bUnchanged);

		// After all of these, no shown row is without a key and no key is on two shown rows.
		TSet<FKey> Keys;
		for (const FStdKeyBindingRow& Row : After)
		{
			TestTrue(FString::Printf(TEXT("%s has a key"), *Row.MappingName.ToString()), Row.CurrentKey.IsValid());
			bool bAlreadyUsed = false;
			Keys.Add(Row.CurrentKey, &bAlreadyUsed);
			TestFalse(FString::Printf(TEXT("%s is on one row only"), *Row.CurrentKey.ToString()), bAlreadyUsed);
		}

		Input->ResetKeysToDefaults();
		bool bDefaults = true;
		for (const FStdKeyBindingRow& Row : Input->GetRemappableKeys())
		{
			bDefaults &= Row.CurrentKey == Row.DefaultKey;
		}
		TestTrue(TEXT("Reset puts every shown row back on its default key"), bDefaults);
	});

	Step([]() { StdPlayTests::RestoreRowSettings(); });
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStdChoiceListBodyTest, "StdCore.UI.ChoiceListBody", StdPlayTests::Flags)

bool FStdChoiceListBodyTest::RunTest(const FString& Parameters)
{
	using namespace StdPlayTests;
	const UStdProjectSettings* Settings = UStdProjectSettings::Get();
	const FString MainMenuName = GetMapShortName(Settings->MainMenuMap);
	StartPlayOnMap(this, Settings->MainMenuMap, MainMenuName);

	Step([this]()
	{
		APlayerController* PlayerController = GetPlayerController();
		UStdChoiceListWidget* Screen = PlayerController ? CreateWidget<UStdChoiceListWidget>(PlayerController, UStdChoiceListWidget::StaticClass()) : nullptr;
		if (!TestNotNull(TEXT("A choice list is created for the player"), Screen))
		{
			return;
		}
		UTextBlock* Body = Screen->WidgetTree ? Cast<UTextBlock>(Screen->WidgetTree->FindWidget(TEXT("Text_Body"))) : nullptr;
		UVerticalBox* Main = Screen->WidgetTree ? Cast<UVerticalBox>(Screen->WidgetTree->FindWidget(TEXT("Box_Main"))) : nullptr;
		UWidget* Title = Screen->WidgetTree ? Screen->WidgetTree->FindWidget(TEXT("Text_Title")) : nullptr;
		UWidget* Entries = Screen->WidgetTree ? Screen->WidgetTree->FindWidget(TEXT("Box_Entries")) : nullptr;
		if (!TestNotNull(TEXT("Has Text_Body"), Body) || !TestNotNull(TEXT("Has Box_Main"), Main))
		{
			return;
		}
		const int32 TitleIndex = Main->GetChildIndex(Title);
		const int32 BodyIndex = Main->GetChildIndex(Body);
		const int32 EntriesIndex = Main->GetChildIndex(Entries);
		TestTrue(TEXT("The body sits below the title and above the entries"), TitleIndex >= 0 && TitleIndex < BodyIndex && BodyIndex < EntriesIndex);
		TestTrue(TEXT("The body wraps"), Body->GetAutoWrapText());
		TestEqual(TEXT("No body by default: collapsed"), Body->GetVisibility(), ESlateVisibility::Collapsed);

		const FText Text = FText::FromString(TEXT("A body line above the entries."));
		Screen->SetBody(Text);
		TestEqual(TEXT("The body is shown"), Body->GetText().ToString(), Text.ToString());
		TestTrue(TEXT("A body is visible"), Body->GetVisibility() != ESlateVisibility::Collapsed);
		TestEqual(TEXT("GetBody returns it"), Screen->GetBody().ToString(), Text.ToString());

		TArray<FStdChoiceEntry> List;
		List.AddDefaulted(2);
		Screen->SetEntries(List);
		TestEqual(TEXT("New entries keep the body"), Body->GetText().ToString(), Text.ToString());

		Screen->SetBody(FText::GetEmpty());
		TestEqual(TEXT("An empty body is collapsed"), Body->GetVisibility(), ESlateVisibility::Collapsed);
	});

	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
