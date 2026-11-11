// SPDX-License-Identifier: MIT

#pragma once

// Helpers shared by the tls.* console commands (implemented in TimeStateDebugCommands.cpp). Development builds only.

#include "CoreMinimal.h"
#include "Time/TimeState.h"

#if !UE_BUILD_SHIPPING

class UScanDirectorComponent;
class UWorld;
struct FKey;

namespace TimeStateDebug
{
	/** The world the command should act on: the given one if it is a game world, else the first PIE/game world. */
	UWorld* ResolveGameWorld(UWorld* World);

	/** Simulates a real key press on the first local player controller: pressed now, released next tick. */
	bool PressKey(UWorld* World, const FKey& Key);

	/** The scan director of the local player's pawn, else the first one in the world. */
	UScanDirectorComponent* FindPlayerDirector(UWorld* World);

	struct FReport
	{
		ETimeState State = ETimeState::Normal;
		float StopDuration = 0.f;
		float TotalStopped = 0.f;
		int32 AliveScans = 0;
		int32 ScanCount = 0;
		bool bCycleActive = false;
		bool bTimersPending = false;
		bool bScanReady = false;
		float CooldownRemaining = 0.f;
		int32 BeatCount = 0;
		float TimeToNextBeat = 0.f;
		bool bFlashlightOn = false;
		int32 NiagaraComponents = 0;
		int32 ActiveNiagaraComponents = 0;
	};

	FReport BuildReport(UWorld* World);

	/** Input state of the first local player (implemented in InputDebugCommands.cpp). */
	struct FInputReport
	{
		bool bHasController = false;
		FString ControllerClass;
		FString InputMode;
		bool bViewportIgnoresInput = false;
		FString MouseCaptureMode;
		bool bViewportFocused = false;
		bool bMouseCaptured = false;
		bool bCursorShown = false;
		/** The pawn's mapping context and StdCore's configured ones: "Name(priority)" when applied to the local player, else "Name(not applied)". */
		TArray<FString> MappingContexts;
		/** The pawn's own mapping context (its InputMapping property) is among them. */
		bool bPawnContextApplied = false;
		/** "Action=Key+Key" for every input action property of the pawn, as Enhanced Input resolves them now. */
		TArray<FString> PawnActionKeys;
		/** Number of the pawn's input actions with no key mapped. */
		int32 UnmappedPawnActions = 0;
		FString PawnName;
		bool bPawnInputEnabled = false;
		bool bPawnCanMove = false;
		bool bHasArea = false;
		bool bAreaReady = false;
		bool bHeldByArea = false;
		bool bTransitioning = false;
		bool bPausedByMenu = false;
		/** The widget holding keyboard focus in the topmost StdCore screen (main or pause menu), or empty. */
		FString MenuFocus;

		/** Game input: viewport focus and capture, cursor hidden, the pawn's context applied, possessed and released. */
		bool IsGameReady() const;
		/** A menu: UI-only input, cursor shown, a menu widget focused for keyboard and gamepad. */
		bool IsMenuReady() const;
	};

	FInputReport BuildInputReport(UWorld* World);

	/** One line of input state with a verdict: "game" (IsGameReady), "menu" (IsMenuReady) or "none". */
	void LogInputReport(UWorld* World, const TCHAR* Prefix);

	/** Clicks the main menu's New game (the game's menu has no Play), as a real click would. False when no main menu is shown. */
	bool ClickMainMenuPlay(UWorld* World);

	/** Clicks the main menu button named Name (Button_Continue, Button_SelectAct, Button_NewGame, ...). False when absent or hidden. */
	bool ClickMainMenuButton(UWorld* World, FName Name);

	/** Clicks entry Index (INDEX_NONE = back) of the topmost choice list (the loss screen, or Select act over the main menu). */
	bool ClickChoiceEntry(UWorld* World, int32 Index);

	/** Labels of the topmost choice list's entries ("" when none is shown). */
	FString DescribeChoiceList(UWorld* World);

	/** Labels of the visible main menu buttons, in order ("" when no main menu is shown). */
	FString DescribeMainMenu(UWorld* World);

	/** One line of time/scan state, then one line per entity in the world. */
	void LogReport(UWorld* World, const TCHAR* Prefix);
}

#endif // !UE_BUILD_SHIPPING
