// SPDX-License-Identifier: MIT

// Debug console commands for testing the time state and scan cadence without a keyboard
// (agents through the editor console, or the developer in PIE). Development builds only.

#include "Area/AreaGenerator.h"
#include "Run/RunSubsystem.h"
#include "Time/HomeStateSubsystem.h"
#include "Components/SpotLightComponent.h"
#include "Debug/ScanTestListener.h"
#include "Debug/TlsDebugCommon.h"
#include "Energy/DoorDevice.h"
#include "Energy/EnergySubsystem.h"
#include "Energy/LightGroupDevice.h"
#include "Energy/PoweredDevice.h"
#include "Energy/PowerTerminal.h"
#include "Entity/Entity.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "LidarScanComponent.h"
#include "Particles/ParticleSystemComponent.h"
#include "Residue/Residue.h"
#include "Scan/ScanDirectorComponent.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"
#include "TimerManager.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"

// --- UTlsScanTestListener (reflected, so it exists in every configuration; only used by tls.ScanTest) ---

void UTlsScanTestListener::Bind(UScanDirectorComponent* InDirector)
{
	Unbind();
	Director = InDirector;
	if (InDirector)
	{
		InDirector->OnScanCompleted.AddDynamic(this, &UTlsScanTestListener::HandleScanCompleted);
		InDirector->OnScanReady.AddDynamic(this, &UTlsScanTestListener::HandleScanReady);
		InDirector->OnBeat.AddDynamic(this, &UTlsScanTestListener::HandleBeat);
	}
}

void UTlsScanTestListener::Unbind()
{
	if (UScanDirectorComponent* Current = Director.Get())
	{
		Current->OnScanCompleted.RemoveDynamic(this, &UTlsScanTestListener::HandleScanCompleted);
		Current->OnScanReady.RemoveDynamic(this, &UTlsScanTestListener::HandleScanReady);
		Current->OnBeat.RemoveDynamic(this, &UTlsScanTestListener::HandleBeat);
	}
	Director.Reset();
}

float UTlsScanTestListener::GetStopElapsed() const
{
	const UScanDirectorComponent* Current = Director.Get();
	const UWorld* World = Current ? Current->GetWorld() : nullptr;
	const UTimeStateSubsystem* TimeState = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
	return TimeState ? TimeState->GetCurrentStopDuration() : 0.f;
}

void UTlsScanTestListener::HandleScanCompleted(int32 ScanIndex, float Timestamp)
{
	const float T = GetStopElapsed();
	const UScanDirectorComponent* Current = Director.Get();
	const float Cooldown = Current ? FMath::Clamp(Current->ScanCooldown, 0.5f, 15.f) : 0.f;
	ScanTimes.Add(T);
	ScanCooldowns.Add(Cooldown);
	UE_LOG(LogTheLastShift, Log, TEXT("ScanTest  %7.3f s  scan %d completed (ScanCooldown %.2f s)"), T, ScanIndex, Cooldown);
}

void UTlsScanTestListener::HandleScanReady()
{
	const float T = GetStopElapsed();
	ReadyTimes.Add(T);
	UE_LOG(LogTheLastShift, Log, TEXT("ScanTest  %7.3f s  scan ready"), T);
}

void UTlsScanTestListener::HandleBeat(int32 BeatIndex)
{
	const float T = GetStopElapsed();
	const UScanDirectorComponent* Current = Director.Get();
	const float Interval = Current ? FMath::Clamp(Current->BeatInterval, 0.5f, 15.f) : 0.f;
	BeatTimes.Add(T);
	BeatIndices.Add(BeatIndex);
	BeatIntervals.Add(Interval);
	UE_LOG(LogTheLastShift, Log, TEXT("ScanTest  %7.3f s  beat %d (GetBeatCount=%d, BeatInterval %.2f s)"), T, BeatIndex,
		Current ? Current->GetBeatCount() : -1, Interval);
}

#if !UE_BUILD_SHIPPING

namespace TimeStateDebug
{
	UWorld* ResolveGameWorld(UWorld* World)
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

	bool PressKey(UWorld* World, const FKey& Key)
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

	FReport BuildReport(UWorld* World)
	{
		FReport Report;
		if (const UTimeStateSubsystem* TimeState = World->GetSubsystem<UTimeStateSubsystem>())
		{
			Report.State = TimeState->GetState();
			Report.StopDuration = TimeState->GetCurrentStopDuration();
			Report.TotalStopped = TimeState->GetCumulativeStoppedSeconds();
		}

		for (TObjectIterator<UScanDirectorComponent> It; It; ++It)
		{
			if (It->GetWorld() != World || !It->GetOwner())
			{
				continue;
			}
			Report.bCycleActive |= It->IsScanCycleActive();
			Report.bTimersPending |= It->HasPendingTimers();
			Report.bScanReady |= It->IsScanReady();
			Report.CooldownRemaining = FMath::Max(Report.CooldownRemaining, It->GetCooldownRemaining());
			Report.BeatCount = FMath::Max(Report.BeatCount, It->GetBeatCount());
			Report.TimeToNextBeat = FMath::Max(Report.TimeToNextBeat, It->GetTimeToNextBeat());
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

	void LogReport(UWorld* World, const TCHAR* Prefix)
	{
		const FReport R = BuildReport(World);
		UE_LOG(LogTheLastShift, Log,
			TEXT("%s t=%.3f state=%s stop=%.3fs cumulative=%.3fs aliveScans=%d scanCount=%d cycleActive=%d timers=%d ready=%d cooldownLeft=%.3fs beats=%d nextBeat=%.3fs flashlight=%s niagara=%d (active %d)"),
			Prefix, World->GetTimeSeconds(), R.State == ETimeState::Stopped ? TEXT("Stopped") : TEXT("Normal"),
			R.StopDuration, R.TotalStopped, R.AliveScans, R.ScanCount, R.bCycleActive ? 1 : 0, R.bTimersPending ? 1 : 0,
			R.bScanReady ? 1 : 0, R.CooldownRemaining, R.BeatCount, R.TimeToNextBeat,
			R.bFlashlightOn ? TEXT("on") : TEXT("off"), R.NiagaraComponents, R.ActiveNiagaraComponents);

		for (TActorIterator<AEntity> It(World); It; ++It)
		{
			const AEntity* Entity = *It;
			UE_LOG(LogTheLastShift, Log,
				TEXT("%s   entity %s: state=%s manifested=%d figure=%s pathDistance=%.1fcm proximity=%.3f nextSpawn=%.1fcm lastExit=%.1fcm lastGathered figure=%d trail=%d"),
				Prefix, *Entity->GetName(), Entity->GetState() == EEntityState::Awake ? TEXT("Awake") : TEXT("Dormant"),
				Entity->IsManifested() ? 1 : 0, *Entity->GetFigureLocation().ToString(), Entity->GetPathDistanceToPlayer(),
				Entity->GetProximity01(), Entity->GetSpawnDistanceForNextStop(), Entity->GetLastExitPathDistance(),
				Entity->GetLastGatheredFigurePoints(), Entity->GetLastGatheredTrailPoints());
		}

		if (const AAreaGenerator* Area = AAreaGenerator::Find(World))
		{
			UE_LOG(LogTheLastShift, Log, TEXT("%s   area: act=%d map=%s ready=%d seed=%d hash=%016llx route=%.1fm pcgInstances=%d timeToReady=%.2fs"),
				Prefix, Area->GetAct(), *UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()), Area->IsReady() ? 1 : 0, Area->GetSeed(),
				static_cast<uint64>(Area->GetLayoutHash()), Area->GetRouteLength() / 100.f, Area->GetPCGInstanceCount(), Area->GetTimeToReadySeconds());
		}
		else
		{
			UE_LOG(LogTheLastShift, Log, TEXT("%s   area: none (act=%d map=%s)"), Prefix, URunSubsystem::GetActIndexOf(World), *UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()));
		}
		if (const UEnergySubsystem* Energy = UEnergySubsystem::Get(World))
		{
			UE_LOG(LogTheLastShift, Log, TEXT("%s   energy: banked=%d session=%d total=%d boundDirector=%d"),
				Prefix, Energy->GetBankedEnergy(), Energy->GetSessionEnergy(), Energy->GetTotalEnergy(), Energy->HasBoundDirector() ? 1 : 0);
		}
		if (const UHomeStateSubsystem* Home = UHomeStateSubsystem::Get(World))
		{
			UE_LOG(LogTheLastShift, Log, TEXT("%s   home: battery=%.4f homeSeconds=%.0f (%.3f years) jumps=%d lost=%d boundDirector=%d"),
				Prefix, Home->GetBatteryFraction(), Home->GetHomeSecondsElapsed(), Home->GetHomeSecondsElapsed() / 31557600.0, Home->GetJumpCount(),
				Home->IsRunLost() ? 1 : 0, Home->HasBoundDirector() ? 1 : 0);
		}
		if (const URunSubsystem* Run = URunSubsystem::Get(World))
		{
			FString List;
			for (const FAreaSnapshot& Snapshot : Run->GetSnapshots())
			{
				List += FString::Printf(TEXT(" [act %d: battery %.4f, home %.0f s, banked %d]"), Snapshot.ActIndex, Snapshot.BatteryFraction, Snapshot.HomeSecondsElapsed, Snapshot.BankedEnergy);
			}
			UE_LOG(LogTheLastShift, Log, TEXT("%s   run: snapshots=%d%s savedRun=%d saveWrites=%d lossScreen=%d"), Prefix, Run->GetSnapshots().Num(), *List,
				Run->HasSavedRun() ? 1 : 0, Run->GetSaveWrites(), Run->GetLossScreen() ? 1 : 0);
		}
		for (TActorIterator<AResidue> It(World); It; ++It)
		{
			UE_LOG(LogTheLastShift, Log, TEXT("%s   residue %s: state=%s regenerates=%d collecting=%d progress=%.2f lastGathered=%d at %s"),
				Prefix, *It->GetName(), LexToString(It->GetState()), It->bRegenerates ? 1 : 0, It->IsCollecting() ? 1 : 0,
				It->GetCollectProgress(), It->GetLastGatheredPoints(), *It->GetActorLocation().ToString());
		}
		for (TActorIterator<APoweredDevice> It(World); It; ++It)
		{
			FString Extra;
			if (const ADoorDevice* Door = Cast<ADoorDevice>(*It))
			{
				Extra = FString::Printf(TEXT(" open=%d closePending=%d doorwayOccupied=%d"), Door->IsOpen() ? 1 : 0,
					Door->IsClosePending() ? 1 : 0, Door->IsDoorwayOccupied() ? 1 : 0);
			}
			else if (const ALightGroupDevice* Lights = Cast<ALightGroupDevice>(*It))
			{
				Extra = FString::Printf(TEXT(" lit=%d/%d"), Lights->GetLitCount(), Lights->Lights.Num());
			}
			UE_LOG(LogTheLastShift, Log, TEXT("%s   device %s: powered=%d remaining=%.3fs fraction=%.3f PoweredSeconds=%.2f resetOnJump=%d%s"),
				Prefix, *It->GetName(), It->IsPowered() ? 1 : 0, It->GetRemainingSeconds(), It->GetPoweredFraction(), It->PoweredSeconds,
				It->bResetOnJump ? 1 : 0, *Extra);
		}
		for (TActorIterator<APowerTerminal> It(World); It; ++It)
		{
			UE_LOG(LogTheLastShift, Log, TEXT("%s   terminal %s: cost=%d targets=%d at %s"), Prefix, *It->GetName(), It->Cost, It->Targets.Num(),
				*It->GetActorLocation().ToString());
		}
		LogInputReport(World, Prefix);
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
		const bool bOk = R.State == ETimeState::Normal && R.AliveScans == 0 && !R.bCycleActive && !R.bTimersPending
			&& R.ActiveNiagaraComponents == 0;
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
	TEXT("Logs time state, alive scans, scan timer state, flashlight, Niagara component counts, entities, energy, residues, powered devices, terminals and the input state (see tls.InputReport)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		if (UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld))
		{
			TimeStateDebug::LogReport(World, TEXT("tls.Report:"));
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsCycleTestCommand(
	TEXT("tls.CycleTest"),
	TEXT("tls.CycleTest [Cycles=20] [StopSeconds=4.6] [Key=the time stop key]: Stop/Resume cycles via simulated key presses, checks each Resume leaves nothing behind."),
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
		GCycleTest.Key = Args.Num() > 2 ? FKey(FName(*Args[2])) : BoundKey(World, TEXT("TimeStopAction"));
		GCycleTest.SettleSeconds = SettleSeconds + 0.15f;
		GCycleTest.bRunning = true;

		UE_LOG(LogTheLastShift, Log, TEXT("CycleTest started: %d cycles, %.2f s stopped each, settle %.2f s."),
			GCycleTest.Total, GCycleTest.StopSeconds, GCycleTest.SettleSeconds);
		LogReport(World, TEXT("CycleTest baseline:"));
		CycleStep(true);
	}));

namespace TimeStateDebug
{
	/**
	 * Stop for N seconds, request a scan every second, record every scan/ready/beat, Resume and summarise.
	 * Checks use the tunables live at each event, so ScanCooldown / BeatInterval may be edited while it runs.
	 */
	struct FScanTest
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UScanDirectorComponent> Director;
		TStrongObjectPtr<UTlsScanTestListener> Listener;
		float Duration = 30.f;
		/** Live values at the Stop: the first scan and the first beat are scheduled with them. */
		float FirstScanDelay = 0.f;
		float FirstBeatInterval = 0.f;
		int32 Requests = 0;
		TArray<float> AcceptedTimes;
		FTimerHandle RequestTimer;
		FTimerHandle EndTimer;
		bool bRunning = false;
	};
	static FScanTest GScanTest;

	UScanDirectorComponent* FindPlayerDirector(UWorld* World)
	{
		const APlayerController* PC = World->GetFirstPlayerController();
		if (const APawn* Pawn = PC ? PC->GetPawn() : nullptr)
		{
			if (UScanDirectorComponent* Director = Pawn->FindComponentByClass<UScanDirectorComponent>())
			{
				return Director;
			}
		}
		for (TObjectIterator<UScanDirectorComponent> It; It; ++It)
		{
			if (It->GetWorld() == World && It->GetOwner())
			{
				return *It;
			}
		}
		return nullptr;
	}

	static void ScanTestRequest()
	{
		UScanDirectorComponent* Director = GScanTest.Director.Get();
		UWorld* World = GScanTest.World.Get();
		const UTimeStateSubsystem* TimeState = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
		if (!Director || !TimeState)
		{
			return;
		}

		const float T = TimeState->GetCurrentStopDuration();
		const float RemainingBefore = Director->GetCooldownRemaining();
		const bool bAccepted = Director->RequestScan();
		++GScanTest.Requests;
		if (bAccepted)
		{
			GScanTest.AcceptedTimes.Add(T);
		}
		UE_LOG(LogTheLastShift, Log, TEXT("ScanTest  %7.3f s  request %d -> %s (cooldown left %.3f s)"),
			T, GScanTest.Requests, bAccepted ? TEXT("SCAN") : TEXT("ignored"), RemainingBefore);
	}

	static void ScanTestFinish()
	{
		UWorld* World = GScanTest.World.Get();
		UScanDirectorComponent* Director = GScanTest.Director.Get();
		UTimeStateSubsystem* TimeState = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
		GScanTest.bRunning = false;
		if (!World || !Director || !TimeState || !GScanTest.Listener)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("ScanTest aborted: world or director gone."));
			GScanTest.Listener.Reset();
			return;
		}

		World->GetTimerManager().ClearTimer(GScanTest.RequestTimer);
		const float StopSeconds = TimeState->GetCurrentStopDuration();
		const int32 ScansIssued = Director->GetScansThisStop();
		const int32 DirectorBeats = Director->GetBeatCount();
		if (TimeState->IsStopped())
		{
			TimeState->Resume();
		}

		UTlsScanTestListener* L = GScanTest.Listener.Get();
		L->Unbind();

		// Live values at the end, for events whose own value was never recorded (a scan issued in the last frame).
		const float EndCooldown = FMath::Clamp(Director->ScanCooldown, 0.5f, 15.f);
		const float EndBeatInterval = FMath::Clamp(Director->BeatInterval, 0.5f, 15.f);

		// Beats: the first is scheduled BeatInterval (live at the Stop) after the Stop; each next one BeatInterval
		// (live at the previous beat) after the previous scheduled time.
		float MaxBeatDriftMs = 0.f;
		bool bBeatIndicesOk = true;
		float ExpectedBeat = GScanTest.FirstBeatInterval;
		for (int32 i = 0; i < L->BeatTimes.Num(); ++i)
		{
			bBeatIndicesOk &= L->BeatIndices[i] == i + 1;
			MaxBeatDriftMs = FMath::Max(MaxBeatDriftMs, FMath::Abs(L->BeatTimes[i] - ExpectedBeat) * 1000.f);
			ExpectedBeat += L->BeatIntervals.IsValidIndex(i) ? L->BeatIntervals[i] : EndBeatInterval;
		}
		// ExpectedBeat is now the next scheduled beat: count the ones that should still have fired before the end.
		int32 ExpectedBeats = L->BeatTimes.Num();
		for (float Next = ExpectedBeat; Next <= StopSeconds; Next += EndBeatInterval)
		{
			++ExpectedBeats;
		}

		// Scans: issued at FirstScanDelay (scheduled) and at each accepted request. Each scan's cooldown is the
		// ScanCooldown live when it was issued; it must separate that scan from the next one, and the ready event must
		// come exactly that long after it.
		TArray<float> IssueTimes;
		IssueTimes.Add(GScanTest.FirstScanDelay);
		IssueTimes.Append(GScanTest.AcceptedTimes);
		auto CooldownOf = [L, EndCooldown](int32 IssueIndex)
		{
			return L->ScanCooldowns.IsValidIndex(IssueIndex) ? L->ScanCooldowns[IssueIndex] : EndCooldown;
		};

		float MinCooldown = TNumericLimits<float>::Max();
		float MaxCooldown = 0.f;
		for (int32 i = 0; i < IssueTimes.Num(); ++i)
		{
			MinCooldown = FMath::Min(MinCooldown, CooldownOf(i));
			MaxCooldown = FMath::Max(MaxCooldown, CooldownOf(i));
		}

		float MinScanGap = TNumericLimits<float>::Max();
		float MinScanSlack = TNumericLimits<float>::Max(); // gap minus the cooldown it had to respect
		for (int32 i = 1; i < IssueTimes.Num(); ++i)
		{
			const float Gap = IssueTimes[i] - IssueTimes[i - 1];
			MinScanGap = FMath::Min(MinScanGap, Gap);
			MinScanSlack = FMath::Min(MinScanSlack, Gap - CooldownOf(i - 1));
		}
		float MaxReadyDevMs = 0.f;
		for (int32 i = 0; i < L->ReadyTimes.Num() && i < IssueTimes.Num(); ++i)
		{
			MaxReadyDevMs = FMath::Max(MaxReadyDevMs, FMath::Abs(L->ReadyTimes[i] - (IssueTimes[i] + CooldownOf(i))) * 1000.f);
		}
		// Requests come every 1 s, so each accepted one must come within ~1 s of the ready time it waited for.
		float MaxAcceptWaitS = 0.f;
		for (int32 i = 0; i < GScanTest.AcceptedTimes.Num(); ++i)
		{
			MaxAcceptWaitS = FMath::Max(MaxAcceptWaitS, GScanTest.AcceptedTimes[i] - (IssueTimes[i] + CooldownOf(i)));
		}

		const bool bCooldownOk = IssueTimes.Num() < 2 || MinScanSlack >= -0.02f;
		const bool bBeatsOk = bBeatIndicesOk && L->BeatTimes.Num() == DirectorBeats && FMath::Abs(DirectorBeats - ExpectedBeats) <= 1
			&& MaxBeatDriftMs <= 20.f;
		const bool bScansOk = ScansIssued == 1 + GScanTest.AcceptedTimes.Num() && MaxAcceptWaitS <= 1.05f;
		const bool bPass = bCooldownOk && bBeatsOk && bScansOk && MaxReadyDevMs <= 20.f;

		UE_LOG(LogTheLastShift, Log, TEXT("ScanTest summary (%s): stopped %.3f s, FirstScanDelay %.2f s, ScanCooldown %.2f..%.2f s, BeatInterval %.2f s at stop / %.2f s at end (checks use the live value per event)"),
			bPass ? TEXT("PASS") : TEXT("FAIL"), StopSeconds, GScanTest.FirstScanDelay, MinCooldown, MaxCooldown, GScanTest.FirstBeatInterval, EndBeatInterval);
		UE_LOG(LogTheLastShift, Log, TEXT("  scans: issued %d (1 automatic + %d requested), completed %d, requests %d (%d ignored), min gap %.3f s, min gap minus its cooldown %.3f s %s"),
			ScansIssued, GScanTest.AcceptedTimes.Num(), L->ScanTimes.Num(), GScanTest.Requests, GScanTest.Requests - GScanTest.AcceptedTimes.Num(),
			IssueTimes.Num() > 1 ? MinScanGap : 0.f, IssueTimes.Num() > 1 ? MinScanSlack : 0.f, bCooldownOk ? TEXT("OK") : TEXT("COOLDOWN VIOLATED"));
		UE_LOG(LogTheLastShift, Log, TEXT("  ready: %d events, max deviation from scan + its cooldown %.1f ms; max wait ready->accepted request %.3f s"),
			L->ReadyTimes.Num(), MaxReadyDevMs, MaxAcceptWaitS);
		UE_LOG(LogTheLastShift, Log, TEXT("  beats: %d (expected %d, GetBeatCount %d, indices %s), max drift from chained live intervals %.1f ms"),
			L->BeatTimes.Num(), ExpectedBeats, DirectorBeats, bBeatIndicesOk ? TEXT("1..n") : TEXT("WRONG"), MaxBeatDriftMs);

		GScanTest.Listener.Reset();

		// Check that Resume left nothing behind once the fade-out is over.
		FTimerHandle SettleTimer;
		TWeakObjectPtr<UWorld> WeakWorld = World;
		World->GetTimerManager().SetTimer(SettleTimer, FTimerDelegate::CreateLambda([WeakWorld]()
		{
			if (UWorld* W = WeakWorld.Get())
			{
				LogReport(W, TEXT("ScanTest after resume:"));
			}
		}), FMath::Max(Director->ResumeFadeOut, 0.f) + 0.15f, false);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsScanTestCommand(
	TEXT("tls.ScanTest"),
	TEXT("tls.ScanTest [Seconds=30]: stops time, requests a scan every 1 s, logs every scan/ready/beat with timestamps, resumes and prints a summary."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		using namespace TimeStateDebug;
		UWorld* World = ResolveGameWorld(InWorld);
		UTimeStateSubsystem* TimeState = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
		UScanDirectorComponent* Director = World ? FindPlayerDirector(World) : nullptr;
		if (!TimeState || !Director)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.ScanTest: no game world, time state or scan director."));
			return;
		}
		// A test from a PIE session that was stopped mid-test left its flag set; only tests of this world count.
		if ((GScanTest.bRunning && GScanTest.World.Get() == World) || (GCycleTest.bRunning && GCycleTest.World.Get() == World))
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.ScanTest: a test is already running."));
			return;
		}
		if (TimeState->IsStopped())
		{
			TimeState->Resume();
		}

		GScanTest = FScanTest();
		GScanTest.World = World;
		GScanTest.Director = Director;
		GScanTest.Duration = Args.Num() > 0 ? FMath::Clamp(FCString::Atof(*Args[0]), 2.f, 600.f) : 30.f;
		GScanTest.FirstScanDelay = FMath::Clamp(Director->FirstScanDelay, 0.2f, 3.f);
		GScanTest.FirstBeatInterval = FMath::Clamp(Director->BeatInterval, 0.5f, 15.f);
		GScanTest.Listener.Reset(NewObject<UTlsScanTestListener>());
		GScanTest.Listener->Bind(Director);
		GScanTest.bRunning = true;

		UE_LOG(LogTheLastShift, Log, TEXT("ScanTest started: %.1f s, requests every 1 s."), GScanTest.Duration);
		TimeState->Stop();

		FTimerManager& Timers = World->GetTimerManager();
		Timers.SetTimer(GScanTest.RequestTimer, FTimerDelegate::CreateStatic(&ScanTestRequest), 1.f, true);
		Timers.SetTimer(GScanTest.EndTimer, FTimerDelegate::CreateStatic(&ScanTestFinish), GScanTest.Duration, false);
	}));

#endif // !UE_BUILD_SHIPPING
