// SPDX-License-Identifier: MIT

// Input state of the local player, for checks that simulated key presses cannot make: tls.Key injects keys into the
// player controller and bypasses the viewport, so a test driven by console commands passes even when a real keyboard
// reaches nothing. This reads what decides whether real keys arrive: the input mode, viewport focus and mouse capture,
// the cursor, the mapping contexts and the keys they resolve to, the possessed pawn and the area's player lock.
// tls.InputReport logs it; tls.Report and tls.RunChainTest include it. tls.MenuPlay presses the main menu's Play button.
// Development builds only.

#include "Area/AreaGenerator.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Debug/TlsDebugCommon.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "Flow/StdFlowSubsystem.h"
#include "Framework/StdMenuPlayerController.h"
#include "Framework/StdPlayerController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Input/StdInputSubsystem.h"
#include "Settings/StdProjectSettings.h"
#include "TheLastShift.h"
#include "Components/TextBlock.h"
#include "Run/RunActListWidget.h"
#include "Run/RunSubsystem.h"
#include "UI/StdChoiceListWidget.h"
#include "UI/StdMainMenuWidget.h"
#include "UI/StdPauseMenuWidget.h"
#include "Containers/Ticker.h"
#include "Framework/Application/SlateApplication.h"
#include "UnrealClient.h"
#include "UObject/UnrealType.h"

#if !UE_BUILD_SHIPPING

namespace TimeStateDebug
{
	bool FInputReport::IsGameReady() const
	{
		const bool bCaptureMode = MouseCaptureMode.Contains(TEXT("CapturePermanently"));
		return bHasController && !bViewportIgnoresInput && bCaptureMode && bViewportFocused && bMouseCaptured && !bCursorShown
			&& bPawnContextApplied && UnmappedPawnActions == 0 && !PawnName.IsEmpty() && bPawnInputEnabled && bPawnCanMove
			&& (!bHasArea || (bAreaReady && !bHeldByArea)) && !bTransitioning && !bPausedByMenu;
	}

	bool FInputReport::IsMenuReady() const
	{
		return bHasController && bViewportIgnoresInput && bCursorShown && !bViewportFocused && !MenuFocus.IsEmpty() && !bTransitioning;
	}

	const UInputAction* FindPawnAction(UWorld* World, FName PropertyName)
	{
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		const FObjectProperty* Property = Pawn ? FindFProperty<FObjectProperty>(Pawn->GetClass(), PropertyName) : nullptr;
		return Property ? Cast<UInputAction>(Property->GetObjectPropertyValue_InContainer(Pawn)) : nullptr;
	}

	UStdInputSubsystem* StdInputOf(UWorld* World)
	{
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		const ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr;
		return LocalPlayer ? LocalPlayer->GetSubsystem<UStdInputSubsystem>() : nullptr;
	}

	FKey BoundKey(UWorld* World, FName PropertyName)
	{
		const UStdInputSubsystem* Input = StdInputOf(World);
		const FKey Key = Input ? Input->GetKeyForAction(FindPawnAction(World, PropertyName)) : FKey();
		if (!Key.IsValid())
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("BoundKey: the pawn's %s has no key."), *PropertyName.ToString());
		}
		return Key;
	}

	TArray<FStdKeyBindingRow> SaveBindings(UWorld* World)
	{
		const UStdInputSubsystem* Input = StdInputOf(World);
		return Input ? Input->GetRemappableKeys() : TArray<FStdKeyBindingRow>();
	}

	bool RestoreBindings(UWorld* World, const TArray<FStdKeyBindingRow>& Saved)
	{
		UStdInputSubsystem* Input = StdInputOf(World);
		if (!Input)
		{
			return false;
		}
		Input->ResetKeysToDefaults();
		// Giving a row its saved key swaps it with the row holding that key, which never holds its own saved key (the
		// saved keys are distinct): rows already restored are not touched again, so one pass is enough.
		for (const FStdKeyBindingRow& Row : Saved)
		{
			if (Row.CurrentKey.IsValid() && Row.CurrentKey != Row.DefaultKey)
			{
				FText Message;
				Input->RemapKey(Row.MappingName, Row.Slot, Row.CurrentKey, Message);
			}
		}
		bool bAllBack = true;
		for (const FStdKeyBindingRow& Now : Input->GetRemappableKeys())
		{
			const FStdKeyBindingRow* Was = Saved.FindByPredicate([&Now](const FStdKeyBindingRow& Row)
			{
				return Row.MappingName == Now.MappingName && Row.Slot == Now.Slot;
			});
			bAllBack &= Was && Was->CurrentKey == Now.CurrentKey;
		}
		return bAllBack;
	}

	FKey FreeKey(UWorld* World, const TArray<FKey>& Avoid)
	{
		const UStdInputSubsystem* Input = StdInputOf(World);
		if (!Input)
		{
			return FKey();
		}
		const TArray<FStdKeyBindingRow> Rows = Input->GetRemappableKeys();
		for (const FKey& Candidate : { EKeys::R, EKeys::T, EKeys::G, EKeys::H, EKeys::J, EKeys::K, EKeys::L, EKeys::Y, EKeys::U, EKeys::O, EKeys::P })
		{
			FText Reason;
			const bool bHeld = Rows.ContainsByPredicate([&Candidate](const FStdKeyBindingRow& Row) { return Row.CurrentKey == Candidate || Row.DefaultKey == Candidate; });
			if (!bHeld && !Avoid.Contains(Candidate) && Input->CanAssignKey(Candidate, Reason))
			{
				return Candidate;
			}
		}
		return FKey();
	}

	/** The topmost screen of the main menu or of the pause menu, or null. */
	static UStdScreenWidget* FindTopScreen(APlayerController* PC)
	{
		UStdScreenWidget* Root = nullptr;
		const URunSubsystem* Run = URunSubsystem::Get(PC);
		if (URunActListWidget* Loss = Run ? Run->GetLossScreen() : nullptr; Loss && Loss->IsInViewport() && Loss->GetOwningPlayer() == PC)
		{
			Root = Loss;
		}
		else if (const AStdMenuPlayerController* MenuPC = Cast<AStdMenuPlayerController>(PC))
		{
			Root = MenuPC->GetMainMenu();
		}
		else if (const AStdPlayerController* GamePC = Cast<AStdPlayerController>(PC))
		{
			Root = GamePC->GetPauseMenu();
		}
		return Root ? Root->GetTopScreen() : nullptr;
	}

	FInputReport BuildInputReport(UWorld* World)
	{
		FInputReport R;
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		if (!PC)
		{
			return R;
		}
		R.bHasController = true;
		R.ControllerClass = PC->GetClass()->GetName();
		R.InputMode = PC->GetCurrentInputModeDebugString();
		if (R.InputMode.IsEmpty())
		{
			R.InputMode = TEXT("<never set>");
		}
		R.bCursorShown = PC->ShouldShowMouseCursor();

		if (UGameViewportClient* Viewport = World->GetGameViewport())
		{
			R.bViewportIgnoresInput = Viewport->IgnoreInput();
			R.MouseCaptureMode = StaticEnum<EMouseCaptureMode>()->GetNameStringByValue(static_cast<int64>(Viewport->GetMouseCaptureMode()));
			if (const FViewport* SceneViewport = Viewport->Viewport)
			{
				R.bViewportFocused = SceneViewport->HasFocus();
				R.bMouseCaptured = SceneViewport->HasMouseCapture();
			}
		}

		APawn* Pawn = PC->GetPawn();
		const UInputMappingContext* PawnContext = nullptr;
		TArray<const UInputAction*> PawnActions;
		if (Pawn)
		{
			R.PawnName = Pawn->GetName();
			R.bPawnInputEnabled = Pawn->InputEnabled();
			const ACharacter* Character = Cast<ACharacter>(Pawn);
			R.bPawnCanMove = !Character || Character->GetCharacterMovement()->MovementMode != MOVE_None;
			// The pawn's input assets, read by type so that any pawn class works.
			for (TFieldIterator<FObjectProperty> It(Pawn->GetClass()); It; ++It)
			{
				const UObject* Value = It->GetObjectPropertyValue_InContainer(Pawn);
				if (const UInputMappingContext* Context = Cast<UInputMappingContext>(Value))
				{
					PawnContext = Context;
				}
				else if (const UInputAction* Action = Cast<UInputAction>(Value))
				{
					PawnActions.AddUnique(Action);
				}
			}
		}

		// The contexts that can be on the player: the pawn's own and those StdCore adds to every local player.
		const UEnhancedInputLocalPlayerSubsystem* EnhancedInput = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer());
		TArray<const UInputMappingContext*> Candidates;
		if (PawnContext)
		{
			Candidates.Add(PawnContext);
		}
		for (const FStdMappingContextSetting& Entry : UStdProjectSettings::Get()->MappingContexts)
		{
			if (const UInputMappingContext* Context = Entry.MappingContext.Get())
			{
				Candidates.AddUnique(Context);
			}
		}
		for (const UInputMappingContext* Context : Candidates)
		{
			int32 Priority = 0;
			const bool bApplied = EnhancedInput && EnhancedInput->HasMappingContext(Context, Priority);
			R.MappingContexts.Add(bApplied ? FString::Printf(TEXT("%s(%d)"), *Context->GetName(), Priority) : FString::Printf(TEXT("%s(not applied)"), *Context->GetName()));
			R.bPawnContextApplied |= bApplied && Context == PawnContext;
		}
		for (const UInputAction* Action : PawnActions)
		{
			TArray<FString> KeyNames;
			if (EnhancedInput)
			{
				for (const FKey& Key : EnhancedInput->QueryKeysMappedToAction(Action))
				{
					KeyNames.Add(Key.ToString());
				}
			}
			R.UnmappedPawnActions += KeyNames.IsEmpty() ? 1 : 0;
			R.PawnActionKeys.Add(FString::Printf(TEXT("%s=%s"), *Action->GetName(), KeyNames.IsEmpty() ? TEXT("<none>") : *FString::Join(KeyNames, TEXT("+"))));
		}

		if (const AAreaGenerator* Area = AAreaGenerator::Find(World))
		{
			R.bHasArea = true;
			R.bAreaReady = Area->IsReady();
			R.bHeldByArea = Area->IsHoldingPlayer();
		}
		const UGameInstance* GameInstance = World->GetGameInstance();
		const UStdFlowSubsystem* Flow = GameInstance ? GameInstance->GetSubsystem<UStdFlowSubsystem>() : nullptr;
		R.bTransitioning = Flow && Flow->IsTransitioning();
		if (const AStdPlayerController* GamePC = Cast<AStdPlayerController>(PC))
		{
			R.bPausedByMenu = GamePC->IsPausedByMenu();
		}

		if (UStdScreenWidget* Screen = FindTopScreen(PC))
		{
			Screen->WidgetTree->ForEachWidget([&R, Screen](const UWidget* Widget)
			{
				if (R.MenuFocus.IsEmpty() && Widget->HasKeyboardFocus())
				{
					R.MenuFocus = FString::Printf(TEXT("%s.%s"), *Screen->GetClass()->GetName(), *Widget->GetName());
				}
			});
		}
		return R;
	}

	void LogInputReport(UWorld* World, const TCHAR* Prefix)
	{
		const FInputReport R = BuildInputReport(World);
		const TCHAR* Verdict = R.IsGameReady() ? TEXT("game") : R.IsMenuReady() ? TEXT("menu") : TEXT("none");
		UE_LOG(LogTheLastShift, Display,
			TEXT("%s   input: verdict=%s controller=%s mode=%s viewportIgnoresInput=%d captureMode=%s viewportFocus=%d mouseCaptured=%d cursor=%s pawn=%s pawnInput=%d pawnCanMove=%d area=%s held=%d transitioning=%d pausedByMenu=%d menuFocus=%s"),
			Prefix, Verdict, *R.ControllerClass, *R.InputMode, R.bViewportIgnoresInput ? 1 : 0, *R.MouseCaptureMode, R.bViewportFocused ? 1 : 0,
			R.bMouseCaptured ? 1 : 0, R.bCursorShown ? TEXT("shown") : TEXT("hidden"), R.PawnName.IsEmpty() ? TEXT("<none>") : *R.PawnName,
			R.bPawnInputEnabled ? 1 : 0, R.bPawnCanMove ? 1 : 0, !R.bHasArea ? TEXT("none") : R.bAreaReady ? TEXT("ready") : TEXT("not-ready"),
			R.bHeldByArea ? 1 : 0, R.bTransitioning ? 1 : 0, R.bPausedByMenu ? 1 : 0, R.MenuFocus.IsEmpty() ? TEXT("<none>") : *R.MenuFocus);
		UE_LOG(LogTheLastShift, Display, TEXT("%s   input: contexts=[%s] pawnContextApplied=%d keys=[%s]"),
			Prefix, *FString::Join(R.MappingContexts, TEXT(", ")), R.bPawnContextApplied ? 1 : 0, *FString::Join(R.PawnActionKeys, TEXT(", ")));
	}

	bool ClickMainMenuButton(UWorld* World, FName Name)
	{
		const AStdMenuPlayerController* MenuPC = Cast<AStdMenuPlayerController>(World ? World->GetFirstPlayerController() : nullptr);
		UStdMainMenuWidget* Menu = MenuPC ? MenuPC->GetMainMenu() : nullptr;
		// The buttons are protected members: find them by name in the tree, then click as the player would.
		UButton* Button = Menu && Menu->WidgetTree ? Cast<UButton>(Menu->WidgetTree->FindWidget(Name)) : nullptr;
		if (!Button || Button->GetVisibility() == ESlateVisibility::Collapsed || !Button->GetIsEnabled() || !Menu->IsVisible())
		{
			return false;
		}
		Button->OnClicked.Broadcast();
		return true;
	}

	bool ClickMainMenuPlay(UWorld* World)
	{
		return ClickMainMenuButton(World, TEXT("Button_NewGame")) || ClickMainMenuButton(World, TEXT("Button_Play"));
	}

	static UStdChoiceListWidget* FindChoiceList(UWorld* World)
	{
		return Cast<UStdChoiceListWidget>(FindTopScreen(World ? World->GetFirstPlayerController() : nullptr));
	}

	bool ClickChoiceEntry(UWorld* World, int32 Index)
	{
		UStdChoiceListWidget* List = FindChoiceList(World);
		UButton* Button = List ? List->GetEntryButton(Index) : nullptr;
		if (!Button || !Button->GetIsEnabled())
		{
			return false;
		}
		Button->OnClicked.Broadcast();
		return true;
	}

	FString DescribeChoiceList(UWorld* World)
	{
		const UStdChoiceListWidget* List = FindChoiceList(World);
		if (!List)
		{
			return FString();
		}
		TArray<FString> Labels;
		for (int32 Index = 0; Index < List->GetEntryCount(); ++Index)
		{
			Labels.Add(List->GetEntryLabel(Index).ToString());
		}
		if (List->GetEntryButton(INDEX_NONE))
		{
			Labels.Add(TEXT("<back>"));
		}
		return FString::Join(Labels, TEXT(" | "));
	}

	FString DescribeMainMenu(UWorld* World)
	{
		const AStdMenuPlayerController* MenuPC = Cast<AStdMenuPlayerController>(World ? World->GetFirstPlayerController() : nullptr);
		UStdMainMenuWidget* Menu = MenuPC ? MenuPC->GetMainMenu() : nullptr;
		if (!Menu || !Menu->WidgetTree)
		{
			return FString();
		}
		TArray<FString> Labels;
		Menu->WidgetTree->ForEachWidget([&Labels](UWidget* Widget)
		{
			const UButton* Button = Cast<UButton>(Widget);
			if (!Button || Button->GetVisibility() == ESlateVisibility::Collapsed)
			{
				return;
			}
			const UTextBlock* Label = Cast<UTextBlock>(Button->GetContent());
			Labels.Add(Label ? Label->GetText().ToString() : Button->GetName());
		});
		return FString::Join(Labels, TEXT(" | "));
	}
}

namespace TimeStateDebug
{
	/** The keys tls.UI.Keys still has to send, and the world it acts on. */
	struct FUIKeys
	{
		TArray<FKey> Keys;
		int32 Next = 0;
		TWeakObjectPtr<UWorld> World;
		bool bActive = false;
	};
	static FUIKeys GUIKeys;

	static FString FocusName(UWorld* World)
	{
		const FInputReport R = BuildInputReport(World);
		return R.MenuFocus.IsEmpty() ? FString(TEXT("<none>")) : R.MenuFocus;
	}

	static bool TickUIKeys(float)
	{
		UWorld* World = GUIKeys.World.Get();
		if (!World || !FSlateApplication::IsInitialized() || GUIKeys.Next >= GUIKeys.Keys.Num())
		{
			UE_LOG(LogTheLastShift, Display, TEXT("tls.UI.Keys: done, focus %s, top screen %s."), World ? *FocusName(World) : TEXT("-"),
				World ? *GetNameSafe(FindTopScreen(World->GetFirstPlayerController())) : TEXT("-"));
			GUIKeys.bActive = false;
			return false;
		}
		const FKey Key = GUIKeys.Keys[GUIKeys.Next++];
		const FString Before = FocusName(World);
		// A real key through Slate: navigation, Accept and Back go where a keyboard or a gamepad would send them.
		FSlateApplication& Slate = FSlateApplication::Get();
		const FKeyEvent Down(Key, FModifierKeysState(), 0, false, 0, 0);
		const bool bHandled = Slate.ProcessKeyDownEvent(Down);
		const FKeyEvent Up(Key, FModifierKeysState(), 0, false, 0, 0);
		Slate.ProcessKeyUpEvent(Up);
		UE_LOG(LogTheLastShift, Display, TEXT("tls.UI.Keys: %s (handled %d): focus %s -> %s, top screen %s."), *Key.ToString(), bHandled ? 1 : 0, *Before,
			*FocusName(World), *GetNameSafe(FindTopScreen(World->GetFirstPlayerController())));
		return true;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsUIKeysCommand(
	TEXT("tls.UI.Keys"),
	TEXT("tls.UI.Keys Key [Key ...]: focuses the default widget of the topmost menu screen (the console box holds the focus while a command is typed), then sends each key through Slate, one per tick, as a keyboard or gamepad would (Down, Up, Enter, Escape, Gamepad_DPad_Down, Gamepad_FaceButton_Bottom, Gamepad_FaceButton_Right, ...), logging the focused widget after each."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		using namespace TimeStateDebug;
		UWorld* World = ResolveGameWorld(InWorld);
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		UStdScreenWidget* Screen = FindTopScreen(PC);
		if (!Screen || GUIKeys.bActive || Args.Num() == 0)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.UI.Keys: needs keys and a menu screen on screen (main menu, Select act, pause menu or loss screen)."));
			return;
		}
		GUIKeys = FUIKeys();
		for (const FString& Arg : Args)
		{
			const FKey Key(*Arg);
			if (Key.IsValid())
			{
				GUIKeys.Keys.Add(Key);
			}
		}
		GUIKeys.World = World;
		GUIKeys.bActive = true;
		Screen->FocusDefaultWidget();
		UE_LOG(LogTheLastShift, Display, TEXT("tls.UI.Keys: %d keys on %s, start focus %s."), GUIKeys.Keys.Num(), *Screen->GetName(), *FocusName(World));
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickUIKeys), 0.15f);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsInputReportCommand(
	TEXT("tls.InputReport"),
	TEXT("tls.InputReport: logs the local player's input state (input mode, viewport focus and mouse capture, cursor, mapping contexts and the keys of the pawn's actions, pawn, area lock, transition, menu pause, focused menu widget) with a verdict: game, menu or none."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		if (UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld))
		{
			TimeStateDebug::LogInputReport(World, TEXT("tls.InputReport:"));
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsMenuPlayCommand(
	TEXT("tls.MenuPlay"),
	TEXT("tls.MenuPlay: clicks the main menu's New game button (the same handler as a real click)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		UE_LOG(LogTheLastShift, Display, TEXT("tls.MenuPlay: %s."), TimeStateDebug::ClickMainMenuPlay(World) ? TEXT("New game clicked") : TEXT("no main menu with a New game button"));
	}));

#endif // !UE_BUILD_SHIPPING
