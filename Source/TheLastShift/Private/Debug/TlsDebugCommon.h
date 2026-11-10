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

	/** One line of time/scan state, then one line per entity in the world. */
	void LogReport(UWorld* World, const TCHAR* Prefix);
}

#endif // !UE_BUILD_SHIPPING
