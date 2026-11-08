// SPDX-License-Identifier: MIT

// Debug console commands for testing the time state and scan cadence without a keyboard
// (agents through the editor console, or the developer in PIE). Development builds only.

#include "Components/SpotLightComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "LidarScanComponent.h"
#include "Particles/ParticleSystemComponent.h"
#include "Scan/ScanDirectorComponent.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"
#include "TimerManager.h"
#include "UObject/UObjectIterator.h"

#if !UE_BUILD_SHIPPING

namespace TimeStateDebug
{
	/** The world the command should act on: the given one if it is a game world, else the first PIE/game world. */
	static UWorld* ResolveGameWorld(UWorld* World)
	{
		if (World && World->IsGameWorld())
		{
			return World;
		}
		if (GEngine)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* Candidate = Context.World();
				if (Candidate && Candidate->IsGameWorld())
				{
					return Candidate;
				}
			}
		}
		return nullptr;
	}

	/** Simulates a real key press on the first local player controller: pressed now, released next tick. */
	static bool PressKey(UWorld* World, const FKey& Key)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		if (!PC || !Key.IsValid())
		{
			return false;
		}

		PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Pressed, 1.f));

		TWeakObjectPtr<APlayerController> WeakPC = PC;
		World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([WeakPC, Key]()
		{
			if (APlayerController* Controller = WeakPC.Get())
			{
				Controller->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Released, 0.f));
			}
		}));
		return true;
	}

	struct FReport
	{
		ETimeState State = ETimeState::Normal;
		float StopDuration = 0.f;
		float TotalStopped = 0.f;
		int32 AliveScans = 0;
		int32 ScanCount = 0;
		bool bCycleActive = false;
		bool bFlashlightOn = false;
		int32 NiagaraComponents = 0;
		int32 ActiveNiagaraComponents = 0;
	};

	static FReport BuildReport(UWorld* World)
	{
		FReport Report;
		if (const UTimeStateSubsystem* TimeState = World->GetSubsystem<UTimeStateSubsystem>())
		{
			Report.State = TimeState->GetState();
			Report.StopDuration = TimeState->GetCurrentStopDuration();
			Report.TotalStopped = TimeState->GetTotalStoppedSeconds();
		}

		for (TObjectIterator<UScanDirectorComponent> It; It; ++It)
		{
			if (It->GetWorld() != World || !It->GetOwner())
			{
				continue;
			}
			Report.bCycleActive |= It->IsScanCycleActive();
			if (const ULidarScanComponent* Lidar = It->GetLidarScan())
			{
				Report.AliveScans += Lidar->GetAliveScanCount();
				Report.ScanCount += Lidar->GetScanCount();
			}
			if (const USpotLightComponent* Light = It->GetOwner()->FindComponentByClass<USpotLightComponent>())
			{
				Report.bFlashlightOn |= Light->IsVisible();
			}
		}

		for (TObjectIterator<UFXSystemComponent> It; It; ++It)
		{
			if (It->GetWorld() == World && It->IsRegistered())
			{
				++Report.NiagaraComponents;
				Report.ActiveNiagaraComponents += It->IsActive() ? 1 : 0;
			}
		}
		return Report;
	}

	static void LogReport(UWorld* World, const TCHAR* Prefix)
	{
		const FReport R = BuildReport(World);
		UE_LOG(LogTheLastShift, Log,
			TEXT("%s t=%.3f state=%s stop=%.3fs total=%.3fs aliveScans=%d scanCount=%d cycleActive=%d flashlight=%s niagara=%d (active %d)"),
			Prefix, World->GetTimeSeconds(), R.State == ETimeState::Stopped ? TEXT("Stopped") : TEXT("Normal"),
			R.StopDuration, R.TotalStopped, R.AliveScans, R.ScanCount, R.bCycleActive ? 1 : 0,
			R.bFlashlightOn ? TEXT("on") : TEXT("off"), R.NiagaraComponents, R.ActiveNiagaraComponents);
	}

	/** Stop/Resume cycle test driven by simulated presses of the time stop key. */
	struct FCycleTest
	{
		TWeakObjectPtr<UWorld> World;
		FKey Key;
		int32 Total = 0;
		int32 Done = 0;
		float StopSeconds = 0.f;
		float SettleSeconds = 0.f;
		int32 Failures = 0;
		int32 FirstNiagaraCount = INDEX_NONE;
		int32 MaxNiagaraCount = 0;
		FTimerHandle Timer;
		bool bRunning = false;
	};
	static FCycleTest GCycleTest;

	static void CycleStep(bool bStopPhase);

	static void ScheduleCycleStep(float Delay, bool bStopPhase)
	{
		if (UWorld* World = GCycleTest.World.Get())
		{
			World->GetTimerManager().SetTimer(GCycleTest.Timer,
				FTimerDelegate::CreateLambda([bStopPhase]() { CycleStep(bStopPhase); }), Delay, false);
		}
		else
		{
			GCycleTest.bRunning = false;
		}
	}

	static void CycleStep(bool bStopPhase)
	{
		UWorld* World = GCycleTest.World.Get();
		if (!World)
		{
			GCycleTest.bRunning = false;
			return;
		}

		if (bStopPhase)
		{
			// Press: Normal -> Stopped. Then press again after StopSeconds.
			PressKey(World, GCycleTest.Key);
			World->GetTimerManager().SetTimer(GCycleTest.Timer, FTimerDelegate::CreateLambda([]()
			{
				if (UWorld* W = GCycleTest.World.Get())
				{
					PressKey(W, GCycleTest.Key);
					ScheduleCycleStep(GCycleTest.SettleSeconds, false);
				}
			}), GCycleTest.StopSeconds, false);
			return;
		}

		// Settled after Resume: check that nothing survived.
		++GCycleTest.Done;
		const FReport R = BuildReport(World);
		const bool bOk = R.State == ETimeState::Normal && R.AliveScans == 0 && !R.bCycleActive && R.ActiveNiagaraComponents == 0;
		GCycleTest.Failures += bOk ? 0 : 1;
		if (GCycleTest.FirstNiagaraCount == INDEX_NONE)
		{
			GCycleTest.FirstNiagaraCount = R.NiagaraComponents;
		}
		GCycleTest.MaxNiagaraCount = FMath::Max(GCycleTest.MaxNiagaraCount, R.NiagaraComponents);

		LogReport(World, *FString::Printf(TEXT("CycleTest %d/%d %s:"), GCycleTest.Done, GCycleTest.Total, bOk ? TEXT("OK") : TEXT("FAIL")));

		if (GCycleTest.Done < GCycleTest.Total)
		{
			ScheduleCycleStep(0.3f, true);
			return;
		}

		GCycleTest.bRunning = false;
		UE_LOG(LogTheLastShift, Log, TEXT("CycleTest finished: %d cycles, %d failures, niagara components first=%d max=%d."),
			GCycleTest.Total, GCycleTest.Failures, GCycleTest.FirstNiagaraCount, GCycleTest.MaxNiagaraCount);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsKeyCommand(
	TEXT("tls.Key"),
	TEXT("Simulates a press and release of a key on the local player, e.g. 'tls.Key Q'."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		const FKey Key(Args.Num() > 0 ? FName(*Args[0]) : NAME_None);
		if (!TimeStateDebug::PressKey(World, Key))
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Key: no game world / player controller, or invalid key."));
			return;
		}
		UE_LOG(LogTheLastShift, Log, TEXT("tls.Key: pressed %s at t=%.3f."), *Key.ToString(), World->GetTimeSeconds());
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsReportCommand(
	TEXT("tls.Report"),
	TEXT("Logs time state, alive scans, scan timer state, flashlight and Niagara component counts."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		if (UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld))
		{
			TimeStateDebug::LogReport(World, TEXT("tls.Report:"));
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsCycleTestCommand(
	TEXT("tls.CycleTest"),
	TEXT("tls.CycleTest [Cycles=20] [StopSeconds=4.6] [Key=Q]: Stop/Resume cycles via simulated key presses, checks each Resume leaves nothing behind."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		using namespace TimeStateDebug;
		UWorld* World = ResolveGameWorld(InWorld);
		if (!World)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.CycleTest: no game world."));
			return;
		}
		if (GCycleTest.bRunning && GCycleTest.World.Get() == World)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.CycleTest: already running."));
			return;
		}

		float SettleSeconds = 0.f;
		for (TObjectIterator<UScanDirectorComponent> It; It; ++It)
		{
			if (It->GetWorld() == World)
			{
				SettleSeconds = FMath::Max(SettleSeconds, It->ResumeFadeOut);
			}
		}

		GCycleTest = FCycleTest();
		GCycleTest.World = World;
		GCycleTest.Total = Args.Num() > 0 ? FMath::Max(FCString::Atoi(*Args[0]), 1) : 20;
		GCycleTest.StopSeconds = Args.Num() > 1 ? FMath::Max(FCString::Atof(*Args[1]), 0.1f) : 4.6f;
		GCycleTest.Key = FKey(Args.Num() > 2 ? FName(*Args[2]) : FName(TEXT("Q")));
		GCycleTest.SettleSeconds = SettleSeconds + 0.15f;
		GCycleTest.bRunning = true;

		UE_LOG(LogTheLastShift, Log, TEXT("CycleTest started: %d cycles, %.2f s stopped each, settle %.2f s."),
			GCycleTest.Total, GCycleTest.StopSeconds, GCycleTest.SettleSeconds);
		LogReport(World, TEXT("CycleTest baseline:"));
		CycleStep(true);
	}));

#endif // !UE_BUILD_SHIPPING
