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
#include "UI/StdTextSceneWidget.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/OverlaySlot.h"
#include "Framework/Application/SlateApplication.h"
#include "UI/StdKeyPromptWidget.h"
#include "UObject/UObjectIterator.h"
#include "StdTextSceneTestListener.h"

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

	/** A point of a widget on the desktop (Fraction of its size from its top left), from its last layout (zero when it has none yet). */
	FVector2D WidgetPoint(const UWidget* Widget, const FVector2D& Fraction)
	{
		const FGeometry& Geometry = Widget->GetCachedGeometry();
		return Geometry.GetLocalSize().IsNearlyZero() ? FVector2D::ZeroVector : FVector2D(Geometry.LocalToAbsolute(Geometry.GetLocalSize() * Fraction));
	}

	FVector2D WidgetCentre(const UWidget* Widget)
	{
		return WidgetPoint(Widget, FVector2D(0.5, 0.5));
	}

	/** A mouse move through Slate to Position (the hover follows it). */
	void MoveMouse(const FVector2D& Position)
	{
		FSlateApplication& Slate = FSlateApplication::Get();
		Slate.ProcessMouseMoveEvent(FPointerEvent(FSlateApplication::CursorUserIndex, FSlateApplication::CursorPointerIndex, Position, Position,
			TSet<FKey>(), EKeys::Invalid, 0.f, FModifierKeysState()));
	}

	/** What Slate hits at Position: the leaf widget of the path and its window (a diagnostic). */
	FString DescribeHit(const FVector2D& Position)
	{
		FSlateApplication& Slate = FSlateApplication::Get();
		const FWidgetPath Path = Slate.LocateWindowUnderMouse(Position, Slate.GetInteractiveTopLevelWindows(), false, FSlateApplication::CursorUserIndex);
		if (!Path.IsValid())
		{
			return TEXT("<nothing>");
		}
		return FString::Printf(TEXT("%s (%d widgets in the path) in window \"%s\""), *Path.Widgets.Last().Widget->ToString(), Path.Widgets.Num(),
			*Path.GetWindow()->GetTitle().ToString());
	}

	/** A left click through Slate at Fraction of Widget (its centre by default), as the mouse would: moved there, pressed, released. Whether the press was handled. */
	bool ClickWidget(const UWidget* Widget, FString* OutHit = nullptr, const FVector2D& Fraction = FVector2D(0.5, 0.5))
	{
		const FVector2D Centre = Widget ? WidgetPoint(Widget, Fraction) : FVector2D::ZeroVector;
		if (Centre.IsZero() || !FSlateApplication::IsInitialized())
		{
			return false;
		}
		MoveMouse(Centre);
		FSlateApplication& Slate = FSlateApplication::Get();
		if (OutHit)
		{
			*OutHit = FString::Printf(TEXT("at (%.0f, %.0f): %s"), Centre.X, Centre.Y, *DescribeHit(Centre));
		}
		const TSet<FKey> Pressed = { EKeys::LeftMouseButton };
		const bool bHandled = Slate.ProcessMouseButtonDownEvent(nullptr, FPointerEvent(FSlateApplication::CursorUserIndex, FSlateApplication::CursorPointerIndex, Centre, Centre,
			Pressed, EKeys::LeftMouseButton, 0.f, FModifierKeysState()));
		Slate.ProcessMouseButtonUpEvent(FPointerEvent(FSlateApplication::CursorUserIndex, FSlateApplication::CursorPointerIndex, Centre, Centre,
			TSet<FKey>(), EKeys::LeftMouseButton, 0.f, FModifierKeysState()));
		return bHandled;
	}

	/** A key through Slate, pressed and released; whether the press was handled. */
	bool PressKey(const FKey& Key)
	{
		FSlateApplication& Slate = FSlateApplication::Get();
		const bool bHandled = Slate.ProcessKeyDownEvent(FKeyEvent(Key, FModifierKeysState(), FSlateApplication::CursorUserIndex, false, 0, 0));
		Slate.ProcessKeyUpEvent(FKeyEvent(Key, FModifierKeysState(), FSlateApplication::CursorUserIndex, false, 0, 0));
		return bHandled;
	}

	/** The text scene the test shows (in the viewport, owned by the player). */
	UStdTextSceneWidget* FindShownTextScene()
	{
		const APlayerController* PlayerController = GetPlayerController();
		for (TObjectIterator<UStdTextSceneWidget> It; It; ++It)
		{
			if (It->GetOwningPlayer() == PlayerController && It->IsInViewport())
			{
				return *It;
			}
		}
		return nullptr;
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

namespace StdPlayTests
{
	/** The project's switches of the covered screen's bar and marker, saved by SetTransitionSwitches and put back by RestoreTransitionSwitches. */
	struct FSavedTransitionSwitches
	{
		bool bBar = false;
		bool bMarker = false;
	};

	FSavedTransitionSwitches& SavedTransitionSwitches()
	{
		static FSavedTransitionSwitches Saved;
		return Saved;
	}

	void SetTransitionSwitches(bool bBar, bool bMarker)
	{
		UStdProjectSettings* Settings = GetMutableDefault<UStdProjectSettings>();
		Settings->bTransitionProgressBar = bBar;
		Settings->bTransitionMarker = bMarker;
	}

	/** What a transition showed, frame after frame, while a test watched it. */
	struct FTransitionWatch
	{
		float LastBar = 0.f;
		float LastMarker = 0.f;
		bool bBarMovedBack = false;
		bool bMarkerMovedBack = false;
		bool bBarShown = false;
		bool bMarkerShown = false;
		bool bSawFadeIn = false;
		float BarAtFadeIn = -1.f;
		float MarkerAtFadeIn = -1.f;
		float CoveredAtFadeIn = -1.f;
		int32 Frames = 0;
	};

	/** Samples the flow once (each frame of a WaitUntil); returns true once the transition is over. */
	bool WatchTransition(FTransitionWatch& Watch)
	{
		const UStdFlowSubsystem* Flow = GetFlow();
		if (!Flow)
		{
			return false;
		}
		if (!Flow->IsTransitioning())
		{
			return Watch.Frames > 0;
		}
		++Watch.Frames;
		const float Bar = Flow->GetTransitionProgress();
		const float Marker = Flow->GetTransitionMarkerFraction();
		Watch.bBarMovedBack |= Bar < Watch.LastBar;
		Watch.bMarkerMovedBack |= Marker < Watch.LastMarker;
		Watch.LastBar = Bar;
		Watch.LastMarker = Marker;
		Watch.bBarShown |= Flow->IsTransitionProgressBarShown();
		Watch.bMarkerShown |= Flow->IsTransitionMarkerShown();
		if (Flow->GetState() == EStdFlowState::FadingIn && !Watch.bSawFadeIn)
		{
			Watch.bSawFadeIn = true;
			Watch.BarAtFadeIn = Bar;
			Watch.MarkerAtFadeIn = Marker;
		}
		if (Flow->GetState() == EStdFlowState::WaitingToFadeIn)
		{
			Watch.CoveredAtFadeIn = Flow->GetCoveredSeconds();
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStdFlowProgressPlayTest, "StdCore.Flow.Progress", StdPlayTests::Flags)

bool FStdFlowProgressPlayTest::RunTest(const FString& Parameters)
{
	using namespace StdPlayTests;
	const UStdProjectSettings* Settings = UStdProjectSettings::Get();
	SavedTransitionSwitches() = { Settings->bTransitionProgressBar, Settings->bTransitionMarker };

	const FString MainMenuName = GetMapShortName(Settings->MainMenuMap);
	const FString GameplayName = GetMapShortName(Settings->GameplayMap);
	StartPlayOnMap(this, Settings->MainMenuMap, MainMenuName);

	// A cover hold outside a transition: the bar starts at 0 and shows only what is reported.
	Step([this]()
	{
		SetTransitionSwitches(true, true);
		UStdFlowSubsystem* Flow = GetFlow();
		if (!TestNotNull(TEXT("Flow subsystem"), Flow))
		{
			return;
		}
		Flow->ReportTransitionProgress(0.9f);
		TestEqual(TEXT("A report with no transition is ignored"), Flow->GetTransitionProgress(), 0.f);
		Flow->AddCoverHold(TEXT("StdCoreProgressTest"));
		TestTrue(TEXT("The hold covers the screen"), Flow->IsTransitioning() && Flow->GetOverlayAlpha() >= 1.f);
		TestEqual(TEXT("The covered screen starts with an empty bar"), Flow->GetTransitionProgress(), 0.f);
		TestTrue(TEXT("The bar is shown"), Flow->IsTransitionProgressBarShown());
		TestFalse(TEXT("No minimum covered time: no marker"), Flow->IsTransitionMarkerShown());
		Flow->ReportTransitionProgress(0.3f);
		TestEqual(TEXT("Report 0.3 shows 0.3"), Flow->GetTransitionProgress(), 0.3f);
		Flow->ReportTransitionProgress(0.2f);
		TestEqual(TEXT("Report 0.2 still shows 0.3"), Flow->GetTransitionProgress(), 0.3f);
		Flow->ReportTransitionProgress(0.7f);
		TestEqual(TEXT("Report 0.7 shows 0.7"), Flow->GetTransitionProgress(), 0.7f);
	});
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(1.5f));
	Step([this]()
	{
		UStdFlowSubsystem* Flow = GetFlow();
		TestTrue(TEXT("1.5 s with no report: the bar has not moved"), Flow && Flow->GetTransitionProgress() == 0.7f);
		if (Flow)
		{
			Flow->RemoveCoverHold(TEXT("StdCoreProgressTest"));
			TestEqual(TEXT("The last hold removed: ready, the bar is full"), Flow->GetTransitionProgress(), 1.f);
		}
	});
	WaitUntil(this, TEXT("the hold's cover to go"), 10.0, []() { const UStdFlowSubsystem* F = GetFlow(); return F && !F->IsTransitioning(); });

	// A travel with a minimum covered time: full bar and marker at the right edge as the screen uncovers.
	TSharedRef<FTransitionWatch> Travel = MakeShared<FTransitionWatch>();
	Step([this]()
	{
		FStdTravelOptions Options;
		Options.MinCoveredSeconds = 2.f;
		UStdFlowSubsystem* Flow = GetFlow();
		TestTrue(TEXT("Travel to the gameplay map with 2 s covered"), Flow && Flow->TravelToLevelWithOptions(UStdProjectSettings::Get()->GameplayMap, Options));
	});
	WaitUntil(this, TEXT("the travel to finish"), 30.0, [Travel]() { return WatchTransition(*Travel); });
	Step([this, Travel]()
	{
		TestTrue(TEXT("The bar was shown"), Travel->bBarShown);
		TestTrue(TEXT("The marker was shown"), Travel->bMarkerShown);
		TestFalse(TEXT("The bar never went back"), Travel->bBarMovedBack);
		TestFalse(TEXT("The marker never went back"), Travel->bMarkerMovedBack);
		TestEqual(TEXT("The bar is full when the screen uncovers"), Travel->BarAtFadeIn, 1.f);
		TestEqual(TEXT("The marker is at the right edge when the screen uncovers"), Travel->MarkerAtFadeIn, 1.f);
		AddInfo(FString::Printf(TEXT("Travel: %d frames, covered %.2f s before the fade in."), Travel->Frames, Travel->CoveredAtFadeIn));
	});
	WaitUntil(this, GameplayName + TEXT(" to load and fade in"), 20.0, [GameplayName]() { return IsOnMap(GameplayName); });

	// Loading outlasts the covered time: the marker waits at the right edge, the bar stays where it was.
	Step([this]()
	{
		FStdTravelOptions Options;
		Options.MinCoveredSeconds = 1.f;
		UStdFlowSubsystem* Flow = GetFlow();
		TestTrue(TEXT("Travel to the main menu with 1 s covered"), Flow && Flow->TravelToLevelWithOptions(UStdProjectSettings::Get()->MainMenuMap, Options));
	});
	WaitUntil(this, TEXT("the main menu map to arrive"), 20.0, [MainMenuName]()
	{
		const UWorld* World = GetPlayWorld();
		const UStdFlowSubsystem* Flow = GetFlow();
		return World && World->GetMapName().EndsWith(MainMenuName) && Flow && Flow->GetState() == EStdFlowState::WaitingToFadeIn;
	});
	Step([this]()
	{
		UStdFlowSubsystem* Flow = GetFlow();
		if (Flow)
		{
			Flow->AddCoverHold(TEXT("StdCoreProgressTest"));
		}
	});
	ADD_LATENT_AUTOMATION_COMMAND(FWaitLatentCommand(2.f));
	Step([this]()
	{
		UStdFlowSubsystem* Flow = GetFlow();
		if (!TestNotNull(TEXT("Flow subsystem"), Flow))
		{
			return;
		}
		TestTrue(TEXT("Still covered while held"), Flow->GetState() == EStdFlowState::WaitingToFadeIn);
		TestEqual(TEXT("Loading outlasts the covered time: the marker waits at the right edge"), Flow->GetTransitionMarkerFraction(), 1.f);
		TestTrue(TEXT("The covered time is over"), Flow->GetCoveredSeconds() > 1.f);
		Flow->RemoveCoverHold(TEXT("StdCoreProgressTest"));
	});
	WaitUntil(this, MainMenuName + TEXT(" to fade in"), 20.0, [MainMenuName]() { return IsOnMap(MainMenuName); });

	// Both switches off: nothing drawn on a travel with a covered time.
	TSharedRef<FTransitionWatch> Off = MakeShared<FTransitionWatch>();
	Step([this]()
	{
		SetTransitionSwitches(false, false);
		FStdTravelOptions Options;
		Options.MinCoveredSeconds = 1.f;
		UStdFlowSubsystem* Flow = GetFlow();
		TestTrue(TEXT("Travel to the gameplay map with both switches off"), Flow && Flow->TravelToLevelWithOptions(UStdProjectSettings::Get()->GameplayMap, Options));
	});
	WaitUntil(this, TEXT("the travel with the switches off to finish"), 30.0, [Off]() { return WatchTransition(*Off); });
	Step([this, Off]()
	{
		TestFalse(TEXT("Switches off: no bar"), Off->bBarShown);
		TestFalse(TEXT("Switches off: no marker"), Off->bMarkerShown);
		const FSavedTransitionSwitches& Saved = SavedTransitionSwitches();
		SetTransitionSwitches(Saved.bBar, Saved.bMarker);
	});

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStdTextScenePlayTest, "StdCore.UI.TextScene", StdPlayTests::Flags)

bool FStdTextScenePlayTest::RunTest(const FString& Parameters)
{
	using namespace StdPlayTests;
	const UStdProjectSettings* Settings = UStdProjectSettings::Get();
	const FString MainMenuName = GetMapShortName(Settings->MainMenuMap);
	StartPlayOnMap(this, Settings->MainMenuMap, MainMenuName);

	Step([this]()
	{
		APlayerController* PlayerController = GetPlayerController();
		UStdTextSceneWidget* Screen = PlayerController ? CreateWidget<UStdTextSceneWidget>(PlayerController, UStdTextSceneWidget::StaticClass()) : nullptr;
		if (!TestNotNull(TEXT("A text scene is created for the player"), Screen) || !Screen->WidgetTree)
		{
			return;
		}
		TestNotNull(TEXT("Has Border_Background"), Screen->WidgetTree->FindWidget(TEXT("Border_Background")));
		TestNotNull(TEXT("Has SizeBox_Line"), Screen->WidgetTree->FindWidget(TEXT("SizeBox_Line")));
		UTextBlock* Line = Cast<UTextBlock>(Screen->WidgetTree->FindWidget(TEXT("Text_Line")));
		if (!TestNotNull(TEXT("Has Text_Line"), Line))
		{
			return;
		}
		UStdTextSceneTestListener* Listener = NewObject<UStdTextSceneTestListener>();
		Listener->AddToRoot();
		Screen->OnEntryStarted.AddDynamic(Listener, &UStdTextSceneTestListener::HandleEntryStarted);
		Screen->OnSceneFinished.AddDynamic(Listener, &UStdTextSceneTestListener::HandleFinished);
		Screen->ShowAsRoot(50);

		// The on-screen controls: in the tree, off by default, never focusable, showing the confirm and back keys.
		UHorizontalBox* Controls = Cast<UHorizontalBox>(Screen->WidgetTree->FindWidget(TEXT("Box_Controls")));
		UButton* ContinueButton = Cast<UButton>(Screen->WidgetTree->FindWidget(TEXT("Button_Continue")));
		UButton* SkipButton = Cast<UButton>(Screen->WidgetTree->FindWidget(TEXT("Button_Skip")));
		UStdKeyPromptWidget* ContinuePrompt = Cast<UStdKeyPromptWidget>(Screen->WidgetTree->FindWidget(TEXT("KeyPrompt_Continue")));
		UStdKeyPromptWidget* SkipPrompt = Cast<UStdKeyPromptWidget>(Screen->WidgetTree->FindWidget(TEXT("KeyPrompt_Skip")));
		TestTrue(TEXT("Controls: Box_Controls, Button_Continue, Button_Skip, KeyPrompt_Continue and KeyPrompt_Skip are in the tree"),
			Controls && ContinueButton && SkipButton && ContinuePrompt && SkipPrompt);
		if (!Controls || !ContinueButton || !SkipButton || !ContinuePrompt || !SkipPrompt)
		{
			Listener->RemoveFromRoot();
			return;
		}
		TestFalse(TEXT("Controls: off by default"), Screen->bShowControls);
		TestTrue(TEXT("Controls: neither button can take the focus"), !ContinueButton->GetIsFocusable() && !SkipButton->GetIsFocusable());
		TestTrue(TEXT("Controls: the keys shown are Enter (go on) and Escape (skip)"), ContinuePrompt->GetDisplayedKey() == EKeys::Enter
			&& SkipPrompt->GetDisplayedKey() == EKeys::Escape);
		TestTrue(TEXT("Controls: the default labels are Continue and Skip"), Screen->ContinueLabel.ToString() == TEXT("Continue") && Screen->SkipLabel.ToString() == TEXT("Skip"));

		const FString Short = TEXT("A short first entry of forty characters.");
		const FString Long = TEXT("A second entry that is much longer than one line of the column. It has three sentences so that it has to wrap. ")
			TEXT("The third sentence makes sure that the block holds more than two lines at the default width of the column.");
		TArray<FStdTextSceneEntry> Entries;
		Entries.Add({ FText::FromString(Short), FLinearColor(0.9f, 0.8f, 0.5f, 1.f), FLinearColor::Black });
		Entries.Add({ FText::FromString(Long), FLinearColor(0.1f, 0.1f, 0.1f, 1.f), FLinearColor::White });
		Entries.Add({ FText::FromString(TEXT("The last entry.")), FLinearColor(0.95f, 0.95f, 0.95f, 1.f), FLinearColor::Black });
		Screen->CharsPerSecond = 40.f;
		Screen->SetEntries(Entries);
		TestEqual(TEXT("Nothing starts before StartScene"), Listener->Started.Num(), 0);
		Screen->StartScene();
		TestTrue(TEXT("Controls off: collapsed while an entry is shown"), !Screen->AreControlsShown() && Controls->GetVisibility() == ESlateVisibility::Collapsed);
		TestEqual(TEXT("StartScene shows entry 0"), Screen->GetCurrentIndex(), 0);
		TestTrue(TEXT("Entry 0 is reported as it starts"), Listener->Started.Num() == 1 && Listener->Started[0] == 0);
		TestTrue(TEXT("Entry 0 starts with nothing typed"), Screen->GetVisibleText().IsEmpty() && Screen->IsTyping());
		TestTrue(TEXT("Entry 0 takes its own text colour"), Screen->GetShownTextColor().Equals(Entries[0].TextColor));
		TestTrue(TEXT("Entry 0 takes its own background colour"), Screen->GetShownBackgroundColor().Equals(Entries[0].BackgroundColor));

		Screen->AdvanceSeconds(0.25f);
		TestEqual(TEXT("At 40 characters per second, 0.25 s types 10 characters"), Screen->GetVisibleText().Len(), 10);
		Screen->AdvanceSeconds(0.25f);
		TestEqual(TEXT("0.5 s types 20 characters"), Screen->GetVisibleText().Len(), 20);
		TestEqual(TEXT("The visible text is the start of the entry"), Screen->GetVisibleText(), Short.Left(20));
		TestEqual(TEXT("Text_Line shows the visible text"), Line->GetText().ToString(), Short.Left(20));

		Screen->Confirm();
		TestEqual(TEXT("Confirm on an entry being typed shows it whole"), Screen->GetVisibleText(), Short);
		TestEqual(TEXT("... and stays on it"), Screen->GetCurrentIndex(), 0);
		TestFalse(TEXT("... which is no longer typing"), Screen->IsTyping());
		TestEqual(TEXT("... and starts no entry"), Listener->Started.Num(), 1);

		Screen->Confirm();
		TestEqual(TEXT("Confirm on a whole entry shows the next"), Screen->GetCurrentIndex(), 1);
		TestTrue(TEXT("Entry 1 is reported as it starts"), Listener->Started.Num() == 2 && Listener->Started[1] == 1);
		TestTrue(TEXT("Entry 1 takes its own text colour"), Screen->GetShownTextColor().Equals(Entries[1].TextColor));
		TestTrue(TEXT("Entry 1 takes its own background colour (white)"), Screen->GetShownBackgroundColor().Equals(FLinearColor::White));
		AddInfo(FString::Printf(TEXT("Entry 1: %d lines, block %.0f x %.0f in a column of %.0f."), Screen->GetLineCount(), Screen->GetBlockSize().X,
			Screen->GetBlockSize().Y, Screen->ColumnWidth));
		TestTrue(TEXT("A long entry wraps onto several lines"), Screen->GetLineCount() >= 2);
		TestTrue(TEXT("... no wider than the column"), Screen->GetBlockSize().X <= Screen->ColumnWidth + 0.5f);
		TestEqual(TEXT("... with every word kept, the breaks in place of spaces"), Screen->GetLaidOutText().Replace(TEXT("\n"), TEXT(" ")), Long);
		const FString LaidOut = Screen->GetLaidOutText();
		Screen->AdvanceSeconds(1.f);
		TestEqual(TEXT("Typing fills the laid-out lines: the visible text is their start"), Screen->GetVisibleText(), LaidOut.Left(40));

		Screen->Confirm();
		Screen->Confirm();
		TestEqual(TEXT("Entry 2 after two confirms"), Screen->GetCurrentIndex(), 2);
		Screen->Confirm();
		Screen->Confirm();
		TestTrue(TEXT("Confirm on the last whole entry finishes the scene, not skipped"), Screen->IsFinished() && !Screen->WasSkipped());
		TestTrue(TEXT("The end is reported once, not skipped"), Listener->Finished.Num() == 1 && !Listener->Finished[0]);
		TestTrue(TEXT("A finished scene shows no text"), Line->GetText().IsEmpty());
		TestTrue(TEXT("A finished scene stays on screen until its owner closes it"), Screen->IsInViewport());
		Screen->Confirm();
		TestEqual(TEXT("A finished scene ignores confirm"), Listener->Started.Num(), 3);

		Screen->SetEntries(Entries);
		Screen->StartScene();
		Screen->Confirm();
		Screen->Confirm();
		TestTrue(TEXT("Back is handled"), Screen->HandleBackAction());
		TestTrue(TEXT("Back on entry 1 ends the scene as skipped"), Screen->IsFinished() && Screen->WasSkipped());
		TestTrue(TEXT("The skip is reported"), Listener->Finished.Num() == 2 && Listener->Finished[1]);
		TestTrue(TEXT("A skipped scene stays on screen until its owner closes it"), Screen->IsInViewport());
		TestTrue(TEXT("Each entry of the second play is reported (0, 1)"), Listener->Started.Num() == 5 && Listener->Started[3] == 0 && Listener->Started[4] == 1);

		Screen->SetEntries({});
		Screen->StartScene();
		TestTrue(TEXT("A scene with no entry finishes at once, not skipped"), Screen->IsFinished() && !Screen->WasSkipped() && Listener->Finished.Num() == 3);

		TestTrue(TEXT("Enter, Space and the gamepad's bottom face button confirm"), StdTextScene::IsConfirmKey(EKeys::Enter) && StdTextScene::IsConfirmKey(EKeys::SpaceBar)
			&& StdTextScene::IsConfirmKey(EKeys::Gamepad_FaceButton_Bottom));
		TestTrue(TEXT("Escape and the gamepad's right face button are back keys, not confirm keys"), UStdScreenWidget::IsBackKey(EKeys::Escape)
			&& UStdScreenWidget::IsBackKey(EKeys::Gamepad_FaceButton_Right) && !StdTextScene::IsConfirmKey(EKeys::Escape));
		TestTrue(TEXT("Controls off: still collapsed after the scenes"), Controls->GetVisibility() == ESlateVisibility::Collapsed);

		// --- The controls switched on ---
		Screen->SetShowControls(true);
		TestFalse(TEXT("Controls on: not shown on a finished scene"), Screen->AreControlsShown());
		Screen->ControlsOpacity = 0.5f;
		Screen->SetEntries(Entries);
		TestTrue(TEXT("Controls on: not shown before the start"), !Screen->AreControlsShown() && Controls->GetVisibility() == ESlateVisibility::Collapsed);
		Screen->StartScene();
		TestTrue(TEXT("Controls on: shown while an entry is on screen"), Screen->AreControlsShown() && Controls->GetVisibility() != ESlateVisibility::Collapsed);
		FLinearColor Expected = Entries[0].TextColor;
		Expected.A *= 0.5f;
		TestTrue(TEXT("Controls on: in the entry's text colour at the controls' opacity"), Screen->GetControlsColor().Equals(Expected)
			&& ContinuePrompt->Color.Equals(Expected) && SkipPrompt->Color.Equals(Expected));
		TestTrue(TEXT("Controls on: labelled Continue and Skip"), ContinuePrompt->Label.ToString() == TEXT("Continue") && SkipPrompt->Label.ToString() == TEXT("Skip"));
		const UOverlaySlot* ControlsSlot = Cast<UOverlaySlot>(Controls->Slot);
		TestTrue(TEXT("Controls on: bottom right corner, ControlsMargin from the edges"), ControlsSlot && ControlsSlot->GetHorizontalAlignment() == HAlign_Right
			&& ControlsSlot->GetVerticalAlignment() == VAlign_Bottom && ControlsSlot->GetPadding() == FMargin(0.f, 0.f, Screen->ControlsMargin, Screen->ControlsMargin));
		Screen->FocusDefaultWidget();
	});

	// The clicks go through Slate, so they need the controls laid out.
	WaitUntil(this, TEXT("the controls are laid out"), 5.0, []()
	{
		const UStdTextSceneWidget* Screen = FindShownTextScene();
		const UWidget* Button = Screen ? Screen->WidgetTree->FindWidget(TEXT("Button_Skip")) : nullptr;
		return Button && !WidgetCentre(Button).IsZero();
	});

	Step([this]()
	{
		APlayerController* PlayerController = GetPlayerController();
		UStdTextSceneWidget* Screen = FindShownTextScene();
		UStdTextSceneTestListener* Listener = nullptr;
		for (TObjectIterator<UStdTextSceneTestListener> It; It; ++It)
		{
			Listener = *It;
		}
		UButton* ContinueButton = Screen ? Cast<UButton>(Screen->WidgetTree->FindWidget(TEXT("Button_Continue"))) : nullptr;
		UButton* SkipButton = Screen ? Cast<UButton>(Screen->WidgetTree->FindWidget(TEXT("Button_Skip"))) : nullptr;
		if (!TestTrue(TEXT("Controls on: the scene and its controls are still shown"), Screen && Listener && ContinueButton && SkipButton))
		{
			return;
		}
		const UWidget* Background = Screen->WidgetTree->FindWidget(TEXT("Border_Background"));
		const int32 StartedBefore = Listener->Started.Num();
		const int32 FinishedBefore = Listener->Finished.Num();
		auto ScreenHasFocus = [Screen, PlayerController]()
		{
			return Screen->HasUserFocus(PlayerController);
		};

		TestTrue(TEXT("Clicks: entry 0 being typed"), Screen->GetCurrentIndex() == 0 && Screen->IsTyping());
		TestTrue(TEXT("Clicks: a click on Continue is delivered"), ClickWidget(ContinueButton));
		TestTrue(TEXT("Clicks: Continue on an entry being typed shows it whole, once"), Screen->GetCurrentIndex() == 0 && !Screen->IsTyping());
		TestTrue(TEXT("Clicks: the screen keeps the focus after a click on Continue"), ScreenHasFocus());
		TestTrue(TEXT("Clicks: a click on Continue is delivered again"), ClickWidget(ContinueButton));
		TestTrue(TEXT("Clicks: Continue on a whole entry shows the next, once"), Screen->GetCurrentIndex() == 1 && Screen->IsTyping());
		TestEqual(TEXT("Clicks: one entry started by the two clicks"), Listener->Started.Num() - StartedBefore, 1);
		FLinearColor Expected = Screen->GetShownTextColor();
		Expected.A *= Screen->ControlsOpacity;
		TestTrue(TEXT("Controls on: entry 1 recolours them (dark on white)"), Screen->GetControlsColor().Equals(Expected));

		// The mouse over a control: the keys still go on and skip.
		MoveMouse(WidgetCentre(ContinueButton));
		TestTrue(TEXT("Focus: Continue is hovered"), ContinueButton->IsHovered());
		TestTrue(TEXT("Focus: hovering Continue leaves the focus on the screen"), ScreenHasFocus());
		TestTrue(TEXT("Focus: Enter with the mouse over Continue is handled"), PressKey(EKeys::Enter));
		TestTrue(TEXT("Focus: ... and shows entry 1 whole, once"), Screen->GetCurrentIndex() == 1 && !Screen->IsTyping());
		MoveMouse(WidgetCentre(SkipButton));
		TestTrue(TEXT("Focus: hovering Skip leaves the focus on the screen"), SkipButton->IsHovered() && ScreenHasFocus());
		TestTrue(TEXT("Focus: Enter with the mouse over Skip goes on, never presses Skip"), PressKey(EKeys::Enter) && Screen->GetCurrentIndex() == 2 && !Screen->IsFinished());

		// A click on the scene away from the controls still goes on (bClickConfirms). Near the top left corner: an editor
		// window (the Message Log) may float over the middle of the viewport during a test run.
		if (Background)
		{
			const FVector2D Corner(0.04, 0.06);
			const FVector2D Point = WidgetPoint(Background, Corner);
			FSlateApplication& Slate = FSlateApplication::Get();
			const FWidgetPath Path = Slate.LocateWindowUnderMouse(Point, Slate.GetInteractiveTopLevelWindows(), false, FSlateApplication::CursorUserIndex);
			TestTrue(TEXT("Clicks: the point clicked on the scene is the scene's"), Path.IsValid() && Path.ContainsWidget(Screen->GetCachedWidget().Get()));
			FString Hit;
			TestTrue(TEXT("Clicks: a click on the scene is handled"), ClickWidget(Background, &Hit, Corner));
			AddInfo(FString::Printf(TEXT("Click on the scene %s"), *Hit));
			TestTrue(TEXT("Clicks: a click on the scene away from the controls still goes on (shows entry 2 whole)"), Screen->GetCurrentIndex() == 2 && !Screen->IsTyping());
		}

		const int32 StartedBeforeSkip = Listener->Started.Num();
		TestTrue(TEXT("Clicks: a click on Skip is delivered"), ClickWidget(SkipButton));
		TestTrue(TEXT("Clicks: Skip ends the scene as skipped"), Screen->IsFinished() && Screen->WasSkipped());
		TestTrue(TEXT("Clicks: the skip is reported once"), Listener->Finished.Num() == FinishedBefore + 1 && Listener->Finished.Last());
		TestEqual(TEXT("Clicks: Skip does not also go on (no entry started)"), Listener->Started.Num() - StartedBeforeSkip, 0);
		TestTrue(TEXT("Clicks: the screen keeps the focus after a click on Skip"), ScreenHasFocus());
		TestFalse(TEXT("Controls on: hidden once the scene has ended"), Screen->AreControlsShown());

		// After the clicks, the keys on a new play: confirm goes on, back skips.
		Screen->SetEntries({ { FText::FromString(TEXT("One more entry.")), FLinearColor::White, FLinearColor::Black } });
		Screen->StartScene();
		TestTrue(TEXT("Focus: after the clicks Enter still goes on"), PressKey(EKeys::Enter) && !Screen->IsTyping());
		TestTrue(TEXT("Focus: after the clicks the back key still skips"), PressKey(EKeys::Gamepad_FaceButton_Right) && Screen->IsFinished() && Screen->WasSkipped());
		TestFalse(TEXT("Focus: neither control holds the keyboard focus"), ContinueButton->HasAnyUserFocus() || SkipButton->HasAnyUserFocus());

		Screen->CloseScreen();
		Listener->RemoveFromRoot();
	});

	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
