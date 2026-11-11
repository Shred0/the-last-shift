// SPDX-License-Identifier: MIT

// Debug console commands for the lidar scan slots: fire many scans in one Stop (cooldown bypassed) and log the
// alive scans and the particle count of every slot's emitters after each one, so the ghost cap and the cost of a
// long Stop can be checked without a keyboard. Development builds only.

#include "Area/AreaGenerator.h"
#include "Containers/Ticker.h"
#include "Debug/TlsDebugCommon.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Entity/Entity.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "LidarScanComponent.h"
#include "Scan/ScanDirectorComponent.h"
#include "TheLastShift.h"
#include "Time/TimeStateSubsystem.h"

#if !UE_BUILD_SHIPPING

namespace ScanSlotDebug
{
	static ULidarScanComponent* FindPlayerLidar(UWorld* World)
	{
		const UScanDirectorComponent* Director = World ? TimeStateDebug::FindPlayerDirector(World) : nullptr;
		return Director ? Director->GetLidarScan() : nullptr;
	}

	/** One line: alive scans, then scan index and particle counts of every slot, then the totals per emitter. */
	static void LogSlots(const ULidarScanComponent* Lidar, const TCHAR* Prefix)
	{
		if (!Lidar)
		{
			return;
		}
		const TArray<FLidarScanSlotStats> Stats = Lidar->GetScanSlotStats();
		int32 Captured = 0;
		int32 Injected = 0;
		FString Slots;
		for (int32 Index = 0; Index < Stats.Num(); ++Index)
		{
			const FLidarScanSlotStats& S = Stats[Index];
			Captured += S.CapturedParticles;
			Injected += S.InjectedParticles;
			Slots += FString::Printf(TEXT("%s[%d] scan %d%s: %d / %d"), Index ? TEXT(", ") : TEXT(""), Index, S.ScanIndex,
				S.bFadingOut ? TEXT(" (fading)") : TEXT(""), S.CapturedParticles, S.InjectedParticles);
		}
		UE_LOG(LogTheLastShift, Log, TEXT("%s alive %d, cap %d, slots %d {%s}, Points total %d, Injected total %d"),
			Prefix, Lidar->GetAliveScanCount(), Lidar->MaxAliveScans, Stats.Num(), *Slots, Captured, Injected);
	}

	static void KeepEntityDormant(UWorld* World, UTimeStateSubsystem* TimeState)
	{
		// The test is about the lidar, not the entity: an awakening (and a contact rewind) would end the Stop.
		if (!TimeState || TimeState->GetCumulativeStoppedSeconds() < 10.f)
		{
			return;
		}
		for (TActorIterator<AEntity> It(World); It; ++It)
		{
			if (It->GetState() != EEntityState::Dormant)
			{
				return;
			}
		}
		TimeState->ResetCumulativeStoppedSeconds();
	}

	struct FSlotTest
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<ULidarScanComponent> Lidar;
		/** Scans this Stop should reach (the director's automatic first scan counts as 1). */
		int32 Target = 25;
		float Interval = 0.5f;
		float StepCm = 0.f;
		/** Lidar scan count when the Stop began. */
		int32 ScanBase = 0;
		FVector StartLocation = FVector::ZeroVector;
		FRotator StartView = FRotator::ZeroRotator;
		FVector StepDirection = FVector::ForwardVector;
		double NextTime = 0.0;
		/** 0 = waiting for the automatic first scan, 1 = firing. */
		int32 Phase = 0;
		bool bRunning = false;
		/** Finished, still Stopped: another tls.ScanSlotTest continues in the same Stop. */
		bool bHolding = false;
		FTSTicker::FDelegateHandle Ticker;
	};
	static FSlotTest GSlotTest;

	static void StopTicker()
	{
		if (GSlotTest.Ticker.IsValid())
		{
			FTSTicker::RemoveTicker(GSlotTest.Ticker);
			GSlotTest.Ticker.Reset();
		}
		GSlotTest.bRunning = false;
	}

	static void SetPose(UWorld* World, const FVector& Location, const FRotator& View)
	{
		APlayerController* PC = World->GetFirstPlayerController();
		if (APawn* Pawn = PC ? PC->GetPawn() : nullptr)
		{
			Pawn->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
			PC->SetControlRotation(View);
		}
	}

	static bool Tick(float)
	{
		UWorld* World = GSlotTest.World.Get();
		ULidarScanComponent* Lidar = GSlotTest.Lidar.Get();
		UTimeStateSubsystem* TimeState = World && !World->bIsTearingDown ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
		if (!TimeState || !Lidar)
		{
			// PIE stopped mid-test: nothing to report, and the next PIE session must be able to start a new test.
			GSlotTest.Ticker.Reset();
			GSlotTest.bRunning = false;
			GSlotTest.bHolding = false;
			return false;
		}
		if (!TimeState->IsStopped())
		{
			if (GSlotTest.bHolding)
			{
				UE_LOG(LogTheLastShift, Log, TEXT("ScanSlotTest: the held Stop ended."));
			}
			else
			{
				UE_LOG(LogTheLastShift, Warning, TEXT("ScanSlotTest: the Stop ended before the last scan."));
				LogSlots(Lidar, TEXT("ScanSlotTest aborted:"));
			}
			GSlotTest.Ticker.Reset();
			GSlotTest.bRunning = false;
			GSlotTest.bHolding = false;
			return false;
		}
		KeepEntityDormant(World, TimeState);
		if (GSlotTest.bHolding)
		{
			// Held between runs (profiling, screenshots): keep the entity dormant until the Stop ends.
			return true;
		}

		const double Now = FPlatformTime::Seconds();
		if (Now < GSlotTest.NextTime)
		{
			return true;
		}

		const int32 Fired = Lidar->GetScanCount() - GSlotTest.ScanBase;
		if (GSlotTest.Phase == 0)
		{
			if (Fired >= 1)
			{
				GSlotTest.Phase = 1;
				GSlotTest.NextTime = Now + GSlotTest.Interval;
			}
			return true;
		}

		// State after the latest scan, Interval after it so the GPU particle counts have been read back.
		LogSlots(Lidar, *FString::Printf(TEXT("ScanSlotTest after scan %d:"), Fired));
		if (Fired >= GSlotTest.Target)
		{
			SetPose(World, GSlotTest.StartLocation, GSlotTest.StartView);
			UE_LOG(LogTheLastShift, Log, TEXT("ScanSlotTest done: %d scans in this Stop, back at the start pose %s %s; still Stopped (tls.ScanSlotTest N adds N more, tls.Resume ends)."),
				Fired, *GSlotTest.StartLocation.ToString(), *GSlotTest.StartView.ToString());
			GSlotTest.bRunning = false;
			GSlotTest.bHolding = true;
			return true;
		}

		// Scan k (1-based) is taken StepCm * (k - 1) from the start pose along the start view's horizontal forward.
		SetPose(World, GSlotTest.StartLocation + GSlotTest.StepDirection * (GSlotTest.StepCm * Fired), GSlotTest.StartView);
		Lidar->Scan();
		GSlotTest.NextTime = Now + GSlotTest.Interval;
		return true;
	}

	// --- tls.LidarCycleTest: Stop/Resume cycles, captured points of every scan ---

	struct FCycleTest
	{
		int32 Cycles = 8;
		int32 ScansPerStop = 3;
		float Interval = 0.6f;
		bool bQuit = false;
		int32 Cycle = 0;
		/** 0 = waiting for the area, 1 = stopping, 2 = in a Stop, 3 = resumed, settling. */
		int32 Phase = 0;
		double NextTime = 0.0;
		double StartReal = 0.0;
		int32 StopScanBase = 0;
		/** Scan count when the slots were last sampled, and what the slots held then. */
		int32 SampledScanCount = 0;
		TArray<FLidarScanSlotStats> SlotsBefore;
		/** Scans waiting for their particle counts: scan index, path taken, real time of the scan. */
		struct FPending { int32 ScanIndex; FString Path; double Time; };
		TArray<FPending> Pending;
		int32 Scans = 0;
		int32 Empty = 0;
		TMap<FString, FIntPoint> ByPath;
		bool bRunning = false;
	};
	static FCycleTest GCycle;

	/** How the lidar will place its next scan, from the slots as they are now. */
	static FString PredictPath(const TArray<FLidarScanSlotStats>& Slots, int32 Cap)
	{
		for (const FLidarScanSlotStats& S : Slots)
		{
			if (S.ScanIndex == INDEX_NONE)
			{
				return TEXT("idle slot");
			}
		}
		return Slots.Num() < Cap ? TEXT("new slot") : TEXT("eviction");
	}

	static void FinishCycleTest(const TCHAR* Why)
	{
		FString Paths;
		for (const TPair<FString, FIntPoint>& Pair : GCycle.ByPath)
		{
			Paths += FString::Printf(TEXT("%s%s %d/%d empty"), Paths.IsEmpty() ? TEXT("") : TEXT(", "), *Pair.Key, Pair.Value.Y, Pair.Value.X);
		}
		UE_LOG(LogTheLastShift, Display, TEXT("LidarCycleTest: ===== %s (%d scans, %d with no captured points; %s)%s%s ====="),
			GCycle.Empty == 0 && !Why ? TEXT("PASS") : TEXT("FAIL"), GCycle.Scans, GCycle.Empty, *Paths, Why ? TEXT(" - ") : TEXT(""), Why ? Why : TEXT(""));
		GCycle.bRunning = false;
		if (GCycle.bQuit)
		{
			FPlatformMisc::RequestExit(false, TEXT("tls.LidarCycleTest"));
		}
	}

	static bool TickCycleTest(float)
	{
		UWorld* World = TimeStateDebug::ResolveGameWorld(nullptr);
		UTimeStateSubsystem* TimeState = World && !World->bIsTearingDown ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
		ULidarScanComponent* Lidar = FindPlayerLidar(World);
		const double Now = FPlatformTime::Seconds();
		if (Now - GCycle.StartReal > 600.0)
		{
			FinishCycleTest(TEXT("timed out"));
			return false;
		}
		if (GCycle.Phase == 0)
		{
			if (!TimeState || !Lidar || !AAreaGenerator::IsAreaReady(World) || World->GetFirstPlayerController() == nullptr)
			{
				return true;
			}
			UE_LOG(LogTheLastShift, Display, TEXT("LidarCycleTest: area ready in %s, MaxAliveScans %d, CaptureResolution %d."),
				*UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()), Lidar->MaxAliveScans, Lidar->CaptureResolution);
			GCycle.Phase = 1;
			GCycle.NextTime = Now + 1.0;
			return true;
		}
		if (!TimeState || !Lidar)
		{
			FinishCycleTest(TEXT("world or lidar gone"));
			return false;
		}

		// Every scan, however it was fired: remember the path it took from the slots sampled the tick before.
		const int32 Count = Lidar->GetScanCount();
		if (Count != GCycle.SampledScanCount)
		{
			const FString Path = PredictPath(GCycle.SlotsBefore, FMath::Clamp(Lidar->MaxAliveScans, 1, 16));
			for (int32 Index = GCycle.SampledScanCount; Index < Count; ++Index)
			{
				GCycle.Pending.Add({ Index, Path, Now });
			}
		}
		GCycle.SampledScanCount = Count;
		GCycle.SlotsBefore = Lidar->GetScanSlotStats();

		// Particle counts are read back from the GPU with a frame or two of latency: read them Interval later.
		for (int32 P = GCycle.Pending.Num() - 1; P >= 0; --P)
		{
			const FCycleTest::FPending& Item = GCycle.Pending[P];
			if (Now - Item.Time < GCycle.Interval * 0.75)
			{
				continue;
			}
			int32 Captured = -1;
			int32 Injected = -1;
			int32 SlotIndex = INDEX_NONE;
			for (int32 S = 0; S < GCycle.SlotsBefore.Num(); ++S)
			{
				if (GCycle.SlotsBefore[S].ScanIndex == Item.ScanIndex)
				{
					SlotIndex = S;
					Captured = GCycle.SlotsBefore[S].CapturedParticles;
					Injected = GCycle.SlotsBefore[S].InjectedParticles;
				}
			}
			// The newest scan's depth capture, read back: tells an empty capture from points that were not spawned.
			FString DepthInfo = TEXT("depth not read (not the newest scan)");
			{
				TArray<float> Depth;
				int32 Width = 0;
				int32 Height = 0;
				FLidarScanContext Context;
				if (Lidar->ReadNewestScanDepth(Depth, Width, Height, Context) && Context.ScanIndex == Item.ScanIndex)
				{
					int32 Valid = 0;
					for (const float D : Depth)
					{
						Valid += (D > 1.f && D < Context.MaxRange) ? 1 : 0;
					}
					DepthInfo = FString::Printf(TEXT("depth texels in range %d of %d"), Valid, Depth.Num());
				}
			}
			const bool bEmpty = Captured <= 0;
			++GCycle.Scans;
			GCycle.Empty += bEmpty ? 1 : 0;
			FIntPoint& Tally = GCycle.ByPath.FindOrAdd(Item.Path);
			Tally.X += 1;
			Tally.Y += bEmpty ? 1 : 0;
			UE_LOG(LogTheLastShift, Display, TEXT("LidarCycleTest: %s - cycle %d, scan %d, %s, slot %d: captured %d, injected %d, %s"),
				bEmpty ? TEXT("EMPTY") : TEXT("ok"), GCycle.Cycle, Item.ScanIndex, *Item.Path, SlotIndex, Captured, Injected, *DepthInfo);
			GCycle.Pending.RemoveAt(P);
		}

		if (Now < GCycle.NextTime)
		{
			return true;
		}
		switch (GCycle.Phase)
		{
		case 1:
			if (GCycle.Cycle >= GCycle.Cycles)
			{
				if (GCycle.Pending.Num() == 0)
				{
					FinishCycleTest(nullptr);
					return false;
				}
				return true;
			}
			++GCycle.Cycle;
			GCycle.StopScanBase = Count;
			TimeState->Stop();
			GCycle.Phase = 2;
			// The director fires the automatic first scan about 1 s into the Stop.
			GCycle.NextTime = Now + 1.0 + GCycle.Interval;
			return true;
		case 2:
			KeepEntityDormant(World, TimeState);
			if (!TimeState->IsStopped())
			{
				UE_LOG(LogTheLastShift, Warning, TEXT("LidarCycleTest: cycle %d, the Stop ended early."), GCycle.Cycle);
				GCycle.Phase = 3;
				GCycle.NextTime = Now + 1.5;
				return true;
			}
			if (Count - GCycle.StopScanBase < GCycle.ScansPerStop)
			{
				Lidar->Scan();
				GCycle.NextTime = Now + GCycle.Interval;
				return true;
			}
			TimeState->Resume();
			GCycle.Phase = 3;
			// Long enough for the ClearScans fade-out to release every slot.
			GCycle.NextTime = Now + 1.5;
			return true;
		default:
			GCycle.Phase = 1;
			return true;
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GTlsLidarCycleTestCommand(
	TEXT("tls.LidarCycleTest"),
	TEXT("tls.LidarCycleTest [Cycles=8] [ScansPerStop=3] [Interval=0.6] [Quit=0]: waits for the area to be ready, then runs Cycles Stop/Resume cycles; in each Stop, after the automatic first scan, fires scans directly on the lidar every Interval s until the Stop has ScansPerStop scans, then resumes and waits 1.5 s. Interval after every scan logs the captured and injected particles of the slot holding it and the slot path it took (new slot, idle slot, eviction); a scan with no captured points is EMPTY. Ends with PASS when no scan was empty. Keeps the entity dormant. Quit=1 exits at the end (packaged -ExecCmds runs)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		using namespace ScanSlotDebug;
		if (GCycle.bRunning)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.LidarCycleTest: already running."));
			return;
		}
		GCycle = FCycleTest();
		GCycle.Cycles = Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]), 1, 100) : 8;
		GCycle.ScansPerStop = Args.Num() > 1 ? FMath::Clamp(FCString::Atoi(*Args[1]), 1, 40) : 3;
		GCycle.Interval = Args.Num() > 2 ? FMath::Clamp(FCString::Atof(*Args[2]), 0.2f, 10.f) : 0.6f;
		GCycle.bQuit = Args.Num() > 3 && FCString::Atoi(*Args[3]) != 0;
		GCycle.StartReal = FPlatformTime::Seconds();
		GCycle.bRunning = true;
		UE_LOG(LogTheLastShift, Display, TEXT("LidarCycleTest started: %d cycles, %d scans per Stop, interval %.2f s; waiting for the area."),
			GCycle.Cycles, GCycle.ScansPerStop, GCycle.Interval);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&ScanSlotDebug::TickCycleTest));
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsScanSlotTestCommand(
	TEXT("tls.ScanSlotTest"),
	TEXT("tls.ScanSlotTest [Scans=25] [Cap=0] [StepCm=0] [Interval=0.5]: stops time and fires scans directly on the lidar (cooldown bypassed) until this Stop has Scans scans, the automatic first scan included, each StepCm farther along the start view; Cap > 0 sets MaxAliveScans first. Interval s after each scan logs alive scans and the particle count of every slot's Points / Injected emitters. Ends back at the start pose, still Stopped; run again while Stopped to add Scans more to the same Stop. Keeps the entity dormant."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		using namespace ScanSlotDebug;
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		UTimeStateSubsystem* TimeState = World ? World->GetSubsystem<UTimeStateSubsystem>() : nullptr;
		ULidarScanComponent* Lidar = FindPlayerLidar(World);
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (!TimeState || !Lidar || !Pawn)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.ScanSlotTest: no game world, time state, lidar or player pawn."));
			return;
		}
		if (GSlotTest.bRunning && GSlotTest.World.Get() == World)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.ScanSlotTest: a slot test is already running."));
			return;
		}

		const int32 Scans = Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]), 1, 200) : 25;
		const int32 Cap = Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 0;
		const float StepCm = Args.Num() > 2 ? FMath::Clamp(FCString::Atof(*Args[2]), -500.f, 500.f) : 0.f;
		const float Interval = Args.Num() > 3 ? FMath::Clamp(FCString::Atof(*Args[3]), 0.1f, 10.f) : 0.5f;

		const bool bContinue = GSlotTest.bHolding && GSlotTest.World.Get() == World && GSlotTest.Lidar.Get() == Lidar && TimeState->IsStopped();
		if (Cap > 0)
		{
			Lidar->MaxAliveScans = Cap;
		}
		if (bContinue)
		{
			GSlotTest.Target += Scans;
			GSlotTest.StepCm = StepCm;
			GSlotTest.Interval = Interval;
			GSlotTest.Phase = 1;
			GSlotTest.NextTime = 0.0;
			UE_LOG(LogTheLastShift, Log, TEXT("ScanSlotTest continued: %d more scans in the same Stop (target %d), MaxAliveScans %d, step %.0f cm, interval %.2f s."),
				Scans, GSlotTest.Target, Lidar->MaxAliveScans, StepCm, Interval);
		}
		else
		{
			StopTicker();
			if (TimeState->IsStopped())
			{
				TimeState->Resume();
			}
			GSlotTest = FSlotTest();
			GSlotTest.World = World;
			GSlotTest.Lidar = Lidar;
			GSlotTest.Target = Scans;
			GSlotTest.StepCm = StepCm;
			GSlotTest.Interval = Interval;
			GSlotTest.StartLocation = Pawn->GetActorLocation();
			GSlotTest.StartView = PC->GetControlRotation();
			GSlotTest.StepDirection = FRotator(0.f, GSlotTest.StartView.Yaw, 0.f).Vector();
			GSlotTest.ScanBase = Lidar->GetScanCount();
			UE_LOG(LogTheLastShift, Log, TEXT("ScanSlotTest started: %d scans, MaxAliveScans %d, step %.0f cm, interval %.2f s, start %s %s."),
				Scans, Lidar->MaxAliveScans, StepCm, Interval, *GSlotTest.StartLocation.ToString(), *GSlotTest.StartView.ToString());
			TimeState->Stop();
		}

		GSlotTest.bRunning = true;
		GSlotTest.bHolding = false;
		if (!GSlotTest.Ticker.IsValid())
		{
			GSlotTest.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&ScanSlotDebug::Tick));
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsLidarSlotsCommand(
	TEXT("tls.Lidar.Slots"),
	TEXT("tls.Lidar.Slots: logs the player lidar's alive scans and the particle count of every slot's Points / Injected emitters."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		if (const ULidarScanComponent* Lidar = ScanSlotDebug::FindPlayerLidar(World))
		{
			ScanSlotDebug::LogSlots(Lidar, TEXT("tls.Lidar.Slots:"));
		}
		else
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Lidar.Slots: no player lidar."));
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsLidarMaxAliveScansCommand(
	TEXT("tls.Lidar.MaxAliveScans"),
	TEXT("tls.Lidar.MaxAliveScans [N]: sets the player lidar's MaxAliveScans (applied at its next scan); without an argument, logs it."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		ULidarScanComponent* Lidar = ScanSlotDebug::FindPlayerLidar(World);
		if (!Lidar)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Lidar.MaxAliveScans: no player lidar."));
			return;
		}
		if (Args.Num() > 0)
		{
			Lidar->MaxAliveScans = FCString::Atoi(*Args[0]);
		}
		UE_LOG(LogTheLastShift, Log, TEXT("tls.Lidar.MaxAliveScans: %d"), Lidar->MaxAliveScans);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsLidarRangeCommand(
	TEXT("tls.Lidar.Range"),
	TEXT("tls.Lidar.Range [Cm]: sets the player lidar's MaxRange (applied at its next scan); without an argument, logs it."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		ULidarScanComponent* Lidar = ScanSlotDebug::FindPlayerLidar(TimeStateDebug::ResolveGameWorld(InWorld));
		if (!Lidar)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Lidar.Range: no player lidar."));
			return;
		}
		if (Args.Num() > 0)
		{
			Lidar->MaxRange = FMath::Clamp(FCString::Atof(*Args[0]), 100.f, 30000.f);
		}
		UE_LOG(LogTheLastShift, Log, TEXT("tls.Lidar.Range: MaxRange %.0f cm"), Lidar->MaxRange);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsLidarResolutionCommand(
	TEXT("tls.Lidar.Resolution"),
	TEXT("tls.Lidar.Resolution [N]: sets the player lidar's CaptureResolution, the points-per-scan budget N x N (applied at its next scan); without an argument, logs it."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		ULidarScanComponent* Lidar = ScanSlotDebug::FindPlayerLidar(TimeStateDebug::ResolveGameWorld(InWorld));
		if (!Lidar)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Lidar.Resolution: no player lidar."));
			return;
		}
		if (Args.Num() > 0)
		{
			Lidar->CaptureResolution = FMath::Clamp(FCString::Atoi(*Args[0]), 64, 1024);
		}
		UE_LOG(LogTheLastShift, Log, TEXT("tls.Lidar.Resolution: CaptureResolution %d (%d points per scan before thinning)"),
			Lidar->CaptureResolution, Lidar->CaptureResolution * Lidar->CaptureResolution);
	}));

static FAutoConsoleCommandWithWorldAndArgs GTlsLidarDepthCheckCommand(
	TEXT("tls.Lidar.DepthCheck"),
	TEXT("tls.Lidar.DepthCheck [Samples=10] [MinCm=20000]: reads back the newest scan's depth capture and, for Samples texels whose point lies farther than MinCm from the capture origin (spread evenly over the image), compares the point the contract places at that texel centre (without SurfaceBias) with a ray cast along the same ray against the area generator's plan boxes (exactly the boxes PCG builds, backdrop included; the backdrop has no collision) and, for reference, a physics line trace. Logs every sample, then the median and the worst distance."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* InWorld)
	{
		UWorld* World = TimeStateDebug::ResolveGameWorld(InWorld);
		ULidarScanComponent* Lidar = ScanSlotDebug::FindPlayerLidar(World);
		if (!Lidar)
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Lidar.DepthCheck: no player lidar."));
			return;
		}
		const int32 Samples = Args.Num() > 0 ? FMath::Clamp(FCString::Atoi(*Args[0]), 1, 100) : 10;
		const double MinCm = Args.Num() > 1 ? FMath::Clamp(FCString::Atof(*Args[1]), 0.f, 30000.f) : 20000.0;

		TArray<float> Depth;
		int32 Width = 0;
		int32 Height = 0;
		FLidarScanContext Context;
		if (!Lidar->ReadNewestScanDepth(Depth, Width, Height, Context))
		{
			UE_LOG(LogTheLastShift, Warning, TEXT("tls.Lidar.DepthCheck: no alive scan, or the depth readback failed."));
			return;
		}

		// Texel centre ray of the contract: u' = 2 U - 1, v' = 1 - 2 V; the forward component of the ray is exactly 1.
		auto RayOf = [&](int32 X, int32 Y)
		{
			const double U = (X + 0.5) / Width * 2.0 - 1.0;
			const double V = 1.0 - (Y + 0.5) / Height * 2.0;
			return Context.CaptureForward + Context.CaptureRight * (U * Context.TanHalfFOV.X) + Context.CaptureUp * (V * Context.TanHalfFOV.Y);
		};
		TArray<int32> Far;
		for (int32 Index = 0; Index < Depth.Num(); ++Index)
		{
			const float D = Depth[Index];
			if (D > 1.f && D < Context.MaxRange && RayOf(Index % Width, Index / Width).Size() * D > MinCm)
			{
				Far.Add(Index);
			}
		}
		UE_LOG(LogTheLastShift, Log, TEXT("tls.Lidar.DepthCheck: scan %d, %dx%d, MaxRange %.0f cm, origin %s: %d of %d texels farther than %.0f cm."),
			Context.ScanIndex, Width, Height, Context.MaxRange, *Context.CaptureOrigin.ToString(), Far.Num(), Depth.Num(), MinCm);
		if (Far.Num() == 0)
		{
			return;
		}

		// The geometry as built: every plan box of the area generator (unit cube scaled, no rotation).
		TArray<FBox> Boxes;
		if (const AAreaGenerator* Generator = AAreaGenerator::Find(World))
		{
			for (const TArray<FTransform>* Plan : { &Generator->PlanFloors, &Generator->PlanWalls, &Generator->PlanCurbs,
				&Generator->PlanPillars, &Generator->PlanCeilings, &Generator->PlanBackdrop })
			{
				for (const FTransform& T : *Plan)
				{
					const FVector Half = T.GetScale3D().GetAbs() * 50.0;
					Boxes.Add(FBox(T.GetLocation() - Half, T.GetLocation() + Half));
				}
			}
		}
		auto CastBoxes = [&Boxes](const FVector& Start, const FVector& Dir, double& OutT)
		{
			OutT = TNumericLimits<double>::Max();
			for (const FBox& Box : Boxes)
			{
				double TMin = 0.0;
				double TMax = TNumericLimits<double>::Max();
				bool bHit = true;
				for (int32 Axis = 0; Axis < 3 && bHit; ++Axis)
				{
					const double D = Dir[Axis];
					const double O = Start[Axis];
					if (FMath::Abs(D) < 1.e-12)
					{
						bHit = O >= Box.Min[Axis] && O <= Box.Max[Axis];
						continue;
					}
					double T1 = (Box.Min[Axis] - O) / D;
					double T2 = (Box.Max[Axis] - O) / D;
					if (T1 > T2)
					{
						Swap(T1, T2);
					}
					TMin = FMath::Max(TMin, T1);
					TMax = FMath::Min(TMax, T2);
					bHit = TMin <= TMax;
				}
				if (bHit && TMin > 0.0 && TMin < OutT)
				{
					OutT = TMin;
				}
			}
			return OutT < TNumericLimits<double>::Max();
		};

		FCollisionQueryParams Params(SCENE_QUERY_STAT(TlsLidarDepthCheck), true, Lidar->GetOwner());
		TArray<double> Errors;
		for (int32 Sample = 0; Sample < Samples; ++Sample)
		{
			const int32 Index = Far[FMath::Min(Far.Num() - 1, (Sample * Far.Num() + Far.Num() / 2) / Samples)];
			const int32 X = Index % Width;
			const int32 Y = Index / Width;
			const FVector Ray = RayOf(X, Y);
			const FVector Point = Context.CaptureOrigin + Ray * Depth[Index];
			const FVector Dir = Ray.GetSafeNormal();
			double BoxT = 0.0;
			const bool bBox = CastBoxes(Context.CaptureOrigin, Dir, BoxT);
			const double Error = bBox ? FVector::Dist(Point, Context.CaptureOrigin + Dir * BoxT) : -1.0;
			if (bBox)
			{
				Errors.Add(Error);
			}
			FHitResult Hit;
			const bool bTrace = World->LineTraceSingleByChannel(Hit, Context.CaptureOrigin, Context.CaptureOrigin + Dir * (Context.MaxRange * 2.0), ECC_Visibility, Params);
			UE_LOG(LogTheLastShift, Log, TEXT("tls.Lidar.DepthCheck sample %d: texel (%d, %d), point at %.2f cm along the ray; plan-box cast %s at %.2f cm, distance %.3f cm; physics trace %s at %.1f cm"),
				Sample, X, Y, (Point - Context.CaptureOrigin).Size(), bBox ? TEXT("hit") : TEXT("missed"), bBox ? BoxT : 0.0, Error,
				bTrace ? TEXT("hit") : TEXT("missed (no collision)"), bTrace ? Hit.Distance : 0.f);
		}

		Errors.Sort();
		if (Errors.Num() > 0)
		{
			const double Median = Errors.Num() % 2 ? Errors[Errors.Num() / 2] : 0.5 * (Errors[Errors.Num() / 2 - 1] + Errors[Errors.Num() / 2]);
			UE_LOG(LogTheLastShift, Log, TEXT("tls.Lidar.DepthCheck: %d of %d rays hit a plan box (%d boxes); point-to-hit distance median %.3f cm, worst %.3f cm (SurfaceBias %.1f cm is not applied to the point)."),
				Errors.Num(), Samples, Boxes.Num(), Median, Errors.Last(), Lidar->SurfaceBias);
		}
	}));

#endif // !UE_BUILD_SHIPPING
