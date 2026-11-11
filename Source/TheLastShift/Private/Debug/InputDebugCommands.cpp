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
#include "Settings/StdProjectSettings.h"
#include "TheLastShift.h"
#include "UI/StdMainMenuWidget.h"
#include "UI/StdPauseMenuWidget.h"
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

	/** The topmost screen of the main menu or of the pause menu, or null. */
	static UStdScreenWidget* FindTopScreen(APlayerController* PC)
	{
		UStdScreenWidget* Root = nullptr;
		if (const AStdMenuPlayerController* MenuPC = Cast<AStdMenuPlayerController>(PC))
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

	bool ClickMainMenuPlay(UWorld* World)
	{
		const AStdMenuPlayerController* MenuPC = Cast<AStdMenuPlayerController>(World ? World->GetFirstPlayerController() : nullptr);
		UStdMainMenuWidget* Menu = MenuPC ? MenuPC->GetMainMenu() : nullptr;
		// Button_Play is a protected BindWidget: read it by name, then click it as the player would.
		const FObjectProperty* Property = Menu ? FindFProperty<FObjectProperty>(Menu->GetClass(), TEXT("Button_Play")) : nullptr;
		UButton* Play = Property ? Cast<UButton>(Property->GetObjectPropertyValue_InContainer(Menu)) : nullptr;
		if (!Play)
		{
			return false;
		}
		Play->OnClicked.Broadcast();
		return true;
	}
}

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
	TEXT("tls.MenuPlay: clicks the main menu's Play button (the same handler as a real click)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		UE_LOG(LogTheLastShift, Display, TEXT("tls.MenuPlay: %s."), TimeStateDebug::ClickMainMenuPlay(World) ? TEXT("Play clicked") : TEXT("no main menu with a Play button"));
	}));

#endif // !UE_BUILD_SHIPPING
